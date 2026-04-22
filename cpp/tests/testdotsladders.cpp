#include <gtest/gtest.h>

#include "testdotsutils.h"
#include "core/test.h"
#include "game/board.h"

using namespace std;

#define EXPECT_EQ_TRIMMED(expected, actual) \
    EXPECT_EQ(Global::trimMultiline(expected), Global::trimMultiline(actual))

// Ensure zobrist hashing is initialized before any tests run.
class BoardEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    Board::initHash();
  }
};

testing::Environment* const boardEnv =
    testing::AddGlobalTestEnvironment(new BoardEnvironment);

string generateBoardStateRepresentation(Board& board,
    unordered_set<Loc> blackWorkingLocs,
    unordered_set<Loc> whiteWorkingLocs,
    unordered_set<Loc> blackCapturedLocs,
    unordered_set<Loc> whiteCapturedLocs
    ) {
    std::ostringstream stream;

    for (int y = 0; y < board.y_size; y++) {
        for (int x = 0; x < board.x_size; x++) {
            const Loc loc = Location::getLoc(x, y, board.x_size);

            stream << PlayerIO::stateToChar(board.getState(loc), true);

            int extraStatusCounter = 0;
            if (blackCapturedLocs.find(loc) != blackCapturedLocs.end()) {
                stream << '\'';
                extraStatusCounter++;
            }
            if (whiteCapturedLocs.find(loc) != whiteCapturedLocs.end()) {
                stream << '\'';
                extraStatusCounter++;
            }
            if (blackWorkingLocs.find(loc) != blackWorkingLocs.end()) {
                stream << 'x';
                extraStatusCounter++;
            }
            if (whiteWorkingLocs.find(loc) != whiteWorkingLocs.end()) {
                stream << 'o';
                extraStatusCounter++;
            }
            //assert(extraStatusCounter <= 1);

            if (extraStatusCounter == 0) {
                stream << ' ';
            }

            stream << ' ';
        }

        stream << endl;
    }

    return stream.str();
}

string generateBoardRepresentation(Board& board) {
    return generateBoardStateRepresentation(board, {}, {}, {}, {});
}

string playAndDumpLadderInfo(Board& board, const XYMove move, Board::LaddersCache& laddersCache) {
    if (const Loc moveLoc = Location::getLoc(move.x, move.y, board.x_size); moveLoc != Board::NULL_LOC) {
        cout << "Move: " << move.toString() << ". ";
        EXPECT_TRUE(board.playMove(moveLoc, move.player, true));
    }

    bool firstIteration = laddersCache.getCacheSize() == 0;

    uint16_t previousMovesCount = laddersCache.getMovesCount();
    uint16_t previousCacheHits = laddersCache.getCacheHits();
    size_t previousCacheSize = laddersCache.getCacheSize();
    laddersCache.clearMaxDepth();

    const vector<const Board::LadderMoveInfo*> workingLadderMoveInfos = board.iterDotsLadders(laddersCache);

    cout << "Total moves count: " << laddersCache.getMovesCount();
    if (!firstIteration) {
        cout << " (+" << laddersCache.getMovesCount() - previousMovesCount << ")";
    }
    cout << ", max depth: " << laddersCache.getMaxDepth();
    cout << ", cache hits: " << laddersCache.getCacheHits();
    if (!firstIteration) {
        cout << " (+" << laddersCache.getCacheHits() - previousCacheHits << ")";
    }
    cout << ", cache size: " << laddersCache.getCacheSize();
    if (!firstIteration) {
        cout << " (+" << laddersCache.getCacheSize() - previousCacheSize << ")";
    }
    cout << endl;

    std::unordered_set<Loc> blackWorkingLocs;
    std::unordered_set<Loc> whiteWorkingLocs;
    std::unordered_set<Loc> blackCapturedLocs;
    std::unordered_set<Loc> whiteCapturedLocs;

    for (const auto* ladderMoveInfo : workingLadderMoveInfos) {
        assert(ladderMoveInfo->type == Board::LadderMoveInfo::LADDER);
        Player ladderPlayer = ladderMoveInfo->player;
        assert(ladderPlayer != C_EMPTY);

        if (ladderMoveInfo->workingMove != Board::NULL_LOC) {
            std::unordered_set<Loc>& workingLocs = ladderPlayer == C_BLACK ? blackWorkingLocs : whiteWorkingLocs;
            workingLocs.insert(ladderMoveInfo->workingMove);
        }
        std::unordered_set<Loc>& capturedLocs = ladderPlayer == C_BLACK ? blackCapturedLocs : whiteCapturedLocs;
        for (const auto& territoryLoc : ladderMoveInfo->territoryLocs) {
            capturedLocs.insert(territoryLoc);
        }
    }

    return generateBoardStateRepresentation(board, blackWorkingLocs, whiteWorkingLocs, blackCapturedLocs,
                                            whiteCapturedLocs);
}

static void checkLadders(const string& boardData, const optional<string>& expectedLaddersInfo) {
    Board field = parseDotsFieldDefault(boardData);
    Board::LaddersCache laddersCache;

    const string actualFieldLadderInfo = playAndDumpLadderInfo(field, XYMove::getNullMove(), laddersCache);

    const string expectedLaddersInfoString = expectedLaddersInfo.has_value()
        ? expectedLaddersInfo.value()
        : generateBoardRepresentation(field);

    EXPECT_EQ_TRIMMED(expectedLaddersInfoString, actualFieldLadderInfo);
}

static void checkLaddersSequence(
    const string& boardData,
    const string& expectedLaddersInfo,
    const XYMove nextMove,
    const string& expectedLaddersInfoOnNextMove,
    const XYMove nextNextMove,
    const string& expectedLaddersInfoOnNextNextMove
) {
    const Board origField = parseDotsFieldDefault(boardData);
    Board::LaddersCache laddersCache;

    Board field = origField;
    const string origFieldLadderInfo = playAndDumpLadderInfo(field, XYMove::getNullMove(), laddersCache);
    EXPECT_EQ_TRIMMED(origFieldLadderInfo, expectedLaddersInfo);

    Board nextField = field;
    const string nextFieldLadderInfo = playAndDumpLadderInfo(nextField, nextMove, laddersCache);
    EXPECT_EQ_TRIMMED(nextFieldLadderInfo, expectedLaddersInfoOnNextMove);

    Board nextNextField = nextField;
    const string nextNextFieldLadderInfo = playAndDumpLadderInfo(nextNextField, nextNextMove, laddersCache);
    EXPECT_EQ_TRIMMED(nextNextFieldLadderInfo, expectedLaddersInfoOnNextNextMove);
}

TEST(LadderTests, MinimalCapturing) {
    checkLadders(
        R"(
.  .  .  .  .
.  X  O  .  X
X  O  O  X  .
.  X  X  .  .
)",
        R"(
.  .  .x .  .
.  X  O' .  X
X  O' O' X  .
.  X  X  .  .
)");
}

TEST(LadderTests, MinimalCapturingWithReplace) {
    checkLadders(
        R"(
.  X  .  .
.  .  X  .
.  O  O  X
.  X  O  X
.  .  X  .
)",
        R"(
.  X  .  .
.  .  X  .
.x O' O' X
.  X  O' X
.  .  X  .
)");
}

TEST(LadderTests, IndirectLadder) {
    checkLadders(
        R"(
.  X  .  .  .
.  .  .  X  .
.  X  O  .  .
.  .  X  .  .
)",
        R"(
.  X  .  .  .
.  .  .  X  .
.  X  O' .x .
.  .  X  .  .
)"
        );
}

TEST(LadderTests, IndirectFarLadder) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .  .
.  X  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  X  .  .  .  .  .  .
.  .  .  .  O  X  .  .  .  .  .
.  .  .  X  O  O  X  .  .  .  .
.  .  .  .  X  O  .  .  .  .  .
.  .  .  .  .  X  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
)",
        R"(
.  .  .  .  .  .  .  .  .  .  .
.  X  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  X  .  .  .  .  .  .
.  .  .  .  O' X  .  .  .  .  .
.  .  .  X  O' O' X  .  .  .  .
.  .  .  .  X  O' .x .  .  .  .
.  .  .  .  .  X  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
)"
        );
}

TEST(LadderTests, DoubleAtariIsNotLadder) {
    checkLadders(
R"(
.  .  .  .  .  .
.  O  X  .  .  .
.  .  O  X  .  .
.  .  .  O  .  .
.  .  .  .  .  .
)",
    nullopt);
}

TEST(LadderTests, WorkingMoveIsAlsoCapturingMove) {
    checkLadders(
        R"(
.  .  .  X  .  .  .
.  .  X  .  .  .  .
.  X  O  O  .  .  .
.  .  X  X  O  X  .
.  .  .  .  X  .  .
)",
        R"(
.  .  .  X  .  .  .
.  .  X  .' .x .  .
.  X  O' O' .x .  .
.  .  X  X  O' X  .
.  .  .  .  X  .  .
)");
}

TEST(LadderTests, EmptyTerrtirotyIsNotAccountable) {
    checkLadders(
        R"(
.  .  .  .  .  .
.  X  .  .  X  .
.  .  X  X  .  .
)",
        R"(
.  .  .  .  .  .
.  X  .  .  X  .
.  .  X  X  .  .
)");
}

TEST(LadderTests, NotLadderBecauseOfOppAtari) {
    checkLadders(
        R"(
.  .  .  X  .
.  .  .  .  .
.  .  .  .  .
X  O  O  X  .
.  X  X  O  .
)",
        nullopt);
}

TEST(LadderTests, NoLadderBecauseOfOppFarAtari) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  X  .  .  .
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  O  X  .  .  .
.  .  .  .  O  X  O  O  .  .
.  X  O  O  X  X  .  .  O  .
.  .  X  X  O  .  .  .  .  .
.  .  .  .  .  O  O  .  .  .
.  .  .  .  .  .  .  .  .  .
)",
        nullopt
        );
}

TEST(LadderTests, OppAtariButWorkingLadder) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .
.  .  .  .  .  .  X  .
.  X  O  O  X  .  X  .
.  .  X  X  O  .  X  .
.  .  .  .  X  X  .  .
)",
        R"(
.  .  .  .  .  .  .  .
.  .  .x .  .  .  X  .
.  X  O' O' X  .  X  .
.  .  X  X  O' .  X  .
.  .  .  .  X  X  .  .
)");
}

TEST(LadderTests, CapturedAndWorkingLocsArePresentedAndDifferent) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O  O  X  X  O  .
.  .  .  X  .  .  .  .
.  X  .  .  .  .  O  .
.  .  .  .  .  .  .  .
)",
        R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O' O' X  X  O  .
.  .  .' X  .  .  .  .
.  X  .x .  .  .  O  .
.  .  .  .  .  .  .  .
)"
    );
}

TEST(LadderTests, Rotation) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  X  .  .
.  .  .  .  .  .  .  .  .  .
.  X  O  O  X  .  .  .  .  .
.  .  X  X  .  .  .  .  X  .
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .
)",
        R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  X  .  .
.  .  .x .  .  .  .  .  .  .
.  X  O' O' X  .  .  .  .  .
.  .  X  X  .  .  .  .  X  .
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, RotatationNotEnoughSpace) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  X  .  .
.  .  .  .  .  .  .  .  .  .
.  X  O  O  X  .  .  .  .  .
.  .  X  X  .  .  .  .  X  .
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .
)",
        nullopt);
}

TEST(LadderTests, MoveInsideEmptyTerritory) {
    checkLadders(
        R"(
.  X  X  X  .
X  O  O  O  X
X  O  .  O  X
.  X  O  .  .
.  .  .  .  .
)",
        R"(
.  X  X  X  .
X  O' O' O' X
X  O' .  O' X
.  X  O  .x .
.  .  .  .  .
)");
}

TEST(LadderTests, ComplexAtari) {
    checkLadders(
        R"(
.  X  X  X  .  .
X  O  O  O  X  .
X  O  X  O  .  .
.  X  .  .  .  .
.  .  .  .  X  .
)",
        R"(
.  X  X  X  .  .
X  O' O' O' X  .
X  O' X  O' .  .
.  X  .  .x .  .
.  .  .  .  X  .
)");
}

TEST(LadderTests, ComplexAtariNotWorking) {
    checkLadders(
        R"(
.  .  .  X  X  X  .  .  .
.  .  X  O  O  O  X  .  .
.  X  O  O  X  O  .  .  .
.  X  O  X  .  .  .  .  .
.  .  X  .  .  .  .  X  .
.  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .
)",
        R"(

)");
}

TEST(LadderTests, StressSimple) {
    constexpr int width = Board::MAX_LEN_X;
    constexpr int height = Board::MAX_LEN_Y;

    cout << "Size: " << width << " x " << height << endl;
    Board board(width, height, Rules::DEFAULT_DOTS);

    // Ladder start
    board.playMove(Location::getLoc(0, 1, width), P_BLACK, true);
    board.playMove(Location::getLoc(1, 1, width), P_WHITE, true);
    board.playMove(Location::getLoc(1, 0, width), P_BLACK, true);
    board.playMove(Location::getLoc(2, 1, width), P_WHITE, true);
    board.playMove(Location::getLoc(2, 0, width), P_BLACK, true);
    board.playMove(Location::getLoc(3, 1, width), P_BLACK, true);

    const string representationWithEscape = generateBoardRepresentation(board);

    cout << "Check escaping:" << endl;
    checkLadders(representationWithEscape, nullopt);

    int farMoveX;
    int farMoveY;
    if (width > height) {
        farMoveX = height - 1;
        farMoveY = farMoveY;
    } else if (width < height) {
        farMoveX = width - 1;
        farMoveY = farMoveX;
    } else {
        farMoveX = width- 2;
        farMoveY = height - 1;
    }
    board.playMove(Location::getLoc(farMoveX, farMoveY, width), P_BLACK, true);

    const string representationWithCapture = generateBoardRepresentation(board);

    const string expectedLadder = generateBoardStateRepresentation(board,
        {Location::getLoc(1, 2, width)},
        {},
        {Location::getLoc(1, 1, width), Location::getLoc(2, 1, width)},
        {});

    cout << "Check capturing:" << endl;
    checkLadders(representationWithCapture, expectedLadder);
}

// LadderSequenceTests
/*
TEST(LadderSequenceTests, SimpleEscape) {
    checkLaddersSequence(
            R"(
.  .  X  X  .  .
.  X  O  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
)",
            R"(
.  .  X  X  .  .
.  X  O  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
)", XYMove(3, 1, P_WHITE),
            R"(
.  .  X  X  .  .
.  X  O  O  .  .
.  .  .  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
)",
            XYMove(4, 1, P_BLACK),
            R"(
.  .  X  X  .  .
.  X  O  O  X  .
.  .  .  .  .  .
.  .  .  .  .  .
.  .  .  .  .  .
)"
        );
}

TEST(LadderSequenceTests, SimpleCapture) {
    checkLaddersSequence(
        R"(
.  X  X  .  .
X  O  .  .  .
.  .  .  .  .
.  .  .  .  .
.  .  .  X  .
)",
        R"(
.  X  X  .  .
X  O' .  .  .
.  .x .  .  .
.  .  .  .  .
.  .  .  X  .
)", XYMove(2, 1, P_WHITE),
        R"(
.  X  X  .  .
X  O  O  .  .
.  .  .  .  .
.  .  .  .  .
.  .  .  X  .
)",
        XYMove(3, 1, P_BLACK),
        R"(
.  X  X  .  .
X  O' O' X  .
.  .x .  .  .
.  .  .  .  .
.  .  .  X  .
)"
    );
}

TEST(LadderSequenceTests, IndirectLadder) {
    checkLaddersSequence(
        R"(
.  O  .  .  .
.  .  .  O  .
.  O  X  .  .
.  .  O  .  .
)",
        R"(
.  O  .  .  .
.  .  .  O  .
.  O  X' .o .
.  .  O  .  .
)",
        XYMove(4, 2, P_WHITE),
        R"(
.  O  .  .  .
.  .  .o O  .
.  O  X' .' O
.  .  O  .o .
)",
        XYMove(3, 2, P_BLACK),
        R"(
.  O  .  .  .
.  .  .  O  .
.  O  X' X' O
.  .  O  .o .
)"
    );
}

TEST(LadderSequenceTests, TwoLadders) {
    checkLaddersSequence(
        R"(
.  .  .  .  .  .  .  .  .
.  O  .  .  .  .  .  .  .
.  .  .  O  .  O  .  .  .
.  O  X  X  .  .  X  O  .
.  .  O  O  .  O  O  .  O
.  .  .  .  .  .  .  .  .
)",
        R"(
.  .  .  .  .  .  .  .  .
.  O  .  .  .  .  .  .  .
.  .  .  O  .  O  .o .  .
.  O  X' X' .o .' X' O  .
.  .  O  O  .  O  O  .  O
.  .  .  .  .  .  .  .  .
)",
        XYMove(7, 1, P_WHITE),
        R"(
.  .  .  .  .  .  .  .  .
.  O  .  .  .  .  .  O  .
.  .  .  O  .  O  .o .  .
.  O  X' X' .o .' X' O  .
.  .  O  O  .  O  O  .  O
.  .  .  .  .  .  .  .  .
)",
        XYMove(5, 3, P_BLACK),
        R"(
.  .  .  .  .  .  .  .  .
.  O  .  .  .  .  .  O  .
.  .  .  O  .  O  .  .  .
.  O  X' X' .o X' X' O  .
.  .  O  O  .  O  O  .  O
.  .  .  .  .  .  .  .  .
)"
    );
}

TEST(LadderSequenceTests, CapturedAndWorkingLocsArePresentedAndDifferent) {
    checkLaddersSequence(
        R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O  O  X  X  O  .
.  .  .  .  .  .  .  .
.  X  .  .  .  .  O  .
.  .  .  .  .  .  .  .
)",
        R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O' O' X' X' O  .
.  .  .  .x .o .  .  .
.  X  .  .  .  .  O  .
.  .  .  .  .  .  .  .
)", XYMove(3, 3, P_BLACK),
        R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O' O' X  X  O  .
.  .  .' X  .  .  .  .
.  X  .x .  .  .  O  .
.  .  .  .  .  .  .  .
)",
        XYMove(2, 3, P_WHITE),
        R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O' O' X' X' O  .
.  .  O' X' .  .o .  .
.  X  .x .o .  .  O  .
.  .  .  .  .  .  .  .
)"
    );
}

TEST(LadderSequenceTests, WorkingMoveForUnrelatedTerritories) {
    checkLaddersSequence(
    R"(
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  X  O  .  .
.  .  O  X  X  O  .  X  O  .  .
.  .  X  O  O  X  X  .  O  .  .
.  .  .  .  .  O  O  O  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  X  O  .  .
.  .  O  X' X' O  .  X  O  .  .
.  .  X  O  O  X  X  .  O  .  .
.  .  .  .  .  O  O  O  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  )", XYMove(3, 2, P_WHITE),
    R"(
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  O  .  .  .  X  O  .  .
.  .  O  X  X  O  .  X  O  .  .
.  .  X  O  O  X  X  .  O  .  .
.  .  .  .  .  O  O  O  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  )",
    XYMove(4, 2, P_BLACK),
    R"(
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  O  X  .! .  X  O  .  .
.  .  O  X  X  O  .  X  O  .  .
.  .  X  O  O  X  X  .  O  .  .
.  .  .  .  .  O  O  O  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
)"
);
}*/