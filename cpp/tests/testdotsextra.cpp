#include "../tests/tests.h"
#include "testdotsutils.h"

#include "../game/graphhash.h"
#include "../program/playutils.h"

using namespace std;
using namespace TestCommon;

static void checkSymmetry(const Board& initBoard, const string& expectedSymmetryBoardInput, const vector<XYMove>& extraMoves, const int symmetry) {
  const Board transformedBoard = SymmetryHelpers::getSymBoard(initBoard, symmetry);
  Board expectedBoard = parseDotsFieldDefault(expectedSymmetryBoardInput);
  for (const XYMove& extraMove : extraMoves) {
    expectedBoard.playMoveAssumeLegal(SymmetryHelpers::getSymLoc(extraMove.x, extraMove.y, initBoard, symmetry), extraMove.player);
  }
  expect(SymmetryHelpers::symmetryToString(symmetry).c_str(), Board::toStringSimple(transformedBoard), Board::toStringSimple(expectedBoard));
  testAssert(transformedBoard.isEqualForTesting(expectedBoard));
}

void Tests::runDotsSymmetryTests() {
  cout << "Running dots symmetry tests" << endl;

  Board initialBoard = parseDotsFieldDefault(R"(
...ox
..ox.
.o.ox
.xo..
)");
  initialBoard.playMoveAssumeLegal(Location::getLoc(4, 1, initialBoard.x_size), P_WHITE);
  testAssert(1 == initialBoard.numBlackCaptures);

  checkSymmetry(initialBoard, R"(
...ox
..ox.
.o.ox
.xo..
)",
{ XYMove(4, 1, P_WHITE)},
SymmetryHelpers::SYMMETRY_NONE);

  checkSymmetry(initialBoard, R"(
.xo..
.o.ox
..ox.
...ox
)",
{ XYMove(4, 1, P_WHITE)},
SymmetryHelpers::SYMMETRY_FLIP_Y);

  checkSymmetry(initialBoard, R"(
xo...
.xo..
xo.o.
..ox.
)",
{ XYMove(4, 1, P_WHITE)},
  SymmetryHelpers::SYMMETRY_FLIP_X);

  checkSymmetry(initialBoard, R"(
..ox.
xo.o.
.xo..
xo...
)",
{ XYMove(4, 1, P_WHITE)},
SymmetryHelpers::SYMMETRY_FLIP_Y_X);

  checkSymmetry(initialBoard, R"(
....
..ox
.o.o
oxo.
x.x.
)",
{ XYMove(4, 1, P_WHITE)},
SymmetryHelpers::SYMMETRY_TRANSPOSE);

  checkSymmetry(initialBoard, R"(
....
xo..
o.o.
.oxo
.x.x
)",
{ XYMove(4, 1, P_WHITE)},
SymmetryHelpers::SYMMETRY_TRANSPOSE_FLIP_X);

  checkSymmetry(initialBoard, R"(
x.x.
oxo.
.o.o
..ox
....
)",
{ XYMove(4, 1, P_WHITE)},
SymmetryHelpers::SYMMETRY_TRANSPOSE_FLIP_Y);

  checkSymmetry(initialBoard, R"(
.x.x
.oxo
o.o.
xo..
....
)",
{ XYMove(4, 1, P_WHITE)},
SymmetryHelpers::SYMMETRY_TRANSPOSE_FLIP_Y_X);

  cout << "Check dots symmetry with start pos" << endl;
  const auto originalRules = Rules(Rules::DEFAULT_DOTS.startPos, false, Rules::DEFAULT_DOTS.multiStoneSuicideLegal, Rules::DEFAULT_DOTS.dotsCaptureEmptyBases, Rules::DEFAULT_DOTS.dotsFreeCapturedDots);
  auto board = Board(5, 4, originalRules);
  const Player pla = board.setStartPos(DOTS_RANDOM);
  board.playMoveAssumeLegal(Location::getLoc(1, 2, board.x_size), pla);

  const auto rotatedBoard = SymmetryHelpers::getSymBoard(board, SymmetryHelpers::SYMMETRY_TRANSPOSE_FLIP_X);

  auto rulesAfterTransformation = originalRules;
  rulesAfterTransformation.startPosIsRandom = true;
  auto expectedBoard = Board(4, 5, rulesAfterTransformation);
  expectedBoard.setStonesFailIfNoLibs({
    Move(Location::getLoc(2, 2,  expectedBoard.x_size), P_BLACK),
    Move(Location::getLoc(2, 3,  expectedBoard.x_size), P_WHITE),
    Move(Location::getLoc(1, 3,  expectedBoard.x_size), P_BLACK),
    Move(Location::getLoc(1, 2,  expectedBoard.x_size), P_WHITE),
  }, true);
  expectedBoard.playMoveAssumeLegal(Location::getLoc(1, 1, expectedBoard.x_size), P_BLACK);

  expect("Dots symmetry with start pos", Board::toStringSimple(rotatedBoard), Board::toStringSimple(expectedBoard));
  testAssert(rotatedBoard.isEqualForTesting(expectedBoard));

  const auto unrotatedBoard = SymmetryHelpers::getSymBoard(rotatedBoard, SymmetryHelpers::SYMMETRY_TRANSPOSE_FLIP_Y);
  testAssert(board.isEqualForTesting(unrotatedBoard));
}

static string getOwnership(const string& boardData, const Color groundingPlayer, const int expectedWhiteScore, const vector<XYMove>& extraMoves) {
  const Board board = parseDotsFieldDefault(boardData, extraMoves);

  Color result[Board::MAX_ARR_SIZE];
  const int whiteScore = board.calculateOwnershipAndWhiteScore(result, groundingPlayer);
  testAssert(expectedWhiteScore == whiteScore);

  std::ostringstream oss;

  for (int y = 0; y < board.y_size; y++) {
    for (int x = 0; x < board.x_size; x++) {
      const Loc loc = Location::getLoc(x, y, board.x_size);
      oss << PlayerIO::colorToChar(result[loc]);
    }
    oss << endl;
  }

  return oss.str();
}

static void expect(
  const char* name,
  const Color groundingPlayer,
  const std::string& actualField,
  const std::string& expectedOwnership,
  const int expectedWhiteScore,
  const vector<XYMove>& extraMoves = {}
) {
  cout << "    " << name << ", Grounding Player: " << PlayerIO::colorToChar(groundingPlayer) << endl;
  expect(name, getOwnership(actualField, groundingPlayer, expectedWhiteScore, extraMoves), expectedOwnership);
}

void Tests::runDotsOwnershipTests() {
  expect("Start Cross", C_EMPTY, R"(
......
......
..ox..
..xo..
......
......
)",
  R"(
......
......
......
......
......
......
)", 0);

  expect("Wins by a base", C_EMPTY, R"(
......
......
..ox..
.oxo..
......
......
)",
R"(
......
......
......
..O...
......
......
)", 1, {XYMove(2, 4, P_WHITE)});

  expect("Loss by grounding", C_BLACK, R"(
..o...
..o...
..ox..
..xo..
...o..
...o..
)",
R"(
......
......
...O..
..O...
......
......
)", 2);

  expect("Loss by grounding", C_WHITE, R"(
...x..
...x..
..ox..
..xo..
..x...
..x...
)",
R"(
......
......
..X...
...X..
......
......
)", -2);

  expect("Wins by grounding with an ungrounded dot", C_WHITE, R"(
......
.oox..
.xxo..
.oo...
....o.
......
)",
R"(
......
......
.OO...
......
....X.
......
)", 1, {XYMove(0, 2, P_WHITE)});
}

static std::pair<string, string> getCapturingAndBases(
  const string& boardData,
  const bool suicide,
  const bool captureEmptyBases,
  const vector<XYMove>& extraMoves
) {
  const Board board = parseDotsField(boardData, false, suicide, captureEmptyBases, Rules::DEFAULT_DOTS.dotsFreeCapturedDots, extraMoves);

  const Board& copy(board);

  vector<Player> captures;
  vector<Player> bases;
  copy.calculateOneMoveCaptureAndBasePositionsForDots(captures, bases);

  std::ostringstream capturesStringStream;
  std::ostringstream basesStringStream;

  for (int y = 0; y < copy.y_size; y++) {
    for (int x = 0; x < copy.x_size; x++) {
      const Loc loc = Location::getLoc(x, y, copy.x_size);
      const Color captureColor = captures[loc];
      if (captureColor == C_WALL) {
        capturesStringStream << PlayerIO::colorToChar(P_BLACK) << PlayerIO::colorToChar(P_WHITE);
      } else {
        capturesStringStream << PlayerIO::colorToChar(captureColor) << " ";
      }

      Color baseColor = bases[loc];
      if (baseColor == C_WALL) {
        basesStringStream << PlayerIO::colorToChar(P_BLACK) << PlayerIO::colorToChar(P_WHITE);
      } else {
        basesStringStream << PlayerIO::colorToChar(baseColor) << " ";
      }

      if (x < copy.x_size - 1) {
        capturesStringStream << " ";
        basesStringStream << " ";
      }
    }
    capturesStringStream << endl;
    basesStringStream << endl;
  }

  // Make sure we didn't change an internal state during calculating
  testAssert(board.isEqualForTesting(copy));

  return {capturesStringStream.str(), basesStringStream.str()};
}

static void checkCapturingAndBase(
  const string& title,
  const string& boardData,
  const string& expectedCaptures,
  const string& expectedBases,
  const bool suicide = Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
  const bool captureEmptyBases = Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
  const vector<XYMove>& extraMoves = {}
) {
  auto [capturing, bases] = getCapturingAndBases(boardData, suicide, captureEmptyBases, extraMoves);
  cout << ("  " + title + ": capturing").c_str() << endl;
  expect("", capturing, expectedCaptures);
  cout << ("  " + title + ": bases").c_str() << endl;
  expect("", bases, expectedBases);
}

void Tests::runDotsCapturingTests() {
  cout << "Running dots capturing tests" << endl;

  checkCapturingAndBase(
    "Two bases",
    R"(
.x...o.
xox.oxo
.......
)", R"(
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  X  .  .  .  O  .
)",
  R"(
.  .  .  .  .  .  .
.  X  .  .  .  O  .
.  .  .  .  .  .  .
)"
);

  checkCapturingAndBase(
    "Overlapping capturing location",
    R"(
.x.
xox
...
oxo
.o.
)", R"(
.  .  .
.  .  .
.  XO .
.  .  .
.  .  .
)",
  R"(
.  .  .
.  X  .
.  .  .
.  O  .
.  .  .
)"
);

  checkCapturingAndBase(
  "Empty base",
  R"(
.x.
x.x
.x.
)", R"(
.  .  .
.  .  .
.  .  .
)",
R"(
.  .  .
.  X  .
.  .  .
)"
);

  checkCapturingAndBase(
"Empty base can be broken",
R"(
.xx.
x..x
x.x.
oxo.
.o..
)", R"(
.  .  .  .
.  .  .  .
.  O  .  .
.  .  .  .
.  .  .  .
)",
R"(
.  .  .  .
.  X  X  .
.  X  .  .
.  O  .  .
.  .  .  .
)"
);

  checkCapturingAndBase(
"No empty base capturing",
R"(
.x.
x.x
...
)", R"(
.  .  .
.  .  .
.  .  .
)",
R"(
.  .  .
.  .  .
.  .  .
)", Rules::DEFAULT_DOTS.multiStoneSuicideLegal, false
);

  checkCapturingAndBase(
"Empty base capturing",
R"(
.x.
x.x
...
)", R"(
.  .  .
.  .  .
.  X  .
)",
R"(
.  .  .
.  X  .
.  .  .
)", Rules::DEFAULT_DOTS.multiStoneSuicideLegal, true
);

  checkCapturingAndBase(
    "Complex example with overlapping of capturing and bases",
    R"(
.ooxx.
o.xo.x
ox.ox.
ox.ox.
.o.x..
)", R"(
.  .  .  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
.  .  XO .  .  .
.  .  XO .  .  .
)",
  R"(
.  .  .  .  .  .
.  O  O  X  X  .
.  O  XO X  .  .
.  O  XO X  .  .
.  .  .  .  .  .
)"
);
}

static Board initializeBoard(const int startPos, const vector<XYMove>& extraMoves = {}) {
  auto board = Board(
    Board::DEFAULT_LEN_X_DOTS,
    Board::DEFAULT_LEN_Y_DOTS,
    Rules(
      startPos,
      false,
      Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
      Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
      Rules::DEFAULT_DOTS.dotsFreeCapturedDots
    )
  );
  board.setStartPos(DOTS_RANDOM);
  for (auto& extraMove : extraMoves) {
    board.playMoveAssumeLegal(Location::getLoc(extraMove.x, extraMove.y, board.x_size), extraMove.player);
  }
  return board;
}

void Tests::runDotsAcceptableKomiRange() {
  cout << "Running acceptable komi ranges tests" << endl;

  const Board& singleStartPosBoard = initializeBoard(Rules::START_POS_SINGLE);
  auto [lowerSingleDraw, upperSingleDraw] = singleStartPosBoard.getAcceptableKomiRange(true);
  testAssert(lowerSingleDraw == -1.0f);
  testAssert(upperSingleDraw == 0.0f);

  auto [lowerSingleNoDraw, upperSingleNoDraw] = singleStartPosBoard.getAcceptableKomiRange(false);
  testAssert(lowerSingleNoDraw == -0.5f);
  testAssert(upperSingleNoDraw == -0.5f);

  const Board& crossStartPosBoard = initializeBoard(Rules::START_POS_CROSS);
  auto [lowerCrossDraw, upperCrossDraw] = crossStartPosBoard.getAcceptableKomiRange(true);
  testAssert(lowerCrossDraw == -2.0f);
  testAssert(upperCrossDraw == +2.0f);

  auto [lowerCrossNoDraw, upperCrossNoDraw] = crossStartPosBoard.getAcceptableKomiRange(false);
  testAssert(lowerCrossNoDraw == -1.5f);
  testAssert(upperCrossNoDraw == 1.5f);

  const Board& cross4StartPosBoard = initializeBoard(Rules::START_POS_CROSS_4);
  auto [lowerCross4Draw, upperCross4Draw] = cross4StartPosBoard.getAcceptableKomiRange(true);
  testAssert(lowerCross4Draw == -8.0f);
  testAssert(upperCross4Draw == +8.0f);

  auto [lowerCross4NoDraw, upperCross4NoDraw] = cross4StartPosBoard.getAcceptableKomiRange(false);
  testAssert(lowerCross4NoDraw == -7.5f);
  testAssert(upperCross4NoDraw == 7.5f);

  const Board& crossStartPosWithExtraMovesBoard = initializeBoard(Rules::START_POS_CROSS, {XYMove(20, 15, P_BLACK)});
  auto [lowerCrossExtraMovesDraw, upperCrossExtraMovesDraw] = crossStartPosWithExtraMovesBoard.getAcceptableKomiRange(true);
  testAssert(lowerCrossExtraMovesDraw == -3.0f);
  testAssert(upperCrossExtraMovesDraw == +2.0f);

  auto [lowerCrossExtraBlackDraw, upperCrossExtraBlackDraw] = crossStartPosBoard.getAcceptableKomiRange(true, 1);
  testAssert(lowerCrossExtraBlackDraw == -3.0f);
  testAssert(upperCrossExtraBlackDraw == +2.0f);

  auto [lowerCrossExtraMovesNoDraw, upperCrossExtraMovesNoDraw] = crossStartPosWithExtraMovesBoard.getAcceptableKomiRange(false);
  testAssert(lowerCrossExtraMovesNoDraw == -2.5f);
  testAssert(upperCrossExtraMovesNoDraw == +1.5f);

  auto [lowerCrossExtraBlackNoDraw, upperCrossExtraBlackNoDraw] = crossStartPosBoard.getAcceptableKomiRange(false, 1);
  testAssert(lowerCrossExtraBlackNoDraw == -2.5f);
  testAssert(upperCrossExtraBlackNoDraw == +1.5f);
}