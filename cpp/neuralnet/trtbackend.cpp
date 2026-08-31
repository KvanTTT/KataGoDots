#ifdef USE_TENSORRT_BACKEND

#define CUDA_API_PER_THREAD_DEFAULT_STREAM
#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cuda_runtime_api.h>

// TensorRT 11 made strongly typed networks mandatory and removed the weak-typing APIs used by the
// old hand-built network path. This backend emits an explicitly typed ONNX graph instead.
#if NV_TENSORRT_MAJOR < 11
#error "The TensorRT backend requires TensorRT 11.0 or newer"
#endif

#include <atomic>
#include <cstdint>
#include <fstream>
#include <random>

#include "../core/fileutils.h"
#include "../core/makedir.h"
#include "../core/sha2.h"
#include "../core/test.h"
#include "../dataio/homedata.h"
#include "../neuralnet/desc.h"
#include "../neuralnet/modelversion.h"
#include "../neuralnet/nneval.h"
#include "../neuralnet/nninputs.h"
#include "../neuralnet/nninterface.h"
#include "../neuralnet/onnxmodelbuilder.h"

using namespace std;
using namespace nvinfer1;

// Define this to print out some of the intermediate values of the neural net
//#define DEBUG_INTERMEDIATE_VALUES

static void checkCudaError(const cudaError_t status, const char* opName, const char* file, const char* func, int line) {
  if(status != cudaSuccess)
    throw StringError(
      string("CUDA Error, for ") + opName + " file " + file + ", func " + func + ", line " + Global::intToString(line) +
      ", error " + cudaGetErrorString(status));
}
#define CUDA_ERR(opName, x) \
  { checkCudaError((x), opName, __FILE__, #x, __LINE__); }

// Write `data` to `path` atomically, so a reader either sees the complete
// old file or the complete new one, never a torn/truncated write from a crash or a racing process.
static void writeFileAtomically(const string& path, const char* data, size_t size) {
  // Unique temp suffix: a per-process random base (distinct across racing processes) plus a
  // monotonic counter (distinct across calls within a process). Both writes here hold tuneMutex, but
  // the random base keeps two processes from picking the same temp name.
  static const uint64_t randBase = std::random_device{}();
  static std::atomic<uint64_t> counter{0};
  string tmpPath = Global::strprintf(
    "%s.tmp_%llx_%llu", path.c_str(),
    (unsigned long long)randBase, (unsigned long long)counter.fetch_add(1));
  {
    ofstream ofs;
    FileUtils::open(ofs, tmpPath, ios::out | ios::binary);
    ofs.write(data, (std::streamsize)size);
    ofs.close();
    if(ofs.fail()) {
      FileUtils::tryRemoveFile(tmpPath);
      throw StringError("TensorRT backend: failed to write cache temp file " + tmpPath);
    }
  }
  if(!FileUtils::tryRename(tmpPath, path)) {
    FileUtils::tryRemoveFile(tmpPath);
    throw StringError("TensorRT backend: failed to rename cache temp file " + tmpPath + " to " + path);
  }
}

void NeuralNet::globalInitialize() {
  // Empty for TensorRT backend
}

void NeuralNet::globalCleanup() {
  // Empty for TensorRT backend
}

struct ComputeContext {
  int nnXLen;
  int nnYLen;
  enabled_t useFP16Mode;
  string homeDataDirOverride;
  bool transformerNHWC;  // ONNX emitter: run transformer blocks channel-last (default true)
  string dumpDebugPlanToDir;  // if non-empty, dump emitted ONNX + built-engine layer info here (debug)
};

ComputeContext* NeuralNet::createComputeContext(
  const vector<int>& gpuIdxs,
  Logger* logger,
  int nnXLen,
  int nnYLen,
  const string& homeDataDirOverride,
  enabled_t useFP16Mode,
  const LoadedModel* loadedModel,
  ConfigParser& cfg) {
  (void)gpuIdxs;
  (void)logger;

  ComputeContext* context = new ComputeContext();
  context->nnXLen = nnXLen;
  context->nnYLen = nnYLen;
  context->useFP16Mode = useFP16Mode;
  context->homeDataDirOverride = homeDataDirOverride;
  // ONNX transformer emitter layout. Default is NHWC (whole trunk channel-last with NCHW<->NHWC
  // conversions around it). Benchmark the alternative on the target GPU before disabling it.
  // Normalize convnets to false so their timing/plan cache keys don't change with this setting
  // (the ONNX builder ignores it for models without transformers anyway).
  context->transformerNHWC = cfg.getOrDefaultBool("trtTransformerNHWC", true) &&
    NeuralNet::getModelDesc(loadedModel).hasAnyTransformerBlocks();
  // Debugging: if set, the ONNX-emitter path dumps the emitted ONNX model and the built engine's
  // per-layer info (precision/format/tactic, via a detailed-profiling build + IEngineInspector) into
  // this directory. Files are disambiguated by board size, FP16/FP32, and exact/max NN-length so the
  // multiple engines built in one process (e.g. an FP16 and an FP32 evaluator) don't overwrite each
  // other. Off by default; only for investigating numerical/precision issues in the TRT graph.
  context->dumpDebugPlanToDir = cfg.getOrDefaultString("trtDumpDebugPlanToDir", "");
  return context;
}

void NeuralNet::freeComputeContext(ComputeContext* computeContext) {
  delete computeContext;
}

struct LoadedModel {
  ModelDesc modelDesc;

  LoadedModel(const string& fileName, const string& expectedSha256) {
    ModelDesc::loadFromFileMaybeGZipped(fileName, modelDesc, expectedSha256);
    modelDesc.applyScale8ToReduceActivations();
  }

  LoadedModel() = delete;
  LoadedModel(const LoadedModel&) = delete;
  LoadedModel& operator=(const LoadedModel&) = delete;
};

LoadedModel* NeuralNet::loadModelFile(const string& file, const string& expectedSha256) {
  LoadedModel* loadedModel = new LoadedModel(file, expectedSha256);
  return loadedModel;
}

void NeuralNet::freeLoadedModel(LoadedModel* loadedModel) {
  delete loadedModel;
}

// Bump when a backend change must invalidate timing and serialized-plan caches.
// 9 introduces TensorRT 11 strongly typed ONNX graphs with explicit FP16/FP32 Cast boundaries.
static constexpr int trtTuneSalt = 9;

const ModelDesc& NeuralNet::getModelDesc(const LoadedModel* loadedModel) {
  return loadedModel->modelDesc;
}

struct TRTBuildState {
  uint8_t tuneHash[32];
  unique_ptr<INetworkDefinition> network;
};

// The builder's autotuner reports tactics that fail to compile or execute as ERROR-severity
// "Skipping tactic ... due to exception ..." messages, but these are recoverable: the autotuner
// moves on to other tactics, and if none work the build fails afterward with its own error.
// Such messages can mention cask convolution execution failures that would otherwise match the
// genuine GPU-health fatal checks below, so exempt them rather than killing the process mid-build.
static bool isRecoverableTacticSkipMessage(const string& msg) {
  return msg.find("Skipping tactic") != string::npos && msg.find("due to exception") != string::npos;
}

struct TRTLogger : ILogger {
  Logger* logger;
  Severity level;

  TRTLogger() {
    logger = nullptr;
    level = Severity::kERROR;
  }

  TRTLogger(const TRTLogger&) = delete;
  TRTLogger& operator=(const TRTLogger&) = delete;

  void log(Severity severity, const char* msg) noexcept override {
    if(logger && severity <= level)
      logger->write("TensorRT backend: " + string(msg));
    if(severity == Severity::kERROR && logger && !logger->isLoggingToStderr() && !logger->isLoggingToStdout()) {
      std::cerr << ("TensorRT backend: " + string(msg)) << std::endl;
    }
    if(severity == Severity::kERROR && !isRecoverableTacticSkipMessage(string(msg))) {
      if((string(msg).find("Cask convolution") != std::string::npos) ||
         (string(msg).find("Cask Convolution") != std::string::npos) ||
         (string(msg).find("elementWiseRunner.cpp") != std::string::npos) ||
         (string(msg).find("convBaseRunner.cpp") != std::string::npos) ||
         (string(msg).find("Cuda Runtime") != std::string::npos)
      ) {
         Global::fatalError("TensorRT backend fatal error: " + string(msg));
      }
    }
  }

  void setLogger(Logger* externalLogger) { logger = externalLogger; }
};

struct TRTErrorRecorder : IErrorRecorder {
  mutable std::mutex mutex;
  std::vector<std::pair<ErrorCode,std::string>> errors;
  std::atomic<int32_t> refCount;
  Logger* logger;

  TRTErrorRecorder()
    :mutex(),
     errors(),
     refCount(0),
     logger(NULL)
  {}

  void clear() noexcept override {
    std::lock_guard<std::mutex> lock(mutex);
    errors.clear();
  }
  int32_t getNbErrors() const noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    return (int32_t)errors.size();
  }
  ErrorCode getErrorCode(int32_t errorIdx) const noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    if(errorIdx < 0 || errorIdx >= errors.size())
      return ErrorCode::kINVALID_ARGUMENT;
    return errors[errorIdx].first;
  }
  IErrorRecorder::ErrorDesc getErrorDesc(int32_t errorIdx) const noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    if(errorIdx < 0 || errorIdx >= errors.size())
      return "";
    return errors[errorIdx].second.c_str();
  }
  bool hasOverflowed() const noexcept {
    return false;
  }
  bool empty() const noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    return errors.size() <= 0;
  }
  bool reportError(ErrorCode val, IErrorRecorder::ErrorDesc desc) noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    errors.emplace_back(val,string(desc));
    if(
      !isRecoverableTacticSkipMessage(errors[errors.size()-1].second)
      && (
        (val != ErrorCode::kUNSPECIFIED_ERROR && val != ErrorCode::kSUCCESS)
        || (errors[errors.size()-1].second.find("Cask convolution") != std::string::npos)
        || (errors[errors.size()-1].second.find("Cask Convolution") != std::string::npos)
        || (errors[errors.size()-1].second.find("elementWiseRunner.cpp") != std::string::npos)
        || (errors[errors.size()-1].second.find("convBaseRunner.cpp") != std::string::npos)
        || (errors[errors.size()-1].second.find("Cuda Runtime") != std::string::npos)
      )
    ) {
      Global::fatalError("Fatal error reported from TensorRT: " + Global::intToString((int)val) + " " + std::string(desc));
    }
    logger->write("TensorRT error reported code: " + Global::intToString((int)val) + " " + std::string(desc));
    return false;
  }

  void setLogger(Logger* externalLogger) { logger = externalLogger; }

  IErrorRecorder::RefCount incRefCount() noexcept {
    return ++refCount;
  }
  IErrorRecorder::RefCount decRefCount() noexcept {
    return --refCount;
  }
};


struct ComputeHandle {
  ComputeContext* ctx;

  bool usingFP16;
  int maxBatchSize;
  int modelVersion;
  vector<pair<string, string>> debugOutputs;

  TRTLogger trtLogger;
  TRTErrorRecorder trtErrorRecorder;
  map<string, void*> buffers;
  unique_ptr<IRuntime> runtime;
  unique_ptr<ICudaEngine> engine;
  unique_ptr<IExecutionContext> exec;

  ComputeHandle(
    Logger* logger,
    const cudaDeviceProp* prop,
    ComputeContext* context,
    const LoadedModel* loadedModel,
    int maxBatchSz,
    bool requireExactNNLen) {
    ctx = context;

    maxBatchSize = maxBatchSz;
    modelVersion = loadedModel->modelDesc.modelVersion;

    // Certain minor versions of TensorRT uses a global logger, which is bad.
    // Since TensorRT maintains ABI compatibility between minor versions, a dynamic library mismatch
    // does not necessarily generate a dynamic link error, therefore, an extra check is required.
    if(getInferLibVersion() / 100 != NV_TENSORRT_VERSION / 100) {
      throw StringError("TensorRT backend: detected incompatible version of TensorRT library");
    }

    trtLogger.setLogger(logger);

    auto builder = unique_ptr<IBuilder>(createInferBuilder(trtLogger));
    if(!builder) {
      throw StringError("TensorRT backend: failed to create builder");
    }
    auto config = unique_ptr<IBuilderConfig>(builder->createBuilderConfig());
    if(!config) {
      throw StringError("TensorRT backend: failed to create builder config");
    }

    // TensorRT 11 supports only strongly typed networks. FP16 is therefore encoded explicitly in
    // the emitted ONNX graph rather than selected through the removed kFP16 builder flag.
    usingFP16 = ctx->useFP16Mode == enabled_t::True || ctx->useFP16Mode == enabled_t::Auto;

    // Debug plan/engine dump (trtDumpDebugPlanToDir). Build a base path inside that dir, disambiguated
    // by board size + precision + exact/max so the multiple engines built in one process don't collide.
    const bool dumpDebugPlan = !ctx->dumpDebugPlanToDir.empty();
    string dumpDebugBasePath;
    if(dumpDebugPlan) {
      MakeDir::make(ctx->dumpDebugPlanToDir);
      dumpDebugBasePath = ctx->dumpDebugPlanToDir + "/plan_" +
        Global::intToString(ctx->nnXLen) + "x" + Global::intToString(ctx->nnYLen) +
        (usingFP16 ? "_fp16" : "_fp32") + (requireExactNNLen ? "_exact" : "_max");
    }

    auto network = unique_ptr<INetworkDefinition>(builder->createNetworkV2(0));
    if(!network) {
      throw StringError("TensorRT backend: failed to create network definition");
    }
    auto profile = builder->createOptimizationProfile();
    if(!profile) {
      throw StringError("TensorRT backend: failed to create optimization profile");
    }
    // Build the strongly typed network by emitting ONNX from the ModelDesc and parsing it with
    // nvonnxparser. The serialized bytes and parser must outlive buildSerializedNetwork below.
    TRTBuildState buildState;
    // These must outlive buildSerializedNetwork below: nvonnxparser::parse() does not necessarily
    // deep-copy initializer weights, so the parsed INetworkDefinition may reference data inside
    // onnxBytes (and the parser object) until the engine is actually built. Keeping them at this
    // scope avoids a use-after-free that manifests as all-NaN engine outputs.
    string onnxBytes;
    unique_ptr<nvonnxparser::IParser> onnxParser;
    logger->write("TensorRT backend: building strongly typed network via ONNX emitter");
    const ModelDesc& desc = loadedModel->modelDesc;
    OnnxModelBuilder::Result onnxResult = OnnxModelBuilder::build(
      desc, ctx->nnXLen, ctx->nnYLen, requireExactNNLen, ctx->transformerNHWC, usingFP16, logger);
    onnxBytes = std::move(onnxResult.serializedModel);

    if(dumpDebugPlan) {
      string onnxPath = dumpDebugBasePath + ".onnx";
      ofstream dumpOut;
      FileUtils::open(dumpOut, onnxPath, ios::binary);
      dumpOut.write(onnxBytes.data(), (std::streamsize)onnxBytes.size());
      dumpOut.close();
      logger->write("TensorRT backend: dumped emitted ONNX to " + onnxPath);
    }

    onnxParser.reset(nvonnxparser::createParser(*network, trtLogger));
    if(!onnxParser)
      throw StringError("TensorRT backend: failed to create ONNX parser");
    if(!onnxParser->parse(onnxBytes.data(), onnxBytes.size())) {
      string msg = "TensorRT backend: failed to parse emitted ONNX model:";
      for(int i = 0; i < onnxParser->getNbErrors(); i++)
        msg += "\n  " + string(onnxParser->getError(i)->desc());
      throw StringError(msg);
    }

    // getOutput does a flat cudaMemcpy of each FP32 output buffer assuming linear layout.
    for(int i = 0; i < network->getNbOutputs(); i++)
      network->getOutput(i)->setAllowedFormats(1U << static_cast<int>(TensorFormat::kLINEAR));

    // Set optimization profile dims for each input the parser created.
    auto setProfile = [&](const char* name, Dims4 minDims, Dims4 optMaxDims) {
      if(
        !profile->setDimensions(name, OptProfileSelector::kMIN, minDims) ||
        !profile->setDimensions(name, OptProfileSelector::kOPT, optMaxDims) ||
        !profile->setDimensions(name, OptProfileSelector::kMAX, optMaxDims)
      )
        throw StringError("TensorRT backend: failed to set optimization profile for " + string(name));
    };
    setProfile("InputMask", Dims4(1, 1, ctx->nnYLen, ctx->nnXLen), Dims4(maxBatchSize, 1, ctx->nnYLen, ctx->nnXLen));
    setProfile(
      "InputSpatial",
      Dims4(1, desc.numInputChannels, ctx->nnYLen, ctx->nnXLen),
      Dims4(maxBatchSize, desc.numInputChannels, ctx->nnYLen, ctx->nnXLen));
    setProfile(
      "InputGlobal",
      Dims4(1, desc.numInputGlobalChannels, 1, 1),
      Dims4(maxBatchSize, desc.numInputGlobalChannels, 1, 1));

    buildState.network = move(network);
    string tuneDesc = Global::strprintf(
      "\"onnxsalt\"(%d)\"nhwc\"(%d)\"fp\"(%d)\"model\"(%d,%d,%d)",
      trtTuneSalt, ctx->transformerNHWC ? 1 : 0, usingFP16 ? 16 : 32,
      desc.modelVersion, desc.numInputChannels, desc.numInputGlobalChannels);
    SHA2::get256(tuneDesc.c_str(), buildState.tuneHash);
    if(config->addOptimizationProfile(profile) < 0)
      throw StringError("TensorRT backend: failed to add optimization profile");

    if(prop->major >= 8) {
      // This is to avoid tactics that have shape switching overhead
      config->setTacticSources(1U << static_cast<uint32_t>(TacticSource::kJIT_CONVOLUTIONS));
      config->setBuilderOptimizationLevel(2);
    }

    // For the debug plan dump, build with detailed profiling so the engine inspector can report
    // per-layer precision/format/tactic (see the inspector dump after deserialize).
    if(dumpDebugPlan)
      config->setProfilingVerbosity(ProfilingVerbosity::kDETAILED);

    // So that there are no concurrent kernel executions probably from other parts of code while profiling
    // See CUDA Runtime API document for more details related to NULL stream and synchronization behaviors
    config->setProfileStream(cudaStreamLegacy);

    // Typical runtime allocation is much less than the 1 GiB specified below
    config->setMemoryPoolLimit(MemoryPoolType::kWORKSPACE, 1U << 30);

    string plan;
    {
      static mutex tuneMutex;
      tuneMutex.lock();

      auto cacheDir = HomeData::getHomeDataDir(true, ctx->homeDataDirOverride);
      cacheDir += "/trtcache";
      MakeDir::make(cacheDir);

      uint8_t deviceHash[32];
      SHA2::get256(prop->name, deviceHash);

      // Truncated to 4 bytes
      char deviceIdent[4 * 2 + 1];
      for(int i = 0; i < 4; i++) {
        sprintf(deviceIdent + i * 2, "%02x", static_cast<unsigned char>(deviceHash[i]));
      }
      deviceIdent[sizeof(deviceIdent) - 1] = 0;

#ifdef CACHE_TENSORRT_PLAN
      // The plan cache stores a fully serialized engine, reused only when the model SHA256 (appended
      // to the blob and verified on read) AND paramStr both match. paramStr must therefore encode
      // every knob that changes the built engine: lib/device/salt, board+batch+precision, and
      // NHWC vs NCHW for transformer trunks.
      string buildModeStr = ctx->transformerNHWC ? "onnxnh" : "onnx";
      const char* lenStr = requireExactNNLen ? "ex" : "mx";
      auto planCacheFile = Global::strprintf(
        "%s/trt-%d_gpu-%s_net-%s_s%d_%s_%s%dx%d_b%d_fp%d",
        cacheDir.c_str(),
        getInferLibVersion(),
        deviceIdent,
        loadedModel->modelDesc.name.c_str(),
        trtTuneSalt,
        buildModeStr.c_str(),
        lenStr,
        ctx->nnYLen,
        ctx->nnXLen,
        maxBatchSize,
        usingFP16 ? 16 : 32);
      string paramStr = Global::strprintf(
        "_%d_%s_s%d_%s_%s_%d_%d_%d_%d",
        getInferLibVersion(),
        deviceIdent,
        trtTuneSalt,
        buildModeStr.c_str(),
        lenStr,
        ctx->nnYLen,
        ctx->nnXLen,
        maxBatchSize,
        usingFP16 ? 16 : 32);
      try {
        plan = FileUtils::readFileBinary(planCacheFile);
      } catch(const StringError& e) {
        (void)e;
      };

      if(plan.size() > 0) {
        if(plan.size() < 64 + paramStr.size()) {
          logger->write("Could not parse plan, unexpected size in " + planCacheFile);
          plan.clear();
        } else {
          string cachedParamStr = plan.substr(plan.size() - paramStr.size());
          string modelHash = plan.substr(plan.size() - 64 - paramStr.size(), 64);
          if(modelHash != loadedModel->modelDesc.sha256) {
            logger->write("Plan cache is corrupted or is for the wrong model in " + planCacheFile);
            plan.clear();
          } else if(cachedParamStr != paramStr) {
            logger->write("Plan cache is corrupted or is for the wrong parameters in " + planCacheFile);
            plan.clear();
          } else {
            plan.erase(plan.size() - 64 - paramStr.size());
          }
        }
      }

      if(plan.size() <= 0) {
        logger->write("Creating new plan cache");
        auto planBuffer = unique_ptr<IHostMemory>(builder->buildSerializedNetwork(*buildState.network, *config));
        if(!planBuffer) {
          throw StringError("TensorRT backend: failed to create plan");
        }
        plan.insert(
          plan.end(),
          static_cast<char*>(planBuffer->data()),
          static_cast<char*>(planBuffer->data()) + planBuffer->size());
        if(loadedModel->modelDesc.sha256.size() != 64) {
          throw StringError("Unexpected model hash size");
        }
        plan.insert(plan.end(), loadedModel->modelDesc.sha256.begin(), loadedModel->modelDesc.sha256.end());
        plan.insert(plan.end(), paramStr.begin(), paramStr.end());
        writeFileAtomically(planCacheFile, plan.data(), plan.size());
        logger->write("Saved new plan cache to " + planCacheFile);
        plan.erase(plan.size() - 64 - paramStr.size());
        tuneMutex.unlock();
      } else {
        tuneMutex.unlock();
        logger->write("Using existing plan cache at " + planCacheFile);
      }
#else
      // Truncated to 6 bytes
      char tuneIdent[6 * 2 + 1];
      for(int i = 0; i < 6; i++) {
        sprintf(tuneIdent + i * 2, "%02x", static_cast<unsigned char>(buildState.tuneHash[i]));
      }
      tuneIdent[sizeof(tuneIdent) - 1] = 0;

      auto timingCacheFile = Global::strprintf(
        "%s/trt-%d_gpu-%s_tune-%s_%s%dx%d_b%d_fp%d",
        cacheDir.c_str(),
        getInferLibVersion(),
        deviceIdent,
        tuneIdent,
        requireExactNNLen ? "ex" : "mx",
        ctx->nnYLen,
        ctx->nnXLen,
        maxBatchSize,
        usingFP16 ? 16 : 32);

      string timingCacheBlob;
      try {
        timingCacheBlob = FileUtils::readFileBinary(timingCacheFile);
      } catch(const StringError& e) {
        (void)e;
      };
      if(timingCacheBlob.size() > 0)
        logger->write("Using existing timing cache at " + timingCacheFile);
      else
        logger->write("Creating new timing cache (usingFP16=" + Global::boolToString(usingFP16) + " " + Global::intToString(ctx->nnXLen) + "x" + Global::intToString(ctx->nnYLen) + " maxBatchSizeLimit=" + Global::intToString(maxBatchSize) + ")");

      auto timingCache =
        unique_ptr<ITimingCache>(config->createTimingCache(timingCacheBlob.data(), timingCacheBlob.size()));
      auto invalidTimingCache = !config->setTimingCache(*timingCache, false);
      if(invalidTimingCache) {
        logger->write("Invalid timing cache, using new one instead");
        timingCache.reset(config->createTimingCache(nullptr, 0));
        config->setTimingCache(*timingCache, false);
      }

      unique_ptr<IHostMemory> planBuffer;
      if(invalidTimingCache || !timingCacheBlob.size()) {
        planBuffer.reset(builder->buildSerializedNetwork(*buildState.network, *config));
        if(!planBuffer) {
          throw StringError("TensorRT backend: failed to create plan");
        }
        auto serializedTimingCache = unique_ptr<IHostMemory>(config->getTimingCache()->serialize());
        writeFileAtomically(
          timingCacheFile, static_cast<char*>(serializedTimingCache->data()), serializedTimingCache->size());
        logger->write("Saved new timing cache to " + timingCacheFile);
        tuneMutex.unlock();
      } else {
        tuneMutex.unlock();
        planBuffer.reset(builder->buildSerializedNetwork(*buildState.network, *config));
        if(!planBuffer) {
          throw StringError("TensorRT backend: failed to create plan");
        }
      }
      plan.insert(
        plan.end(),
        static_cast<char*>(planBuffer->data()),
        static_cast<char*>(planBuffer->data()) + planBuffer->size());
#endif
    }

    if(dumpDebugPlan) {
      string planPath = dumpDebugBasePath + ".plan";
      ofstream pofs;
      FileUtils::open(pofs, planPath, ios::out | ios::binary);
      pofs.write(plan.data(), (std::streamsize)plan.size());
      pofs.close();
      logger->write("TensorRT backend: dumped serialized plan to " + planPath);
    }

    runtime.reset(createInferRuntime(trtLogger));
    if(!runtime) {
      throw StringError("TensorRT backend: failed to create runtime");
    }
    trtErrorRecorder.setLogger(logger);
    runtime->setErrorRecorder(&trtErrorRecorder);

    engine.reset(runtime->deserializeCudaEngine(plan.data(), plan.size()));
    if(!engine) {
      throw StringError("TensorRT backend: failed to create cuda engine");
    }
    exec.reset(engine->createExecutionContext());
    if(!exec) {
      throw StringError("TensorRT backend: failed to create execution context");
    }

    // For the debug plan dump, write the built engine's per-layer info (precision, format, tactic) as
    // JSON. This shows the realized graph: which ops fused (Myelin kgen/gemm kernels), the per-tensor
    // Format/Datatype (Half vs Float), and where reformats/casts sit. Note: Myelin-fused kernels do not
    // expose their internal accumulation precision here, so this reveals fusion + boundary types but not
    // FP16-vs-FP32 inside a fused reduction (use a numerical activation comparison for that).
    if(dumpDebugPlan) {
      auto inspector = unique_ptr<IEngineInspector>(engine->createEngineInspector());
      if(inspector) {
        const char* info = inspector->getEngineInformation(LayerInformationFormat::kJSON);
        string outPath = dumpDebugBasePath + ".engine.json";
        std::ofstream ofs(outPath);
        if(info != nullptr) ofs << info;
        ofs.close();
        if(logger != nullptr) logger->write("TensorRT backend: dumped engine layer info to " + outPath);
      }
    }

    for(int i = 0; i < engine->getNbIOTensors(); i++) {
      void* buffer = nullptr;
      auto name = engine->getIOTensorName(i);
      auto dims = engine->getTensorShape(name);
      size_t bytes = accumulate(dims.d + 1, dims.d + dims.nbDims, static_cast<size_t>(maxBatchSize) * sizeof(float), multiplies<>());
      CUDA_ERR("ComputeHandle", cudaMalloc(&buffer, bytes));
      buffers.emplace(make_pair(name, buffer));
      if(!exec->setTensorAddress(name, buffer))
        throw StringError("TensorRT backend: failed to set tensor address for " + string(name));
    }

    if(!exec->setOptimizationProfileAsync(0, cudaStreamPerThread))
      throw StringError("TensorRT backend: failed to select optimization profile");
    CUDA_ERR("ComputeHandle", cudaStreamSynchronize(cudaStreamPerThread));
    trtErrorRecorder.clear();
  }

  ~ComputeHandle() {
    for(auto ptr: buffers) {
      CUDA_ERR("~ComputeHandle", cudaFree(ptr.second));
    }
  }

  ComputeHandle() = delete;
  ComputeHandle(const ComputeHandle&) = delete;
  ComputeHandle& operator=(const ComputeHandle&) = delete;

  void* getBuffer(const char* name) {
    auto search = buffers.find(name);
    if(search != buffers.end()) {
      return search->second;
    } else {
      throw StringError(Global::strprintf("ComputeHandle: unknown tensor name %s", name));
    }
  }

  size_t getBufferBytes(const char* name) {
    auto dims = engine->getTensorShape(name);
    if(dims.nbDims != -1) {
      return accumulate(dims.d + 1, dims.d + dims.nbDims, static_cast<size_t>(maxBatchSize) * sizeof(float), multiplies<>());
    } else {
      throw StringError(Global::strprintf("ComputeHandle: unknown tensor name %s", name));
    }
  }

  size_t getBufferRowElts(const char* name) {
    auto dims = engine->getTensorShape(name);
    if(dims.nbDims != -1) {
      return accumulate(dims.d + 1, dims.d + dims.nbDims, static_cast<size_t>(1), multiplies<>());
    } else {
      throw StringError(Global::strprintf("ComputeHandle: unknown tensor name %s", name));
    }
  }

  Dims getBufferDynamicShape(const char* name, int batchSize) {
    auto dims = engine->getTensorShape(name);
    if(dims.nbDims != -1) {
      dims.d[0] = batchSize;
      return dims;
    } else {
      throw StringError(Global::strprintf("ComputeHandle: unknown tensor name %s", name));
    }
  }

  // DEBUG (kept commented out): when KATAGO_TRT_DUMP_ACTS is set, dump every DBG__ output tensor (added
  // by the ONNX emitter under KATAGO_TRT_DEBUG_ALL_OUTPUTS) to that file: name, shape, min/max/mean/L2,
  // nan/inf counts, and the first few values of the first batch row. One append-block per eval. Running
  // it once for fp32 and once for fp16 on a single isolated position (KATAGO_TEST_ONLY_POS) is how the
  // trunk-tip RMSNorm sum-of-squares FP16 overflow was localized. Uncomment this, the call site after
  // enqueueV3, the emitter block, and the testnnevalcanary.cpp hooks to re-enable.
  // void maybeDumpDebugActivations(int batchSize) {
  //   const char* dumpPath = std::getenv("KATAGO_TRT_DUMP_ACTS");
  //   if(dumpPath == nullptr)
  //     return;
  //   cudaStreamSynchronize(cudaStreamPerThread);
  //   std::ofstream ofs(dumpPath, std::ios::app);
  //   for(auto& kv : buffers) {
  //     const string& name = kv.first;
  //     if(name.rfind("DBG__", 0) != 0)
  //       continue;
  //     auto dims = getBufferDynamicShape(name.c_str(), batchSize);
  //     size_t total = accumulate(dims.d, dims.d + dims.nbDims, (size_t)1, multiplies<size_t>());
  //     vector<float> v(total);
  //     CUDA_ERR("maybeDumpDebugActivations",
  //       cudaMemcpy(v.data(), getBuffer(name.c_str()), total * sizeof(float), cudaMemcpyDeviceToHost));
  //     double mn = 1e30, mx = -1e30, sum = 0.0, sumsq = 0.0;
  //     int nNan = 0, nInf = 0;
  //     for(double x : v) {
  //       if(std::isnan(x)) { nNan++; continue; }
  //       if(std::isinf(x)) { nInf++; continue; }
  //       mn = std::min(mn, x); mx = std::max(mx, x); sum += x; sumsq += x * x;
  //     }
  //     size_t rowElts = total / (size_t)dims.d[0];
  //     ofs << name << " shape=[";
  //     for(int d = 0; d < dims.nbDims; d++) ofs << dims.d[d] << (d + 1 < dims.nbDims ? "," : "");
  //     ofs << "] min=" << mn << " max=" << mx << " mean=" << (sum / total)
  //         << " l2=" << std::sqrt(sumsq) << " nan=" << nNan << " inf=" << nInf << " first:";
  //     for(size_t i = 0; i < rowElts && i < 8; i++) ofs << " " << v[i];
  //     ofs << "\n";
  //   }
  //   ofs.close();
  // }

  void printDebugOutput(int batchSize) {
    for(auto& debugOutput: debugOutputs) {
      auto name = debugOutput.first;
      auto desc = debugOutput.second;
      auto dims = getBufferDynamicShape(name.c_str(), batchSize);

      vector<float> values(accumulate(dims.d, dims.d + dims.nbDims, static_cast<size_t>(1), multiplies<>()));
      CUDA_ERR(
        "printDebugOutput",
        cudaMemcpy(values.data(), getBuffer(name.c_str()), values.size() * sizeof(float), cudaMemcpyDeviceToHost));

      cout << "=========================================================" << endl;
      cout << desc << endl;
      int i = 0;
      if(dims.nbDims == 2) {
        for(int n = 0; n < dims.d[0]; n++) {
          cout << "-(n=" << n << ")--------------------" << endl;
          for(int c = 0; c < dims.d[1]; c++) {
            cout << values[i++] << " ";
          }
          cout << endl;
        }
        cout << endl;
      } else if(dims.nbDims == 4) {
        for(int n = 0; n < dims.d[0]; n++) {
          cout << "-(n=" << n << ")--------------------" << endl;
          for(int c = 0; c < dims.d[1]; c++) {
            cout << "(c=" << c << ")" << endl;
            for(int y = 0; y < dims.d[2]; y++) {
              for(int x = 0; x < dims.d[3]; x++)
                cout << values[i++] << " ";
              cout << endl;
            }
            cout << endl;
          }
        }
      }
      cout << "=========================================================" << endl;
    }
  }
};

ComputeHandle* NeuralNet::createComputeHandle(
  ComputeContext* context,
  const LoadedModel* loadedModel,
  Logger* logger,
  int maxBatchSize,
  bool requireExactNNLen,
  bool inputsUseNHWC,
  int gpuIdxForThisThread,
  int serverThreadIdx
) {
  if(inputsUseNHWC) {
    throw StringError("TensorRT backend: inputsUseNHWC = false required, other configurations not supported");
  }

  // Use whatever CUDA believes GPU 0 to be.
  if(gpuIdxForThisThread == -1)
    gpuIdxForThisThread = 0;
  CUDA_ERR("createComputeHandle", cudaSetDevice(gpuIdxForThisThread));

  cudaDeviceProp prop;
  CUDA_ERR("createComputeHandle", cudaGetDeviceProperties(&prop, gpuIdxForThisThread));

  if(logger != NULL) {
    logger->write(
      "TensorRT backend thread " + Global::intToString(serverThreadIdx) + ": Found GPU " + string(prop.name) +
      " memory " + Global::uint64ToString(prop.totalGlobalMem) + " compute capability major " +
      Global::intToString(prop.major) + " minor " + Global::intToString(prop.minor));
    logger->write(
      "TensorRT backend thread " + Global::intToString(serverThreadIdx) + ": Initializing (may take a long time)");
  }

  auto handle = new ComputeHandle(logger, &prop, context, loadedModel, maxBatchSize, requireExactNNLen);

  if(logger != NULL) {
    logger->write(
      "TensorRT backend thread " + Global::intToString(serverThreadIdx) + ": Model version " +
      Global::intToString(loadedModel->modelDesc.modelVersion) +
      " useFP16 = " + Global::boolToString(handle->usingFP16));
    logger->write(
      "TensorRT backend thread " + Global::intToString(serverThreadIdx) +
      ": Model name: " + loadedModel->modelDesc.name +
      " (" + loadedModel->modelDesc.getShortInfoString() + ")");
  }

  return handle;
}

void NeuralNet::freeComputeHandle(ComputeHandle* gpuHandle) {
  delete gpuHandle;
}

bool NeuralNet::isUsingFP16(const ComputeHandle* gpuHandle) {
  return gpuHandle->usingFP16;
}

bool NeuralNet::setIsWarmup(const ComputeHandle* gpuHandle, bool isWarmup) {
  (void)gpuHandle;
  (void)isWarmup;
  return false;
}

void NeuralNet::printDevices() {
  int numDevices = 0;
  CUDA_ERR("printDevices", cudaGetDeviceCount(&numDevices));
  for(int i = 0; i < numDevices; i++) {
    cudaDeviceProp prop;
    CUDA_ERR("printDevices", cudaGetDeviceProperties(&prop, i));
    cout << "Found GPU device " << i << ": " << prop.name << endl;
  }
}

struct InputBuffers {
  int maxBatchSize;

  size_t singleMaskElts;
  size_t singleMaskBytes;
  size_t singleInputElts;
  size_t singleInputBytes;
  size_t singleInputGlobalElts;
  size_t singleInputGlobalBytes;
  size_t singleInputMetaElts;
  size_t singleInputMetaBytes;
  size_t singlePolicyPassResultElts;
  size_t singlePolicyPassResultBytes;
  size_t singlePolicyResultElts;
  size_t singlePolicyResultBytes;
  size_t singleValueResultElts;
  size_t singleValueResultBytes;
  size_t singleScoreValueResultElts;
  size_t singleScoreValueResultBytes;
  size_t singleOwnershipResultElts;
  size_t singleOwnershipResultBytes;

  size_t inputMaskBufferBytes;
  size_t inputSpatialBufferBytes;
  size_t inputGlobalBufferBytes;
  size_t inputMetaBufferBytes;
  size_t policyPassResultBufferBytes;
  size_t policyResultBufferBytes;
  size_t valueResultBufferBytes;
  size_t scoreValueResultBufferBytes;
  size_t ownershipResultBufferBytes;

  unique_ptr<float[]> maskInputs;           // Host pointer
  unique_ptr<float[]> spatialInputs;        // Host pointer
  unique_ptr<float[]> globalInputs;  // Host pointer
  unique_ptr<float[]> metaInputs;  // Host pointer
  unique_ptr<float[]> policyPassResults;    // Host pointer
  unique_ptr<float[]> policyResults;        // Host pointer
  unique_ptr<float[]> valueResults;         // Host pointer
  unique_ptr<float[]> scoreValueResults;    // Host pointer
  unique_ptr<float[]> ownershipResults;     // Host pointer

  InputBuffers(const LoadedModel* loadedModel, int maxBatchSz, int nnXLen, int nnYLen) {
    const ModelDesc& m = loadedModel->modelDesc;

    if(nnXLen > NNPos::MAX_BOARD_LEN_X)
      throw StringError(
        Global::strprintf("nnXLen (%d) is greater than NNPos::MAX_BOARD_LEN_X (%d)", nnXLen, NNPos::MAX_BOARD_LEN_X));
    if(nnYLen > NNPos::MAX_BOARD_LEN_Y)
      throw StringError(
        Global::strprintf("nnYLen (%d) is greater than NNPos::MAX_BOARD_LEN_Y (%d)", nnYLen, NNPos::MAX_BOARD_LEN_Y));

    maxBatchSize = maxBatchSz;
    singleMaskElts = nnXLen * nnYLen;
    singleMaskBytes = singleMaskElts * sizeof(float);
    singleInputElts = m.numInputChannels * nnXLen * nnYLen;
    singleInputBytes = singleInputElts * sizeof(float);
    singleInputGlobalElts = m.numInputGlobalChannels;
    singleInputGlobalBytes = singleInputGlobalElts * sizeof(float);
    singleInputMetaElts = m.numInputMetaChannels;
    singleInputMetaBytes = singleInputMetaElts * sizeof(float);
    singlePolicyPassResultElts = (size_t)m.numPolicyChannels;
    singlePolicyPassResultBytes = singlePolicyPassResultElts * sizeof(float);
    singlePolicyResultElts = (size_t)m.numPolicyChannels * nnXLen * nnYLen;
    singlePolicyResultBytes = singlePolicyResultElts * sizeof(float);
    singleValueResultElts = m.numValueChannels;
    singleValueResultBytes = singleValueResultElts * sizeof(float);
    singleScoreValueResultElts = m.numScoreValueChannels;
    singleScoreValueResultBytes = singleScoreValueResultElts * sizeof(float);
    singleOwnershipResultElts = m.numOwnershipChannels * nnXLen * nnYLen;
    singleOwnershipResultBytes = singleOwnershipResultElts * sizeof(float);

    testAssert(NNModelVersion::getNumSpatialFeatures(m.modelVersion, m.isDotsGame) == m.numInputChannels);
    testAssert(NNModelVersion::getNumGlobalFeatures(m.modelVersion, m.isDotsGame) == m.numInputGlobalChannels);
    if(m.numInputMetaChannels > 0) {
      testAssert(SGFMetadata::METADATA_INPUT_NUM_CHANNELS == m.numInputMetaChannels);
    }

    inputMaskBufferBytes = maxBatchSize * singleMaskBytes;
    inputSpatialBufferBytes = maxBatchSize * singleInputBytes;
    inputGlobalBufferBytes = maxBatchSize * singleInputGlobalBytes;
    inputMetaBufferBytes = maxBatchSize * singleInputMetaBytes;
    policyPassResultBufferBytes = maxBatchSize * singlePolicyPassResultBytes;
    policyResultBufferBytes = maxBatchSize * singlePolicyResultBytes;
    valueResultBufferBytes = maxBatchSize * singleValueResultBytes;
    scoreValueResultBufferBytes = maxBatchSize * singleScoreValueResultBytes;
    ownershipResultBufferBytes = maxBatchSize * singleOwnershipResultBytes;

    maskInputs = make_unique<float[]>(maxBatchSize * singleMaskElts);
    spatialInputs = make_unique<float[]>(maxBatchSize * singleInputElts);
    globalInputs = make_unique<float[]>(maxBatchSize * singleInputGlobalElts);
    metaInputs = make_unique<float[]>(maxBatchSize * singleInputMetaElts);
    policyPassResults = make_unique<float[]>(maxBatchSize * singlePolicyPassResultElts);
    policyResults = make_unique<float[]>(maxBatchSize * singlePolicyResultElts);
    valueResults = make_unique<float[]>(maxBatchSize * singleValueResultElts);
    scoreValueResults = make_unique<float[]>(maxBatchSize * singleScoreValueResultElts);
    ownershipResults = make_unique<float[]>(maxBatchSize * singleOwnershipResultElts);
  }

  InputBuffers() = delete;
  InputBuffers(const InputBuffers&) = delete;
  InputBuffers& operator=(const InputBuffers&) = delete;
};

InputBuffers* NeuralNet::createInputBuffers(const LoadedModel* loadedModel, int maxBatchSize, int nnXLen, int nnYLen) {
  return new InputBuffers(loadedModel, maxBatchSize, nnXLen, nnYLen);
}

void NeuralNet::freeInputBuffers(InputBuffers* inputBuffers) {
  delete inputBuffers;
}

void NeuralNet::getOutput(
  ComputeHandle* gpuHandle,
  InputBuffers* inputBuffers,
  int numBatchEltsFilled,
  NNResultBuf** inputBufs,
  const vector<NNOutput*>& outputs,
  const bool dotsGame) {
  assert(numBatchEltsFilled <= inputBuffers->maxBatchSize);
  assert(numBatchEltsFilled > 0);

  const int batchSize = numBatchEltsFilled;
  const int nnXLen = gpuHandle->ctx->nnXLen;
  const int nnYLen = gpuHandle->ctx->nnYLen;
  const int modelVersion = gpuHandle->modelVersion;

  const int numSpatialFeatures = NNModelVersion::getNumSpatialFeatures(modelVersion, dotsGame);
  const int numGlobalFeatures = NNModelVersion::getNumGlobalFeatures(modelVersion, dotsGame);
  const int numMetaFeatures = inputBuffers->singleInputMetaElts;
  assert(numSpatialFeatures * nnXLen * nnYLen == inputBuffers->singleInputElts);
  assert(numGlobalFeatures == inputBuffers->singleInputGlobalElts);

  for(int nIdx = 0; nIdx < batchSize; nIdx++) {
    float* rowMaskInput = &inputBuffers->maskInputs[inputBuffers->singleMaskElts * nIdx];
    float* rowSpatialInput = &inputBuffers->spatialInputs[inputBuffers->singleInputElts * nIdx];
    float* rowGlobalInput = &inputBuffers->globalInputs[inputBuffers->singleInputGlobalElts * nIdx];
    float* rowMetaInput = &inputBuffers->metaInputs[inputBuffers->singleInputMetaElts * nIdx];

    const float* rowGlobal = inputBufs[nIdx]->rowGlobalBuf.data();
    const float* rowSpatial = inputBufs[nIdx]->rowSpatialBuf.data();
    const float* rowMeta = inputBufs[nIdx]->rowMetaBuf.data();
    const bool hasRowMeta = inputBufs[nIdx]->hasRowMeta;
    copy(rowGlobal, rowGlobal + numGlobalFeatures, rowGlobalInput);
    std::copy(rowGlobal,rowGlobal+numGlobalFeatures,rowGlobalInput);
    if(numMetaFeatures > 0) {
      testAssert(rowMeta != NULL);
      testAssert(hasRowMeta);
      std::copy(rowMeta,rowMeta+numMetaFeatures,rowMetaInput);
    }
    else {
      testAssert(!hasRowMeta);
    }
    SymmetryHelpers::copyInputsWithSymmetry(
      rowSpatial, rowSpatialInput, 1, nnYLen, nnXLen, numSpatialFeatures, false, inputBufs[nIdx]->symmetry);
    copy(rowSpatialInput, rowSpatialInput + inputBuffers->singleMaskElts, rowMaskInput);
  }

  assert(inputBuffers->singleMaskElts == gpuHandle->getBufferRowElts("InputMask"));
  assert(inputBuffers->singleInputElts == gpuHandle->getBufferRowElts("InputSpatial"));
  assert(inputBuffers->singleInputGlobalElts == gpuHandle->getBufferRowElts("InputGlobal"));
  if(numMetaFeatures > 0)
    assert(inputBuffers->singleInputMetaElts == gpuHandle->getBufferRowElts("InputMeta"));
  assert(inputBuffers->singlePolicyPassResultElts == gpuHandle->getBufferRowElts("OutputPolicyPass"));
  assert(inputBuffers->singlePolicyResultElts == gpuHandle->getBufferRowElts("OutputPolicy"));
  assert(inputBuffers->singleValueResultElts == gpuHandle->getBufferRowElts("OutputValue"));
  assert(inputBuffers->singleScoreValueResultElts == gpuHandle->getBufferRowElts("OutputScoreValue"));
  assert(inputBuffers->singleOwnershipResultElts == gpuHandle->getBufferRowElts("OutputOwnership"));

  assert(inputBuffers->inputMaskBufferBytes == gpuHandle->getBufferBytes("InputMask"));
  assert(inputBuffers->inputSpatialBufferBytes == gpuHandle->getBufferBytes("InputSpatial"));
  assert(inputBuffers->inputGlobalBufferBytes == gpuHandle->getBufferBytes("InputGlobal"));
  if(numMetaFeatures > 0)
    assert(inputBuffers->inputMetaBufferBytes == gpuHandle->getBufferBytes("InputMeta"));
  assert(inputBuffers->policyPassResultBufferBytes == gpuHandle->getBufferBytes("OutputPolicyPass"));
  assert(inputBuffers->policyResultBufferBytes == gpuHandle->getBufferBytes("OutputPolicy"));
  assert(inputBuffers->valueResultBufferBytes == gpuHandle->getBufferBytes("OutputValue"));
  assert(inputBuffers->scoreValueResultBufferBytes == gpuHandle->getBufferBytes("OutputScoreValue"));
  assert(inputBuffers->ownershipResultBufferBytes == gpuHandle->getBufferBytes("OutputOwnership"));

  const int numPolicyChannels = inputBuffers->singlePolicyPassResultElts;
  assert(inputBuffers->singlePolicyResultElts == numPolicyChannels * nnXLen * nnYLen);

  // Transfers from host memory to device memory are asynchronous with respect to the host
  CUDA_ERR(
    "getOutput",
    cudaMemcpyAsync(
      gpuHandle->getBuffer("InputMask"),
      inputBuffers->maskInputs.get(),
      inputBuffers->singleMaskBytes * batchSize,
      cudaMemcpyHostToDevice));
  CUDA_ERR(
    "getOutput",
    cudaMemcpyAsync(
      gpuHandle->getBuffer("InputSpatial"),
      inputBuffers->spatialInputs.get(),
      inputBuffers->singleInputBytes * batchSize,
      cudaMemcpyHostToDevice));
  CUDA_ERR(
    "getOutput",
    cudaMemcpyAsync(
      gpuHandle->getBuffer("InputGlobal"),
      inputBuffers->globalInputs.get(),
      inputBuffers->singleInputGlobalBytes * batchSize,
      cudaMemcpyHostToDevice));
  if(numMetaFeatures > 0) {
    CUDA_ERR(
      "getOutput",
      cudaMemcpyAsync(
        gpuHandle->getBuffer("InputMeta"),
        inputBuffers->metaInputs.get(),
        inputBuffers->singleInputMetaBytes * batchSize,
        cudaMemcpyHostToDevice));
  }

  auto maskInputDims = gpuHandle->getBufferDynamicShape("InputMask", batchSize);
  auto spatialInputDims = gpuHandle->getBufferDynamicShape("InputSpatial", batchSize);
  auto globalInputDims = gpuHandle->getBufferDynamicShape("InputGlobal", batchSize);

  if(
    !gpuHandle->exec->setInputShape("InputMask", maskInputDims) ||
    !gpuHandle->exec->setInputShape("InputSpatial", spatialInputDims) ||
    !gpuHandle->exec->setInputShape("InputGlobal", globalInputDims)
  )
    throw StringError("TensorRT backend: failed to set input shapes");

  if(numMetaFeatures > 0) {
    auto metaInputDims = gpuHandle->getBufferDynamicShape("InputMeta", batchSize);
    if(!gpuHandle->exec->setInputShape("InputMeta", metaInputDims))
      throw StringError("TensorRT backend: failed to set InputMeta shape");
  }

  if(!gpuHandle->exec->enqueueV3(cudaStreamPerThread))
    throw StringError("TensorRT backend: enqueueV3 failed");

  CUDA_ERR(
    "getOutput",
    cudaMemcpy(
      inputBuffers->policyPassResults.get(),
      gpuHandle->getBuffer("OutputPolicyPass"),
      inputBuffers->singlePolicyPassResultBytes * batchSize,
      cudaMemcpyDeviceToHost));
  CUDA_ERR(
    "getOutput",
    cudaMemcpy(
      inputBuffers->policyResults.get(),
      gpuHandle->getBuffer("OutputPolicy"),
      inputBuffers->singlePolicyResultBytes * batchSize,
      cudaMemcpyDeviceToHost));
  CUDA_ERR(
    "getOutput",
    cudaMemcpy(
      inputBuffers->valueResults.get(),
      gpuHandle->getBuffer("OutputValue"),
      inputBuffers->singleValueResultBytes * batchSize,
      cudaMemcpyDeviceToHost));
  CUDA_ERR(
    "getOutput",
    cudaMemcpy(
      inputBuffers->scoreValueResults.get(),
      gpuHandle->getBuffer("OutputScoreValue"),
      inputBuffers->singleScoreValueResultBytes * batchSize,
      cudaMemcpyDeviceToHost));
  CUDA_ERR(
    "getOutput",
    cudaMemcpy(
      inputBuffers->ownershipResults.get(),
      gpuHandle->getBuffer("OutputOwnership"),
      inputBuffers->singleOwnershipResultBytes * batchSize,
      cudaMemcpyDeviceToHost));

  gpuHandle->printDebugOutput(batchSize);
  gpuHandle->trtErrorRecorder.clear();

  assert(outputs.size() == batchSize);

  float policyProbsTmp[NNPos::MAX_NN_POLICY_SIZE];

  for(int row = 0; row < batchSize; row++) {
    NNOutput* output = outputs[row];

    assert(output->nnXLen == nnXLen);
    assert(output->nnYLen == nnYLen);
    float policyOptimism = (float)inputBufs[row]->policyOptimism;

    const float* policyPassSrcBuf = &inputBuffers->policyPassResults[row * inputBuffers->singlePolicyPassResultElts];
    const float* policySrcBuf = &inputBuffers->policyResults[row * inputBuffers->singlePolicyResultElts];
    float* policyProbs = output->policyProbs;

    // These are in logits, the client does the postprocessing to turn them into
    // policy probabilities and white game outcome probabilities
    // Also we don't fill in the nnHash here either
    // Handle version >= 12 policy optimism
    if(numPolicyChannels == 2 || (numPolicyChannels == 4 && modelVersion >= 16)) {
      // TRT is all NCHW
      for(int i = 0; i < nnXLen * nnYLen; i++) {
        float p = policySrcBuf[i];
        float pOpt = policySrcBuf[i + nnXLen * nnYLen];
        policyProbsTmp[i] = p + (pOpt - p) * policyOptimism;
      }
      SymmetryHelpers::copyOutputsWithSymmetry(
        policyProbsTmp, policyProbs, 1, nnYLen, nnXLen, inputBufs[row]->symmetry);
      policyProbs[nnXLen * nnYLen] = policyPassSrcBuf[0] + (policyPassSrcBuf[1] - policyPassSrcBuf[0]) * policyOptimism;
    } else {
      assert(numPolicyChannels == 1);
      SymmetryHelpers::copyOutputsWithSymmetry(policySrcBuf, policyProbs, 1, nnYLen, nnXLen, inputBufs[row]->symmetry);
      policyProbs[nnXLen * nnYLen] = policyPassSrcBuf[0];
    }

    int numValueChannels = inputBuffers->singleValueResultElts;
    assert(numValueChannels == 3);
    output->whiteWinProb = inputBuffers->valueResults[row * numValueChannels];
    output->whiteLossProb = inputBuffers->valueResults[row * numValueChannels + 1];
    output->whiteNoResultProb = inputBuffers->valueResults[row * numValueChannels + 2];

    // As above, these are NOT actually from white's perspective, but rather the player to move.
    // As usual the client does the postprocessing.
    if(output->whiteOwnerMap != NULL) {
      const float* ownershipSrcBuf = &inputBuffers->ownershipResults[row * nnXLen * nnYLen];
      assert(inputBuffers->singleOwnershipResultElts == nnXLen * nnYLen);
      SymmetryHelpers::copyOutputsWithSymmetry(
        ownershipSrcBuf, output->whiteOwnerMap, 1, nnYLen, nnXLen, inputBufs[row]->symmetry);
    }

    int numScoreValueChannels = inputBuffers->singleScoreValueResultElts;
    if(modelVersion >= 9) {
      assert(numScoreValueChannels == 6);
      output->whiteScoreMean = inputBuffers->scoreValueResults[row * numScoreValueChannels];
      output->whiteScoreMeanSq = inputBuffers->scoreValueResults[row * numScoreValueChannels + 1];
      output->whiteLead = inputBuffers->scoreValueResults[row * numScoreValueChannels + 2];
      output->varTimeLeft = inputBuffers->scoreValueResults[row * numScoreValueChannels + 3];
      output->shorttermWinlossError = inputBuffers->scoreValueResults[row * numScoreValueChannels + 4];
      output->shorttermScoreError = inputBuffers->scoreValueResults[row * numScoreValueChannels + 5];
    } else if(modelVersion >= 8) {
      assert(numScoreValueChannels == 4);
      output->whiteScoreMean = inputBuffers->scoreValueResults[row * numScoreValueChannels];
      output->whiteScoreMeanSq = inputBuffers->scoreValueResults[row * numScoreValueChannels + 1];
      output->whiteLead = inputBuffers->scoreValueResults[row * numScoreValueChannels + 2];
      output->varTimeLeft = inputBuffers->scoreValueResults[row * numScoreValueChannels + 3];
      output->shorttermWinlossError = 0;
      output->shorttermScoreError = 0;
    } else if(modelVersion >= 4) {
      assert(numScoreValueChannels == 2);
      output->whiteScoreMean = inputBuffers->scoreValueResults[row * numScoreValueChannels];
      output->whiteScoreMeanSq = inputBuffers->scoreValueResults[row * numScoreValueChannels + 1];
      output->whiteLead = output->whiteScoreMean;
      output->varTimeLeft = 0;
      output->shorttermWinlossError = 0;
      output->shorttermScoreError = 0;
    } else if(modelVersion >= 3) {
      assert(numScoreValueChannels == 1);
      output->whiteScoreMean = inputBuffers->scoreValueResults[row * numScoreValueChannels];
      // Version 3 neural nets don't have any second moment output, implicitly already folding it in, so we just use the
      // mean squared
      output->whiteScoreMeanSq = output->whiteScoreMean * output->whiteScoreMean;
      output->whiteLead = output->whiteScoreMean;
      output->varTimeLeft = 0;
      output->shorttermWinlossError = 0;
      output->shorttermScoreError = 0;
    } else {
      ASSERT_UNREACHABLE;
    }
  }
}

bool NeuralNet::testEvaluateConv(
  const ConvLayerDesc* desc,
  int batchSize,
  int nnXLen,
  int nnYLen,
  bool useFP16,
  bool useNHWC,
  const vector<float>& inputBuffer,
  vector<float>& outputBuffer) {
  (void)desc;
  (void)batchSize;
  (void)nnXLen;
  (void)nnYLen;
  (void)useFP16;
  (void)useNHWC;
  (void)inputBuffer;
  (void)outputBuffer;
  return false;
}

// Mask should be in 'NHW' format (no "C" channel).
bool NeuralNet::testEvaluateBatchNorm(
  const BatchNormLayerDesc* desc,
  int batchSize,
  int nnXLen,
  int nnYLen,
  bool useFP16,
  bool useNHWC,
  const vector<float>& inputBuffer,
  const vector<float>& maskBuffer,
  vector<float>& outputBuffer) {
  (void)desc;
  (void)batchSize;
  (void)nnXLen;
  (void)nnYLen;
  (void)useFP16;
  (void)useNHWC;
  (void)inputBuffer;
  (void)maskBuffer;
  (void)outputBuffer;
  return false;
}

bool NeuralNet::testEvaluateResidualBlock(
  const ResidualBlockDesc* desc,
  int batchSize,
  int nnXLen,
  int nnYLen,
  bool useFP16,
  bool useNHWC,
  const vector<float>& inputBuffer,
  const vector<float>& maskBuffer,
  vector<float>& outputBuffer) {
  (void)desc;
  (void)batchSize;
  (void)nnXLen;
  (void)nnYLen;
  (void)useFP16;
  (void)useNHWC;
  (void)inputBuffer;
  (void)maskBuffer;
  (void)outputBuffer;
  return false;
}

bool NeuralNet::testEvaluateGlobalPoolingResidualBlock(
  const GlobalPoolingResidualBlockDesc* desc,
  int batchSize,
  int nnXLen,
  int nnYLen,
  bool useFP16,
  bool useNHWC,
  const vector<float>& inputBuffer,
  const vector<float>& maskBuffer,
  vector<float>& outputBuffer) {
  (void)desc;
  (void)batchSize;
  (void)nnXLen;
  (void)nnYLen;
  (void)useFP16;
  (void)useNHWC;
  (void)inputBuffer;
  (void)maskBuffer;
  (void)outputBuffer;
  return false;
}

#endif  // USE_TENSORRT_BACKEND
