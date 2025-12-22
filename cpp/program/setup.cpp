#include "../program/setup.h"

#include "../core/datetime.h"
#include "../core/makedir.h"
#include "../core/fileutils.h"
#include "../neuralnet/nninterface.h"
#include "../search/patternbonustable.h"

using namespace std;

void Setup::initializeSession(ConfigParser& cfg) {
  (void)cfg;
  NeuralNet::globalInitialize();
}

std::vector<std::string> Setup::getBackendPrefixes() {
  std::vector<std::string> prefixes;
  prefixes.push_back("cuda");
  prefixes.push_back("trt");
  prefixes.push_back("metal");
  prefixes.push_back("opencl");
  prefixes.push_back("eigen");
  prefixes.push_back("dummybackend");
  return prefixes;
}

NNEvaluator* Setup::initializeNNEvaluator(
  const string& nnModelName,
  const string& nnModelFile,
  const string& expectedSha256,
  ConfigParser& cfg,
  Logger& logger,
  Rand& seedRand,
  int expectedConcurrentEvals,
  int defaultNNXLen,
  int defaultNNYLen,
  int defaultMaxBatchSize,
  bool defaultRequireExactNNLen,
  bool disableFP16,
  setup_for_t setupFor
) {
  vector<NNEvaluator*> nnEvals =
    initializeNNEvaluators(
      {nnModelName},
      {nnModelFile},
      {expectedSha256},
      cfg,
      logger,
      seedRand,
      expectedConcurrentEvals,
      defaultNNXLen,
      defaultNNYLen,
      defaultMaxBatchSize,
      defaultRequireExactNNLen,
      disableFP16,
      setupFor
    );
  assert(nnEvals.size() == 1);
  return nnEvals[0];
}

vector<NNEvaluator*> Setup::initializeNNEvaluators(
  const vector<string>& nnModelNames,
  const vector<string>& nnModelFiles,
  const vector<string>& expectedSha256s,
  ConfigParser& cfg,
  Logger& logger,
  Rand& seedRand,
  int expectedConcurrentEvals,
  int defaultNNXLen,
  int defaultNNYLen,
  int defaultMaxBatchSize,
  bool defaultRequireExactNNLen,
  bool disableFP16,
  setup_for_t setupFor
) {
  vector<NNEvaluator*> nnEvals;
  assert(nnModelNames.size() == nnModelFiles.size());
  assert(expectedSha256s.size() == 0 || expectedSha256s.size() == nnModelFiles.size());

  #if defined(USE_CUDA_BACKEND)
  string backendPrefix = "cuda";
  #elif defined(USE_TENSORRT_BACKEND)
  string backendPrefix = "trt";
  #elif defined(USE_METAL_BACKEND)
  string backendPrefix = "metal";
  #elif defined(USE_OPENCL_BACKEND)
  string backendPrefix = "opencl";
  #elif defined(USE_EIGEN_BACKEND)
  string backendPrefix = "eigen";
  #else
  string backendPrefix = "dummybackend";
  #endif

  //Automatically flag keys that are for other backends as used so that we don't warn about unused keys
  //for those options
  for(const string& prefix: getBackendPrefixes()) {
    if(prefix != backendPrefix)
      cfg.markAllKeysUsedWithPrefix(prefix);
  }

  for(size_t i = 0; i<nnModelFiles.size(); i++) {
    string idxStr = Global::uint64ToString(i);
    const string& nnModelName = nnModelNames[i];
    const string& nnModelFile = nnModelFiles[i];
    const string& expectedSha256 = expectedSha256s.size() > 0 ? expectedSha256s[i]: "";

    bool debugSkipNeuralNetDefault = (nnModelFile == "/dev/null");
    bool debugSkipNeuralNet =
      setupFor == SETUP_FOR_DISTRIBUTED ? debugSkipNeuralNetDefault :
      cfg.getBoolOrDefault("debugSkipNeuralNet", debugSkipNeuralNetDefault);

    int nnXLen = std::max(defaultNNXLen,2);
    int nnYLen = std::max(defaultNNYLen,2);
    if(setupFor != SETUP_FOR_DISTRIBUTED) {
      (void)(cfg.tryGetInt("maxBoardXSizeForNNBuffer" + idxStr, nnXLen, 2, NNPos::MAX_BOARD_LEN_X) ||
        cfg.tryGetInt("maxBoardXSizeForNNBuffer", nnXLen, 2, NNPos::MAX_BOARD_LEN_X) ||
        cfg.tryGetInt("maxBoardSizeForNNBuffer" + idxStr, nnXLen, 2, NNPos::MAX_BOARD_LEN_X) ||
        cfg.tryGetInt("maxBoardSizeForNNBuffer", nnXLen, 2, NNPos::MAX_BOARD_LEN_X));

      (void)(cfg.tryGetInt("maxBoardYSizeForNNBuffer" + idxStr, nnYLen, 2, NNPos::MAX_BOARD_LEN_Y) ||
        cfg.tryGetInt("maxBoardYSizeForNNBuffer", nnYLen, 2, NNPos::MAX_BOARD_LEN_Y) ||
        cfg.tryGetInt("maxBoardSizeForNNBuffer" + idxStr, nnYLen, 2, NNPos::MAX_BOARD_LEN_Y) ||
        cfg.tryGetInt("maxBoardSizeForNNBuffer", nnYLen, 2, NNPos::MAX_BOARD_LEN_Y));
    }

    bool requireExactNNLen = defaultRequireExactNNLen;
    if(setupFor != SETUP_FOR_DISTRIBUTED) {
      (void)(cfg.tryGetBool("requireMaxBoardSize" + idxStr, requireExactNNLen) ||
        cfg.tryGetBool("requireMaxBoardSize", requireExactNNLen));
    }

    bool inputsUseNHWC = backendPrefix != "opencl" && backendPrefix != "trt" && backendPrefix != "metal";
    (void)(cfg.tryGetBool(backendPrefix+"InputsUseNHWC"+idxStr, inputsUseNHWC) ||
      cfg.tryGetBool("inputsUseNHWC"+idxStr, inputsUseNHWC) ||
      cfg.tryGetBool(backendPrefix+"InputsUseNHWC", inputsUseNHWC) ||
      cfg.tryGetBool("inputsUseNHWC", inputsUseNHWC));

    bool nnRandomize;
    string nnRandSeed;

    if (setupFor == SETUP_FOR_DISTRIBUTED) {
      nnRandomize = true;
      nnRandSeed = Global::uint64ToString(seedRand.nextUInt64());
    } else {
      nnRandomize = cfg.getBoolOrDefault("nnRandomize", true);
      if (!cfg.tryGetString("nnRandSeed" + idxStr, nnRandSeed) &&
          !cfg.tryGetString("nnRandSeed", nnRandSeed)) {
        nnRandSeed = Global::uint64ToString(seedRand.nextUInt64());
      }
    }

    logger.write("nnRandSeed" + idxStr + " = " + nnRandSeed);

#ifndef USE_EIGEN_BACKEND
    (void)expectedConcurrentEvals;
    cfg.markAllKeysUsedWithPrefix("numEigenThreadsPerModel");
    int numNNServerThreadsPerModel = cfg.getIntOrDefault("numNNServerThreadsPerModel", 1, 1024, 1);
#else
    cfg.markAllKeysUsedWithPrefix("numNNServerThreadsPerModel");
    int numNNServerThreadsPerModel = cfg.getIntOrDefault("numEigenThreadsPerModel", 1, 1024, computeDefaultEigenBackendThreads(expectedConcurrentEvals,logger));
#endif

    vector<int> gpuIdxByServerThread;
    for(int j = 0; j<numNNServerThreadsPerModel; j++) {
      string threadIdxStr = Global::intToString(j);
      int idx = -1;
      constexpr int min = 0;
      constexpr int max = 1023;
      (void)(cfg.tryGetInt(backendPrefix+"DeviceToUseModel"+idxStr+"Thread"+threadIdxStr, idx, min, max) ||
        cfg.tryGetInt(backendPrefix+"GpuToUseModel"+idxStr+"Thread"+threadIdxStr, idx, min, max) ||
        cfg.tryGetInt("deviceToUseModel"+idxStr+"Thread"+threadIdxStr, idx, min, max) ||
        cfg.tryGetInt("gpuToUseModel"+idxStr+"Thread"+threadIdxStr, idx, min, max) ||
        cfg.tryGetInt(backendPrefix+"DeviceToUseModel"+idxStr, idx, min, max) ||
        cfg.tryGetInt(backendPrefix+"GpuToUseModel"+idxStr, idx, min, max) ||
        cfg.tryGetInt("deviceToUseModel"+idxStr, idx, min, max) ||
        cfg.tryGetInt("gpuToUseModel"+idxStr, idx, min, max) ||
        cfg.tryGetInt(backendPrefix+"DeviceToUseThread"+threadIdxStr, idx, min, max) ||
        cfg.tryGetInt(backendPrefix+"GpuToUseThread"+threadIdxStr, idx, min, max) ||
        cfg.tryGetInt("deviceToUseThread"+threadIdxStr, idx, min, max) ||
        cfg.tryGetInt("gpuToUseThread"+threadIdxStr, idx, min, max) ||
        cfg.tryGetInt(backendPrefix+"DeviceToUse", idx, min, max) ||
        cfg.tryGetInt(backendPrefix+"GpuToUse", idx, min, max) ||
        cfg.tryGetInt("deviceToUse", idx, min, max) ||
        cfg.tryGetInt("gpuToUse", idx, min, max));
      gpuIdxByServerThread.push_back(idx);
    }

    string homeDataDirOverride = loadHomeDataDirOverride(cfg);

    string openCLTunerFile = cfg.getStringOrDefault("openclTunerFile", "");
    bool openCLReTunePerBoardSize = cfg.getBoolOrDefault("openclReTunePerBoardSize", false);

    enabled_t useFP16Mode = enabled_t::Auto;
    (void)(cfg.tryGetEnabled(backendPrefix+"UseFP16-"+idxStr, useFP16Mode) ||
      cfg.tryGetEnabled("useFP16-"+idxStr, useFP16Mode) ||
      cfg.tryGetEnabled(backendPrefix+"UseFP16", useFP16Mode) ||
      cfg.tryGetEnabled("UseFP16", useFP16Mode));

    enabled_t useNHWCMode = enabled_t::Auto;
    (void)(cfg.tryGetEnabled(backendPrefix+"UseNHWC"+idxStr, useNHWCMode) ||
      cfg.tryGetEnabled("useNHWC"+idxStr, useNHWCMode) ||
      cfg.tryGetEnabled(backendPrefix+"UseNHWC", useNHWCMode) ||
      cfg.tryGetEnabled("useNHWC", useNHWCMode));

    int forcedSymmetry = -1;
    if (setupFor != SETUP_FOR_DISTRIBUTED) {
      cfg.tryGetInt("nnForcedSymmetry", forcedSymmetry, 0, SymmetryHelpers::NUM_SYMMETRIES-1);
    }

    logger.write(
      "After dedups: nnModelFile" + idxStr + " = " + nnModelFile
      + " useFP16 " + useFP16Mode.toString()
      + " useNHWC " + useNHWCMode.toString()
    );

    int nnCacheSizePowerOfTwo;
    if(!cfg.tryGetInt("nnCacheSizePowerOfTwo", nnCacheSizePowerOfTwo, -1, 48)) {
      nnCacheSizePowerOfTwo =
        setupFor == SETUP_FOR_GTP ? 20 :
        setupFor == SETUP_FOR_BENCHMARK ? 20 :
        setupFor == SETUP_FOR_DISTRIBUTED ? 19 :
        setupFor == SETUP_FOR_MATCH ? 21 :
        setupFor == SETUP_FOR_ANALYSIS ? 23 :
        cfg.getInt("nnCacheSizePowerOfTwo", -1, 48);
    }

    int nnMutexPoolSizePowerOfTwo;
    if(!cfg.tryGetInt("nnMutexPoolSizePowerOfTwo", nnMutexPoolSizePowerOfTwo, -1, 24)) {
      nnMutexPoolSizePowerOfTwo =
        setupFor == SETUP_FOR_GTP ? 16 :
        setupFor == SETUP_FOR_BENCHMARK ? 16 :
        setupFor == SETUP_FOR_DISTRIBUTED ? 16 :
        setupFor == SETUP_FOR_MATCH ? 17 :
        setupFor == SETUP_FOR_ANALYSIS ? 17 :
        cfg.getInt("nnMutexPoolSizePowerOfTwo", -1, 24);
    }

#ifndef USE_EIGEN_BACKEND
    int nnMaxBatchSize;
    if(setupFor == SETUP_FOR_BENCHMARK || setupFor == SETUP_FOR_DISTRIBUTED) {
      nnMaxBatchSize = defaultMaxBatchSize;
    }
    else if (defaultMaxBatchSize > 0) {
      nnMaxBatchSize = cfg.getIntOrDefault("nnMaxBatchSize", 1, 65536, defaultMaxBatchSize);
    }
    else {
      nnMaxBatchSize = cfg.getInt("nnMaxBatchSize", 1, 65536);
    }
#else
    //Large batches don't really help CPUs the way they do GPUs because a single CPU on its own is single-threaded
    //and doesn't greatly benefit from having a bigger chunk of parallelizable work to do on the large scale.
    //So we just fix a size here that isn't crazy and saves memory, completely ignore what the user would have
    //specified for GPUs.
    int nnMaxBatchSize = 2;
    cfg.markAllKeysUsedWithPrefix("nnMaxBatchSize");
    (void)defaultMaxBatchSize;
#endif

    int defaultSymmetry = forcedSymmetry >= 0 ? forcedSymmetry : 0;
    if(disableFP16)
      useFP16Mode = enabled_t::False;

    bool dotsGame = cfg.getBoolOrDefault(DOTS_KEY, false);
    NNEvaluator* nnEval = new NNEvaluator(
      nnModelName,
      nnModelFile,
      expectedSha256,
      &logger,
      nnMaxBatchSize,
      nnXLen,
      nnYLen,
      requireExactNNLen,
      inputsUseNHWC,
      nnCacheSizePowerOfTwo,
      nnMutexPoolSizePowerOfTwo,
      debugSkipNeuralNet,
      openCLTunerFile,
      homeDataDirOverride,
      openCLReTunePerBoardSize,
      useFP16Mode,
      useNHWCMode,
      numNNServerThreadsPerModel,
      gpuIdxByServerThread,
      nnRandSeed,
      (forcedSymmetry >= 0 ? false : nnRandomize),
      defaultSymmetry,
      dotsGame
    );

    nnEval->spawnServerThreads();

    nnEvals.push_back(nnEval);
  }

  return nnEvals;
}

int Setup::computeDefaultEigenBackendThreads(int expectedConcurrentEvals, Logger& logger) {
  auto getNumCores = [&logger]() {
    int numCores = (int)std::thread::hardware_concurrency();
    if(numCores <= 0) {
      logger.write("Could not determine number of cores on this machine, choosing eigen backend threads as if it were 8");
      numCores = 8;
    }
    return numCores;
  };
  return std::min(expectedConcurrentEvals,getNumCores());
}

string Setup::loadHomeDataDirOverride(
  ConfigParser& cfg
){
  return cfg.getStringOrDefault("homeDataDir", "");
}

SearchParams Setup::loadSingleParams(
  ConfigParser& cfg,
  setup_for_t setupFor
) {
  const bool hasHumanModel = false;
  return loadSingleParams(cfg,setupFor,hasHumanModel);
}
SearchParams Setup::loadSingleParams(
  ConfigParser& cfg,
  setup_for_t setupFor,
  bool hasHumanModel
) {
  const bool loadSingleConfigOnly = true;
  vector<SearchParams> paramss = loadParams(cfg, setupFor, hasHumanModel, loadSingleConfigOnly);
  if(paramss.size() != 1)
    throw StringError("Config contains parameters for multiple bot configurations, but this KataGo command only supports a single configuration");
  return paramss[0];
}

static Player parsePlayer(const char* field, const string& s) {
  Player pla = C_EMPTY;
  bool suc = PlayerIO::tryParsePlayer(s,pla);
  if(!suc)
    throw StringError("Could not parse player in field " + string(field) + ", should be BLACK or WHITE");
  return pla;
}

vector<SearchParams> Setup::loadParams(
  ConfigParser& cfg,
  setup_for_t setupFor
) {
  const bool hasHumanModel = false;
  const bool loadSingleConfigOnly = false;
  return loadParams(cfg,setupFor,hasHumanModel,loadSingleConfigOnly);
}

vector<SearchParams> Setup::loadParams(
  ConfigParser& cfg,
  setup_for_t setupFor,
  bool hasHumanModel
) {
  const bool loadSingleConfigOnly = false;
  return loadParams(cfg,setupFor,hasHumanModel,loadSingleConfigOnly);
}

template<typename T>
static bool tryGetValueForBot(ConfigParser& cfg, const string& key, T& value, const T& min, const T& max) {
  bool success = false;
  if constexpr (std::is_same_v<T, int>) success = cfg.tryGetInt(key, value, min, max);
  else if constexpr (std::is_same_v<T, int64_t>) success = cfg.tryGetInt64(key, value, min, max);
  else if constexpr (std::is_same_v<T, uint64_t>) success = cfg.tryGetUInt64(key, value, min, max);
  else if constexpr (std::is_same_v<T, float>) success = cfg.tryGetFloat(key, value, min, max);
  else if constexpr (std::is_same_v<T, double>) success = cfg.tryGetDouble(key, value, min, max);
  else if constexpr (std::is_same_v<T, bool>) success = cfg.tryGetBool(key, value);
  else if constexpr (std::is_same_v<T, string>) success = cfg.tryGetString(key, value);
  return success;
}

template<typename T>
static T getValueForBot(ConfigParser& cfg, const string& key, const string& idxStr, const T& min, const T& max, const T& defaultValue, const bool reportNoHumanModelIfKeyFound = false) {
  T value = defaultValue;
  string keyIdx = key + idxStr;
  string foundKey;
  if (tryGetValueForBot(cfg, keyIdx, value, min, max)) {
    foundKey = keyIdx;
  } else {
    // Optimization: don't perform the second check if the key is empty because it doesn't make sense
    if (!idxStr.empty() && tryGetValueForBot(cfg, key, value, min, max)) {
      foundKey = key;
    }
  }
  if (!foundKey.empty() && reportNoHumanModelIfKeyFound) {
    throw ConfigParsingError(
        "Provided parameter " + foundKey + " but no human model was specified (e.g -human-model b18c384nbt-humanv0.bin.gz)"
      );
  }
  return value;
}

static string getStringValueForBot(ConfigParser& cfg, const string& key, const string& idxStr, const string& defaultValue) {
  return getValueForBot<string>(cfg, key, idxStr, "", "", defaultValue);
}

static bool getBoolValueForBot(ConfigParser& cfg, const string& key, const string& idxStr, const bool& defaultValue, const bool reportNoHumanModelIfKeyFound = false) {
  return getValueForBot<bool>(cfg, key, idxStr, false, true, defaultValue, reportNoHumanModelIfKeyFound);
}

vector<SearchParams> Setup::loadParams(
  ConfigParser& cfg,
  setup_for_t setupFor,
  bool hasHumanModel,
  bool loadSingleConfigOnly
) {

  vector<SearchParams> paramss;
  int numBots = cfg.getIntOrDefault("numBots", 1, MAX_BOT_PARAMS_FROM_CFG, 1);

  if(loadSingleConfigOnly) {
    if(numBots != 1)
      throw ConfigParsingError("The config for this command cannot have numBots > 0");
  }

  for(int i = 0; i<numBots; i++) {
    SearchParams params;

    string idxStr = loadSingleConfigOnly ? "" : Global::intToString(i);

    params.maxPlayouts = getValueForBot<int64_t>(cfg, "maxPlayouts", idxStr, 1, static_cast<int64_t>(1) << 50, params.maxPlayouts);
    params.maxVisits = getValueForBot<int64_t>(cfg, "maxVisits", idxStr, 1, static_cast<int64_t>(1) << 50, params.maxVisits);
    params.maxTime = getValueForBot<double>(cfg, "maxTime", idxStr, 0.0, 1.0e20, params.maxTime);
    params.maxPlayoutsPondering = getValueForBot<int64_t>(cfg, "maxPlayoutsPondering", idxStr, 1, static_cast<int64_t>(1) << 50, static_cast<int64_t>(1) << 50);
    params.maxVisitsPondering = getValueForBot<int64_t>(cfg, "maxVisitsPondering", idxStr, 1, static_cast<int64_t>(1) << 50, static_cast<int64_t>(1) << 50);
    params.maxTimePondering = getValueForBot<double>(cfg, "maxTimePondering", idxStr, 0.0, 1.0e20, 1.0e20);
    params.lagBuffer = getValueForBot<double>(cfg, "lagBuffer", idxStr, 0.0, 3600.0, 0.0);
    params.searchFactorAfterOnePass = getValueForBot<double>(cfg, "searchFactorAfterOnePass", idxStr, 0.0, 1.0, params.searchFactorAfterOnePass);
    params.searchFactorAfterTwoPass = getValueForBot<double>(cfg, "searchFactorAfterTwoPass", idxStr, 0.0, 1.0, params.searchFactorAfterTwoPass);
    params.numThreads = getValueForBot<int>(cfg, "numSearchThreads", idxStr, 1, 4096, params.numThreads);
    params.minPlayoutsPerThread = getValueForBot<double>(cfg, "minPlayoutsPerThread", idxStr, 0.0, 1.0e20, setupFor == SETUP_FOR_ANALYSIS || setupFor == SETUP_FOR_GTP ? 8.0 : 0.0);
    params.winLossUtilityFactor = getValueForBot<double>(cfg, "winLossUtilityFactor", idxStr, 0.0, 1.0, 1.0);
    params.staticScoreUtilityFactor = getValueForBot<double>(cfg, "staticScoreUtilityFactor", idxStr, 0.0, 1.0, 0.1);
    params.dynamicScoreUtilityFactor = getValueForBot<double>(cfg, "dynamicScoreUtilityFactor", idxStr, 0.0, 1.0, 0.3);
    params.noResultUtilityForWhite = getValueForBot<double>(cfg, "noResultUtilityForWhite", idxStr, -1.0, 1.0, 0.0);
    params.drawEquivalentWinsForWhite = getValueForBot<double>(cfg, "drawEquivalentWinsForWhite", idxStr, 0.0, 1.0, 0.5);
    params.dynamicScoreCenterZeroWeight = getValueForBot<double>(cfg, "dynamicScoreCenterZeroWeight", idxStr, 0.0, 1.0, 0.20);
    params.dynamicScoreCenterScale = getValueForBot<double>(cfg, "dynamicScoreCenterScale", idxStr, 0.2, 5.0, 0.75);
    params.cpuctExploration = getValueForBot<double>(cfg, "cpuctExploration", idxStr, 0.0, 10.0, 1.0);
    params.cpuctExplorationLog = getValueForBot<double>(cfg, "cpuctExplorationLog", idxStr, 0.0, 10.0, 0.45);
    params.cpuctExplorationBase = getValueForBot<double>(cfg, "cpuctExplorationBase", idxStr, 10.0, 100000.0, 500.0);
    params.cpuctUtilityStdevPrior = getValueForBot<double>(cfg, "cpuctUtilityStdevPrior", idxStr, 0.0, 10.0, 0.40);
    params.cpuctUtilityStdevPriorWeight = getValueForBot<double>(cfg, "cpuctUtilityStdevPriorWeight", idxStr, 0.0, 100.0, 2.0);
    params.cpuctUtilityStdevScale = getValueForBot<double>(cfg, "cpuctUtilityStdevScale", idxStr, 0.0, 1.0, setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER ? 0.85 : 0.0);
    params.fpuReductionMax = getValueForBot<double>(cfg, "fpuReductionMax", idxStr, 0.0, 2.0, 0.2);
    params.fpuLossProp = getValueForBot<double>(cfg, "fpuLossProp", idxStr, 0.0, 1.0, 0.0);
    params.fpuParentWeightByVisitedPolicy = getBoolValueForBot(cfg, "fpuParentWeightByVisitedPolicy", idxStr, setupFor != SETUP_FOR_DISTRIBUTED);

    if (params.fpuParentWeightByVisitedPolicy) {
      params.fpuParentWeightByVisitedPolicyPow = getValueForBot<double>(cfg, "fpuParentWeightByVisitedPolicyPow", idxStr, 0.0, 5.0, 2.0);
    }
    else {
      params.fpuParentWeight = getValueForBot<double>(cfg, "fpuParentWeight", idxStr, 0.0, 1.0, 0.0);
    }

    params.policyOptimism = getValueForBot<double>(cfg, "policyOptimism", idxStr, 0.0, 1.0, setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER ? 1.0 : 0.0);
    params.valueWeightExponent = getValueForBot<double>(cfg, "valueWeightExponent", idxStr, 0.0, 1.0, 0.25);
    params.useNoisePruning = getBoolValueForBot(cfg, "useNoisePruning", idxStr, setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER);
    params.noisePruneUtilityScale = getValueForBot<double>(cfg, "noisePruneUtilityScale", idxStr, 0.001, 10.0, 0.15);
    params.noisePruningCap = getValueForBot<double>(cfg, "noisePruningCap", idxStr, 0.0, 1e50, 1e50);
    params.useUncertainty = getBoolValueForBot(cfg, "useUncertainty", idxStr, setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER);
    params.uncertaintyCoeff = getValueForBot<double>(cfg, "uncertaintyCoeff", idxStr, 0.0001, 1.0, 0.25);
    params.uncertaintyExponent = getValueForBot<double>(cfg, "uncertaintyExponent", idxStr, 0.0, 2.0, 1.0);
    params.uncertaintyMaxWeight = getValueForBot<double>(cfg, "uncertaintyMaxWeight", idxStr, 1.0, 100.0, 8.0);
    params.useGraphSearch = getBoolValueForBot(cfg, "useGraphSearch", idxStr, setupFor != SETUP_FOR_DISTRIBUTED);
    params.graphSearchRepBound = getValueForBot<int>(cfg, "graphSearchRepBound", idxStr, 3, 50, 11);
    params.graphSearchCatchUpLeakProb = getValueForBot<double>(cfg, "graphSearchCatchUpLeakProb", idxStr, 0.0, 1.0, 0.0);
    params.rootNoiseEnabled = getBoolValueForBot(cfg, "rootNoiseEnabled", idxStr, false);
    params.rootDirichletNoiseTotalConcentration = getValueForBot<double>(cfg, "rootDirichletNoiseTotalConcentration", idxStr, 0.001, 10000.0, 10.83);
    params.rootDirichletNoiseWeight = getValueForBot<double>(cfg, "rootDirichletNoiseWeight", idxStr, 0.0, 1.0, 0.25);
    params.rootPolicyTemperature = getValueForBot<double>(cfg, "rootPolicyTemperature", idxStr, 0.01, 100.0, 1.0);
    params.rootPolicyTemperatureEarly = getValueForBot<double>(cfg, "rootPolicyTemperatureEarly", idxStr, 0.01, 100.0, params.rootPolicyTemperature);
    params.rootFpuReductionMax = getValueForBot<double>(cfg, "rootFpuReductionMax", idxStr, 0.0, 2.0, params.rootNoiseEnabled ? 0.0 : 0.1);
    params.rootFpuLossProp = getValueForBot<double>(cfg, "rootFpuLossProp", idxStr, 0.0, 1.0, params.fpuLossProp);
    params.rootNumSymmetriesToSample = getValueForBot<int>(cfg, "rootNumSymmetriesToSample", idxStr, 1, SymmetryHelpers::NUM_SYMMETRIES, 1);
    params.rootSymmetryPruning = getBoolValueForBot(cfg, "rootSymmetryPruning", idxStr, setupFor == SETUP_FOR_ANALYSIS || setupFor == SETUP_FOR_GTP);
    params.rootDesiredPerChildVisitsCoeff = getValueForBot<double>(cfg, "rootDesiredPerChildVisitsCoeff", idxStr, 0.0, 100.0, 0.0);
    params.rootPolicyOptimism = getValueForBot<double>(cfg, "rootPolicyOptimism", idxStr, 0.0, 1.0, setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER ? std::min(params.policyOptimism, 0.2) : 0.0);
    params.chosenMoveTemperature = getValueForBot<double>(cfg, "chosenMoveTemperature", idxStr, 0.0, 5.0, 0.1);
    params.chosenMoveTemperatureEarly = getValueForBot<double>(cfg, "chosenMoveTemperatureEarly", idxStr, 0.0, 5.0, 0.5);
    params.chosenMoveTemperatureHalflife = getValueForBot<double>(cfg, "chosenMoveTemperatureHalflife", idxStr, 0.1, 100000.0, 19.0);
    params.chosenMoveTemperatureOnlyBelowProb = getValueForBot<double>(cfg, "chosenMoveTemperatureOnlyBelowProb", idxStr, 0.0, 1.0, 1.0);
    params.chosenMoveSubtract = getValueForBot<double>(cfg, "chosenMoveSubtract", idxStr, 0.0, 1.0e10, 0.0);
    params.chosenMovePrune = getValueForBot<double>(cfg, "chosenMovePrune", idxStr, 0.0, 1.0e10, 1.0);
    params.useLcbForSelection = getBoolValueForBot(cfg, "useLcbForSelection", idxStr, true);
    params.lcbStdevs = getValueForBot<double>(cfg, "lcbStdevs", idxStr, 1.0, 12.0, 5.0);
    params.minVisitPropForLCB = getValueForBot<double>(cfg, "minVisitPropForLCB", idxStr, 0.0, 1.0, 0.15);
    params.useNonBuggyLcb = getBoolValueForBot(cfg, "useNonBuggyLcb", idxStr, setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER);
    params.rootEndingBonusPoints = getValueForBot<double>(cfg, "rootEndingBonusPoints", idxStr, -1.0, 1.0, 0.5);
    params.rootPruneUselessMoves = getBoolValueForBot(cfg, "rootPruneUselessMoves", idxStr,  true);
    params.conservativePass = getBoolValueForBot(cfg, "conservativePass", idxStr, false);
    params.fillDameBeforePass = getBoolValueForBot(cfg, "fillDameBeforePass", idxStr, false);
    params.avoidMYTDaggerHackPla = C_EMPTY;
    params.wideRootNoise = getValueForBot<double>(cfg, "wideRootNoise", idxStr, 0.0, 5.0, setupFor == SETUP_FOR_ANALYSIS ? DEFAULT_ANALYSIS_WIDE_ROOT_NOISE : 0.00);
    params.enablePassingHacks = getBoolValueForBot(cfg, "enablePassingHacks", idxStr,  setupFor == SETUP_FOR_GTP || setupFor == SETUP_FOR_ANALYSIS);
    params.enableMorePassingHacks = getBoolValueForBot(cfg, "enableMorePassingHacks", idxStr,  setupFor == SETUP_FOR_GTP || setupFor == SETUP_FOR_ANALYSIS);
    params.playoutDoublingAdvantage = getValueForBot<double>(cfg, "playoutDoublingAdvantage", idxStr, -3.0, 3.0, 0.0);

    string playoutDoublingAdvantagePlaStr = getStringValueForBot(cfg, "playoutDoublingAdvantagePla", idxStr, "");
    params.playoutDoublingAdvantagePla = !playoutDoublingAdvantagePlaStr.empty()
      ? parsePlayer("playoutDoublingAdvantagePla", playoutDoublingAdvantagePlaStr)
      : C_EMPTY;

    params.avoidRepeatedPatternUtility = getValueForBot<double>(cfg, "avoidRepeatedPatternUtility", idxStr, -3.0, 3.0, 0.0);
    params.nnPolicyTemperature = getValueForBot<float>(cfg, "nnPolicyTemperature", idxStr, 0.01f, 5.0f, 1.0f);
    params.antiMirror = getBoolValueForBot(cfg, "antiMirror", idxStr, false);
    params.ignorePreRootHistory = getBoolValueForBot(cfg, "ignorePreRootHistory", idxStr, setupFor == SETUP_FOR_ANALYSIS ? DEFAULT_ANALYSIS_IGNORE_PRE_ROOT_HISTORY : false);
    params.ignoreAllHistory = getBoolValueForBot(cfg, "ignoreAllHistory", idxStr, false);
    params.subtreeValueBiasFactor = getValueForBot<double>(cfg, "subtreeValueBiasFactor", idxStr, 0.0, 1.0, 0.45);
    params.subtreeValueBiasFreeProp = getValueForBot<double>(cfg, "subtreeValueBiasFreeProp", idxStr, 0.0, 1.0, 0.8);
    params.subtreeValueBiasWeightExponent = getValueForBot<double>(cfg, "subtreeValueBiasWeightExponent", idxStr, 0.0, 1.0, 0.85);
    params.useEvalCache = getBoolValueForBot(cfg, "useEvalCache", idxStr, false);
    params.evalCacheMinVisits = getValueForBot<int64_t>(cfg, "evalCacheMinVisits", idxStr, 1, static_cast<int64_t>(1) << 50, 100);
    params.nodeTableShardsPowerOfTwo = getValueForBot<int>(cfg, "nodeTableShardsPowerOfTwo", idxStr, 8, 24, 16);
    params.numVirtualLossesPerThread = getValueForBot<double>(cfg, "numVirtualLossesPerThread", idxStr, 0.01, 1000.0, 1.0);
    params.treeReuseCarryOverTimeFactor = getValueForBot<double>(cfg, "treeReuseCarryOverTimeFactor", idxStr, 0.0, 1.0, 0.0);
    params.overallocateTimeFactor = getValueForBot<double>(cfg, "overallocateTimeFactor", idxStr, 0.01, 100.0, 1.0);
    params.midgameTimeFactor = getValueForBot<double>(cfg, "midgameTimeFactor", idxStr, 0.01, 100.0, 1.0);
    params.midgameTurnPeakTime = getValueForBot<double>(cfg, "midgameTurnPeakTime", idxStr, 0.0, 1000.0, 130.0);
    params.endgameTurnTimeDecay = getValueForBot<double>(cfg, "endgameTurnTimeDecay", idxStr, 0.0, 1000.0, 100.0);
    params.obviousMovesTimeFactor = getValueForBot<double>(cfg, "obviousMovesTimeFactor", idxStr, 0.01, 1.0, 1.0);
    params.obviousMovesPolicyEntropyTolerance = getValueForBot<double>(cfg, "obviousMovesPolicyEntropyTolerance", idxStr, 0.001, 2.0, 0.30);
    params.obviousMovesPolicySurpriseTolerance = getValueForBot<double>(cfg, "obviousMovesPolicySurpriseTolerance", idxStr, 0.001, 2.0, 0.15);
    params.futileVisitsThreshold = getValueForBot<double>(cfg, "futileVisitsThreshold", idxStr, 0.01, 1.0, 0.0);

    // This does NOT report an error under throwHumanParsingError like the parameters below that expect a second model
    // because the user might be providing the human model as the MAIN model. In which case humanSLProfile is still a
    // valid param but the others are not.
    if(setupFor != SETUP_FOR_DISTRIBUTED) {
      auto humanSLProfileName = getStringValueForBot(cfg, "humanSLProfile", idxStr, "");
      params.humanSLProfile = SGFMetadata::getProfile(humanSLProfileName);
    }

    bool reportNoHumanModelIfKeyFound = !hasHumanModel;
    params.humanSLCpuctExploration = getValueForBot<double>(cfg, "humanSLCpuctExploration", idxStr, 0.0, 1000.0, 1.0, reportNoHumanModelIfKeyFound);
    params.humanSLCpuctPermanent = getValueForBot<double>(cfg, "humanSLCpuctPermanent", idxStr, 0.0, 1000.0, 0.0, reportNoHumanModelIfKeyFound);
    params.humanSLRootExploreProbWeightless = getValueForBot<double>(cfg, "humanSLRootExploreProbWeightless", idxStr, 0.0, 1.0, 0.0, reportNoHumanModelIfKeyFound);
    params.humanSLRootExploreProbWeightful = getValueForBot<double>(cfg, "humanSLRootExploreProbWeightful", idxStr, 0.0, 1.0, 0.0, reportNoHumanModelIfKeyFound);
    params.humanSLPlaExploreProbWeightless = getValueForBot<double>(cfg, "humanSLPlaExploreProbWeightless", idxStr, 0.0, 1.0, 0.0, reportNoHumanModelIfKeyFound);
    params.humanSLPlaExploreProbWeightful = getValueForBot<double>(cfg, "humanSLPlaExploreProbWeightful", idxStr, 0.0, 1.0, 0.0, reportNoHumanModelIfKeyFound);
    params.humanSLOppExploreProbWeightless = getValueForBot<double>(cfg, "humanSLOppExploreProbWeightless", idxStr, 0.0, 1.0, 0.0, reportNoHumanModelIfKeyFound);
    params.humanSLOppExploreProbWeightful = getValueForBot<double>(cfg, "humanSLOppExploreProbWeightful", idxStr, 0.0, 1.0, 0.0, reportNoHumanModelIfKeyFound);
    params.humanSLChosenMoveProp = getValueForBot<double>(cfg, "humanSLChosenMoveProp", idxStr, 0.0, 1.0, 0.0, reportNoHumanModelIfKeyFound);
    params.humanSLChosenMoveIgnorePass = getBoolValueForBot(cfg, "humanSLChosenMoveIgnorePass", idxStr, false, reportNoHumanModelIfKeyFound);
    params.humanSLChosenMovePiklLambda = getValueForBot<double>(cfg, "humanSLChosenMovePiklLambda", idxStr, 0.0, 1000000000.0, 1000000000.0, reportNoHumanModelIfKeyFound);

    //On distributed, tolerate reading mutexPoolSize since older version configs use it.
    if(setupFor == SETUP_FOR_DISTRIBUTED)
      cfg.markAllKeysUsedWithPrefix("mutexPoolSize");

    paramss.push_back(params);
  }

  return paramss;
}


bool Setup::maybeWarnHumanSLParams(
  const SearchParams& params,
  const NNEvaluator* nnEval,
  const NNEvaluator* humanEval,
  std::ostream& out,
  Logger* logger
) {
  if(params.humanSLProfile.initialized) {
    bool hasAnySGFMetaUse =
      (nnEval != NULL && nnEval->requiresSGFMetadata()) ||
      (humanEval != NULL && humanEval->requiresSGFMetadata());
    if(!hasAnySGFMetaUse) {
      string modelNames;
      if(nnEval != NULL)
        modelNames += nnEval->getModelName();
      if(humanEval != NULL) {
        if(modelNames.size() > 0)
          modelNames += " and ";
        modelNames += humanEval->getModelName();
      }
      if(logger != NULL)
        logger->write("WARNING: humanSLProfile is specified as config param but model(s) don't use it: " + modelNames);
      out << "WARNING: humanSLProfile is specified as config param but model(s) don't use it: " << modelNames << endl;
      return true;
    }
  }
  return false;
}


Player Setup::parseReportAnalysisWinrates(
  ConfigParser& cfg, Player defaultPerspective
) {
  string sOrig;
  if(!cfg.tryGetString("reportAnalysisWinratesAs", sOrig))
    return defaultPerspective;

  string s = Global::toLower(sOrig);
  if(s == "b" || s == "black")
    return P_BLACK;
  else if(s == "w" || s == "white")
    return P_WHITE;
  else if(s == "sidetomove")
    return C_EMPTY;

  throw StringError("Could not parse config value for reportAnalysisWinratesAs: " + sOrig);
}

Rules Setup::loadSingleRules(ConfigParser& cfg, const bool loadKomi) {
  const bool dotsGame = cfg.getBoolOrDefault(DOTS_KEY, false);
  Rules rules = Rules::getDefault(dotsGame);

  if(string rulesStr; cfg.tryGetString("rules", rulesStr)) {
    if(cfg.contains(START_POS_KEY)) throw StringError("Cannot both specify 'rules' and individual rules like " + START_POS_KEY);
    if(cfg.contains(START_POS_RANDOM_KEY)) throw StringError("Cannot both specify 'rules' and individual rules like " + START_POS_RANDOM_KEY);
    if(cfg.contains("multiStoneSuicideLegal")) throw StringError("Cannot both specify 'rules' and individual rules like multiStoneSuicideLegal");

    if (dotsGame) {
      if (cfg.contains(DOTS_CAPTURE_EMPTY_BASE_KEY)) throw StringError("Cannot both specify 'rules' and individual rules like " + DOTS_CAPTURE_EMPTY_BASE_KEY);
    } else {
      if(cfg.contains("koRule")) throw StringError("Cannot both specify 'rules' and individual rules like koRule");
      if(cfg.contains("scoringRule")) throw StringError("Cannot both specify 'rules' and individual rules like scoringRule");
      if(cfg.contains("hasButton")) throw StringError("Cannot both specify 'rules' and individual rules like hasButton");
      if(cfg.contains("taxRule")) throw StringError("Cannot both specify 'rules' and individual rules like taxRule");
      if(cfg.contains("whiteHandicapBonus")) throw StringError("Cannot both specify 'rules' and individual rules like whiteHandicapBonus");
      if(cfg.contains("friendlyPassOk")) throw StringError("Cannot both specify 'rules' and individual rules like friendlyPassOk");
      if(cfg.contains("whiteBonusPerHandicapStone")) throw StringError("Cannot both specify 'rules' and individual rules like whiteBonusPerHandicapStone");
    }

    rules = Rules::parseRules(rulesStr, cfg.getBoolOrDefault(DOTS_KEY, false));
  }
  else {
    if (string startPosStr; cfg.tryGetString(START_POS_KEY, startPosStr)) {
      rules.startPos = Rules::parseStartPos(startPosStr);
    }
    rules.startPosIsRandom = cfg.getBoolOrDefault(START_POS_RANDOM_KEY, rules.startPosIsRandom);
    rules.multiStoneSuicideLegal = cfg.getBoolOrDefault("multiStoneSuicideLegal", rules.multiStoneSuicideLegal);

    if (dotsGame) {
      rules.dotsCaptureEmptyBases = cfg.getBoolOrDefault(DOTS_CAPTURE_EMPTY_BASE_KEY, rules.dotsCaptureEmptyBases);
    } else {
      rules.koRule = Rules::parseKoRule(cfg.getString("koRule", Rules::koRuleStrings()));
      rules.scoringRule = Rules::parseScoringRule(cfg.getString("scoringRule", Rules::scoringRuleStrings()));
      rules.hasButton = cfg.getBoolOrDefault("hasButton", false);
      rules.komi = 7.5f;

      if(string taxRule; cfg.tryGetString("taxRule", taxRule, Rules::taxRuleStrings())) {
        rules.taxRule = Rules::parseTaxRule(taxRule);
      }
      else {
        rules.taxRule = rules.scoringRule == Rules::SCORING_TERRITORY ? Rules::TAX_SEKI : Rules::TAX_NONE;
      }

      if(rules.hasButton && rules.scoringRule != Rules::SCORING_AREA)
        throw StringError("Config specifies hasButton=true on a scoring system other than AREA");

      int whiteBonusPerHandicapStone = 0;
      const bool whiteBonusPerHandicapStoneSpecified = cfg.tryGetInt("whiteBonusPerHandicapStone", whiteBonusPerHandicapStone, 0, 1);
      string whiteHandicapBonusString;
      const bool whiteHandicapBonusSpecified = cfg.tryGetString("whiteHandicapBonus", whiteHandicapBonusString, Rules::whiteHandicapBonusRuleStrings());

      //Also handles parsing of legacy option whiteBonusPerHandicapStone
      if(whiteBonusPerHandicapStoneSpecified && whiteHandicapBonusSpecified)
        throw StringError("May specify only one of whiteBonusPerHandicapStone and whiteHandicapBonus in config");

      rules.whiteHandicapBonusRule = whiteHandicapBonusSpecified
        ? Rules::parseWhiteHandicapBonusRule(whiteHandicapBonusString)
        : whiteBonusPerHandicapStone == 0 ? Rules::WHB_ZERO
                                          : Rules::WHB_N;

      rules.friendlyPassOk = cfg.getBoolOrDefault("friendlyPassOk", rules.friendlyPassOk);

      //Drop default komi to 6.5 for territory rules, and to 7.0 for button
      if(rules.scoringRule == Rules::SCORING_TERRITORY)
        rules.komi = 6.5f;
      else if(rules.hasButton)
        rules.komi = 7.0f;
    }
  }

  if(loadKomi) {
    rules.komi = cfg.getFloat("komi",Rules::MIN_USER_KOMI,Rules::MAX_USER_KOMI);
  }

  return rules;
}

bool Setup::loadDefaultBoardXYSize(
  ConfigParser& cfg,
  Logger& logger,
  int& defaultBoardXSizeRet,
  int& defaultBoardYSizeRet
) {
  int defaultBoardXSize = -1;
  (void)(cfg.tryGetInt("defaultBoardXSize", defaultBoardXSize, 2, Board::MAX_LEN_X) ||
         cfg.tryGetInt("defaultBoardSize", defaultBoardXSize, 2, Board::MAX_LEN_X));

  int defaultBoardYSize = -1;
  (void)(cfg.tryGetInt("defaultBoardYSize", defaultBoardYSize, 2, Board::MAX_LEN_Y) ||
         cfg.tryGetInt("defaultBoardSize", defaultBoardYSize, 2, Board::MAX_LEN_Y));
  if((defaultBoardXSize == -1) != (defaultBoardYSize == -1))
    logger.write("Warning: Config specified only one of defaultBoardXSize or defaultBoardYSize and no other board size parameter, ignoring it");

  if(defaultBoardXSize == -1 || defaultBoardYSize == -1) {
    return false;
  }
  defaultBoardXSizeRet = defaultBoardXSize;
  defaultBoardYSizeRet = defaultBoardYSize;
  return true;
}

vector<pair<set<string>,set<string>>> Setup::getMutexKeySets() {
  vector<pair<set<string>,set<string>>> mutexKeySets = {
    std::make_pair<set<string>,set<string>>(
    {"rules"},{"koRule","scoringRule","multiStoneSuicideLegal","taxRule","hasButton","whiteBonusPerHandicapStone","friendlyPassOk","whiteHandicapBonus"}
    ),
  };
  return mutexKeySets;
}

std::vector<std::unique_ptr<PatternBonusTable>> Setup::loadAvoidSgfPatternBonusTables(ConfigParser& cfg, Logger& logger) {
  const int numBots = cfg.getIntOrDefault("numBots", 1, MAX_BOT_PARAMS_FROM_CFG, 1);

  std::vector<std::unique_ptr<PatternBonusTable>> tables;
  for(int i = 0; i<numBots; i++) {
    //Indexes different bots, such as in a match config
    const string idxStr = Global::intToString(i);

    std::unique_ptr<PatternBonusTable> patternBonusTable = nullptr;
    for(int j = 1; j<100000; j++) {
      //Indexes different sets of params for different sets of files, to combine into one bot.
      const string setStr = j == 1 ? string() : Global::intToString(j);
      const string prefix = "avoidSgf"+setStr;

      //Tries to find prefix+suffix+optional index
      //E.g. "avoidSgf"+"PatternUtility"+(optional integer indexing which bot for match)
      auto contains = [&cfg,&idxStr,&prefix](const string& suffix) {
        return cfg.containsAny({prefix+suffix+idxStr,prefix+suffix});
      };
      auto find = [&cfg,&idxStr,&prefix](const string& suffix) {
        return cfg.firstFoundOrFail({prefix+suffix+idxStr,prefix+suffix});
      };

      if(contains("PatternUtility")) {
        const double penalty = cfg.getDouble(find("PatternUtility"), -3.0, 3.0);
        const double lambda = cfg.getDoubleOrDefault(find("PatternLambda"), 0.0, 1.0, 1.0);
        const int minTurnNumber = cfg.getIntOrDefault(find("PatternMinTurnNumber"), 0, 1000000, 0);
        const size_t maxFiles = static_cast<size_t>(cfg.getIntOrDefault(find("PatternMaxFiles"), 1, 1000000, 1000000));
        vector<string> allowedPlayerNames;
        if(contains("PatternAllowedNames"))
          allowedPlayerNames = cfg.getStrings(find("PatternAllowedNames"), {}, true);
        vector<string> sgfDirs = cfg.getStrings(find("PatternDirs"));
        if(patternBonusTable == nullptr)
          patternBonusTable = std::make_unique<PatternBonusTable>();
        string logSource = "bot " + idxStr;
        patternBonusTable->avoidRepeatedSgfMoves(sgfDirs,penalty,lambda,minTurnNumber,maxFiles,allowedPlayerNames,logger,logSource);
      }
    }
    tables.push_back(std::move(patternBonusTable));
  }
  return tables;
}

static string boardSizeToStr(int boardXSize, int boardYSize) {
  return Global::intToString(boardXSize) + "x" + Global::intToString(boardYSize);
}

static int getAutoPatternIntParam(ConfigParser& cfg, const string& param, const int boardXSize,
  const int boardYSize,
  const int min,
  const int max) {
  if (int value; cfg.tryGetInt(param + boardSizeToStr(boardXSize, boardYSize), value, min, max)) {
    return value;
  }
  return cfg.getInt(param, min, max);
}
static int64_t getAutoPatternInt64Param(ConfigParser& cfg, const string& param,
  const int boardXSize,
  const int boardYSize,
  const int64_t min,
  const int64_t max) {
  if (int64_t value; cfg.tryGetInt64(param + boardSizeToStr(boardXSize, boardYSize), value, min, max)) {
    return value;
  }
  return cfg.getInt64(param, min, max);
}
static double getAutoPatternDoubleParam(ConfigParser& cfg, const string& param,
  const int boardXSize,
  const int boardYSize,
  const double min,
  const double max) {
  if(double value; cfg.tryGetDouble(param + boardSizeToStr(boardXSize, boardYSize), value, min, max))
    return value;
  return cfg.getDouble(param, min, max);
}

bool Setup::saveAutoPatternBonusData(const std::vector<Sgf::PositionSample>& genmoveSamples, ConfigParser& cfg, Logger& logger, Rand& rand) {
  if(genmoveSamples.size() <= 0)
    return false;

  string autoAvoidPatternsDir;
  if(!cfg.tryGetString("autoAvoidRepeatDir", autoAvoidPatternsDir))
    return false;

  MakeDir::make(autoAvoidPatternsDir);

  std::map<std::pair<int,int>, std::unique_ptr<ofstream>> outByBoardSize;
  string fileName = Global::uint64ToHexString(rand.nextUInt64()) + "_poses.txt";
  for(const Sgf::PositionSample& sampleToWrite : genmoveSamples) {
    int boardXSize = sampleToWrite.board.x_size;
    int boardYSize = sampleToWrite.board.y_size;
    std::pair<int,int> boardSize = std::make_pair(boardXSize, boardYSize);

    int minTurnNumber = getAutoPatternIntParam(cfg,"autoAvoidRepeatMinTurnNumber",boardXSize,boardYSize,0,1000000);
    int maxTurnNumber = getAutoPatternIntParam(cfg,"autoAvoidRepeatMaxTurnNumber",boardXSize,boardYSize,0,1000000);
    if(sampleToWrite.initialTurnNumber < minTurnNumber || sampleToWrite.initialTurnNumber > maxTurnNumber)
      continue;
    assert(sampleToWrite.moves.size() == 0);
    if(!contains(outByBoardSize,boardSize)) {
      MakeDir::make(autoAvoidPatternsDir + "/" + boardSizeToStr(boardXSize, boardYSize));
      outByBoardSize[boardSize] = std::make_unique<ofstream>();
      string filePath = autoAvoidPatternsDir + "/" + boardSizeToStr(boardXSize, boardYSize) + "/" + fileName;
      bool suc = FileUtils::tryOpen(*(outByBoardSize[boardSize]), filePath);
      if(!suc) {
        logger.write("ERROR: could not open " + filePath);
        return false;
      }
    }
    *(outByBoardSize[boardSize]) << Sgf::PositionSample::toJsonLine(sampleToWrite) << "\n";
  }
  for(auto iter = outByBoardSize.begin(); iter != outByBoardSize.end(); ++iter) {
    iter->second->close();
  }
  logger.write("Saved " + Global::uint64ToString(genmoveSamples.size()) + " avoid poses to " + autoAvoidPatternsDir);
  return true;
}

std::unique_ptr<PatternBonusTable> Setup::loadAndPruneAutoPatternBonusTables(ConfigParser& cfg, Logger& logger) {
  std::unique_ptr<PatternBonusTable> patternBonusTable = nullptr;

  if(string baseDir; cfg.tryGetString("autoAvoidRepeatDir", baseDir)) {
    std::vector<string> boardSizeDirs = FileUtils::listFiles(baseDir);

    patternBonusTable = std::make_unique<PatternBonusTable>();

    for(const string& dirName: boardSizeDirs) {
      std::vector<string> pieces = Global::split(dirName,'x');
      if(pieces.size() != 2)
        continue;
      int boardXSize;
      int boardYSize;
      bool suc = Global::tryStringToInt(pieces[0],boardXSize) && Global::tryStringToInt(pieces[1],boardYSize);
      if(!suc)
        continue;
      if(boardXSize < 2 || boardXSize > Board::MAX_LEN_X || boardYSize < 2 || boardYSize > Board::MAX_LEN_Y)
        continue;

      string dirPath = baseDir + "/" + dirName;
      if(!FileUtils::isDirectory(dirPath))
        continue;

      double penalty = getAutoPatternDoubleParam(cfg,"autoAvoidRepeatUtility",boardXSize,boardYSize,-3.0,3.0);
      double lambda = getAutoPatternDoubleParam(cfg,"autoAvoidRepeatLambda",boardXSize,boardYSize,0.0,1.0);
      int minTurnNumber = getAutoPatternIntParam(cfg,"autoAvoidRepeatMinTurnNumber",boardXSize,boardYSize,0,1000000);
      int maxTurnNumber = getAutoPatternIntParam(cfg,"autoAvoidRepeatMaxTurnNumber",boardXSize,boardYSize,0,1000000);
      size_t maxPoses = getAutoPatternInt64Param(cfg,"autoAvoidRepeatMaxPoses",boardXSize,boardYSize,0,(int64_t)1000000000000LL);

      string logSource = dirPath;
      patternBonusTable->avoidRepeatedPosMovesAndDeleteExcessFiles({baseDir + "/" + dirName},penalty,lambda,minTurnNumber,maxTurnNumber,maxPoses,logger,logSource);
    }


    cfg.markAllKeysUsedWithPrefix("autoAvoidRepeatUtility");
    cfg.markAllKeysUsedWithPrefix("autoAvoidRepeatLambda");
    cfg.markAllKeysUsedWithPrefix("autoAvoidRepeatMinTurnNumber");
    cfg.markAllKeysUsedWithPrefix("autoAvoidRepeatMaxTurnNumber");
    cfg.markAllKeysUsedWithPrefix("autoAvoidRepeatMaxPoses");
    cfg.markAllKeysUsedWithPrefix("autoAvoidRepeatSaveChunkSize");
  }
  return patternBonusTable;
}
