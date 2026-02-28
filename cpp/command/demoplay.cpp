#include "../command/commandline.h"
#include "../core/config_parser.h"
#include "../core/global.h"
#include "../core/parallel.h"
#include "../core/test.h"
#include "../dataio/sgf.h"
#include "../main.h"
#include "../program/play.h"
#include "../program/playutils.h"
#include "../program/setup.h"
#include "../search/asyncbot.h"

#include <chrono>

using namespace std;

static void writeLine(
  const Search* search, const BoardHistory& baseHist,
  const vector<double>& winLossHistory, const vector<double>& scoreHistory, const vector<double>& scoreStdevHistory
) {
  const Board& board = search->getRootBoard();

  const int nnXLen = search->nnXLen;
  const int nnYLen = search->nnYLen;

  const bool isDots = board.isDots();
  if (!isDots) {
    cout << board.x_size << " ";
    cout << board.y_size << " ";
    cout << nnXLen << " ";
    cout << nnYLen << " ";
    cout << baseHist.rules.komi << " ";
    if(baseHist.isGameFinished) {
      cout << PlayerIO::playerToString(baseHist.winner, baseHist.rules.isDots) << " ";
      cout << baseHist.isResignation << " ";
      cout << baseHist.finalWhiteMinusBlackScore << " ";
    }
    else {
      cout << "-" << " ";
      cout << "false" << " ";
      cout << "0" << " ";
    }

    //Last move
    Loc moveLoc = Board::NULL_LOC;
    if(!baseHist.moveHistory.empty())
      moveLoc = baseHist.moveHistory[baseHist.moveHistory.size()-1].loc;
    cout << NNPos::locToPos(moveLoc,board.x_size,nnXLen,nnYLen) << " ";

    cout << baseHist.moveHistory.size() << " ";
    cout << board.numBlackCaptures << " ";
    cout << board.numWhiteCaptures << " ";

    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        cout << PlayerIO::stateToChar(board.getState(Location::getLoc(x, y, board.x_size)), board.isDots());
      }
    }
    cout << " ";
  } else {
    baseHist.printBasicInfo(cout, board);
  }

  vector<AnalysisData> buf;
  if(!baseHist.isGameFinished) {
    constexpr int minMovesToTryToGet = 0; // just get the default number
    constexpr bool duplicateForSymmetries = true;
    search->getAnalysisData(buf,minMovesToTryToGet,false,9,duplicateForSymmetries);
  }

  const int bufSize = static_cast<int>(buf.size());

  if (!isDots) {
    cout << bufSize << " ";
    for(int i = 0; i < bufSize; i++) {
      const AnalysisData& data = buf[i];
      cout << NNPos::locToPos(data.move,board.x_size,nnXLen,nnYLen) << " ";
      cout << data.numVisits << " ";
      cout << data.winLossValue << " ";
      cout << data.scoreMean << " ";
      cout << data.scoreStdev << " ";
      cout << data.policyPrior << " ";
    }
  } else {
    cout << "bufSize: " << bufSize << endl;
    for(int i = 0; i < min(10, bufSize); i++) {
      const AnalysisData& data = buf[i];
      cout << "pos: " << Location::toString(data.move, board.x_size, board.y_size, isDots) << ", ";
      cout << "numVisits: " << data.numVisits << ", ";
      cout << "winLossValue: " << data.winLossValue << ", ";
      cout << "scoreMean: " << data.scoreMean << ", ";
      cout << "scoreStdev: " << data.scoreStdev << ", ";
      cout << "policyPrior: " << data.policyPrior;
      cout << endl;
    }
    if (bufSize > 10) cout << "... " << endl;
  }

  /* TODO: https://github.com/lightvector/KataGo/issues/1163
   *
   vector<double> ownership = search->getAverageTreeOwnership();
  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      int pos = NNPos::xyToPos(x,y,nnXLen);
      cout << ownership[pos] << " ";
    }
  }*/

  auto printHistoryEstimations = [&](const vector<double>& history, const string& name) {
    const int size = static_cast<int>(history.size());
    if (!isDots) {
      cout << size << " ";
      for(int i = 0; i< size; i++)
        cout << history[i] << " ";
    } else {
      cout << name << " (size: " << size << "): ";
      if (size > 10) cout << "... ";
      for (int i = max(0, size - 10); i < size; i++) {
        cout << history[i] << " ";
      }
      cout << endl;
    }
  };

  printHistoryEstimations(winLossHistory, "winLossHistory");
  printHistoryEstimations(scoreHistory, "scoreHistory");

  cout << endl;
}

static BoardHistory initializeDemoGame(bool isDots, Rand& rand, AsyncBot* bot) {
  Rules rules;
  int sizeX, sizeY;
  if (!isDots) {
    rules = Rules::getTrompTaylorish();
    static constexpr int numSizes = 9;
    int sizes[numSizes] = {19,13,9,15,11,10,12,14,16};
    int sizeFreqs[numSizes] = {240,18,12,6,2,1,1,1,1};
    sizeX = sizes[rand.nextUInt(sizeFreqs,numSizes)];
    sizeY = sizeX;
  } else {
    rules = Rules::DEFAULT_DOTS;

    static constexpr int numStartPoses = 4;
    int startPoses[numStartPoses] = { Rules::START_POS_CROSS, Rules::START_POS_CROSS_4, Rules::START_POS_CROSS_2, Rules::START_POS_SINGLE };
    int startPosesFreqs[numStartPoses] = { 10, 5, 2, 1 };
    rules.startPos = startPoses[rand.nextUInt(startPosesFreqs, numStartPoses)];

    switch (rand.nextInt(0, 2)) {
      case 0:
        rules.komi = 0.5;
        break;
      case 1:
        rules.komi = -0.5;
        break;
      default:
        rules.komi = 0.0;
        break;
    }

    if (rand.nextBool(0.5)) {
      sizeX = 20;
      sizeY = 20;
    } else {
      sizeX = 39;
      sizeY = 32;
    }

    if (sizeX > Board::MAX_LEN_X) {
      sizeX = Board::MAX_LEN_X;
    }
    if (sizeY > Board::MAX_LEN_Y) {
      sizeY = Board::MAX_LEN_Y;
    }
  }

  auto board = Board(sizeX, sizeY, rules);
  Player pla = board.setStartPos(rand);
  BoardHistory hist(board, pla, rules, 0);

  bot->setPosition(pla,board,hist);

  if(!isDots && sizeX == 19 && sizeY == 19) {
    //Many games use a special opening
    if(rand.nextBool(0.6)) {
      auto g = [sizeX](int x, int y) { return Location::getLoc(x,y,sizeX); };
      const Move nb = Move(Board::NULL_LOC, P_BLACK);
      const Move nw = Move(Board::NULL_LOC, P_WHITE);
      Player b = P_BLACK;
      Player w = P_WHITE;
      vector<vector<Move>> specialOpenings = {
        //Sanrensei
        { Move(g(3,3), b), nw, Move(g(15,3), b), nw, Move(g(9,3), b) },
        //Low Chinese
        { Move(g(3,3), b), nw, Move(g(16,3), b), nw, Move(g(10,2), b) },
        //Low Chinese
        { Move(g(3,3), b), nw, Move(g(16,3), b), nw, Move(g(10,2), b) },
        //High chinese
        { Move(g(3,3), b), nw, Move(g(16,3), b), nw, Move(g(10,3), b) },
        //Low small chinese
        { Move(g(3,3), b), nw, Move(g(16,3), b), nw, Move(g(11,2), b) },
        //Kobayashi
        { Move(g(3,3), b), Move(g(15,15), w), Move(g(16,3), b), nw, Move(g(16,13), b), Move(g(13,16), w), Move(g(15,9), b) },
        //Kobayashi
        { Move(g(3,3), b), Move(g(15,15), w), Move(g(16,3), b), nw, Move(g(16,13), b), Move(g(13,16), w), Move(g(15,9), b) },
        //Mini chinese
        { Move(g(3,3), b), Move(g(15,15), w), Move(g(15,2), b), nw, Move(g(16,13), b), Move(g(13,16), w), Move(g(16,8), b) },
        //Mini chinese
        { Move(g(3,3), b), Move(g(15,15), w), Move(g(15,2), b), nw, Move(g(16,13), b), Move(g(13,16), w), Move(g(16,8), b) },
        //Micro chinese
        { Move(g(3,3), b), Move(g(15,15), w), Move(g(15,2), b), nw, Move(g(16,13), b), Move(g(13,16), w), Move(g(16,7), b) },
        //Micro chinese with variable other corner
        { Move(g(15,2), b), Move(g(15,15), w), nb, nw, Move(g(16,13), b), Move(g(13,16), w), Move(g(16,7), b) },
        //Boring star points
        { Move(g(15,3), b), Move(g(15,15), w), nb, nw, Move(g(16,13), b), Move(g(13,16), w), Move(g(15,9), b) },
        //High 3-4 counter approaches
        { Move(g(3,3), b), Move(g(15,16), w), Move(g(16,3), b), nw, Move(g(15,14), b), Move(g(14,3), w) },
        //Double 3-3
        { Move(g(2,2), b), nw, Move(g(16,2), b) },
        //Low enclosure
        { Move(g(2,3), b), nw, Move(g(4,2), b) },
        //High enclosure
        { Move(g(2,3), b), nw, Move(g(4,3), b) },
        //5-5 point
        { Move(g(4,4), b) },
        //5-3 point
        { Move(g(2,4), b) },
        //5-4 point
        { Move(g(3,4), b) },
        //3-3 point
        { Move(g(2,2), b) },
        //3-4 point far approach
        { Move(g(3,2), b), Move(g(2,5), w) },
        //Tengen
        { Move(g(9,9), b) },
        //2-2 point
        { Move(g(1,1), b) },
        //Shusaku fuseki
        { Move(g(16,15), b), Move(g(3,16), w), Move(g(15,2), b), Move(g(14,16), w), nb, Move(g(16,4), w), Move(g(15,14), b) },
        //Miyamoto fuseki
        { Move(g(16,13), b), Move(g(3,15), w), Move(g(13,2), b), nw, Move(g(9,16), b) },
        //4-4 1-space low pincer - shared side
        { Move(g(15,15), b), Move(g(3,15), w), nb, nw, Move(g(5,16), b), Move(g(7,16), w) },
        //4-4 2-space high pincer - shared side
        { Move(g(15,15), b), Move(g(3,15), w), nb, nw, Move(g(5,16), b), Move(g(8,15), w) },
        //4-4 1-space low pincer - opponent side
        { Move(g(15,15), b), Move(g(3,15), w), nb, nw, Move(g(2,13), b), Move(g(2,11), w) },
        //4-4 2-space high pincer - opponent side
        { Move(g(15,15), b), Move(g(3,15), w), nb, nw, Move(g(2,13), b), Move(g(3,10), w) },
        //3-4 1-space low approach - shusaku kosumi and long extend
        { Move(g(15,15), b), Move(g(3,16), w), nb, nw, Move(g(2,14), b), Move(g(4,15), w), Move(g(2,10), b) },
        //3-4 1-space low approach low pincer - opponent side
        { Move(g(15,15), b), Move(g(3,16), w), nb, nw, Move(g(2,14), b), Move(g(2,12), w) },
        //3-4 2-space low approach high pincer - opponent side
        { Move(g(15,15), b), Move(g(3,16), w), nb, nw, Move(g(2,14), b), Move(g(3,11), w) },
        //3-4 1-space high approach - opponent side
        { Move(g(15,15), b), Move(g(3,16), w), nb, nw, Move(g(3,14), b) },
        //3-4 1-space high approach low pincer - opponent side
        { Move(g(15,15), b), Move(g(3,16), w), nb, nw, Move(g(3,14), b), Move(g(2,12), w) },
        //3-4 2-space high approach high pincer - opponent side
        { Move(g(15,15), b), Move(g(3,16), w), nb, nw, Move(g(3,14), b), Move(g(3,11), w) },
        //Orthodox
        { Move(g(3,3), b), nw, Move(g(15,2), b), nw, Move(g(16,4), b), Move(g(9,2), w) },
        //Manchurian
        { Move(g(4,3), b), nw, Move(g(16,3), b), nw, Move(g(10,3), b) },
        //Upper Manchurian
        { Move(g(4,4), b), nw, Move(g(16,4), b), nw, Move(g(10,4), b) },
        //Great wall
        { Move(g(9,9), b), nw, Move(g(9,15), b), nw, Move(g(9,3), b), nw, Move(g(8,12), b), nw, Move(g(10,6), b) },
        //Small wall
        { Move(g(9,8), b), nw, Move(g(8,11), b), nw, Move(g(10,5), b) },
        //High approaches
        { Move(g(3,2), b), Move(g(3,4), w), Move(g(16,3), b), Move(g(14,3), w), Move(g(15,16), b), Move(g(15,14), w) },
        //Black hole
        { Move(g(12,14), b), nw, Move(g(14,6), b), nw, Move(g(4,12), b), nw, Move(g(6,4), b) },
        //Crosscut
        { Move(g(9,9), b), Move(g(9,10), w), Move(g(10,10), b), Move(g(10,9), w) },
        //One-point jump center
        { Move(g(9,8), b), nw, Move(g(9,10), b) },
      };

      vector<Move> chosenOpening = specialOpenings[rand.nextUInt((int)specialOpenings.size())];
      vector<vector<Move>> chosenOpenings;

      for(int j = 0; j<8; j++) {
        vector<Move> symmetric;
        for(int k = 0; k<chosenOpening.size(); k++) {
          Loc loc = chosenOpening[k].loc;
          Player movePla = chosenOpening[k].pla;
          if(loc == Board::NULL_LOC || loc == Board::PASS_LOC)
            symmetric.emplace_back(loc,movePla);
          else {
            int x = Location::getX(loc,sizeX);
            int y = Location::getY(loc,sizeX);
            if(j & 1) x = sizeX-1-x;
            if(j & 2) y = sizeY-1-y;
            if(j & 4) std::swap(x,y);
            symmetric.emplace_back(Location::getLoc(x,y,sizeX),movePla);
          }
        }
        chosenOpenings.push_back(symmetric);
      }
      for(int j = (int)chosenOpenings.size()-1; j>=1; j--) {
        int r = rand.nextUInt(j+1);
        vector<Move> tmp = chosenOpenings[j];
        chosenOpenings[j] = chosenOpenings[r];
        chosenOpenings[r] = tmp;
      }

      vector<Move> movesPlayed;
      vector<Move> freeMovesPlayed;
      vector<Move> specifiedMovesPlayed;
      while(true) {
        auto withinRadius1 = [sizeX](Loc l0, Loc l1) {
          if(l0 == Board::NULL_LOC || l1 == Board::NULL_LOC || l0 == Board::PASS_LOC || l1 == Board::PASS_LOC)
            return false;
          int x0 = Location::getX(l0,sizeX);
          int y0 = Location::getY(l0,sizeX);
          int x1 = Location::getX(l1,sizeX);
          int y1 = Location::getY(l1,sizeX);
          return std::abs(x0-x1) <= 1 && std::abs(y0-y1) <= 1;
        };
        auto symmetryIsGood = [&movesPlayed,&specifiedMovesPlayed,&freeMovesPlayed,&withinRadius1](const vector<Move>& moves) {
          assert(movesPlayed.size() <= moves.size());
          //Make sure the symmetry matches up to the desired point,
          //and that free moves are not within radius 1 of any specified move
          for(int j = 0; j<movesPlayed.size(); j++) {
            if(moves[j].loc == Board::NULL_LOC) {
              Loc actualLoc = movesPlayed[j].loc;
              for(int k = 0; k<specifiedMovesPlayed.size(); k++) {
                if(withinRadius1(specifiedMovesPlayed[k].loc,actualLoc))
                  return false;
              }
            }
            else if(movesPlayed[j].loc != moves[j].loc)
              return false;
          }

          //Make sure the next move will also not be within radius 1 of any free move.
          if(movesPlayed.size() < moves.size()) {
            Loc nextLoc = moves[movesPlayed.size()].loc;
            for(int k = 0; k<freeMovesPlayed.size(); k++) {
              if(withinRadius1(freeMovesPlayed[k].loc,nextLoc))
                return false;
            }
          }

          return true;
        };

        //Take the first good symmetry
        vector<Move> goodSymmetry;
        for(int i = 0; i<chosenOpenings.size(); i++) {
          if(symmetryIsGood(chosenOpenings[i])) {
            goodSymmetry = chosenOpenings[i];
            break;
          }
        }

        //If we have no further moves on that symmetry, we're done
        if(movesPlayed.size() >= goodSymmetry.size())
          break;

        Move nextMove = goodSymmetry[movesPlayed.size()];
        bool wasSpecified = true;

        if(nextMove.loc == Board::NULL_LOC) {
          wasSpecified = false;
          Search* search = bot->getSearchStopAndWait();
          NNResultBuf buf;
          MiscNNInputParams nnInputParams;
          nnInputParams.drawEquivalentWinsForWhite = search->searchParams.drawEquivalentWinsForWhite;
          search->nnEvaluator->evaluate(board,hist,pla,nnInputParams,buf,false,false);
          std::shared_ptr<NNOutput> nnOutput = std::move(buf.result);

          double temperature = 0.8;
          bool allowPass = false;
          Loc banMove = Board::NULL_LOC;
          Loc loc = PlayUtils::chooseRandomPolicyMove(nnOutput.get(), board, hist, pla, rand, temperature, allowPass, banMove);
          nextMove.loc = loc;
        }

        //Make sure the next move is legal
        if(!hist.isLegal(board,nextMove.loc,nextMove.pla))
          break;

        //Make the move!
        hist.makeBoardMoveAssumeLegal(board,nextMove.loc,nextMove.pla,NULL);
        pla = getOpp(pla);

        hist.clear(board,pla,hist.rules,0);
        bot->setPosition(pla,board,hist);

        movesPlayed.push_back(nextMove);
        if(wasSpecified)
          specifiedMovesPlayed.push_back(nextMove);
        else
          freeMovesPlayed.push_back(nextMove);

        bot->clearSearch();
        writeLine(bot->getSearch(),hist,vector<double>(),vector<double>(),vector<double>());
        std::this_thread::sleep_for(std::chrono::duration<double>(1.0));

      } //Close while(true)

      int numVisits = 20;
      PlayUtils::adjustKomiToEven(bot->getSearchStopAndWait(),NULL,board,hist,pla,numVisits,OtherGameProperties(),rand);
      double komi = hist.rules.komi + 0.3 * rand.nextGaussian();
      komi = 0.5 * round(2.0 * komi);
      hist.setKomi((float)komi);
      bot->setPosition(pla,board,hist);
    }
  }

  bot->clearSearch();
  writeLine(bot->getSearch(),hist,vector<double>(),vector<double>(),vector<double>());
  std::this_thread::sleep_for(std::chrono::duration<double>(2.0));

  return hist;
}


int MainCmds::demoplay(const vector<string>& args) {
  Board::initHash();
  ScoreValue::initTables();
  Rand seedRand;

  ConfigParser cfg;
  string logFile;
  string modelFile;
  try {
    KataGoCommandLine cmd("Self-play demo dumping status to stdout");
    cmd.addConfigFileArg("","");
    cmd.addModelFileArg();
    cmd.addOverrideConfigArg();

    TCLAP::ValueArg logFileArg("","log-file","Log file to output to",false,string(),"FILE");
    cmd.add(logFileArg);
    cmd.parseArgs(args);

    modelFile = cmd.getModelFile();
    logFile = logFileArg.getValue();

    cmd.getConfig(cfg);
  }
  catch (TCLAP::ArgException &e) {
    cerr << "Error: " << e.error() << " for argument " << e.argId() << endl;
    return 1;
  }

  Logger logger(&cfg);
  logger.addFile(logFile);

  logger.write("Engine starting...");

  string searchRandSeed = Global::uint64ToString(seedRand.nextUInt64());

  SearchParams params = Setup::loadSingleParams(cfg,Setup::SETUP_FOR_OTHER);

  NNEvaluator* nnEval;
  {
    Setup::initializeSession(cfg);
    const int expectedConcurrentEvals = params.numThreads;
    const int defaultMaxBatchSize = -1;
    const bool defaultRequireExactNNLen = false;
    const bool disableFP16 = false;
    const string expectedSha256;
    nnEval = Setup::initializeNNEvaluator(
      modelFile,modelFile,expectedSha256,cfg,logger,seedRand,expectedConcurrentEvals,
      NNPos::MAX_BOARD_LEN_X,NNPos::MAX_BOARD_LEN_Y,defaultMaxBatchSize,defaultRequireExactNNLen,disableFP16,
      Setup::SETUP_FOR_OTHER
    );
  }
  logger.write("Loaded neural net");

  const bool allowResignation = cfg.getOrDefaultBool("allowResignation", false);
  const double resignThreshold = allowResignation ? cfg.getDouble("resignThreshold",-1.0,0.0) : -1.0; //Threshold on [-1,1], regardless of winLossUtilityFactor
  const double resignScoreThreshold = allowResignation ? cfg.getDouble("resignScoreThreshold",-10000.0,0.0) : -10000.0;

  const double searchFactorWhenWinning = cfg.getOrDefaultDouble("searchFactorWhenWinning", 0.01, 1.0, 1.0);
  const double searchFactorWhenWinningThreshold = cfg.getOrDefaultDouble("searchFactorWhenWinningThreshold", 0.0, 1.0, 1.0);

  bool isDots = cfg.getOrDefaultBool(DOTS_KEY, false);
  int numGamesTotal = cfg.getOrDefaultInt("numGamesTotal", 1, std::numeric_limits<int>::max(), 1);
  int printGameEvery = cfg.getOrDefaultInt("printGameEvery", 1, std::numeric_limits<int>::max(), 1);

  //Check for unused config keys
  cfg.warnUnusedKeys(cerr,&logger);
  Setup::maybeWarnHumanSLParams(params,nnEval,nullptr,cerr,&logger);

  auto* bot = new AsyncBot(params, nnEval, &logger, searchRandSeed);
  bot->setAlwaysIncludeOwnerMap(true);
  Rand gameRand;

  //Done loading!
  //------------------------------------------------------------------------------------
  logger.write("Loaded all config stuff, starting demo");

  int gameNumber = 0;

  //Game loop
  while (gameNumber++ < numGamesTotal) {
    TimeControls tc;

    BoardHistory baseHist = initializeDemoGame(isDots, gameRand, bot);
    Player pla = baseHist.presumedNextMovePla;
    Board baseBoard = baseHist.getRecentBoard(0);

    bot->setPosition(pla,baseBoard,baseHist);

    vector<double> recentWinLossValues;
    vector<double> recentScores;
    vector<double> recentScoreStdevs;

    double callbackPeriod = 0.05;

    std::function callback = [&baseHist,&recentWinLossValues,&recentScores,&recentScoreStdevs](const Search* search) {
      writeLine(search,baseHist,recentWinLossValues,recentScores,recentScoreStdevs);
    };

    std::function emptyCallback = [](const Search*) {};

    //Move loop
    int maxMovesPerGame = 1600;
    for(int i = 0; i < maxMovesPerGame; i++) {
      if (baseHist.endGameIfReasonable(baseBoard, true, pla))
        break;

      // Skip the initial board/field because it was already printed during initialization
      if (i > 0 && i % printGameEvery == 0) {
        callback(bot->getSearch());
      }

      double searchFactor =
        //Speed up when either player is winning confidently, not just the winner only
        std::min(
          PlayUtils::getSearchFactor(searchFactorWhenWinningThreshold,searchFactorWhenWinning,params,recentWinLossValues,P_BLACK),
          PlayUtils::getSearchFactor(searchFactorWhenWinningThreshold,searchFactorWhenWinning,params,recentWinLossValues,P_WHITE)
        );
      Loc moveLoc = bot->genMoveSynchronousAnalyze(pla,tc,searchFactor,callbackPeriod,callbackPeriod,
        isDots ? emptyCallback : callback
      );

      bool isLegal = bot->isLegalStrict(moveLoc,pla);
      if(moveLoc == Board::NULL_LOC || !isLegal) {
        ostringstream sout;
        sout << "genmove null location or illegal move!?!" << "\n";
        sout << bot->getRootBoard() << "\n";
        sout << "Pla: " << PlayerIO::playerToString(pla, bot->getRootBoard().isDots()) << "\n";
        sout << "MoveLoc: " << Location::toString(moveLoc,bot->getRootBoard()) << "\n";
        logger.write(sout.str());
        cerr << sout.str() << endl;
        throw StringError("illegal move");
      }

      double winLossValue;
      double expectedScore;
      double expectedScoreStdev;
      {
        ReportedSearchValues values = bot->getSearch()->getRootValuesRequireSuccess();
        winLossValue = values.winLossValue;
        expectedScore = values.expectedScore;
        expectedScoreStdev = values.expectedScoreStdev;
      }

      recentWinLossValues.push_back(winLossValue);
      recentScores.push_back(expectedScore);
      recentScoreStdevs.push_back(expectedScoreStdev);

      bool resigned = false;
      if(allowResignation) {
        const BoardHistory hist = bot->getRootHist();
        const Board initialBoard = hist.initialBoard;

        //Play at least some moves no matter what
        int minTurnForResignation = 1 + initialBoard.x_size * initialBoard.y_size / 6;

        Player resignPlayerThisTurn = C_EMPTY;
        if(winLossValue < resignThreshold && expectedScore < resignScoreThreshold)
          resignPlayerThisTurn = P_WHITE;
        else if(winLossValue > -resignThreshold && expectedScore > -resignScoreThreshold)
          resignPlayerThisTurn = P_BLACK;

        if(resignPlayerThisTurn == pla &&
           bot->getRootHist().moveHistory.size() >= minTurnForResignation)
          resigned = true;
      }

      Loc finalLoc = resigned ? Board::RESIGN_LOC : moveLoc;

      //And make the move on our copy of the board
      assert(baseHist.isLegal(baseBoard, finalLoc, pla));
      baseHist.makeBoardMoveAssumeLegal(baseBoard, finalLoc,pla, nullptr);

      //If the game is over (because of resignation or other reason), skip making the move on the bot, to preserve
      //the last known value of the search tree for display purposes
      //Just immediately terminate the game loop
      if (baseHist.isGameFinished)
        break;

      bool suc = bot->makeMove(finalLoc,pla);
      assert(suc);
      (void)suc; //Avoid warning when asserts are off

      pla = getOpp(pla);
    }

    //End of game display line
    writeLine(bot->getSearch(),baseHist,recentWinLossValues,recentScores,recentScoreStdevs);
    //Wait a bit before diving into the next game
    std::this_thread::sleep_for(std::chrono::seconds(10));

    bot->clearSearch();
  }

  delete bot;
  delete nnEval;
  NeuralNet::globalCleanup();
  ScoreValue::freeTables();

  logger.write("All cleaned up, quitting");
  return 0;

}
