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
.x.
xox
...
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  boardWithMoveRecords.playMove(1, 2, P_BLACK);
  testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
});

  checkDotsField("Capturing with empty loc inside",
    R"(
.oo.
ox..
.oo.
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
.x.x.
xo.ox
.xox.
..x..
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
  boardWithMoveRecords.playMove(2, 1, P_BLACK);
  testAssert(3 == boardWithMoveRecords.board.numWhiteCaptures);
});

  checkDotsField("Base inside base inside base",
    R"(
.xxxxxxx.
x..ooo..x
x.o.x.o.x
x.oxoxo.x
x.o...o.x
x..o.o..x
.xxx.xxx.
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
.xxxxxxxxx..
x..oooooo.x.
x.o.xx...o.x
x.oxo.xo.o.x
x.o.x.o..o.x
x..o....ox.x
x...o.oo...x
.xxxx.xxxxx.
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
.x..o.
x.xo.o
.x..o.
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    // Suicide move is not capture
    testAssert(!boardWithMoveRecords.wouldBeCapture(1, 1, P_WHITE));
    testAssert(!boardWithMoveRecords.wouldBeCapture(1, 1, P_BLACK));
    testAssert(!boardWithMoveRecords.wouldBeCapture(4, 1, P_WHITE));
    testAssert(!boardWithMoveRecords.wouldBeCapture(4, 1, P_BLACK));

    testAssert(boardWithMoveRecords.isSuicide(1, 1, P_WHITE));
    testAssert(!boardWithMoveRecords.isSuicide(1, 1, P_BLACK));
    boardWithMoveRecords.playMove(1, 1, P_WHITE);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

    testAssert(boardWithMoveRecords.isSuicide(4, 1, P_BLACK));
    testAssert(!boardWithMoveRecords.isSuicide(4, 1, P_WHITE));
    boardWithMoveRecords.playMove(4, 1, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);
});

  checkDotsField("Empty bases when they are allowed",
  R"(
.x..o.
x.xo.o
......
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
.xo.
xo.o
.xo.
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    testAssert(!boardWithMoveRecords.isSuicide(2, 1, P_BLACK));
    boardWithMoveRecords.playMove(2, 1, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
});

  checkDotsField("Single dot doesn't break searching inside empty base",
    R"(
.oooo.
o....o
o.o..o
o....o
.oooo.
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(4, 2, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);
  });

  checkDotsField("Ignored already surrounded territory",
    R"(
..xxx...
.x...x..
x..x..x.
x.x.x..x
x..x..x.
.x...x..
..x.x...
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(3, 6, P_BLACK);

    boardWithMoveRecords.playMove(3, 3, P_WHITE);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

    boardWithMoveRecords.playMove(6, 3, P_WHITE);
    testAssert(2 == boardWithMoveRecords.board.numWhiteCaptures);
});

  checkDotsField("Invalidation of empty base locations",
    R"(
.oox.
o..ox
.oox.
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(2, 1, P_BLACK);
    boardWithMoveRecords.playMove(1, 1, P_BLACK);
    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);
  });

  checkDotsField("Invalidation of empty base locations ignoring borders",
    R"(
..xxx....
.x...x...
x..x..xo.
x.x.x..xo
x..x..xo.
.x...x...
..xxx....
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
.xx.xx.
x..xo.x
x.x.x.x
x..x..x
.x...x.
..x.x..
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
      boardWithMoveRecords.playMove(3, 5, P_BLACK);
      testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

      testAssert(!boardWithMoveRecords.isLegal(3, 2, P_BLACK));
      testAssert(!boardWithMoveRecords.isLegal(3, 2, P_WHITE));
    });

  checkDotsField("Recalculate square during dangling dots removing",
    R"(
.ooo..
o...o.
o.o..o
..xo.o
o.o..o
o...o.
.ooo..
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
      boardWithMoveRecords.playMove(1, 3, P_WHITE);
      testAssert(1 == boardWithMoveRecords.board.numBlackCaptures);

      boardWithMoveRecords.playMove(4, 3, P_BLACK);
      testAssert(2 == boardWithMoveRecords.board.numBlackCaptures);
    });

  checkDotsField("Base sorting by size",
    R"(
..xxx..
.x...x.
x..x..x
x.xox.x
x.....x
.xx.xx.
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
      boardWithMoveRecords.playMove(3, 4, P_BLACK);
      testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

      boardWithMoveRecords.playMove(4, 1, P_WHITE);
      testAssert(2 == boardWithMoveRecords.board.numWhiteCaptures);
    });
}

void Tests::runDotsGroundingTests() {
  cout << "Running dots grounding tests:" << endl;

    checkDotsField("Grounding propagation",
R"(
.x..
o.o.
.x..
.xo.
..x.
....
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
..x..
.x.x.
.x.x.
..x..
.....
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
.x.
xox
...
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(1, 2, P_BLACK);

    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(-1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
}
);

  checkDotsField("Grounding score with ungrounded base",
R"(
.....
..o..
.oxo.
.....
.....
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(2, 3, P_WHITE);

    testAssert(4 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(1 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
}
);

  checkDotsField("Grounding score with grounded and ungrounded bases",
R"(
.x.....
xox.o..
...oxo.
.......
.......
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playMove(1, 2, P_BLACK);
    boardWithMoveRecords.playMove(4, 3, P_WHITE);

    testAssert(5 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(0 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
}
);

  checkDotsField("Grounding draw with ungrounded bases",
R"(
.........
..x...o..
.xox.oxo.
.........
.........
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
..x..
..x..
.xox.
.....
.x.x.
..x..
.....
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
..x..
..x..
.....
.xox.
..x..
.....
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
.......
..ooo..
.o.x.o.
.oxoxo.
.o...o.
..o.o..
.......
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
.........
..xxx....
.x....x..
.x.xx..x.
.x.x.x.x.
.x.xxx.x.
.x..xo.x.
..xxxxx..
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
...........
.xxxxxxx...
.x.........
.x.xxxx..x.
.x.x...x.x.
.x.x.x.x.x.
.x.x...x.x.
.x.xxxxx.x.
.x..xo...x.
.xxxxxxxxx.
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
.....
.xxo.
.....
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playGroundingMove(P_BLACK);

    testAssert(2 == boardWithMoveRecords.board.numBlackCaptures);

    testAssert(1 == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    testAssert(boardWithMoveRecords.getWhiteScore() == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);

    boardWithMoveRecords.undo();

    boardWithMoveRecords.playGroundingMove(P_WHITE);

    testAssert(1 == boardWithMoveRecords.board.numWhiteCaptures);

    testAssert(2 == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
    testAssert(boardWithMoveRecords.getBlackScore() == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);

    boardWithMoveRecords.undo();
  }
);

  checkDotsField("Draw",
R"(
.x...
.xxo.
...o.
)", [](const BoardWithMoveRecords& boardWithMoveRecords) {
    boardWithMoveRecords.playGroundingMove(P_BLACK);
    testAssert(0 == boardWithMoveRecords.board.numBlackCaptures);
    testAssert(boardWithMoveRecords.getWhiteScore() == boardWithMoveRecords.board.whiteScoreIfBlackGrounds);
    boardWithMoveRecords.undo();

    boardWithMoveRecords.playGroundingMove(P_WHITE);
    testAssert(0 == boardWithMoveRecords.board.numWhiteCaptures);
    testAssert(boardWithMoveRecords.getBlackScore() == boardWithMoveRecords.board.blackScoreIfWhiteGrounds);
    boardWithMoveRecords.undo();
}
);

  checkDotsField("Bases",
R"(
.........
..xx...x.
.xo.x.xox
..x......
.........
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
......
xxo..o
.ox...
x...oo
...o..
......
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
......
..oo..
.o..o.
..oo..
......
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
.x.
x.x
.x.
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
....
.xo.
.ox.
....
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
.xo.
.xo.
.ox.
.ox.
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
.x....
xox...
....o.
...oxo
......
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
.x....
xox.x.
......
....o.
.o.oxo
......
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
.....
..o..
.oxo.
.....
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
.....
..x..
.xox.
.....
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
.....
..x..
.xox.
.....
.....
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
...
.o.
...
)");
    const auto boardHistory = BoardHistory(board);
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
  }

  {
    const Board board = parseDotsFieldDefault(R"(
...
.x.
...
)");
    const auto boardHistory = BoardHistory(board);
    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
.x....
xox...
....x.
......
)",
      {XYMove(1, 2, P_BLACK)});
    const auto boardHistory = BoardHistory(board);

    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
.x....
xox...
xox.x.
......
......
)",
      {XYMove(1, 3, P_BLACK)});
    const auto boardHistory = BoardHistory(board);

    testAssert(-1.0f == boardHistory.whiteScoreIfGroundingAlive(board));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    // Ungrounded own dot -> black can't ground without score losing
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
.x....
xox...
xox.o.
......
......
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
.o....
oxo...
oxo.x.
......
......
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
.o....
oxo...
....o.
......
)",
      {XYMove(1, 2, P_WHITE)});
    const auto boardHistory = BoardHistory(board);

    testAssert(std::isnan(boardHistory.whiteScoreIfGroundingAlive(board)));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
.o....
oxo...
oxo.o.
......
......
)",
      {XYMove(1, 3, P_WHITE)});
    const auto boardHistory = BoardHistory(board);

    testAssert(1.0f == boardHistory.whiteScoreIfGroundingAlive(board));

    testAssert(std::isnan(boardHistory.whiteScoreIfAllDotsAreGrounded(board)));
    // Ungrounded own dot -> White can't ground without score losing
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_WHITE)));
    testAssert(std::isnan(boardHistory.whiteScoreIfNotCapturingGroundingAlive(board, P_BLACK)));
  }

  {
    const Board board = parseDotsFieldDefault(
      R"(
.o....
oxo...
oxo.x.
......
......
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
xo
xo
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
xo
xo
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
ooo
oxo
o.o
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
xxxxx
x.xox
xxx.x
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
xxxxx
x.xox
xxx.x
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
xxxxx
x...x
x.x.x
x...x
xxxxx
)",
      Rules::DEFAULT_DOTS.startPosIsRandom,
      false,
      Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
      Rules::DEFAULT_DOTS.dotsFreeCapturedDots,
      {});
    auto boardHistory = BoardHistory(board);

    // The field is not grounding alive; however, the game should be finished because there are no legal moves for WHITE
    testAssert(!boardHistory.isGroundReasonable(board));
    testAssert(boardHistory.endGameIfReasonable(board, false, P_WHITE));
    testAssert(C_EMPTY == boardHistory.winner);
    testAssert(0.0f == boardHistory.finalWhiteMinusBlackScore);
  }
}

// We need to check both playMoveRecorded and playMoveAssumeLegal because they have different implementations
// playMoveAssumeLegal is faster but doesn't return move records.
static void checkHashAfterMovesAndRollback(
  const string& description,
  const string& field1Str,
  const string& field2Str,
  const vector<XYMove>& field1Moves,
  const vector<XYMove>& field2Moves,
  const bool hashIsEqualAfterMoves,
  const bool captureEmptyBase1 = false,
  const bool captureEmptyBase2 = false
  ) {
  cout << "  " << description << endl;

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

  const auto field1HashAfterMoveRecords = field1.pos_hash;
  const auto field2HashAfterMoveRecords = field2.pos_hash;
  testAssert(hashIsEqualAfterMoves == (field1HashAfterMoveRecords == field2HashAfterMoveRecords));

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

  testAssert(field1HashAfterMoveRecords == field1.pos_hash);
  testAssert(field2HashAfterMoveRecords == field2.pos_hash);
  testAssert(hashIsEqualAfterMoves == (field1.pos_hash == field2.pos_hash));
}

void Tests::runDotsPosHashTests() {
  cout << "Running dots pos hashes tests:" << endl;

  checkHashAfterMovesAndRollback(
     "Simple",
     R"(
...
.x.
...
)",
     R"(
...
.o.
...
)",
     {},
     {},
     false
);

  checkHashAfterMovesAndRollback(
   "Different moves order doesn't affect hash",
   R"(
...
...
...
)",
   R"(
...
...
...
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
   true
);

  checkHashAfterMovesAndRollback(
       "Capturing order doesn't affect hash",
       R"(
.x.
x.x
.x.
)",
       R"(
.x.
xox
...
)",
       { XYMove(1, 1, P_WHITE)},
       { XYMove(1, 2, P_BLACK) },
       true
);

  checkHashAfterMovesAndRollback(
     "Field with different sizes have different hashes",
     R"(
...
.x.
...
)",
     R"(
....
.x..
....
....
)",
     {},
     {},
     false
);

  checkHashAfterMovesAndRollback(
"Same shape and same captures but different captures locations",
R"(
.xx.
xo..
.xx.
)",
R"(
.xx.
x.o.
.xx.
)",
{ XYMove(3, 1, P_BLACK) },
{ XYMove(3, 1, P_BLACK) },
true
);

  checkHashAfterMovesAndRollback(
    "Field captures affects hash (https://github.com/KvanTTT/KataGoDots/issues/45)",
    R"(
.xxx.
.o..x
.xxx.
)",
    R"(
.xxx.
.ooxx
.xxx.
)",
{ XYMove(0, 1, P_BLACK) },
{ XYMove(0, 1, P_BLACK) },
false
    );

  checkHashAfterMovesAndRollback(
  "Equal captures diff affects hash (https://github.com/KvanTTT/KataGoDots/issues/45)",
  R"(
.xx..oo.
xo....xo
.xx..oo.
)",
  R"(
.xx..oo.
xoo..xxo
.xx..oo.
)",
{ XYMove(3, 1, P_BLACK), XYMove(4, 1, P_WHITE) },
{ XYMove(3, 1, P_BLACK), XYMove(4, 1, P_WHITE) },
false
  );

  const string& fieldForSameShapeButDifferentCaptures = R"(
.xx.
xo..
.xx.
)";
  checkHashAfterMovesAndRollback(
"Different hashes when same shape but different captures",
fieldForSameShapeButDifferentCaptures,
fieldForSameShapeButDifferentCaptures,
{ XYMove(3, 1, P_BLACK) },
{ XYMove(2, 1, P_BLACK), XYMove(3, 1, P_BLACK) },
false
);

  const string& fieldForSameShapeButDifferentCapturesWithFree = R"(
..oooo..
.oxxxxo.
ox.o....
.oxxxxo.
..oooo..
)";
  checkHashAfterMovesAndRollback(
"Different hashes when for same shape but different captures with free",
fieldForSameShapeButDifferentCapturesWithFree,
fieldForSameShapeButDifferentCapturesWithFree,
{ XYMove(6, 2, P_BLACK), XYMove(7, 2, P_WHITE) },
{ XYMove(4, 2, P_WHITE), XYMove(6, 2, P_BLACK), XYMove(7, 2, P_WHITE) },
false
);

  const auto field1WhenSurroundLocsDontAffectHash = R"(
..xxxxxx..
.x......x.
x..x..o..x
x.xoxoxo.x
x........x
.x......x.
..xxx.xx..
)";
  const auto field2WhenSurroundLocsDontAffectHash = R"(
..xxxxxx..
.x......x.
x..o..x..x
x.oxoxox.x
x........x
.x......x.
..xxx.xx..
)";

  checkHashAfterMovesAndRollback(
    "Surrounded locations (first) doesn't affect hash (it's erased)",
    field1WhenSurroundLocsDontAffectHash,
    field2WhenSurroundLocsDontAffectHash,
    { XYMove(3, 4, P_BLACK), XYMove(6, 4, P_WHITE), XYMove(5, 6, P_BLACK) },
    { XYMove(3, 4, P_WHITE), XYMove(6, 4, P_BLACK), XYMove(5, 6, P_BLACK) },
    true
  );

  checkHashAfterMovesAndRollback(
    "Surrounded locations (second) doesn't affect hash (it's erased)",
    invertColors(field1WhenSurroundLocsDontAffectHash),
    invertColors(field2WhenSurroundLocsDontAffectHash),
    { XYMove(3, 4, P_WHITE), XYMove(6, 4, P_BLACK), XYMove(5, 6, P_WHITE) },
    { XYMove(3, 4, P_BLACK), XYMove(6, 4, P_WHITE), XYMove(5, 6, P_WHITE) },
    true
  );

  const string fieldWithAllGroundedDots = R"(
.xo.
.xo.
.ox.
.ox.
)";
  checkHashAfterMovesAndRollback(
  "Grounding with all grounded dots doesn't affect hash",
  fieldWithAllGroundedDots,
  fieldWithAllGroundedDots,
{ XYMove::getGroundMove(P_BLACK) },
{ },
true
  );

  const string fieldWithSomeUngroundedDots = R"(
....
.xo.
.ox.
....
)";
  checkHashAfterMovesAndRollback(
  "Grounding with some ungrounded dots affects hash",
  fieldWithSomeUngroundedDots,
  fieldWithSomeUngroundedDots,
{ XYMove::getGroundMove(P_BLACK) },
{ },
false
  );

  const string emptyBaseField = R"(
.o.
o.o
...
)";
  checkHashAfterMovesAndRollback(
    "Different hash for empty base when it's enabled and not",
    emptyBaseField,
    emptyBaseField,
{ XYMove(1, 2, P_WHITE) },
{ XYMove(1, 2, P_WHITE) },
false,
false,
true
    );

  checkHashAfterMovesAndRollback(
  "Different hash for empty base and non-empty base",
  emptyBaseField,
  R"(
.o.
oxo
...
)",
{ XYMove(1, 2, P_WHITE) },
{ XYMove(1, 2, P_WHITE) },
false,
true,
true
  );

  checkHashAfterMovesAndRollback(
  "Expected false negative (limitation of current hashing approach)",
  R"(
.x..
xxox
.xx.
)",
  R"(
..x.
xoxx
.xx.
)",
{ XYMove(2, 0, P_BLACK) },
{ XYMove(1, 0, P_BLACK) },
false
  );
}