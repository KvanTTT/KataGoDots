#include "../tests/tests.h"
#include "testdotsutils.h"

#include "../game/graphhash.h"
#include "../program/playutils.h"

using namespace std;
using namespace TestCommon;

static void checkSymmetry(const Board& initBoard, const string& expectedSymmetryBoardInput, const vector<XYMove>& extraMoves, const int symmetry) {
  const string symmetryString = SymmetryHelpers::symmetryToString(symmetry);
  cout << "  Check field consistency for symmetry " + symmetryString << endl;

  const Board transformedBoard = SymmetryHelpers::getSymBoard(initBoard, symmetry);
  Board expectedBoard = parseDotsFieldDefault(expectedSymmetryBoardInput);
  for (const XYMove& extraMove : extraMoves) {
    expectedBoard.playMoveAssumeLegal(SymmetryHelpers::getSymLoc(extraMove.x, extraMove.y, initBoard, symmetry), extraMove.player);
  }
  expect(symmetryString.c_str(), Board::toStringSimple(transformedBoard), Board::toStringSimple(expectedBoard));
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

  cout << "  Check dots symmetry with start pos" << endl;
  const auto originalRules = Rules(Rules::DEFAULT_DOTS.startPos, false, Rules::DEFAULT_DOTS.multiStoneSuicideLegal, Rules::DEFAULT_DOTS.dotsCaptureEmptyBases, Rules::DEFAULT_DOTS.dotsFreeCapturedDots);
  auto board = Board(5, 4, originalRules);
  const Player pla = board.setStartPos(DOTS_RANDOM);
  board.playMoveAssumeLegal(Location::getLoc(1, 2, board.x_size), pla);
  board.playMoveAssumeLegal(Location::getLoc(2, 3, board.x_size), pla);
  testAssert(1 == board.numWhiteCaptures);

  const auto rotatedBoard = SymmetryHelpers::getSymBoard(board, SymmetryHelpers::SYMMETRY_TRANSPOSE_FLIP_X);
  testAssert(1 == rotatedBoard.numWhiteCaptures);

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
  expectedBoard.playMoveAssumeLegal(Location::getLoc(0, 2, expectedBoard.x_size), P_BLACK);
  testAssert(1 == expectedBoard.numWhiteCaptures);

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
  cout << "  " << name << ", Grounding Player: " << PlayerIO::colorToChar(groundingPlayer) << endl;
  expect(name, getOwnership(actualField, groundingPlayer, expectedWhiteScore, extraMoves), expectedOwnership);
}

void Tests::runDotsOwnershipTests() {
  cout << "Running dots ownership tests" << endl;
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

enum class NextMoveType {
  BlackReasonable,
  WhiteReasonable,
  OneMoveCapture,
  OneMoveTerritory,
  OneMoveEmptyCapture,
  OneMoveEmptyTerritory,
  ZeroMoveEmptyTerritory
};

static std::map<NextMoveType, std::ostringstream> getNextMoveTypes(const Board& board) {
  std::map<NextMoveType, std::ostringstream> nextMoveTypeStreams;

  nextMoveTypeStreams.emplace(NextMoveType::BlackReasonable, std::ostringstream());
  nextMoveTypeStreams.emplace(NextMoveType::WhiteReasonable, std::ostringstream());
  nextMoveTypeStreams.emplace(NextMoveType::OneMoveCapture, std::ostringstream());
  nextMoveTypeStreams.emplace(NextMoveType::OneMoveTerritory, std::ostringstream());
  nextMoveTypeStreams.emplace(NextMoveType::OneMoveEmptyCapture, std::ostringstream());
  nextMoveTypeStreams.emplace(NextMoveType::OneMoveEmptyTerritory, std::ostringstream());
  nextMoveTypeStreams.emplace(NextMoveType::ZeroMoveEmptyTerritory, std::ostringstream());

  const auto* capturesAndTerritoriesInfos = board.calculateCapturesAndTerritoriesColorsForDots();

  const BoardHistory history(board);
  const vector<Loc> reasonableBlackLocs = history.getReasonableMoves(board, P_BLACK, Board::PASS_LOC, false, capturesAndTerritoriesInfos);
  const vector<Loc> reasonableWhiteLocs = history.getReasonableMoves(board, P_WHITE, Board::PASS_LOC, false, capturesAndTerritoriesInfos);
  int currentReasonableBlackLocIndex = 0;
  int currentReasonableWhiteLocIndex = 0;

  for (int y = 0; y < board.y_size; y++) {
    for (int x = 0; x < board.x_size; x++) {
      const Loc loc = Location::getLoc(x, y, board.x_size);
      const auto* captureAndTerritoryInfos = capturesAndTerritoriesInfos->at(loc);

      auto appendColor = [&](const NextMoveType type, std::ostringstream& stream) {
        Color colorOrPlayer = C_EMPTY;
        switch (type) {
          case NextMoveType::BlackReasonable:
            if (currentReasonableBlackLocIndex < reasonableBlackLocs.size() && loc == reasonableBlackLocs[currentReasonableBlackLocIndex]) {
              colorOrPlayer = P_BLACK;
              currentReasonableBlackLocIndex++;
            }
            break;
          case NextMoveType::WhiteReasonable:
            if (currentReasonableWhiteLocIndex < reasonableWhiteLocs.size() && loc == reasonableWhiteLocs[currentReasonableWhiteLocIndex]) {
              colorOrPlayer = P_WHITE;
              currentReasonableWhiteLocIndex++;
            }
            break;
          case NextMoveType::OneMoveCapture:
            if (captureAndTerritoryInfos == nullptr) break;
            colorOrPlayer = captureAndTerritoryInfos->getOneMoveCaptureColor();
            break;
          case NextMoveType::OneMoveTerritory:
            if (captureAndTerritoryInfos == nullptr) break;
            colorOrPlayer = captureAndTerritoryInfos->getOneMoveTerritoryColor();
            break;
          case NextMoveType::OneMoveEmptyCapture:
            if (captureAndTerritoryInfos == nullptr) break;
            colorOrPlayer = captureAndTerritoryInfos->getOneMoveEmptyCaptureColor();
            break;
          case NextMoveType::OneMoveEmptyTerritory:
            if (captureAndTerritoryInfos == nullptr) break;
            colorOrPlayer = captureAndTerritoryInfos->getOneMoveEmptyTerritoryPlayer();
            break;
          case NextMoveType::ZeroMoveEmptyTerritory:
            if (captureAndTerritoryInfos == nullptr) break;
            colorOrPlayer = captureAndTerritoryInfos->getZeroMoveEmptyTerritoryPlayer();
            break;
        }

        if (colorOrPlayer == C_WALL) {
          stream << PlayerIO::colorToChar(P_BLACK) << PlayerIO::colorToChar(P_WHITE);
        } else {
          stream << PlayerIO::colorToChar(colorOrPlayer) << " ";
        }
      };

      for (auto& [type, stream] : nextMoveTypeStreams) {
        appendColor(type, stream);
      }

      if (x < board.x_size - 1) {
        for (auto& [type, stream] : nextMoveTypeStreams) {
          if (type != NextMoveType::BlackReasonable && type != NextMoveType::WhiteReasonable) {
            stream << " ";
          }
        }
      }
    }
    for (auto& [type, stream] : nextMoveTypeStreams) {
      stream << endl;
    }
  }

  delete capturesAndTerritoriesInfos;

  testAssert(currentReasonableBlackLocIndex == reasonableBlackLocs.size());
  testAssert(currentReasonableWhiteLocIndex == reasonableWhiteLocs.size());

  return nextMoveTypeStreams;
}

static void checkNextMoveInfos(
  const string& title,
  const string& boardData,
  const std::optional<string>& expectedBlackReasonableMoves,
  const std::optional<string>& expectedWhiteReasonableMoves,
  const std::optional<string>& expectedOneMoveCaptures,
  const std::optional<string>& expectedOneMoveTerritory,
  const std::optional<string>& expectedOneMoveEmptyCaptures = std::nullopt,
  const std::optional<string>& expectedOneMoveEmptyTerritory = std::nullopt,
  const std::optional<string>& expectedZeroMoveEmptyTerritory = std::nullopt,
  const bool suicide = Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
  const bool captureEmptyBases = Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
  const vector<XYMove>& extraMoves = {}
) {
  const Board board = parseDotsField(boardData, false, suicide, captureEmptyBases, Rules::DEFAULT_DOTS.dotsFreeCapturedDots, extraMoves);
  auto nextMoveTypes = getNextMoveTypes(board);

  std::ostringstream emptyFieldForReasonableStream;
  std::ostringstream emptyFieldStream;
  for (int y = 0; y < board.y_size; y++) {
    for (int x = 0; x < board.x_size; x++) {
      emptyFieldStream << ". ";
      emptyFieldForReasonableStream << ".";
      if (x < board.x_size - 1) {
        emptyFieldStream << " ";
        emptyFieldForReasonableStream << " ";
      }
    }
    emptyFieldStream << endl;
    emptyFieldForReasonableStream << endl;
  }
  string emptyFieldString = emptyFieldStream.str();
  string emptyFieldForReasonableString = emptyFieldForReasonableStream.str();

  auto checkTextRepresentation = [&title, &emptyFieldString, &emptyFieldForReasonableString, &nextMoveTypes](const string& name, const NextMoveType nextMoveType, const std::optional<string>& expected) {
    cout << "  " + title + ": " << name << endl;
    const string actualString = nextMoveTypes[nextMoveType].str();
    const string expectedString = expected.has_value()
      ? expected.value()
      : nextMoveType == NextMoveType::BlackReasonable || nextMoveType == NextMoveType::WhiteReasonable
        ? emptyFieldForReasonableString
        : emptyFieldString;
    expect("", actualString, expectedString);
  };

  checkTextRepresentation("black reasonable", NextMoveType::BlackReasonable, expectedBlackReasonableMoves);
  checkTextRepresentation("white reasonable", NextMoveType::WhiteReasonable, expectedWhiteReasonableMoves);
  checkTextRepresentation("one move captures", NextMoveType::OneMoveCapture, expectedOneMoveCaptures);
  checkTextRepresentation("one move territory", NextMoveType::OneMoveTerritory, expectedOneMoveTerritory);
  checkTextRepresentation("one move empty captures", NextMoveType::OneMoveEmptyCapture, expectedOneMoveEmptyCaptures);
  checkTextRepresentation("one move empty territory", NextMoveType::OneMoveEmptyTerritory, expectedOneMoveEmptyTerritory);
  checkTextRepresentation("zero move empty territory", NextMoveType::ZeroMoveEmptyTerritory, expectedZeroMoveEmptyTerritory);
}

void Tests::runDotsCapturesAndTerritoriesTests() {
  cout << "Running dots captures and territories tests" << endl;

  checkNextMoveInfos(
    "Two bases",
    R"(
.x...o.
xox.oxo
xox.oxo
.......
)",
    R"(
. . X X X . .
. . . X . . .
. . . X . . .
. X X X X X .
)",
    R"(
. . O O O . .
. . . O . . .
. . . O . . .
. O O O O O .
)",
    R"(
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  X  .  .  .  O  .
)",
    R"(
.  .  .  .  .  .  .
.  X  .  .  .  O  .
.  X  .  .  .  O  .
.  .  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Overlapping captures location",
    R"(
.x.
xox
...
oxo
.o.
)",
    R"(
. . .
. . .
X X X
. . .
. . .
)",
    R"(
. . .
. . .
O O O
. . .
. . .
)",
    R"(
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

  checkNextMoveInfos(
    "Empty territories",
    R"(
.x..o.
x.xo.o
x.xo.o
.x..o.
)",
    R"(
. . X X . .
. X . . . .
. X . . . .
. . X X . .
)",
    R"(
. . O O . .
. . . . O .
. . . . O .
. . O O . .
)",
    nullopt,
    nullopt,
    nullopt,
    nullopt,
    R"(
.  .  .  .  .  .
.  X  .  .  O  .
.  X  .  .  O  .
.  .  .  .  .  .
)");

  checkNextMoveInfos(
    "One move empty territory",
    R"(
.x..o.
x.xo.o
......
)",
    R"(
. . X X . .
. X . . . .
. X X X X .
)",
    R"(
. . O O . .
. . . . O .
. O O O O .
)",
    nullopt,
    nullopt,
    R"(
.  .  .  .  .  .
.  .  .  .  .  .
.  X  .  .  O  .
)",
    R"(
.  .  .  .  .  .
.  X  .  .  O  .
.  .  .  .  .  .
)",
    nullopt,
    Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
    false
);

  checkNextMoveInfos(
    "Empty territory can be broken",
    R"(
.xx..oo.
x..xo..o
x.x..o.o
oxo..xox
.o....x.
)",
    R"(
. . . X X . . .
. X X . . . . .
. X . X X . X .
. . . X X . . .
. . X X X X . .
)",
    R"(
. . . O O . . .
. . . . . O O .
. O . O O . O .
. . . O O . . .
. . O O O O . .
)",
    R"(
.  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .
.  O  .  .  .  .  X  .
.  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .
.  O  .  .  .  .  X  .
.  .  .  .  .  .  .  .
)",
    nullopt,
    nullopt,
    R"(
.  .  .  .  .  .  .  .
.  X  X  .  .  O  O  .
.  X  .  .  .  .  O  .
.  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Trivial overlapping of captures with empty territory",
    R"(
.xo.
x..o
.xo.
)",
    R"(
. . . .
. X . .
. . . .
)",
    R"(
. . . .
. . O .
. . . .
)",
    nullopt,
    nullopt,
    R"(
.  .  .  .
.  O  X  .
.  .  .  .
)",
    R"(
.  .  .  .
.  X  O  .
.  .  .  .
)"
);

  checkNextMoveInfos(
    "Overlapping of captures with empty territory",
    R"(
.xxoo.
x....o
.xxoo.
)",
    R"(
. . . . . .
. X X . . .
. . . . . .
)",
    R"(
. . . . . .
. . . O O .
. . . . . .
)",
    nullopt,
    nullopt,
    R"(
.  .  .  .  .  .
.  .  O  X  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  X  X  O  O  .
.  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Overlapping one move normal capture with empty loc",
    R"(
.xxoo.
xo..xo
.xxoo.
)",
    R"(
. . . . . .
. . . X . .
. . . . . .
)",
    R"(
. . . . . .
. . O . . .
. . . . . .
)",
    R"(
.  .  .  .  .  .
.  .  O  X  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  X  X  O  O  .
.  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Overlapping with one move normal and empty capture",
    R"(
.xxoo.
x...xo
.xxoo.
)",
    R"(
. . . . . .
. X X . . .
. . . . . .
)",
    R"(
. . . . . .
. . O . . .
. . . . . .
)",
    R"(
.  .  .  .  .  .
.  .  O  .  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  .  .  O  O  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  .  .  X  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  X  X  .  .  .
.  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Overlapping with one move normal and empty capture (reversed)",
    R"(
.xxoo.
xo...o
.xxoo.
)",
    R"(
. . . . . .
. . . X . .
. . . . . .
)",
    R"(
. . . . . .
. . . O O .
. . . . . .
)",
    R"(
.  .  .  .  .  .
.  .  .  X  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  X  X  .  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  .  O  .  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  .  .  O  O  .
.  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Outer empty and inner normal captures",
    R"(
.ooxx.
...o.x
.ooxx.
)",
    R"(
. . . . . .
X . X . . .
. . . . . .
)",
    R"(
. . . . . .
O O O . . .
. . . . . .
)",
    R"(
.  .  .  .  .  .
.  .  X  .  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  .  .  X  X  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
O  .  .  .  .  .
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  O  O  .  .  .
.  .  .  .  .  .
)"
    );

  checkNextMoveInfos(
    "Outer normal and inner normal captures",
    R"(
.ooxx.
..xo..
.ooxx.
)",
    R"(
. . . . . .
X . . . . X
. . . . . .
)",
    R"(
. . . . . .
O . . . . O
. . . . . .
)",
    R"(
.  .  .  .  .  .
O  .  .  .  .  X
.  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .
.  O  O  X  X  .
.  .  .  .  .  .
)"
  );

  checkNextMoveInfos(
    "Normal base supersedes suicidal without capturing location",
    R"(
.ooo.
o.xxo
ox..x
o.xxo
.ooo.
)",
    R"(
. . . . .
. . . . .
. . . X .
. . . . .
. . . . .
)",
    R"(
. . . . .
. . . . .
. . . O .
. . . . .
. . . . .
)",
    R"(
.  .  .  .  .
.  .  .  .  .
.  .  .  O  .
.  .  .  .  .
.  .  .  .  .
)",
    R"(
.  .  .  .  .
.  O  O  O  .
.  O  O  .  .
.  O  O  O  .
.  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Unrelated bases of same color",
    R"(
.o...o.
oxo.oxo
.......
)",
    R"(
. . X X X . .
. . . X . . .
. X X X X X .
)",
    R"(
. . O O O . .
. . . O . . .
. O O O O O .
)",
    R"(
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  O  .  .  .  O  .
)",
    R"(
.  .  .  .  .  .  .
.  O  .  .  .  O  .
.  .  .  .  .  .  .
)"
  );

  checkNextMoveInfos(
    "Big territory supersedes multiple small ones",
    R"(
..X..
.XOX.
XO.OX
.X.X.
.....
)",
    R"(
. X . X .
X . . . X
. . . . .
X . . . X
. X X X .
)",
    R"(
. O . O .
O . . . O
. . . . .
O . . . O
. O O O .
)",
    R"(
.  .  .  .  .
.  .  .  .  .
.  .  .  .  .
.  .  .  .  .
.  .  X  .  .
)",
    R"(
.  .  .  .  .
.  .  X  .  .
.  X  X  X  .
.  .  X  .  .
.  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Big empty territory supersedes multiple small ones",
    R"(
..O.
.O.O
O..O
.O..
)",
    R"(
. X . .
X . . .
. . . .
. . X .
)",
    R"(
. O . .
O . O .
. O O .
. . O .
)",
    nullopt,
    nullopt,
    R"(
.  .  .  .
.  .  .  .
.  .  .  .
.  .  O  .
)",
    R"(
.  .  .  .
.  .  O  .
.  O  O  .
.  .  .  .
)"
);

  checkNextMoveInfos(
    "Mutual capturing location",
    R"(
.xo.
xoxo
xo..
.x..
)",
    R"(
. . . .
. . . .
. . X X
. . X .
)",
    R"(
. . . .
. . . .
. . O O
. . O .
)",
    R"(
.  .  .  .
.  .  .  .
.  .  XO .
.  .  .  .
)",
    R"(
.  .  .  .
.  X  O  .
.  X  .  .
.  .  .  .
)"
);

  checkNextMoveInfos(
    "4 captures locs (3 of the pla, 1 of the opp)",
    R"(
.oxx.o.
oxoxo.o
ox...xo
oxoxo.o
.ox..o.
..ooo..
)",
    R"(
. . . . X . .
. . . . . . .
. . . . X . .
. . . . . . .
X . . . . . X
. X . . . X .
)",
    R"(
. . . . O . .
. . . . . . .
. . . O . . .
. . . . . . .
O . . . . . O
. O . . . O .
)",
    R"(
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  O  X  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .
.  O  X  .  .  O  .
.  O  XO X  O  O  .
.  O  X  O  .  O  .
.  .  O  O  O  .  .
.  .  .  .  .  .  .
)"
  );
  checkNextMoveInfos(
    "No any captures and territory after grounding",
    R"(
.x..
x.O.
x.x.
x..x
.xx.
)",
    nullopt,
    nullopt,
    nullopt,
    nullopt,
    nullopt,
    nullopt,
    nullopt,
    Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
    Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
    {XYMove::getGroundMove(P_WHITE)}
);

  checkNextMoveInfos(
    "Overlapping of captures and territories",
    R"(
.ooxx.
o.xo.x
ox.ox.
ox.ox.
.o.x..
)",
    R"(
. . . . . .
. . . . . .
. . . . . X
. . . . . X
. . X . X .
)",
    R"(
. . . . . .
. . . . . .
. . . . . O
. . . . . O
. . O . O .
)",
    R"(
.  .  .  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
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

  checkNextMoveInfos(
    "Drop internal territory",
    R"(
..xxx..
.x...x.
x..x..x
x.xox.x
x.....x
.x...x.
..x.x..
.......
)",
    R"(
. X . . . X .
X . . . . . X
. . . . . . .
. . . . . . .
. . . . . . .
X . . . . . X
X X . . . X X
. X X X X X .
)",
    R"(
. O . . . O .
O . . . . . O
. . . . . . .
. . . . . . .
. . . . . . .
O . . . . . O
O O . . . O O
. O O O O O .
)",
    R"(
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  X  .  .  .
)",
    R"(
.  .  .  .  .  .  .
.  .  X  X  X  .  .
.  X  X  X  X  X  .
.  X  X  X  X  X  .
.  X  X  X  X  X  .
.  .  X  X  X  .  .
.  .  .  X  .  .  .
.  .  .  .  .  .  .
)"
    );

  checkNextMoveInfos(
    "Drop internal empty territory",
    R"(
..xxx..
.x...x.
x..x..x
x.x.x.x
x..x..x
.x...x.
..x.x..
.......
)",
    R"(
. X . . . X .
X . X X X . X
. X X . X X .
. X . X . X .
. X X . X X .
X . X X X . X
X X . X . X X
. X X X X X .
)",
    R"(
. O . . . O .
O . . . . . O
. . . . . . .
. . . . . . .
. . . . . . .
O . . . . . O
O O . . . O O
. O O O O O .
)",
    nullopt,
    nullopt,
    R"(
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  X  .  .  .
)",
    R"(
.  .  .  .  .  .  .
.  .  X  X  X  .  .
.  X  X  .  X  X  .
.  X  .  X  .  X  .
.  X  X  .  X  X  .
.  .  X  X  X  .  .
.  .  .  X  .  .  .
.  .  .  .  .  .  .
)"
  );

  checkNextMoveInfos(
    "Drop internal empty territory 2",
    R"(
..o.o..
.o...o.
o..o..o
o.o.o.o
o..o..o
.o.x.o.
..ooo..
)",
    R"(
. X . X . X .
X . . . . . X
. . . . . . .
. . . . . . .
. . . . . . .
X . . . . . X
. X . . . X .
)",
    R"(
. O . O . O .
O . . . . . O
. . . . . . .
. . . . . . .
. . . . . . .
O . . . . . O
. O . . . O .
)",
    R"(
.  .  .  O  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .
.  .  O  O  O  .  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  .  O  O  O  .  .
.  .  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Drop dangling territory",
    R"(
.......
.oo.oo.
o.....o
o.oxo.o
o..o..o
o.....o
.ooooo.
)",
    R"(
. X X X X X .
X . . . . . X
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
)",
    R"(
. O O O O O .
O . . . . . O
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
)",
    R"(
.  .  .  O  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .
.  .  .  O  .  .  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  .  .  .  .  .  .
)"
  );

  checkNextMoveInfos(
    "Drop internal opp captures and territory",
    R"(
..xxx..
.x...x.
x..o..x
x.oxo.x
x.....x
.x...x.
..x.x..
...o...
)",
    R"(
. X . . . X .
X . . . . . X
. . . . . . .
. . . . . . .
. . . . . . .
X . . . . . X
X X . X . X X
. X X . X X .
)",
    R"(
. O . . . O .
O . . . . . O
. . . . . . .
. . . . . . .
. . . . . . .
O . . . . . O
O O . O . O O
. O O . O O .
)",
    R"(
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  X  .  .  .
.  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .
.  .  X  X  X  .  .
.  X  X  X  X  X  .
.  X  X  X  X  X  .
.  X  X  X  X  X  .
.  .  X  X  X  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Drop internal opp captures and territory 2",
    R"(
.......
.oo.oo.
o.....o
o.xox.o
o..x..o
o.....o
.ooooo.
)",
    R"(
. X X X X X .
X . . . . . X
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
)",
    R"(
. O O O O O .
O . . . . . O
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
)",
    R"(
.  .  .  O  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .
.  .  .  O  .  .  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  .  .  .  .  .  .
)"
);

  checkNextMoveInfos(
    "Drop inner empty territory if outer captures",
    R"(
.oo.oo.
o.....o
o..x..o
o.x.x.o
o..x..o
o.....o
.ooooo.
)",
    R"(
. . . X . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
)",
    R"(
. . . O . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
. . . . . . .
)",
    R"(
.  .  .  O  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  O  O  O  O  O  .
.  .  .  .  .  .  .
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

void Tests::runDotsKomiRandomization() {
  cout << "Running Dots komi randomization tests" << endl;

  auto check = [](const int startPos, const float mean, const float stdev, const bool allowInteger) {
    const Board board = initializeBoard(startPos);
    BoardHistory boardHistory(board);
    auto [lowerBound, upperBound] = board.getAcceptableKomiRange(true, 0);

    float min = std::numeric_limits<float>::infinity();
    float max = -std::numeric_limits<float>::infinity();
    set<float> values;
    for (int i = 0; i < 256; i++) {
      ExtraBlackAndKomi extraBlackAndKomi;
      extraBlackAndKomi.komiMean = mean;
      extraBlackAndKomi.komiStdev = stdev;
      extraBlackAndKomi.allowInteger = allowInteger;

      PlayUtils::setKomiWithNoise(extraBlackAndKomi, boardHistory, DOTS_RANDOM);
      const float newKomi = boardHistory.rules.komi;

      if (newKomi < min) min = newKomi;
      if (newKomi > max) max = newKomi;

      values.insert(newKomi);

      testAssert(newKomi >= lowerBound && newKomi <= upperBound);
      if (!allowInteger) {
        testAssert(Global::isEqual(std::abs(std::fmod(newKomi, 1.0f)), 0.5f));
      }
    }

    cout << "  Pos: " << board.rules.writeStartPosRule(startPos) << "; mean: " << mean << "; stdev: " << stdev << ", allowInteger: " << boolalpha << allowInteger << ", values: ";
    for (auto it = values.begin(); it != values.end(); ++it) {
      cout << *it;
      if (it != std::prev(values.end())) {
        cout << ", ";
      }
    }
    cout << endl;
  };

  // SINGLE

  // Normal range
  check(Rules::START_POS_SINGLE, -0.25f, 0.25f, false);
  check(Rules::START_POS_SINGLE, -0.25f, 0.25f, true);

  // Zero range
  check(Rules::START_POS_SINGLE, 0.0f, 0.0f, false);
  check(Rules::START_POS_SINGLE, 0.0f, 0.0f, true);

  // Out-of-range
  check(Rules::START_POS_SINGLE, 2.0f, 1.0f, false);
  check(Rules::START_POS_SINGLE, 2.0f, 1.0f, true);

  // CROSS

  // Normal range
  check(Rules::START_POS_CROSS, 0.0f, 2.0f, false);
  check(Rules::START_POS_CROSS, 0.0f, 2.0f, true);

  // Zero range
  check(Rules::START_POS_CROSS, 0.0f, 0.0f, false);
  check(Rules::START_POS_CROSS, 0.0f, 0.0f, true);

  // Out-of-range
  check(Rules::START_POS_CROSS, 4.0f, 1.0f, false);
  check(Rules::START_POS_CROSS, 4.0f, 1.0f, true);

  // CROSS_4

  // Normal range
  check(Rules::START_POS_CROSS_4, 0.0f, 8.0f, false);
  check(Rules::START_POS_CROSS_4, 0.0f, 8.0f, true);

  // Zero range
  check(Rules::START_POS_CROSS_4, 0.0f, 0.0f, false);
  check(Rules::START_POS_CROSS_4, 0.0f, 0.0f, true);

  // Out-of-range
  check(Rules::START_POS_CROSS_4, 16.0f, 2.0f, false);
  check(Rules::START_POS_CROSS_4, 16.0f, 2.0f, true);
}