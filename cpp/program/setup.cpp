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

    (void)(cfg.tryGetInt64("maxPlayouts"+idxStr, params.maxPlayouts, 1, static_cast<int64_t>(1) << 50) ||
           cfg.tryGetInt64("maxPlayouts", params.maxPlayouts, 1, static_cast<int64_t>(1) << 50));

    (void)(cfg.tryGetInt64("maxVisits"+idxStr, params.maxVisits, 1, static_cast<int64_t>(1) << 50) ||
           cfg.tryGetInt64("maxVisits", params.maxVisits, 1, static_cast<int64_t>(1) << 50));

    (void)(cfg.tryGetDouble("maxTime"+idxStr, params.maxTime, 0.0, 1.0e20) ||
           cfg.tryGetDouble("maxTime", params.maxTime, 0.0, 1.0e20));

    params.maxPlayoutsPondering = static_cast<int64_t>(1) << 50;
    (void)(cfg.tryGetInt64("maxPlayoutsPondering"+idxStr, params.maxPlayoutsPondering, 1, static_cast<int64_t>(1) << 50) ||
           cfg.tryGetInt64("maxPlayoutsPondering", params.maxPlayoutsPondering, 1, static_cast<int64_t>(1) << 50));

    params.maxVisitsPondering = static_cast<int64_t>(1) << 50;
    (void)(cfg.tryGetInt64("maxVisitsPondering"+idxStr, params.maxVisitsPondering, 1, static_cast<int64_t>(1) << 50) ||
           cfg.tryGetInt64("maxVisitsPondering", params.maxVisitsPondering, 1, static_cast<int64_t>(1) << 50));

    params.maxTimePondering = 1.0e20;
    (void)(cfg.tryGetDouble("maxTimePondering"+idxStr, params.maxTimePondering, 0.0, 1.0e20) ||
           cfg.tryGetDouble("maxTimePondering", params.maxTimePondering, 0.0, 1.0e20));

    params.lagBuffer = 0.0;
    (void)(cfg.tryGetDouble("lagBuffer"+idxStr, params.lagBuffer, 0.0, 3600.0) ||
           cfg.tryGetDouble("lagBuffer", params.lagBuffer, 0.0, 3600.0));

    (void)(cfg.tryGetDouble("searchFactorAfterOnePass"+idxStr, params.searchFactorAfterOnePass, 0.0, 1.0) ||
           cfg.tryGetDouble("searchFactorAfterOnePass", params.searchFactorAfterOnePass, 0.0, 1.0));

    (void)(cfg.tryGetDouble("searchFactorAfterTwoPass"+idxStr, params.searchFactorAfterTwoPass, 0.0, 1.0) ||
           cfg.tryGetDouble("searchFactorAfterTwoPass", params.searchFactorAfterTwoPass, 0.0, 1.0));

    (void)(cfg.tryGetInt("numSearchThreads"+idxStr, params.numThreads, 1, 4096) ||
           cfg.tryGetInt("numSearchThreads", params.numThreads, 1, 4096));

    params.minPlayoutsPerThread = setupFor == SETUP_FOR_ANALYSIS || setupFor == SETUP_FOR_GTP ? 8.0 : 0.0;
    (void)(cfg.tryGetDouble("minPlayoutsPerThread"+idxStr, params.minPlayoutsPerThread, 0.0, 1.0e20) ||
           cfg.tryGetDouble("minPlayoutsPerThread", params.minPlayoutsPerThread, 0.0, 1.0e20));

    params.winLossUtilityFactor = 1.0;
    (void)(cfg.tryGetDouble("winLossUtilityFactor"+idxStr, params.winLossUtilityFactor, 0.0, 1.0) ||
           cfg.tryGetDouble("winLossUtilityFactor", params.winLossUtilityFactor, 0.0, 1.0));

    params.staticScoreUtilityFactor = 0.1;
    (void)(cfg.tryGetDouble("staticScoreUtilityFactor"+idxStr, params.staticScoreUtilityFactor, 0.0, 1.0) ||
           cfg.tryGetDouble("staticScoreUtilityFactor", params.staticScoreUtilityFactor, 0.0, 1.0));

    params.dynamicScoreUtilityFactor = 0.3;
    (void)(cfg.tryGetDouble("dynamicScoreUtilityFactor"+idxStr, params.dynamicScoreUtilityFactor, 0.0, 1.0) ||
           cfg.tryGetDouble("dynamicScoreUtilityFactor", params.dynamicScoreUtilityFactor, 0.0, 1.0));

    params.noResultUtilityForWhite = 0.0;
    (void)(cfg.tryGetDouble("noResultUtilityForWhite"+idxStr, params.noResultUtilityForWhite, -1.0, 1.0) ||
           cfg.tryGetDouble("noResultUtilityForWhite", params.noResultUtilityForWhite, -1.0, 1.0));

    params.drawEquivalentWinsForWhite = 0.5;
    (void)(cfg.tryGetDouble("drawEquivalentWinsForWhite"+idxStr, params.drawEquivalentWinsForWhite, 0.0, 1.0) ||
           cfg.tryGetDouble("drawEquivalentWinsForWhite", params.drawEquivalentWinsForWhite, 0.0, 1.0));

    params.dynamicScoreCenterZeroWeight = 0.20;
    (void)(cfg.tryGetDouble("dynamicScoreCenterZeroWeight"+idxStr, params.dynamicScoreCenterZeroWeight, 0.0, 1.0) ||
           cfg.tryGetDouble("dynamicScoreCenterZeroWeight", params.dynamicScoreCenterZeroWeight, 0.0, 1.0));

    params.dynamicScoreCenterScale = 0.75;
    (void)(cfg.tryGetDouble("dynamicScoreCenterScale"+idxStr, params.dynamicScoreCenterScale, 0.2, 5.0) ||
           cfg.tryGetDouble("dynamicScoreCenterScale", params.dynamicScoreCenterScale, 0.2, 5.0));

    params.cpuctExploration = 1.0;
    (void)(cfg.tryGetDouble("cpuctExploration"+idxStr, params.cpuctExploration, 0.0, 10.0) ||
           cfg.tryGetDouble("cpuctExploration", params.cpuctExploration, 0.0, 10.0));

    params.cpuctExplorationLog = 0.45;
    (void)(cfg.tryGetDouble("cpuctExplorationLog"+idxStr, params.cpuctExplorationLog, 0.0, 10.0) ||
           cfg.tryGetDouble("cpuctExplorationLog", params.cpuctExplorationLog, 0.0, 10.0));

    params.cpuctExplorationBase = 500.0;
    (void)(cfg.tryGetDouble("cpuctExplorationBase"+idxStr, params.cpuctExplorationBase, 10.0, 100000.0) ||
           cfg.tryGetDouble("cpuctExplorationBase", params.cpuctExplorationBase, 10.0, 100000.0));

    params.cpuctUtilityStdevPrior = 0.40;
    (void)(cfg.tryGetDouble("cpuctUtilityStdevPrior"+idxStr, params.cpuctUtilityStdevPrior, 0.0, 10.0) ||
           cfg.tryGetDouble("cpuctUtilityStdevPrior", params.cpuctUtilityStdevPrior, 0.0, 10.0));

    params.cpuctUtilityStdevPriorWeight = 2.0;
    (void)(cfg.tryGetDouble("cpuctUtilityStdevPriorWeight"+idxStr, params.cpuctUtilityStdevPriorWeight, 0.0, 100.0) ||
           cfg.tryGetDouble("cpuctUtilityStdevPriorWeight", params.cpuctUtilityStdevPriorWeight, 0.0, 100.0));

    params.cpuctUtilityStdevScale = setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER ? 0.85 : 0.0;
    (void)(cfg.tryGetDouble("cpuctUtilityStdevScale"+idxStr, params.cpuctUtilityStdevScale, 0.0, 1.0) ||
           cfg.tryGetDouble("cpuctUtilityStdevScale", params.cpuctUtilityStdevScale, 0.0, 1.0));

    params.fpuReductionMax = 0.2;
    (void)(cfg.tryGetDouble("fpuReductionMax"+idxStr, params.fpuReductionMax, 0.0, 2.0) ||
           cfg.tryGetDouble("fpuReductionMax", params.fpuReductionMax, 0.0, 2.0));

    params.fpuLossProp = 0.0;
    (void)(cfg.tryGetDouble("fpuLossProp"+idxStr, params.fpuLossProp, 0.0, 1.0) ||
           cfg.tryGetDouble("fpuLossProp", params.fpuLossProp, 0.0, 1.0));

    params.fpuParentWeightByVisitedPolicy = setupFor != SETUP_FOR_DISTRIBUTED;
    (void)(cfg.tryGetBool("fpuParentWeightByVisitedPolicy"+idxStr, params.fpuParentWeightByVisitedPolicy) ||
           cfg.tryGetBool("fpuParentWeightByVisitedPolicy", params.fpuParentWeightByVisitedPolicy));

    if (params.fpuParentWeightByVisitedPolicy) {
      params.fpuParentWeightByVisitedPolicyPow = 2.0;
      (void)(cfg.tryGetDouble("fpuParentWeightByVisitedPolicyPow"+idxStr, params.fpuParentWeightByVisitedPolicyPow, 0.0, 5.0) ||
             cfg.tryGetDouble("fpuParentWeightByVisitedPolicyPow", params.fpuParentWeightByVisitedPolicyPow, 0.0, 5.0));
    }
    else {
      params.fpuParentWeight = 0.0;
      (void)(cfg.tryGetDouble("fpuParentWeight"+idxStr, params.fpuParentWeight, 0.0, 1.0) ||
             cfg.tryGetDouble("fpuParentWeight", params.fpuParentWeight, 0.0, 1.0));
    }

    params.policyOptimism = setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER ? 1.0 : 0.0;
    (void)(cfg.tryGetDouble("policyOptimism"+idxStr, params.policyOptimism, 0.0, 1.0) ||
           cfg.tryGetDouble("policyOptimism", params.policyOptimism, 0.0, 1.0));

    params.valueWeightExponent = 0.25;
    (void)(cfg.tryGetDouble("valueWeightExponent"+idxStr, params.valueWeightExponent, 0.0, 1.0) ||
           cfg.tryGetDouble("valueWeightExponent", params.valueWeightExponent, 0.0, 1.0));

    params.useNoisePruning = (setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER);
    (void)(cfg.tryGetBool("useNoisePruning"+idxStr, params.useNoisePruning) ||
           cfg.tryGetBool("useNoisePruning", params.useNoisePruning));

    params.noisePruneUtilityScale = 0.15;
    (void)(cfg.tryGetDouble("noisePruneUtilityScale"+idxStr, params.noisePruneUtilityScale, 0.001, 10.0) ||
           cfg.tryGetDouble("noisePruneUtilityScale", params.noisePruneUtilityScale, 0.001, 10.0));

    params.noisePruningCap = 1e50;
    (void)(cfg.tryGetDouble("noisePruningCap"+idxStr, params.noisePruningCap, 0.0, 1e50) ||
           cfg.tryGetDouble("noisePruningCap", params.noisePruningCap, 0.0, 1e50));

    params.useUncertainty = setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER;
    (void)(cfg.tryGetBool("useUncertainty"+idxStr, params.useUncertainty) ||
           cfg.tryGetBool("useUncertainty", params.useUncertainty));

    params.uncertaintyCoeff = 0.25;
    (void)(cfg.tryGetDouble("uncertaintyCoeff"+idxStr, params.uncertaintyCoeff, 0.0001, 1.0) ||
           cfg.tryGetDouble("uncertaintyCoeff", params.uncertaintyCoeff, 0.0001, 1.0));

    params.uncertaintyExponent = 1.0;
    (void)(cfg.tryGetDouble("uncertaintyExponent"+idxStr, params.uncertaintyExponent, 0.0, 2.0) ||
           cfg.tryGetDouble("uncertaintyExponent", params.uncertaintyExponent, 0.0, 2.0));

    params.uncertaintyMaxWeight = 8.0;
    (void)(cfg.tryGetDouble("uncertaintyMaxWeight"+idxStr, params.uncertaintyMaxWeight, 1.0, 100.0) ||
           cfg.tryGetDouble("uncertaintyMaxWeight", params.uncertaintyMaxWeight, 1.0, 100.0));

    params.useGraphSearch = setupFor != SETUP_FOR_DISTRIBUTED;
    (void)(cfg.tryGetBool("useGraphSearch"+idxStr, params.useGraphSearch) ||
           cfg.tryGetBool("useGraphSearch", params.useGraphSearch));

    params.graphSearchRepBound = 11;
    (void)(cfg.tryGetInt("graphSearchRepBound"+idxStr, params.graphSearchRepBound, 3, 50) ||
           cfg.tryGetInt("graphSearchRepBound", params.graphSearchRepBound, 3, 50));

    params.graphSearchCatchUpLeakProb = 0.0;

    (void)(cfg.tryGetDouble("graphSearchCatchUpLeakProb"+idxStr, params.graphSearchCatchUpLeakProb, 0.0, 1.0) ||
           cfg.tryGetDouble("graphSearchCatchUpLeakProb", params.graphSearchCatchUpLeakProb, 0.0, 1.0));

    // if(cfg.contains("graphSearchCatchUpProp"+idxStr)) params.graphSearchCatchUpProp = cfg.getDouble("graphSearchCatchUpProp"+idxStr, 0.0, 1.0);
    // else if(cfg.contains("graphSearchCatchUpProp"))   params.graphSearchCatchUpProp = cfg.getDouble("graphSearchCatchUpProp", 0.0, 1.0);
    // else                                              params.graphSearchCatchUpProp = 0.0;

    params.rootNoiseEnabled = false;
    (void)(cfg.tryGetBool("rootNoiseEnabled"+idxStr, params.rootNoiseEnabled) ||
           cfg.tryGetBool("rootNoiseEnabled", params.rootNoiseEnabled));

    params.rootDirichletNoiseTotalConcentration = 10.83;
    (void)(cfg.tryGetDouble("rootDirichletNoiseTotalConcentration"+idxStr, params.rootDirichletNoiseTotalConcentration, 0.001, 10000.0) ||
           cfg.tryGetDouble("rootDirichletNoiseTotalConcentration", params.rootDirichletNoiseTotalConcentration, 0.001, 10000.0));

    params.rootDirichletNoiseWeight = 0.25;
    (void)(cfg.tryGetDouble("rootDirichletNoiseWeight"+idxStr, params.rootDirichletNoiseWeight, 0.0, 1.0) ||
           cfg.tryGetDouble("rootDirichletNoiseWeight", params.rootDirichletNoiseWeight, 0.0, 1.0));

    params.rootPolicyTemperature = 1.0;
    (void)(cfg.tryGetDouble("rootPolicyTemperature"+idxStr, params.rootPolicyTemperature, 0.01, 100.0) ||
           cfg.tryGetDouble("rootPolicyTemperature", params.rootPolicyTemperature, 0.01, 100.0));

    params.rootPolicyTemperatureEarly = params.rootPolicyTemperature;
    (void)(cfg.tryGetDouble("rootPolicyTemperatureEarly"+idxStr, params.rootPolicyTemperatureEarly, 0.01, 100.0) ||
           cfg.tryGetDouble("rootPolicyTemperatureEarly", params.rootPolicyTemperatureEarly, 0.01, 100.0));

    params.rootFpuReductionMax = params.rootNoiseEnabled ? 0.0 : 0.1;
    (void)(cfg.tryGetDouble("rootFpuReductionMax"+idxStr, params.rootFpuReductionMax, 0.0, 2.0) ||
           cfg.tryGetDouble("rootFpuReductionMax", params.rootFpuReductionMax, 0.0, 2.0));

    params.rootFpuLossProp = params.fpuLossProp;
    (void)(cfg.tryGetDouble("rootFpuLossProp"+idxStr, params.rootFpuLossProp, 0.0, 1.0) ||
           cfg.tryGetDouble("rootFpuLossProp", params.rootFpuLossProp, 0.0, 1.0));

    params.rootNumSymmetriesToSample = 1;
    (void)(cfg.tryGetInt("rootNumSymmetriesToSample"+idxStr, params.rootNumSymmetriesToSample, 1, SymmetryHelpers::NUM_SYMMETRIES) ||
           cfg.tryGetInt("rootNumSymmetriesToSample", params.rootNumSymmetriesToSample, 1, SymmetryHelpers::NUM_SYMMETRIES));

    params.rootSymmetryPruning = (setupFor == SETUP_FOR_ANALYSIS || setupFor == SETUP_FOR_GTP);
    (void)(cfg.tryGetBool("rootSymmetryPruning"+idxStr, params.rootSymmetryPruning) ||
           cfg.tryGetBool("rootSymmetryPruning", params.rootSymmetryPruning));

    params.rootDesiredPerChildVisitsCoeff = 0.0;
    (void)(cfg.tryGetDouble("rootDesiredPerChildVisitsCoeff"+idxStr, params.rootDesiredPerChildVisitsCoeff, 0.0, 100.0) ||
           cfg.tryGetDouble("rootDesiredPerChildVisitsCoeff", params.rootDesiredPerChildVisitsCoeff, 0.0, 100.0));

    params.rootPolicyOptimism = setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER ? std::min(params.policyOptimism, 0.2) : 0.0;
    (void)(cfg.tryGetDouble("rootPolicyOptimism"+idxStr, params.rootPolicyOptimism, 0.0, 1.0) ||
           cfg.tryGetDouble("rootPolicyOptimism", params.rootPolicyOptimism, 0.0, 1.0));

    params.chosenMoveTemperature = 0.1;
    (void)(cfg.tryGetDouble("chosenMoveTemperature"+idxStr, params.chosenMoveTemperature, 0.0, 5.0) ||
           cfg.tryGetDouble("chosenMoveTemperature", params.chosenMoveTemperature, 0.0, 5.0));

    params.chosenMoveTemperatureEarly = 0.5;
    (void)(cfg.tryGetDouble("chosenMoveTemperatureEarly"+idxStr, params.chosenMoveTemperatureEarly, 0.0, 5.0) ||
           cfg.tryGetDouble("chosenMoveTemperatureEarly", params.chosenMoveTemperatureEarly, 0.0, 5.0));

    params.chosenMoveTemperatureHalflife = 19.0;
    (void)(cfg.tryGetDouble("chosenMoveTemperatureHalflife"+idxStr, params.chosenMoveTemperatureHalflife, 0.1, 100000.0) ||
           cfg.tryGetDouble("chosenMoveTemperatureHalflife", params.chosenMoveTemperatureHalflife, 0.1, 100000.0));

    params.chosenMoveTemperatureOnlyBelowProb = 1.0;
    (void)(cfg.tryGetDouble("chosenMoveTemperatureOnlyBelowProb"+idxStr, params.chosenMoveTemperatureOnlyBelowProb, 0.0, 1.0) ||
           cfg.tryGetDouble("chosenMoveTemperatureOnlyBelowProb", params.chosenMoveTemperatureOnlyBelowProb, 0.0, 1.0));

    params.chosenMoveSubtract = 0.0;
    (void)(cfg.tryGetDouble("chosenMoveSubtract"+idxStr, params.chosenMoveSubtract, 0.0, 1.0e10) ||
           cfg.tryGetDouble("chosenMoveSubtract", params.chosenMoveSubtract, 0.0, 1.0e10));

    params.chosenMovePrune = 1.0;
    (void)(cfg.tryGetDouble("chosenMovePrune"+idxStr, params.chosenMovePrune, 0.0, 1.0e10) ||
           cfg.tryGetDouble("chosenMovePrune", params.chosenMovePrune, 0.0, 1.0e10));

    params.useLcbForSelection = true;
    (void)(cfg.tryGetBool("useLcbForSelection"+idxStr, params.useLcbForSelection) ||
           cfg.tryGetBool("useLcbForSelection", params.useLcbForSelection));

    params.lcbStdevs = 5.0;
    (void)(cfg.tryGetDouble("lcbStdevs"+idxStr, params.lcbStdevs, 1.0, 12.0) ||
           cfg.tryGetDouble("lcbStdevs", params.lcbStdevs, 1.0, 12.0));

    params.minVisitPropForLCB = 0.15;
    (void)(cfg.tryGetDouble("minVisitPropForLCB"+idxStr, params.minVisitPropForLCB, 0.0, 1.0) ||
           cfg.tryGetDouble("minVisitPropForLCB", params.minVisitPropForLCB, 0.0, 1.0));

    //For distributed and selfplay, we default to buggy LCB for the moment since it has effects on the policy training target.
    params.useNonBuggyLcb = setupFor != SETUP_FOR_DISTRIBUTED && setupFor != SETUP_FOR_OTHER;
    (void)(cfg.tryGetBool("useNonBuggyLcb"+idxStr, params.useNonBuggyLcb) ||
           cfg.tryGetBool("useNonBuggyLcb", params.useNonBuggyLcb));

    params.rootEndingBonusPoints = 0.5;
    (void)(cfg.tryGetDouble("rootEndingBonusPoints"+idxStr, params.rootEndingBonusPoints, -1.0, 1.0) ||
           cfg.tryGetDouble("rootEndingBonusPoints", params.rootEndingBonusPoints, -1.0, 1.0));

    params.rootPruneUselessMoves = true;
    (void)(cfg.tryGetBool("rootPruneUselessMoves"+idxStr, params.rootPruneUselessMoves) ||
           cfg.tryGetBool("rootPruneUselessMoves", params.rootPruneUselessMoves));

    params.conservativePass = false;
    (void)(cfg.tryGetBool("conservativePass"+idxStr, params.conservativePass) ||
           cfg.tryGetBool("conservativePass", params.conservativePass));

    params.fillDameBeforePass = false;
    (void)(cfg.tryGetBool("fillDameBeforePass"+idxStr, params.fillDameBeforePass) ||
           cfg.tryGetBool("fillDameBeforePass", params.fillDameBeforePass));

    //Controlled by GTP directly, not used in any other mode
    params.avoidMYTDaggerHackPla = C_EMPTY;
    params.wideRootNoise = setupFor == SETUP_FOR_ANALYSIS ? DEFAULT_ANALYSIS_WIDE_ROOT_NOISE : 0.00;
    (void)(cfg.tryGetDouble("wideRootNoise"+idxStr, params.wideRootNoise, 0.0, 5.0) ||
           cfg.tryGetDouble("wideRootNoise", params.wideRootNoise, 0.0, 5.0));

    params.enablePassingHacks = setupFor == SETUP_FOR_GTP || setupFor == SETUP_FOR_ANALYSIS;
    (void)(cfg.tryGetBool("enablePassingHacks"+idxStr, params.enablePassingHacks) ||
           cfg.tryGetBool("enablePassingHacks", params.enablePassingHacks));

    params.enableMorePassingHacks = setupFor == SETUP_FOR_GTP || setupFor == SETUP_FOR_ANALYSIS;
    (void)(cfg.tryGetBool("enableMorePassingHacks"+idxStr, params.enableMorePassingHacks) ||
           cfg.tryGetBool("enableMorePassingHacks", params.enableMorePassingHacks));

    params.playoutDoublingAdvantage = 0.0;
    (void)(cfg.tryGetDouble("playoutDoublingAdvantage"+idxStr, params.playoutDoublingAdvantage, -3.0, 3.0) ||
           cfg.tryGetDouble("playoutDoublingAdvantage", params.playoutDoublingAdvantage, -3.0, 3.0));

    string playoutDoublingAdvantagePlaStr;
    bool playoutDoublingAdvantagePlaSpecified =
      cfg.tryGetString("playoutDoublingAdvantagePla"+idxStr, playoutDoublingAdvantagePlaStr) ||
      cfg.tryGetString("playoutDoublingAdvantagePla", playoutDoublingAdvantagePlaStr);

    params.playoutDoublingAdvantagePla = playoutDoublingAdvantagePlaSpecified
      ? parsePlayer("playoutDoublingAdvantagePla", playoutDoublingAdvantagePlaStr)
      : C_EMPTY;

    params.avoidRepeatedPatternUtility = 0.0;
    (void)(cfg.tryGetDouble("avoidRepeatedPatternUtility"+idxStr, params.avoidRepeatedPatternUtility, -3.0, 3.0) ||
           cfg.tryGetDouble("avoidRepeatedPatternUtility", params.avoidRepeatedPatternUtility, -3.0, 3.0));

    params.nnPolicyTemperature = 1.0f;
    (void)(cfg.tryGetFloat("nnPolicyTemperature"+idxStr, params.nnPolicyTemperature, 0.01f, 5.0f) ||
           cfg.tryGetFloat("nnPolicyTemperature", params.nnPolicyTemperature, 0.01f, 5.0f));

    params.antiMirror = false;
    (void)(cfg.tryGetBool("antiMirror"+idxStr, params.antiMirror) ||
           cfg.tryGetBool("antiMirror", params.antiMirror));

    params.ignorePreRootHistory = (setupFor == SETUP_FOR_ANALYSIS ? Setup::DEFAULT_ANALYSIS_IGNORE_PRE_ROOT_HISTORY : false);
    (void)(cfg.tryGetBool("ignorePreRootHistory"+idxStr, params.ignorePreRootHistory) ||
           cfg.tryGetBool("ignorePreRootHistory", params.ignorePreRootHistory));

    params.ignoreAllHistory = false;
    (void)(cfg.tryGetBool("ignoreAllHistory"+idxStr, params.ignoreAllHistory) ||
           cfg.tryGetBool("ignoreAllHistory", params.ignoreAllHistory));

    params.subtreeValueBiasFactor = 0.45;
    (void)(cfg.tryGetDouble("subtreeValueBiasFactor"+idxStr, params.subtreeValueBiasFactor, 0.0, 1.0) ||
           cfg.tryGetDouble("subtreeValueBiasFactor", params.subtreeValueBiasFactor, 0.0, 1.0));

    params.subtreeValueBiasFreeProp = 0.8;
    (void)(cfg.tryGetDouble("subtreeValueBiasFreeProp"+idxStr, params.subtreeValueBiasFreeProp, 0.0, 1.0) ||
           cfg.tryGetDouble("subtreeValueBiasFreeProp", params.subtreeValueBiasFreeProp, 0.0, 1.0));

    params.subtreeValueBiasWeightExponent = 0.85;
    (void)(cfg.tryGetDouble("subtreeValueBiasWeightExponent"+idxStr, params.subtreeValueBiasWeightExponent, 0.0, 1.0) ||
           cfg.tryGetDouble("subtreeValueBiasWeightExponent", params.subtreeValueBiasWeightExponent, 0.0, 1.0));

    params.useEvalCache = false;
    (void)(cfg.tryGetBool("useEvalCache"+idxStr, params.useEvalCache) ||
           cfg.tryGetBool("useEvalCache", params.useEvalCache));

    params.evalCacheMinVisits = 100;
    (void)(cfg.tryGetInt64("evalCacheMinVisits"+idxStr, params.evalCacheMinVisits, 1, static_cast<int64_t>(1) << 50) ||
           cfg.tryGetInt64("evalCacheMinVisits", params.evalCacheMinVisits, 1, static_cast<int64_t>(1) << 50));

    params.nodeTableShardsPowerOfTwo = 16;
    (void)(cfg.tryGetInt("nodeTableShardsPowerOfTwo"+idxStr, params.nodeTableShardsPowerOfTwo, 8, 24) ||
           cfg.tryGetInt("nodeTableShardsPowerOfTwo", params.nodeTableShardsPowerOfTwo, 8, 24));

    params.numVirtualLossesPerThread = 1.0;
    (void)(cfg.tryGetDouble("numVirtualLossesPerThread"+idxStr, params.numVirtualLossesPerThread, 0.01, 1000.0) ||
           cfg.tryGetDouble("numVirtualLossesPerThread", params.numVirtualLossesPerThread, 0.01, 1000.0));

    params.treeReuseCarryOverTimeFactor = 0.0;
    (void)(cfg.tryGetDouble("treeReuseCarryOverTimeFactor"+idxStr, params.treeReuseCarryOverTimeFactor, 0.0, 1.0) ||
           cfg.tryGetDouble("treeReuseCarryOverTimeFactor", params.treeReuseCarryOverTimeFactor, 0.0, 1.0));

    params.overallocateTimeFactor = 1.0;
    (void)(cfg.tryGetDouble("overallocateTimeFactor"+idxStr, params.overallocateTimeFactor, 0.01, 100.0) ||
           cfg.tryGetDouble("overallocateTimeFactor", params.overallocateTimeFactor, 0.01, 100.0));

    params.midgameTimeFactor = 1.0;
    (void)(cfg.tryGetDouble("midgameTimeFactor"+idxStr, params.midgameTimeFactor, 0.01, 100.0) ||
           cfg.tryGetDouble("midgameTimeFactor", params.midgameTimeFactor, 0.01, 100.0));

    params.midgameTurnPeakTime = 130.0;
    (void)(cfg.tryGetDouble("midgameTurnPeakTime"+idxStr, params.midgameTurnPeakTime, 0.0, 1000.0) ||
           cfg.tryGetDouble("midgameTurnPeakTime", params.midgameTurnPeakTime, 0.0, 1000.0));

    params.endgameTurnTimeDecay = 100.0;
    (void)(cfg.tryGetDouble("endgameTurnTimeDecay"+idxStr, params.endgameTurnTimeDecay, 0.0, 1000.0) ||
           cfg.tryGetDouble("endgameTurnTimeDecay", params.endgameTurnTimeDecay, 0.0, 1000.0));

    params.obviousMovesTimeFactor = 1.0;
    (void)(cfg.tryGetDouble("obviousMovesTimeFactor"+idxStr, params.obviousMovesTimeFactor, 0.01, 1.0) ||
           cfg.tryGetDouble("obviousMovesTimeFactor", params.obviousMovesTimeFactor, 0.01, 1.0));

    params.obviousMovesPolicyEntropyTolerance = 0.30;
    (void)(cfg.tryGetDouble("obviousMovesPolicyEntropyTolerance"+idxStr, params.obviousMovesPolicyEntropyTolerance, 0.001, 2.0) ||
           cfg.tryGetDouble("obviousMovesPolicyEntropyTolerance", params.obviousMovesPolicyEntropyTolerance, 0.001, 2.0));

    params.obviousMovesPolicySurpriseTolerance = 0.15;
    (void)(cfg.tryGetDouble("obviousMovesPolicySurpriseTolerance"+idxStr, params.obviousMovesPolicySurpriseTolerance, 0.001, 2.0) ||
           cfg.tryGetDouble("obviousMovesPolicySurpriseTolerance", params.obviousMovesPolicySurpriseTolerance, 0.001, 2.0));

    params.futileVisitsThreshold = 0.0;
    (void)(cfg.tryGetDouble("futileVisitsThreshold"+idxStr, params.futileVisitsThreshold, 0.01, 1.0) ||
           cfg.tryGetDouble("futileVisitsThreshold", params.futileVisitsThreshold, 0.01, 1.0));

    // This does NOT report an error under throwHumanParsingError like the parameters below that expect a second model
    // because the user might be providing the human model as the MAIN model. In which case humanSLProfile is still a
    // valid param but the others are not.
    if(setupFor != SETUP_FOR_DISTRIBUTED) {
      string humanSLProfileName;
      (void)(cfg.tryGetString("humanSLProfile"+idxStr, humanSLProfileName) ||
             cfg.tryGetString("humanSLProfile", humanSLProfileName));
      params.humanSLProfile = SGFMetadata::getProfile(humanSLProfileName);
    }

    auto tryGetDoubleAndValidate = [hasHumanModel](ConfigParser& configParser, const string& param, double& value, const double min, const double max) {
      if (configParser.tryGetDouble(param, value, min, max)) {
        if(!hasHumanModel) {
          throw ConfigParsingError(
            string("Provided parameter ") + param + string(" but no human model was specified (e.g -human-model b18c384nbt-humanv0.bin.gz)")
          );
        }
        return true;
      }
      return false;
    };

    auto tryGetBoolAndValidate = [hasHumanModel](ConfigParser& configParser, const string& param, bool& value) {
      if (configParser.tryGetBool(param, value)) {
        if(!hasHumanModel) {
          throw ConfigParsingError(
            string("Provided parameter ") + param + string(" but no human model was specified (e.g -human-model b18c384nbt-humanv0.bin.gz)")
          );
        }
        return true;
      }
      return false;
    };

    params.humanSLCpuctExploration = 1.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLCpuctExploration"+idxStr, params.humanSLCpuctExploration, 0.0, 1000.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLCpuctExploration", params.humanSLCpuctExploration, 0.0, 1000.0));

    params.humanSLCpuctPermanent = 0.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLCpuctPermanent"+idxStr, params.humanSLCpuctPermanent, 0.0, 1000.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLCpuctPermanent", params.humanSLCpuctPermanent, 0.0, 1000.0));

    params.humanSLRootExploreProbWeightless = 0.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLRootExploreProbWeightless"+idxStr, params.humanSLRootExploreProbWeightless, 0.0, 1.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLRootExploreProbWeightless", params.humanSLRootExploreProbWeightless, 0.0, 1.0));

    params.humanSLRootExploreProbWeightful = 0.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLRootExploreProbWeightful"+idxStr, params.humanSLRootExploreProbWeightful, 0.0, 1.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLRootExploreProbWeightful", params.humanSLRootExploreProbWeightful, 0.0, 1.0));

    params.humanSLPlaExploreProbWeightless = 0.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLPlaExploreProbWeightless"+idxStr, params.humanSLPlaExploreProbWeightless, 0.0, 1.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLPlaExploreProbWeightless", params.humanSLPlaExploreProbWeightless, 0.0, 1.0));

    params.humanSLPlaExploreProbWeightful = 0.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLPlaExploreProbWeightful"+idxStr, params.humanSLPlaExploreProbWeightful, 0.0, 1.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLPlaExploreProbWeightful", params.humanSLPlaExploreProbWeightful, 0.0, 1.0));

    params.humanSLOppExploreProbWeightless = 0.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLOppExploreProbWeightless"+idxStr, params.humanSLOppExploreProbWeightless, 0.0, 1.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLOppExploreProbWeightless", params.humanSLOppExploreProbWeightless, 0.0, 1.0));

    params.humanSLOppExploreProbWeightful = 0.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLOppExploreProbWeightful"+idxStr, params.humanSLOppExploreProbWeightful, 0.0, 1.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLOppExploreProbWeightful", params.humanSLOppExploreProbWeightful, 0.0, 1.0));

    params.humanSLChosenMoveProp = 0.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLChosenMoveProp"+idxStr, params.humanSLChosenMoveProp, 0.0, 1.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLChosenMoveProp", params.humanSLChosenMoveProp, 0.0, 1.0));


    params.humanSLChosenMoveIgnorePass = false;
    (void)(
      tryGetBoolAndValidate(cfg, "humanSLChosenMoveIgnorePass"+idxStr, params.humanSLChosenMoveIgnorePass) ||
      tryGetBoolAndValidate(cfg, "humanSLChosenMoveIgnorePass", params.humanSLChosenMoveIgnorePass));

    params.humanSLChosenMovePiklLambda = 1000000000.0;
    (void)(
      tryGetDoubleAndValidate(cfg, "humanSLChosenMovePiklLambda"+idxStr, params.humanSLChosenMovePiklLambda, 0.0, 1000000000.0) ||
      tryGetDoubleAndValidate(cfg, "humanSLChosenMovePiklLambda", params.humanSLChosenMovePiklLambda, 0.0, 1000000000.0));

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
