#include "../tests/tests.h"
#include "../tests/testdotsutils.h"

#include "../game/graphhash.h"
#include "../program/playutils.h"

using namespace std;
using namespace TestCommon;

static void checkDotsField(const string& description, const string& input,
  const std::function<void(BoardWithMoveRecords&)>& check,
  const bool suicide = Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
  const bool captureEmptyBases = Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
  const bool freeCapturedDots = Rules::DEFAULT_DOTS.dotsFreeCapturedDots) {
  cout << "  " << description << endl;

  auto moveRecords = vector<Board::MoveRecord>();

  Board initialBoard = parseDotsField(input, false, suicide, captureEmptyBases, freeCapturedDots, {});

  auto board = Board(initialBoard);

  auto boardWithMoveRecords = BoardWithMoveRecords(board, moveRecords);
  check(boardWithMoveRecords);

  while (!moveRecords.empty()) {
    board.undo(moveRecords.back());
    moveRecords.pop_back();
  }
  testAssert(initialBoard.isEqualForTesting(board));
}

void Tests::runDotsFieldTests() {
  cout << "Running dots basic tests: " << endl;

  checkDotsField("Simple capturing",
    R"(
. x .
x o x
. . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  boardWithMoveRecords.playMove(1, 2, P_BLACK);
  testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

  testAssert(Board::Base::Type::NORMAL == boardWithMoveRecords.moveRecords.back().bases.back().type);
});

  checkDotsField("Capturing with empty loc inside",
    R"(
. o o .
o x . .
. o o .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(boardWithMoveRecords.isLegal(2, 1, P_BLACK));
    testAssert(boardWithMoveRecords.isLegal(2, 1, P_WHITE));

    boardWithMoveRecords.playMove(3, 1, P_WHITE);
    testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);
    testAssert(!boardWithMoveRecords.isLegal(2, 1, P_BLACK));
    testAssert(!boardWithMoveRecords.isLegal(2, 1, P_WHITE));
});

  checkDotsField("Triple capture",
    R"(
. x . x .
x o . o x
. x o x .
. . x . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  boardWithMoveRecords.playMove(2, 1, P_BLACK);
  testAssert(3 == boardWithMoveRecords.board.numWhiteCaptures);
  testAssert(3 == boardWithMoveRecords.moveRecords.back().bases.size());
});

  checkDotsField("Base inside base inside base",
    R"(
. x x x x x x x .
x . . o o o . . x
x . o . x . o . x
x . o x o x o . x
x . o . . . o . x
x . . o . o . . x
. x x x . x x x .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  boardWithMoveRecords.playMove(4, 4, P_BLACK);
  testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
  testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);

  boardWithMoveRecords.playMove(4, 5, P_WHITE);
  testAssert(0 == boardWithMoveRecords.board.numWhiteCaptures);
  testAssert(4 == boardWithMoveRecords.board.numBlackCaptures);

  boardWithMoveRecords.playMove(4, 6, P_BLACK);
  testAssert(13 == boardWithMoveRecords.board.numWhiteCaptures);
  testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);
});

  /*checkDotsField("Base inside base inside base don't free captured dots",
  R"(
. x x x x x x x x x . .
x . . o o o o o o . x .
x . o . x x . . . o . x
x . o x o . x o . o . x
x . o . x . o . . o . x
x . . o . . . . o x . x
x . . . o . o o . . . x
. x x x x . x x x x x .
)", true, false, [](const BoardWithMoveRecords& boardWithMoveRecords) {
boardWithMoveRecords.playMove(5, 4, P_BLACK);
testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);

boardWithMoveRecords.playMove(5, 6, P_WHITE);
testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures); // Don't free the captured dot
testAssert(6 == boardWithMoveRecords.board.numBlackCaptures); // Ignore owned color dots

boardWithMoveRecords.playMove(5, 7, P_BLACK);
testAssert(21 == boardWithMoveRecords.board.numWhiteCaptures); // Don't count already counted dots
testAssert(6 == boardWithMoveRecords.board.numBlackCaptures);  // Don't free the captured dot
});*/

  checkDotsField("Empty bases and suicide",
    R"(
. x . . o .
x . x o . o
. x . . o .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    const auto& moveRecords = boardWithMoveRecords.moveRecords;

    // Suicide move is not capture
    testAssert(!boardWithMoveRecords.wouldBeCapture(1, 1, P_WHITE));
    testAssert(!boardWithMoveRecords.wouldBeCapture(1, 1, P_BLACK));
    testAssert(!boardWithMoveRecords.wouldBeCapture(4, 1, P_WHITE));
    testAssert(!boardWithMoveRecords.wouldBeCapture(4, 1, P_BLACK));

    testAssert(boardWithMoveRecords.isSuicide(1, 1, P_WHITE));
    testAssert(!boardWithMoveRecords.isSuicide(1, 1, P_BLACK));
    boardWithMoveRecords.playMove(1, 1, P_WHITE);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
    testAssert(Board::Base::Type::SUICIDAL == moveRecords.back().bases.back().type);

    testAssert(boardWithMoveRecords.isSuicide(4, 1, P_BLACK));
    testAssert(!boardWithMoveRecords.isSuicide(4, 1, P_WHITE));
    boardWithMoveRecords.playMove(4, 1, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);
    testAssert(Board::Base::Type::SUICIDAL == moveRecords.back().bases.back().type);
});

  checkDotsField("Empty base creation",
  R"(
. x . . o .
x . x o . o
. . . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    const auto& moveRecords = boardWithMoveRecords.moveRecords;
    boardWithMoveRecords.playMove(1, 2, P_BLACK);
    testAssert(Board::Base::Type::EMPTY == moveRecords.back().bases.back().type);
    boardWithMoveRecords.playMove(4, 2, P_WHITE);
    testAssert(Board::Base::Type::EMPTY == moveRecords.back().bases.back().type);
});

  checkDotsField("Empty bases when they are treated as normal",
  R"(
. x . . o .
x . x o . o
. . . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  boardWithMoveRecords.playMove(1, 2, P_BLACK);
  boardWithMoveRecords.playMove(4, 2, P_WHITE);

  // Suicide is not possible in this mode
  testAssert(!boardWithMoveRecords.isSuicide(1, 1, P_WHITE));
  testAssert(!boardWithMoveRecords.isSuicide(1, 1, P_BLACK));
  testAssert(!boardWithMoveRecords.isSuicide(4, 1, P_BLACK));
  testAssert(!boardWithMoveRecords.isSuicide(4, 1, P_WHITE));

  testAssert(0 == boardWithMoveRecords.board.numWhiteCaptures);
  testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);
}, Rules::DEFAULT_DOTS.multiStoneSuicideLegal, true, Rules::DEFAULT_DOTS.dotsFreeCapturedDots);

  checkDotsField("Capture wins suicide",
    R"(
. x o .
x o . o
. x o .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(!boardWithMoveRecords.isSuicide(2, 1, P_BLACK));
    boardWithMoveRecords.playMove(2, 1, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
    testAssert(Board::Base::Type::NORMAL == boardWithMoveRecords.moveRecords.back().bases.back().type);
});

  checkDotsField("Single dot doesn't break searching inside empty base",
    R"(
. o o o o .
o . . . . o
o . o . . o
o . . . . o
. o o o o .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(4, 2, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);
  });

  checkDotsField("Ignored already surrounded territory",
    R"(
. . x x x . . .
. x . . . x . .
x . . x . . x .
x . x . x . . x
x . . x . . x .
. x . . . x . .
. . x . x . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(3, 6, P_BLACK);

    boardWithMoveRecords.playMove(3, 3, P_WHITE);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

    boardWithMoveRecords.playMove(6, 3, P_WHITE);
    testAssert(2 == boardWithMoveRecords.board.numWhiteCaptures);
});

  checkDotsField("Invalidation of empty base locations",
    R"(
. o o x .
o . . o x
. o o x .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(2, 1, P_BLACK);
    boardWithMoveRecords.playMove(1, 1, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
  });

  checkDotsField("Invalidation of empty base locations ignoring borders",
    R"(
. . x x x . . . .
. x . . . x . . .
x . . x . . x o .
x . x . x . . x o
x . . x . . x o .
. x . . . x . . .
. . x x x . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(6, 3, P_WHITE);
    testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);

    boardWithMoveRecords.playMove(1, 3, P_WHITE);
    testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);

    boardWithMoveRecords.playMove(3, 3, P_WHITE);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
  });

  checkDotsField("Dangling dots removing",
    R"(
. x x . x x .
x . . x o . x
x . x . x . x
x . . x . . x
. x . . . x .
. . x . x . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
      boardWithMoveRecords.playMove(3, 5, P_BLACK);
      testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

      testAssert(!boardWithMoveRecords.isLegal(3, 2, P_BLACK));
      testAssert(!boardWithMoveRecords.isLegal(3, 2, P_WHITE));
    });

  checkDotsField("Recalculate square during dangling dots removing",
    R"(
. o o o . .
o . . . o .
o . o . . o
. . x o . o
o . o . . o
o . . . o .
. o o o . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
      boardWithMoveRecords.playMove(1, 3, P_WHITE);
      testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);

      boardWithMoveRecords.playMove(4, 3, P_BLACK);
      testAssert(2 == boardWithMoveRecords.board.numBlackCaptures);
    });

  checkDotsField("Base sorting by size",
    R"(
. . x x x . .
. x . . . x .
x . . x . . x
x . x o x . x
x . . . . . x
. x x . x x .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
      boardWithMoveRecords.playMove(3, 4, P_BLACK);
      testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

      boardWithMoveRecords.playMove(4, 1, P_WHITE);
      testAssert(2 == boardWithMoveRecords.board.numWhiteCaptures);
    });

  checkDotsField("Incorrect board state if outer empty surrounding encloses normal one with an empty location (https://github.com/KvanTTT/KataGoDots/issues/57)",
    R"(
. . x x x . .
. x . . . x .
x . . x . . x
x . x o x . x
x . x . x . x
x . . . . . x
. x . . . x .
. . x . x . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(3, 5, P_BLACK);
    boardWithMoveRecords.playMove(3, 7, P_BLACK);

    const State emptyLocState = boardWithMoveRecords.getState(3, 4);
    testAssert(isTerritory(emptyLocState));
    testAssert(C_EMPTY == getEmptyTerritoryColor(emptyLocState));
    testAssert(C_EMPTY == getPlacedDotColor(emptyLocState));
    testAssert(P_BLACK == getActiveColor(emptyLocState));

    boardWithMoveRecords.board.checkConsistency();
  });

  checkDotsField("Resignation",
    R"(
. . .
. . .
. . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playResignationMove(P_BLACK);
    testAssert(boardWithMoveRecords.board.is_finished);

    boardWithMoveRecords.undo();
    testAssert(!boardWithMoveRecords.board.is_finished);

    boardWithMoveRecords.playResignationMove(P_WHITE);
    testAssert(boardWithMoveRecords.board.is_finished);
});

  checkDotsField(
    "Drop redundant empty enclosure in case of suicidal surrounding",
  R"(
. x . x x .
x . x . . x
. x . x x .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(getEmptyTerritoryColor(boardWithMoveRecords.getState(3, 1)) == P_BLACK);
    testAssert(getEmptyTerritoryColor(boardWithMoveRecords.getState(4, 1)) == P_BLACK);

    boardWithMoveRecords.playMove(3, 1, P_WHITE);

    const auto& lastBases = boardWithMoveRecords.moveRecords.back().bases;
    testAssert(1 == lastBases.size());
    testAssert(Board::Base::Type::SUICIDAL == lastBases.back().type);
    testAssert(1 == boardWithMoveRecords.getBlackScore());

    testAssert(getActiveColor(boardWithMoveRecords.getState(2, 1)) == P_BLACK);
    testAssert(getActiveColor(boardWithMoveRecords.getState(3, 1)) == P_BLACK);
});
}

void Tests::runDotsGroundingTests() {
  cout << "Running dots grounding tests:" << endl;

    checkDotsField("Grounding propagation",
R"(
. x . .
o . o .
. x . .
. x o .
. . x .
. . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(2 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(3 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    // Dot adjacent to WALL is already grounded
    testAssert(isGrounded(boardWithMoveRecords.getState(1, 0)));

    // Ignore enemy's dots
    testAssert(isGrounded(boardWithMoveRecords.getState(0, 1)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 1)));

    // Not yet grounded
    testAssert(!isGrounded(boardWithMoveRecords.getState(1, 2)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(1, 3)));

    boardWithMoveRecords.playMove(1, 1, P_BLACK);

    testAssert(2 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    testAssert(isGrounded(boardWithMoveRecords.getState(1, 1)));

    // Check grounding propagation
    testAssert(isGrounded(boardWithMoveRecords.getState(1, 2)));
    testAssert(isGrounded(boardWithMoveRecords.getState(1, 3)));
    // Diagonal connection is not actual
    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 4)));

    // Ignore enemy's dots
    testAssert(isGrounded(boardWithMoveRecords.getState(0, 1)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 1)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 3)));
}
  );

  checkDotsField("Grounding propagation with empty base",
  R"(
. . x . .
. x . x .
. x . x .
. . x . .
. . . . .
)",
  [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(0 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(5 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    testAssert(!isGrounded(boardWithMoveRecords.getState(1, 2)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(3, 2)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 3)));

    boardWithMoveRecords.playMove(2, 2, P_WHITE);

    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(-1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    testAssert(isGrounded(boardWithMoveRecords.getState(2, 2)));

    testAssert(isGrounded(boardWithMoveRecords.getState(1, 2)));
    testAssert(isGrounded(boardWithMoveRecords.getState(3, 2)));
    testAssert(isGrounded(boardWithMoveRecords.getState(2, 3)));
  });

  checkDotsField("Grounding score with grounded base",
R"(
. x .
x o x
. . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(1, 2, P_BLACK);

    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(-1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
}
);

  checkDotsField("Grounding score with ungrounded base",
R"(
. . . . .
. . o . .
. o x o .
. . . . .
. . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(2, 3, P_WHITE);

    testAssert(4 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
}
);

  checkDotsField("Grounding score with grounded and ungrounded bases",
R"(
. x . . . . .
x o x . o . .
. . . o x o .
. . . . . . .
. . . . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(1, 2, P_BLACK);
    boardWithMoveRecords.playMove(4, 3, P_WHITE);

    testAssert(5 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(0 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
}
);

  checkDotsField("Grounding draw with ungrounded bases",
R"(
. . . . . . . . .
. . x . . . o . .
. x o x . o x o .
. . . . . . . . .
. . . . . . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(2, 3, P_BLACK);
    boardWithMoveRecords.playMove(6, 3, P_WHITE);

    testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
    testAssert(5 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(5 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
}
);


  checkDotsField("Grounding of real and empty adjacent bases",
R"(
. . x . .
. . x . .
. x o x .
. . . . .
. x . x .
. . x . .
. . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(5 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 2)));

    boardWithMoveRecords.playMove(2, 3, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(2 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    // Real base becomes grounded
    testAssert(isGrounded(boardWithMoveRecords.getState(2, 2)));
    testAssert(isGrounded(boardWithMoveRecords.getState(2, 3)));

    // Grounding does not affect an empty location
    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 4)));
    // Grounding does not affect empty surrounding
    testAssert(!isGrounded(boardWithMoveRecords.getState(3, 4)));
}
);

  checkDotsField("Grounding of real base when it touches grounded",
R"(
. . x . .
. . x . .
. . . . .
. x o x .
. . x . .
. . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(3 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 3)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(2, 4)));

    boardWithMoveRecords.playMove(2, 2, P_BLACK);

    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(-1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    testAssert(isGrounded(boardWithMoveRecords.getState(2, 3)));
    testAssert(isGrounded(boardWithMoveRecords.getState(2, 4)));
}
);

  checkDotsField("Base inside base inside base and grounding score",
R"(
. . . . . . .
. . o o o . .
. o . x . o .
. o x o x o .
. o . . . o .
. . o . o . .
. . . . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  testAssert(12 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
  testAssert(3 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

  boardWithMoveRecords.playMove(3, 4, P_BLACK);

  testAssert(12 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
  testAssert(4 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

  boardWithMoveRecords.playMove(3, 5, P_WHITE);

  testAssert(13 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
  testAssert(4 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

  boardWithMoveRecords.playMove(3, 6, P_WHITE);

  testAssert(-4 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
  testAssert(4 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
});

  const auto fieldInCaseOfDanglingLocsRemoving = R"(
. . . . . . . . .
. . x x x . . . .
. x . . . . x . .
. x . x x . . x .
. x . x . x . x .
. x . x x x . x .
. x . . x o . x .
. . x x x x x . .
)";

  checkDotsField("Ground empty territory in case of dangling locs removing (first)", fieldInCaseOfDanglingLocsRemoving, [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(!isGrounded(boardWithMoveRecords.getState(4, 4)));

    boardWithMoveRecords.playMove(5, 1, P_BLACK);
    boardWithMoveRecords.playGroundingMove(P_BLACK);

    testAssert(isGrounded(boardWithMoveRecords.getState(4, 4)));
  });

  checkDotsField("Ground empty territory in case of dangling locs removing (second)", invertColors(fieldInCaseOfDanglingLocsRemoving), [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(!isGrounded(boardWithMoveRecords.getState(4, 4)));

    boardWithMoveRecords.playMove(5, 1, P_WHITE);
    boardWithMoveRecords.playGroundingMove(P_WHITE);

    testAssert(isGrounded(boardWithMoveRecords.getState(4, 4)));
  });

  const auto fieldInCaseOfDanglingLocsAndDotsRemoving = R"(
. . . . . . . . . . .
. x x x x x x x . . .
. x . . . . . . . . .
. x . x x x x . . x .
. x . x . . . x . x .
. x . x . x . x . x .
. x . x . . . x . x .
. x . x x x x x . x .
. x . . x o . . . x .
. x x x x x x x x x .
)";

  checkDotsField("Ground empty territory with dot inside in case of dangling dots removing (first)",
    fieldInCaseOfDanglingLocsAndDotsRemoving, [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(0 == boardWithMoveRecords.getBlackScore());
    testAssert(1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
    testAssert(!isGrounded(boardWithMoveRecords.getState(5, 5)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(6, 5)));

    boardWithMoveRecords.playMove(8, 2, P_BLACK);

    testAssert(1 == boardWithMoveRecords.getBlackScore());
    testAssert(-1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
    testAssert(isGrounded(boardWithMoveRecords.getState(5, 5)));
    testAssert(isGrounded(boardWithMoveRecords.getState(6, 5)));
  });

  checkDotsField("Ground empty territory with dot inside in case of dangling dots removing (second)",
    invertColors(fieldInCaseOfDanglingLocsAndDotsRemoving), [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(0 == boardWithMoveRecords.getWhiteScore());
    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(!isGrounded(boardWithMoveRecords.getState(5, 5)));
    testAssert(!isGrounded(boardWithMoveRecords.getState(6, 5)));

    boardWithMoveRecords.playMove(8, 2, P_WHITE);

    testAssert(1 == boardWithMoveRecords.getWhiteScore());
    testAssert(-1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(isGrounded(boardWithMoveRecords.getState(5, 5)));
    testAssert(isGrounded(boardWithMoveRecords.getState(6, 5)));
  });

  checkDotsField("Simple",
  R"(
. . . . .
. x x o .
. . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playGroundingMove(P_BLACK);
    testAssert(boardWithMoveRecords.board.is_finished);

    const vector<Board::Base> groundingBases = boardWithMoveRecords.moveRecords.back().bases;
    testAssert(1 == groundingBases.size());
    testAssert(Board::Base::Type::UNGROUNDED == groundingBases.back().type);

    testAssert(2 == boardWithMoveRecords.board.numBlackCaptures);

    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(boardWithMoveRecords.getWhiteScore() == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    // Grounding after grounding is unreasonable because of ungrounded dots (although already captured)
    const auto history = BoardHistory(boardWithMoveRecords.board);
    testAssert(!history.isGroundReasonable(boardWithMoveRecords.board));

    boardWithMoveRecords.undo();
    testAssert(!boardWithMoveRecords.board.is_finished);

    boardWithMoveRecords.playGroundingMove(P_WHITE);
    testAssert(boardWithMoveRecords.board.is_finished);

    const vector<Board::Base> groundingBases2 = boardWithMoveRecords.moveRecords.back().bases;
    testAssert(1 == groundingBases2.size());
    testAssert(Board::Base::Type::UNGROUNDED == groundingBases2.back().type);

    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

    testAssert(2 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
    testAssert(boardWithMoveRecords.getBlackScore() == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);

    // Grounding after grounding is unreasonable because of ungrounded dots (although already captured)
    const auto history2 = BoardHistory(boardWithMoveRecords.board);
    testAssert(!history2.isGroundReasonable(boardWithMoveRecords.board));

    boardWithMoveRecords.undo();
  }
);

  checkDotsField("Draw",
R"(
. x . . .
. x x o .
. . . o .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playGroundingMove(P_BLACK);
    testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);
    testAssert(boardWithMoveRecords.getWhiteScore() == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
    // Grounding after grounding is reasonable because all dots are grounded
    const auto history = BoardHistory(boardWithMoveRecords.board);
    testAssert(history.isGroundReasonable(boardWithMoveRecords.board));
    boardWithMoveRecords.undo();

    boardWithMoveRecords.playGroundingMove(P_WHITE);
    testAssert(0 == boardWithMoveRecords.board.numWhiteCaptures);
    testAssert(boardWithMoveRecords.getBlackScore() == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    // Grounding after grounding is reasonable because all dots are grounded
    const auto history2 = BoardHistory(boardWithMoveRecords.board);
    testAssert(history2.isGroundReasonable(boardWithMoveRecords.board));
    boardWithMoveRecords.undo();
}
);

  checkDotsField("Bases",
R"(
. . . . . . . . .
. . x x . . . x .
. x o . x . x o x
. . x . . . . . .
. . . . . . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  boardWithMoveRecords.playMove(3, 3, P_BLACK);
  boardWithMoveRecords.playMove(7, 3, P_BLACK);
  testAssert(2 == boardWithMoveRecords.board.numWhiteCaptures);

  boardWithMoveRecords.playGroundingMove(P_BLACK);
  testAssert(6 == boardWithMoveRecords.board.numBlackCaptures);
  testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
  testAssert(boardWithMoveRecords.getWhiteScore() == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
}
);

  checkDotsField("Multiple groups",
R"(
. . . . . .
x x o . . o
. o x . . .
x . . . o o
. . . o . .
. . . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  boardWithMoveRecords.playGroundingMove(P_BLACK);
  testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);
  testAssert(0 == boardWithMoveRecords.board.numWhiteCaptures);
  testAssert(boardWithMoveRecords.getWhiteScore() == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
  boardWithMoveRecords.undo();

  boardWithMoveRecords.playGroundingMove(P_WHITE);
  testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);
  testAssert(3 == boardWithMoveRecords.board.numWhiteCaptures);
  testAssert(boardWithMoveRecords.getBlackScore() == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
  boardWithMoveRecords.undo();
}
);

  checkDotsField("Invalidate empty territory",
R"(
. . . . . .
. . o o . .
. o . . o .
. . o o . .
. . . . . .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    const Board board = boardWithMoveRecords.board;

    State state = boardWithMoveRecords.board.getState(Location::getLoc(2, 2, board.x_size));
    testAssert(C_WHITE == getEmptyTerritoryColor(state));

    state = boardWithMoveRecords.board.getState(Location::getLoc(3, 2, board.x_size));
    testAssert(C_WHITE == getEmptyTerritoryColor(state));

    boardWithMoveRecords.playGroundingMove(P_WHITE);
    testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);
    testAssert(6 == boardWithMoveRecords.board.numWhiteCaptures);
    testAssert(boardWithMoveRecords.getBlackScore() == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);

    state = boardWithMoveRecords.board.getState(Location::getLoc(2, 2, board.x_size));
    testAssert(C_EMPTY == getEmptyTerritoryColor(state));

    state = boardWithMoveRecords.board.getState(Location::getLoc(3, 2, board.x_size));
    testAssert(C_EMPTY == getEmptyTerritoryColor(state));
}
);

  checkDotsField("Don't invalidate empty territory for strong connection",
R"(
. x .
x . x
. x .
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    const Board board = boardWithMoveRecords.board;

    boardWithMoveRecords.playGroundingMove(P_BLACK);
    testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);
    testAssert(0 == boardWithMoveRecords.board.numWhiteCaptures);
    testAssert(boardWithMoveRecords.getWhiteScore() == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    State state = boardWithMoveRecords.board.getState(Location::getLoc(1, 1, board.x_size));
    testAssert(C_BLACK == getEmptyTerritoryColor(state));

    state = boardWithMoveRecords.board.getState(Location::getLoc(0, 0, board.x_size));
    testAssert(C_EMPTY == getEmptyTerritoryColor(state));
}
);
}

void Tests::runDotsBoardHistoryGroundingTests() {
  {
    const Board board = parseDotsFieldDefault(R"(
. . . .
. x o .
. o x .
. . . .
)");
    auto boardHistory = BoardHistory(board);

    // No draw because there are some ungrounded dots
    testAssert(!boardHistory.isGroundReasonable(board));
    testAssert(!boardHistory.isResignReasonable(board, P_BLACK));
    testAssert(!boardHistory.isResignReasonable(board, P_WHITE));

    boardHistory.rules.komi = -0.5f;
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));

    // No draw because there are some ungrounded dots even considering komi that makes draw for white
    boardHistory.rules.komi = 2.0f;
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));

    boardHistory.rules.komi = -2.0f;
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));

    boardHistory.rules.komi = 2.5f;
    testAssert(0.5f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));

    boardHistory.rules.komi = -2.5f;
    testAssert(-0.5f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
  }

  {
    const Board board = parseDotsFieldDefault(R"(
. x o .
. x o .
. o x .
. o x .
)");
    auto boardHistory = BoardHistory(board);

    // Effective draw because all dots are grounded
    testAssert(boardHistory.isGroundReasonable(board));
    testAssert(!boardHistory.isResignReasonable(board, P_BLACK));
    testAssert(!boardHistory.isResignReasonable(board, P_WHITE));

    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(0.0f == boardHistory.whiteScoreIfAllDotsAreGrounded(board));

    boardHistory.rules.komi = 0.5f;
    testAssert(0.5f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(0.5f == boardHistory.whiteScoreIfAllDotsAreGrounded(board));

    boardHistory.rules.komi = -0.5f;
    testAssert(-0.5f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(-0.5f == boardHistory.whiteScoreIfAllDotsAreGrounded(board));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
. x . . . .
x o x . . .
. . . . o .
. . . o x o
. . . . . .
)",
      {XYMove(1, 2, P_BLACK), XYMove(4, 4, P_WHITE)});
    const auto boardHistory = BoardHistory(board);

    // Also effective draw because all bases are grounded
    testAssert(boardHistory.isGroundReasonable(board));

    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(0.0f == boardHistory.whiteScoreIfAllDotsAreGrounded(board));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
. x . . . .
x o x . x .
. . . . . .
. . . . o .
. o . o x o
. . . . . .
)",
      {XYMove(1, 2, P_BLACK), XYMove(4, 5, P_WHITE)});
    const auto boardHistory = BoardHistory(board);

    // No effective draw because there are ungrounded dots
    testAssert(!boardHistory.isGroundReasonable(board));

    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
  }

  {
    Board board = parseDotsFieldDefault(
      R"(
. . . . .
. . o . .
. o x o .
. . . . .
)",
      {XYMove(2, 3, P_WHITE)});
    testAssert(1 == board.numBlackCaptures);
    const auto boardHistory = BoardHistory(board);

    testAssert(boardHistory.isGroundReasonable(board));
    testAssert(boardHistory.isResignReasonable(board, C_BLACK));
    testAssert(!boardHistory.isResignReasonable(board, C_WHITE));

    testAssert(1.0f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(1.0f == boardHistory.whiteScoreIfAllDotsAreGrounded(board));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
. . . . .
. . x . .
. x o x .
. . . . .
)",
      {XYMove(2, 3, P_BLACK)});
    testAssert(1 == board.numWhiteCaptures);
    auto boardHistory = BoardHistory(board);

    testAssert(boardHistory.isGroundReasonable(board));
    testAssert(boardHistory.isResignReasonable(board, C_WHITE));
    testAssert(!boardHistory.isResignReasonable(board, C_BLACK));

    boardHistory.rules.komi = +1.0f;
    // Draw by grounding because the komi compensates score and there are no ungrounded dots
    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(0.0f == boardHistory.whiteScoreIfAllDotsAreGrounded(board));

    boardHistory.rules.komi = +0.5f;
    testAssert(-0.5f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(-0.5f == boardHistory.whiteScoreIfAllDotsAreGrounded(board));

    boardHistory.rules.komi = -0.5f;
    testAssert(-1.5f == boardHistory.whiteScoreIfGroundingAlive(board));
    testAssert(-1.5f == boardHistory.whiteScoreIfAllDotsAreGrounded(board));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
. . . . .
. . x . .
. x o x .
. . . . .
. . . . .
)",
      {XYMove(2, 3, P_BLACK)});
    testAssert(1 == board.numWhiteCaptures);
    const auto boardHistory = BoardHistory(board);
    testAssert(!boardHistory.isGroundReasonable(board));
    testAssert(!boardHistory.isResignReasonable(board, C_WHITE));
    testAssert(!boardHistory.isResignReasonable(board, C_BLACK));
  }

  {
    const Board board = parseDotsFieldDefault(R"(
. . .
. o .
. . .
)");
    const auto boardHistory = BoardHistory(board);
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
  }

  {
    const Board board = parseDotsFieldDefault(R"(
. . .
. x .
. . .
)");
    const auto boardHistory = BoardHistory(board);
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
. x . . . .
x o x . . .
. . . . x .
. . . . . .
)",
      {XYMove(1, 2, P_BLACK)});
    const auto boardHistory = BoardHistory(board);

    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));
  }

  {
    Board board = parseDotsFieldDefault(
      R"(
. x . . . .
x o x . . .
x o x . x .
. . . . . .
. . . . . .
)",
      {XYMove(1, 3, P_BLACK)});
    auto boardHistory = BoardHistory(board);

    testAssert(-1.0f == boardHistory.whiteScoreIfGroundingAlive(board));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    // Ungrounded own dot -> black can't ground without score losing
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));

    boardHistory.makeBoardMoveAssumeLegal(board, Board::PASS_LOC, P_BLACK, nullptr);
    testAssert(-1.0f == boardHistory.finalWhiteMinusBlackScore);
    // Grounding after grounding is reasonable because it anyway wins the game
    testAssert(boardHistory.isGroundReasonable(board));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
. x . . . .
x o x . . .
x o x . o .
. . . . . .
. . . . . .
)",
      {XYMove(1, 3, P_BLACK)});
    const auto boardHistory = BoardHistory(board);

    testAssert(-2.0f == boardHistory.whiteScoreIfGroundingAlive(board));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    // Ungrounded opponent dot -> Black can ground without score losing
    testAssert(-2.0f == boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK));
  }

    {
      const Board board = parseDotsFieldDefault(
        R"(
. o . . . .
o x o . . .
o x o . x .
. . . . . .
. . . . . .
)",
        {XYMove(1, 3, P_WHITE)});
      const auto boardHistory = BoardHistory(board);

      testAssert(+2.0f == boardHistory.whiteScoreIfGroundingAlive(board));

      testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
      testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));
      // Ungrounded opponent dot -> White can ground without score losing
      testAssert(2.0f == boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE));
    }

  {
    const Board board = parseDotsFieldDefault(
      R"(
. o . . . .
o x o . . .
. . . . o .
. . . . . .
)",
      {XYMove(1, 2, P_WHITE)});
    const auto boardHistory = BoardHistory(board);

    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));
  }

  {
    Board board = parseDotsFieldDefault(
      R"(
. o . . . .
o x o . . .
o x o . o .
. . . . . .
. . . . . .
)",
      {XYMove(1, 3, P_WHITE)});
    auto boardHistory = BoardHistory(board);

    testAssert(1.0f == boardHistory.whiteScoreIfGroundingAlive(board));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    // Ungrounded own dot -> White can't ground without score losing
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));

    boardHistory.makeBoardMoveAssumeLegal(board, Board::PASS_LOC, P_WHITE, nullptr);
    testAssert(1.0f == boardHistory.finalWhiteMinusBlackScore);
    // Grounding after grounding is reasonable because it anyway wins the game
    testAssert(boardHistory.isGroundReasonable(board));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
. o . . . .
o x o . . .
o x o . x .
. . . . . .
. . . . . .
)",
      {XYMove(1, 3, P_WHITE)});
    const auto boardHistory = BoardHistory(board);

    testAssert(2.0f == boardHistory.whiteScoreIfGroundingAlive(board));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    // Ungrounded own dot -> White can't ground without score losing
    testAssert(2.0f == boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));
  }

  {
    const Board board = parseDotsField(
      R"(
x o
x o
)",
      Rules::DEFAULT_DOTS.startPosIsRandom,
      true,
      Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
      Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
      {});
    auto boardHistory = BoardHistory(board);
    testAssert(boardHistory.endGameIfReasonable(board, false, P_BLACK));
    testAssert(C_EMPTY == boardHistory.winner);
    testAssert(0.0f == boardHistory.finalWhiteMinusBlackScore);
  }

  {
    const Board board = parseDotsField(
      R"(
x o
x o
)",
      Rules::DEFAULT_DOTS.startPosIsRandom,
      true,
      Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
      Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
      {});
    auto boardHistory = BoardHistory(board);
    testAssert(boardHistory.endGameIfReasonable(board, false, P_WHITE));
    testAssert(C_EMPTY == boardHistory.winner);
    testAssert(0.0f == boardHistory.finalWhiteMinusBlackScore);
  }

  {
    const Board board = parseDotsField(
      R"(
o o o
o x o
o . o
)",
      Rules::DEFAULT_DOTS.startPosIsRandom,
      true,
      Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
      Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
      {XYMove(1, 2, P_WHITE)});
    auto boardHistory = BoardHistory(board);
    testAssert(boardHistory.endGameIfReasonable(board, false, P_BLACK));
    testAssert(P_WHITE == boardHistory.winner);
    testAssert(1.0f == boardHistory.finalWhiteMinusBlackScore);
  }

  {
    const Board board = parseDotsField(
      R"(
x x x x x
x . x o x
x x x . x
)",
      Rules::DEFAULT_DOTS.startPosIsRandom,
      true,
      Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
      Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
      {XYMove(3, 2, P_BLACK)});
    auto boardHistory = BoardHistory(board);

    testAssert(!boardHistory.endGameIfReasonable(board, false, P_BLACK));
    testAssert(boardHistory.endGameIfReasonable(board, false, P_WHITE)); // sui is never beneficial -> game is finished for WHITE
  }

  {
    const Board board = parseDotsField(
      R"(
x x x x x
x . x o x
x x x . x
)",
      Rules::DEFAULT_DOTS.startPosIsRandom,
      false,
      Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
      Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
      {XYMove(3, 2, P_BLACK)});
    auto boardHistory = BoardHistory(board);


    testAssert(!boardHistory.endGameIfReasonable(board, false, P_BLACK));
    testAssert(boardHistory.endGameIfReasonable(board, false, P_WHITE)); // sui is never beneficial -> game is finished for WHITE
    testAssert(P_BLACK == boardHistory.winner);
    testAssert(-1.0f == boardHistory.finalWhiteMinusBlackScore);
  }

  {
    const Board board = parseDotsField(
      R"(
x x x x x
x . . . x
x . x . x
x . . . x
x x x x x
)",
      Rules::DEFAULT_DOTS.startPosIsRandom,
      false,
      Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
      Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
      {});
    auto boardHistory = BoardHistory(board);

    // The field is not grounding alive; however, the game should be finished because there are no legal moves for WHITE
    testAssert(!boardHistory.isGroundReasonable(board));

    testAssert(!boardHistory.getReasonableMoves(board, P_BLACK).empty());
    // White can and should only ground
    const vector<Loc> whiteReasonableMoves = boardHistory.getReasonableMoves(board, P_WHITE);
    testAssert(1 == whiteReasonableMoves.size());
    testAssert(Board::PASS_LOC == whiteReasonableMoves[0]);

    testAssert(boardHistory.endGameIfReasonable(board, false, P_WHITE));
    testAssert(C_EMPTY == boardHistory.winner);
    testAssert(0.0f == boardHistory.finalWhiteMinusBlackScore);
  }

  {
    Board board = parseDotsFieldDefault(
      R"(
. . x . . .
. x . x . .
. o x o . .
. o . o . .
)"
      );
    auto boardHistory = BoardHistory(board);

    const auto* capturesAndTerritoriesInfosBeforeAtari = board.calculateCapturesAndTerritoriesColorsForDots();
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board, C_EMPTY, nullptr)));
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board, C_WALL, nullptr)));

    // Draw by grounding if consider empty capturing locs
    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_EMPTY, capturesAndTerritoriesInfosBeforeAtari));
    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_WALL, capturesAndTerritoriesInfosBeforeAtari));
    delete capturesAndTerritoriesInfosBeforeAtari;

    testAssert(boardHistory.endGameIfReasonable(board, true, P_BLACK));
    testAssert(boardHistory.endGameIfReasonable(board, true, P_WHITE));

    board.playMoveAssumeLegal(Location::getLoc(2, 3, board.x_size), P_WHITE);
    auto boardHistoryAfterAtari = BoardHistory(board);

    const auto* capturesAndTerritoriesInfosAfterAtari = board.calculateCapturesAndTerritoriesColorsForDots();
    // The position is unsettled because of atari
    testAssert(std::isnan(boardHistoryAfterAtari.whiteScoreIfGroundingAlive(board, C_EMPTY, capturesAndTerritoriesInfosAfterAtari)));
    testAssert(std::isnan(boardHistoryAfterAtari.whiteScoreIfGroundingAlive(board, C_WALL, capturesAndTerritoriesInfosAfterAtari)));
    delete capturesAndTerritoriesInfosAfterAtari;

    testAssert(!boardHistoryAfterAtari.endGameIfReasonable(board, true, P_BLACK));
    testAssert(!boardHistoryAfterAtari.endGameIfReasonable(board, true, P_WHITE));
  }

  {
    // Central dot formally is ungrounded but there is no way to surround it. The game can be finished.
    const Board board = parseDotsFieldDefault(
      R"(
. . x x x . .
. x . . . x .
. x . x . x .
. x . . . x .
. . x x x . .
. . . . . . .
)"
      );
    auto boardHistory = BoardHistory(board);

    const auto* capturesAndTerritoriesInfos = board.calculateCapturesAndTerritoriesColorsForDots();
    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_EMPTY, capturesAndTerritoriesInfos));
    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_BLACK, capturesAndTerritoriesInfos));
    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_WHITE, capturesAndTerritoriesInfos));
    testAssert(0.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_WALL, capturesAndTerritoriesInfos));
    delete capturesAndTerritoriesInfos;

    testAssert(boardHistory.endGameIfReasonable(board, true, P_BLACK));
    testAssert(boardHistory.endGameIfReasonable(board, true, P_WHITE));
  }

  {
    // Check empty-base wave propagation via existing base
    const Board board = parseDotsFieldDefault(
      R"(
. . o o o . .
. o . . . o .
. . o o o . .
. o x x . . .
. . o o o . .
. o . . . o .
. . o o o . .
. . . . . . .
)", {XYMove(5, 3, P_WHITE)});

    const auto boardHistory = BoardHistory(board);
    const auto* capturesAndTerritoriesInfos = board.calculateCapturesAndTerritoriesColorsForDots();
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board, C_WALL, nullptr)));
    testAssert(2.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_WALL, capturesAndTerritoriesInfos));
    delete capturesAndTerritoriesInfos;
  }

  {
    // Check complicated case that contains empty bases of both colors
    Board board = parseDotsFieldDefault(
      R"(
. . o . . . . x . .
. o . o . . x . x .
. . o . . . . x . .
. o x . . . x o . .
. . o . x . . x . .
. o . o x . x . x .
. . o x x . . x . .
)", {XYMove(3, 3, P_WHITE), XYMove(8, 3, P_BLACK)});
    const auto boardHistoryBeforeAtari = BoardHistory(board);
    const auto* infosBeforeAtari = board.calculateCapturesAndTerritoriesColorsForDots();
    testAssert(isnan(boardHistoryBeforeAtari.whiteScoreIfGroundingAlive(board, C_WALL, nullptr)));
    testAssert(0.0f == boardHistoryBeforeAtari.whiteScoreIfGroundingAlive(board, C_WALL, infosBeforeAtari));
    delete infosBeforeAtari;

    board.playMoveAssumeLegal(Location::getLoc(3, 4, board.x_size), P_BLACK);

    const auto boardHistoryAfterAtari = BoardHistory(board);
    const auto* infosAfterAtari = board.calculateCapturesAndTerritoriesColorsForDots();
    testAssert(isnan(boardHistoryAfterAtari.whiteScoreIfGroundingAlive(board, C_WALL, infosAfterAtari)));
    testAssert(isnan(boardHistoryAfterAtari.whiteScoreIfGroundingAlive(board, C_EMPTY, infosAfterAtari)));
    delete infosAfterAtari;
  }

  {
    // Check base-in-base that's empty-base grounded. The empty base contains another inner base
    Board board = parseDotsFieldDefault(
      R"(
. . . . x x x . . . .
. . . x . . . x . . .
. . x . . x . . x . o
. . x . x . x . . x o
. . x . . x . . x o o
. . . x . . . x . . .
. . . . x x x . . . .
. . . . x . x . . . .
. . . x . o . x . . .
. . . x o . o . . . .
. . . x . o . x . . .
. . . . x x x . . . .
. . . . . . . . . . .
)", {XYMove(5, 9, P_BLACK), XYMove(7, 9, P_BLACK)});

    const auto boardHistory = BoardHistory(board);
    testAssert(4 == board.numWhiteCaptures);
    testAssert(0 == board.numBlackCaptures);

    const auto* capturesAndTerritoriesInfosBeforeAtari = board.calculateCapturesAndTerritoriesColorsForDots();
    testAssert(-4.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_EMPTY, capturesAndTerritoriesInfosBeforeAtari));
    testAssert(-4.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_WALL, capturesAndTerritoriesInfosBeforeAtari));
    delete capturesAndTerritoriesInfosBeforeAtari;

    board.playMoveAssumeLegal(Location::getLoc(9, 2, board.x_size), P_WHITE);

    const auto* capturesAndTerritoriesInfosAfterAtari = board.calculateCapturesAndTerritoriesColorsForDots();
    testAssert(isnan(boardHistory.whiteScoreIfGroundingAlive(board, C_EMPTY, capturesAndTerritoriesInfosAfterAtari)));
    testAssert(isnan(boardHistory.whiteScoreIfGroundingAlive(board, C_WALL, capturesAndTerritoriesInfosAfterAtari)));
    delete capturesAndTerritoriesInfosAfterAtari;
  }

  {
    // Check combination of empty base, ungrounded dot and regular grounded unrelated base
    const Board board = parseDotsFieldDefault(
      R"(
. . x . . . . x .
. x . x . . x o x
. . x . . . x o .
. . . . x . . x .
. . . . . . . . .
)", {XYMove(8, 2, P_BLACK)});

    const auto boardHistory = BoardHistory(board);

    const auto* capturesAndTerritoriesInfos = board.calculateCapturesAndTerritoriesColorsForDots();
    testAssert(isnan(boardHistory.whiteScoreIfGroundingAlive(board, C_WALL, capturesAndTerritoriesInfos)));
    testAssert(-1.0f == boardHistory.whiteScoreIfGroundingAlive(board, C_EMPTY, capturesAndTerritoriesInfos));
    testAssert(isnan(boardHistory.whiteScoreIfGroundingAlive(board, C_BLACK, capturesAndTerritoriesInfos)));
    testAssert(isnan(boardHistory.whiteScoreIfGroundingAlive(board, C_WHITE, capturesAndTerritoriesInfos)));
    delete capturesAndTerritoriesInfos;
  }
}

// We need to check both playMoveRecorded and playMoveAssumeLegal because they have different implementations
// playMoveAssumeLegal is faster but doesn't return move records.
// The method makes the specified moves and compares resulting field hashes.
// If field2Hash is null, then it's expected that field hashes should be equal.
static void checkHashAfterMovesAndRollback(
  const string& description,
  const string& field1Str,
  const string& field2Str,
  const vector<XYMove>& field1Moves,
  const vector<XYMove>& field2Moves,
  const string& field1Hash,
  const std::optional<string>& field2Hash = std::nullopt,
  const bool captureEmptyBase1 = false,
  const bool captureEmptyBase2 = false) {
  cout << "  " << description << endl;

  const Hash128 expectedField1Hash = Hash128::ofString(field1Hash);
  const Hash128 expectedField2Hash = field2Hash.has_value() ? Hash128::ofString(field2Hash.value()) : expectedField1Hash;

  Board field1 = parseDotsField(
    field1Str,
    Rules::DEFAULT_DOTS.startPosIsRandom,
    Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
    captureEmptyBase1,
    Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
    {});
  Board field2 = parseDotsField(
    field2Str,
    Rules::DEFAULT_DOTS.startPosIsRandom,
    Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
    captureEmptyBase2,
    Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
    {});

  const auto origField1 = field1;
  const auto origField2 = field2;

  vector<Board::MoveRecord> field1MovesRecords;
  vector<Board::MoveRecord> field2MovesRecords;

  field1MovesRecords.reserve(field1Moves.size());
  for (const auto move : field1Moves) {
    field1MovesRecords.push_back(field1.playMoveRecorded(Location::getLoc(move.x, move.y, field1.x_size), move.player));
  }

  field2MovesRecords.reserve(field2Moves.size());
  for (const auto move : field2Moves) {
    field2MovesRecords.push_back(field2.playMoveRecorded(Location::getLoc(move.x, move.y, field2.x_size), move.player));
  }

  testAssert(expectedField1Hash == field1.pos_hash);
  testAssert(expectedField2Hash == field2.pos_hash);

  for (auto it = field1MovesRecords.rbegin(); it != field1MovesRecords.rend(); ++it) {
    field1.undo(*it);
  }

  for (auto it = field2MovesRecords.rbegin(); it != field2MovesRecords.rend(); ++it) {
    field2.undo(*it);
  }

  testAssert(origField1.isEqualForTesting(field1));
  testAssert(origField2.isEqualForTesting(field2));

  for (const auto move : field1Moves) {
    field1.playMoveAssumeLegal(Location::getLoc(move.x, move.y, field1.x_size), move.player);
  }

  for (const auto move : field2Moves) {
    field2.playMoveAssumeLegal(Location::getLoc(move.x, move.y, field2.x_size), move.player);
  }

  testAssert(expectedField1Hash == field1.pos_hash);
  testAssert(expectedField2Hash == field2.pos_hash);
}

void Tests::runDotsPosHashTests() {
  cout << "Running dots pos hashes tests:" << endl;

  checkHashAfterMovesAndRollback(
     "Simple",
     R"(
. . .
. x .
. . .
)",
     R"(
. . .
. o .
. . .
)",
     {},
     {},
     "BCD32A5AC99A98B88EEACB7AF641CDC5",
     "249080D7F57ADF9A0F6FFEBB5B474316"
);

  checkHashAfterMovesAndRollback(
   "Different moves order doesn't affect hash",
   R"(
. . .
. . .
. . .
)",
   R"(
. . .
. . .
. . .
)",
   {
     XYMove(0, 1, P_WHITE),
     XYMove(1, 0, P_WHITE),
     XYMove(1, 1, P_WHITE),
     XYMove(2, 1, P_WHITE),
     XYMove(1, 2, P_WHITE)
   },
   {
     XYMove(1, 2, P_WHITE),
     XYMove(0, 1, P_WHITE),
     XYMove(1, 0, P_WHITE),
     XYMove(1, 1, P_WHITE),
     XYMove(2, 1, P_WHITE),
   },
   "FA1193BCDF50E70C7943322333AE621B"
);

  checkHashAfterMovesAndRollback(
       "Capturing order doesn't affect hash",
       R"(
. x .
x . x
. x .
)",
       R"(
. x .
x o x
. . .
)",
       { XYMove(1, 1, P_WHITE)},
       { XYMove(1, 2, P_BLACK) },
       "7DE7F3E444707E264660086265EB5042"
);

  checkHashAfterMovesAndRollback(
     "Field with different sizes have different hashes",
     R"(
. . .
. x .
. . .
)",
     R"(
. . . .
. x . .
. . . .
. . . .
)",
     {},
     {},
     "BCD32A5AC99A98B88EEACB7AF641CDC5",
     "53B4F243232F66C20A18ED5604260A86"
);

  checkHashAfterMovesAndRollback(
"Same shape and same captures but different captures locations",
R"(
. x x .
x o . .
. x x .
)",
R"(
. x x .
x . o .
. x x .
)",
    { XYMove(3, 1, P_BLACK) },
    { XYMove(3, 1, P_BLACK) },
    "44CC8AA64E2DC895C1FBBC7F739AEA0B"
);

  checkHashAfterMovesAndRollback(
    "Field captures affects hash (https://github.com/KvanTTT/KataGoDots/issues/45)",
    R"(
. x x x .
. o . . x
. x x x .
)",
    R"(
. x x x .
. o o x x
. x x x .
)",
    { XYMove(0, 1, P_BLACK) },
    { XYMove(0, 1, P_BLACK) },
    "07C9110FFEF3C5B173E4739A22E7F13C",
    "93A23E3C97060AD03DB91BEB9EDA18B4"
    );

  checkHashAfterMovesAndRollback(
  "Equal captures diff affects hash (https://github.com/KvanTTT/KataGoDots/issues/45)",
  R"(
. x x . . o o .
x o . . . . x o
. x x . . o o .
)",
  R"(
. x x . . o o .
x o o . . x x o
. x x . . o o .
)",
    { XYMove(3, 1, P_BLACK), XYMove(4, 1, P_WHITE) },
    { XYMove(3, 1, P_BLACK), XYMove(4, 1, P_WHITE) },
    "7CE643C7E11D8F07B165AFEDCFC2CA2E",
    "4633E5CD39C049EB385E20486331BBFF"
  );

  const string& fieldForSameShapeButDifferentCaptures = R"(
. x x .
x o . .
. x x .
)";
  checkHashAfterMovesAndRollback(
    "Different hashes when same shape but different captures",
    fieldForSameShapeButDifferentCaptures,
    fieldForSameShapeButDifferentCaptures,
    { XYMove(3, 1, P_BLACK) },
    { XYMove(2, 1, P_BLACK), XYMove(3, 1, P_BLACK) },
    "44CC8AA64E2DC895C1FBBC7F739AEA0B",
    "F6495D08CA5A956DF587BBDFF819B36F"
);

  const string& fieldForSameShapeButDifferentCapturesWithFree = R"(
. . o o o o . .
. o x x x x o .
o x . o . . . .
. o x x x x o .
. . o o o o . .
)";
  checkHashAfterMovesAndRollback(
    "Different hashes when for same shape but different captures with free",
    fieldForSameShapeButDifferentCapturesWithFree,
    fieldForSameShapeButDifferentCapturesWithFree,
    { XYMove(6, 2, P_BLACK), XYMove(7, 2, P_WHITE) },
    { XYMove(4, 2, P_WHITE), XYMove(6, 2, P_BLACK), XYMove(7, 2, P_WHITE) },
    "2210EB52EDD61D2F1D24012F92FD65B4",
    "0C875121F18DA2B91AABD939EED49169"
);

  const auto field1WhenSurroundLocsDontAffectHash = R"(
. . x x x x x x . .
. x . . . . . . x .
x . . x . . o . . x
x . x o x o x o . x
x . . . . . . . . x
. x . . . . . . x .
. . x x x . x x . .
)";
  const auto field2WhenSurroundLocsDontAffectHash = R"(
. . x x x x x x . .
. x . . . . . . x .
x . . o . . x . . x
x . o x o x o x . x
x . . . . . . . . x
. x . . . . . . x .
. . x x x . x x . .
)";

  checkHashAfterMovesAndRollback(
    "Surrounded locations (first) doesn't affect hash (it's erased)",
    field1WhenSurroundLocsDontAffectHash,
    field2WhenSurroundLocsDontAffectHash,
    { XYMove(3, 4, P_BLACK), XYMove(6, 4, P_WHITE), XYMove(5, 6, P_BLACK) },
    { XYMove(3, 4, P_WHITE), XYMove(6, 4, P_BLACK), XYMove(5, 6, P_BLACK) },
    "9FC349BC827BEBB46E86DEC810E52D43"
  );

  checkHashAfterMovesAndRollback(
    "Surrounded locations (second) doesn't affect hash (it's erased)",
    invertColors(field1WhenSurroundLocsDontAffectHash),
    invertColors(field2WhenSurroundLocsDontAffectHash),
    { XYMove(3, 4, P_WHITE), XYMove(6, 4, P_BLACK), XYMove(5, 6, P_WHITE) },
    { XYMove(3, 4, P_BLACK), XYMove(6, 4, P_WHITE), XYMove(5, 6, P_WHITE) },
    "3A44E523C250CF9A70B2BB5BD1F92224"
  );

  const string fieldWithAllGroundedDots = R"(
. x o .
. x o .
. o x .
. o x .
)";
  checkHashAfterMovesAndRollback(
    "Grounding with all grounded dots doesn't affect hash",
    fieldWithAllGroundedDots,
    fieldWithAllGroundedDots,
    { XYMove::getGroundMove(P_BLACK) },
    { },
    "7F8BB94476F72F4D52B1C69131D4E100"
  );

  const string fieldWithSomeUngroundedDots = R"(
. . . .
. x o .
. o x .
. . . .
)";
  checkHashAfterMovesAndRollback(
    "Grounding with some ungrounded dots affects hash",
    fieldWithSomeUngroundedDots,
    fieldWithSomeUngroundedDots,
    { XYMove::getGroundMove(P_BLACK) },
    { },
    "91F96C3887C93EBA1DC9078BEAD21118",
    "0C4C2C369093972F94F8319761667E55"
  );

  const string emptyBaseField = R"(
. o .
o . o
. . .
)";
  checkHashAfterMovesAndRollback(
    "Different hash for empty base when it's enabled and not",
    emptyBaseField,
    emptyBaseField,
    { XYMove(1, 2, P_WHITE) },
    { XYMove(1, 2, P_WHITE) },
    "0F0BE495C26D2C126D81E72B872858E1",
    "AF8E361A3CEB8C1C0DB4BA26EAE5A6FF",
    true
  );

  checkHashAfterMovesAndRollback(
  "Different hash for empty base and non-empty base",
  emptyBaseField,
  R"(
. o .
o x o
. . .
)",
    { XYMove(1, 2, P_WHITE) },
    { XYMove(1, 2, P_WHITE) },
     "0F0BE495C26D2C126D81E72B872858E1",
     "E4689E4202FFA7A9E552C0464776E95A",
     true,
     true
  );

  checkHashAfterMovesAndRollback(
  "Expected false negative (limitation of current hashing approach)",
  R"(
. x . .
x x o x
. x x .
)",
  R"(
. . x .
x o x x
. x x .
)",
    { XYMove(2, 0, P_BLACK) },
    { XYMove(1, 0, P_BLACK) },
    "AC99B8C50626A57899A093FD91BF30A2",
    "F6495D08CA5A956DF587BBDFF819B36F"
  );
}