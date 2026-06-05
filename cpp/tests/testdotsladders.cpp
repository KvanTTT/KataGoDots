#include <gtest/gtest.h>

#include "testdotsutils.h"
#include "tests.h"
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

static string generateBoardStateRepresentation(Board& board,
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

static string generateBoardRepresentation(Board& board) {
    return generateBoardStateRepresentation(board, {}, {}, {}, {});
}

string playAndDumpLadderInfo(Board& board, const XYMove move, Board::LaddersInfo& laddersInfo) {
    if (const Loc moveLoc = Location::getLoc(move.x, move.y, board.x_size); moveLoc != Board::NULL_LOC) {
        cout << "Move: " << move.toString() << ". ";
        EXPECT_TRUE(board.playMove(moveLoc, move.player, true));
    }

    bool firstIteration = laddersInfo.getCacheSize() == 0;

    uint16_t previousMovesCount = laddersInfo.getMovesCount();
    uint16_t previousCacheHits = laddersInfo.getCacheHits();
    size_t previousCacheSize = laddersInfo.getCacheSize();
    laddersInfo.clearMaxDepth();

    const vector<Board::LadderLocInfo> workingLadderLocInfos = board.iterDotsLadders(laddersInfo);

    cout << "Total moves count: " << laddersInfo.getMovesCount();
    if (!firstIteration) {
        cout << " (+" << laddersInfo.getMovesCount() - previousMovesCount << ")";
    }
    cout << ", max depth: " << laddersInfo.getMaxDepth();
    cout << ", cache hits: " << laddersInfo.getCacheHits();
    if (!firstIteration) {
        cout << " (+" << laddersInfo.getCacheHits() - previousCacheHits << ")";
    }
    cout << ", cache size: " << laddersInfo.getCacheSize();
    if (!firstIteration) {
        cout << " (+" << laddersInfo.getCacheSize() - previousCacheSize << ")";
    }
    cout << endl;

    std::unordered_set<Loc> blackWorkingLocs;
    std::unordered_set<Loc> whiteWorkingLocs;
    std::unordered_set<Loc> blackCapturedLocs;
    std::unordered_set<Loc> whiteCapturedLocs;

    for (const auto& ladderLocInfo : workingLadderLocInfos) {
        Player ladderPlayer = ladderLocInfo.player;
        assert(ladderPlayer != C_EMPTY);

        if (ladderLocInfo.workingLoc != Board::NULL_LOC) {
            std::unordered_set<Loc>& workingLocs = ladderPlayer == C_BLACK ? blackWorkingLocs : whiteWorkingLocs;
            workingLocs.insert(ladderLocInfo.workingLoc);
        }
        std::unordered_set<Loc>& capturedLocs = ladderPlayer == C_BLACK ? blackCapturedLocs : whiteCapturedLocs;
        for (const auto& territoryLoc : ladderLocInfo.territoryLocs) {
            capturedLocs.insert(territoryLoc);
        }
    }

    return generateBoardStateRepresentation(board, blackWorkingLocs, whiteWorkingLocs, blackCapturedLocs,
                                            whiteCapturedLocs);
}

static void checkLadders(const string& fieldDataWithLaddersInfo, const optional<string>& expectedLaddersInfo) {
    Board field = parseDotsFieldDefault(fieldDataWithLaddersInfo);
    Board::LaddersInfo laddersInfo(field);

    const string actualFieldLadderInfo = playAndDumpLadderInfo(field, XYMove::getNullMove(), laddersInfo);

    const string expectedLaddersInfoString = expectedLaddersInfo.has_value()
        ? expectedLaddersInfo.value()
        : generateBoardRepresentation(field);

    EXPECT_EQ_TRIMMED(expectedLaddersInfoString, actualFieldLadderInfo);
}

static Board parseDotsFieldWithLaddersInfo(const string& boardData, const bool captureEmptyBases) {
    int y = 0;
    int x = 0;
    int maxX = 0;

    vector<XYMove> moves;

    for (size_t i = 0; i < boardData.size(); ++i) {
        switch (const char c = boardData[i]) {
            case ' ':
                continue;
            case '\n':
                // Skip empty lines
                if (x > 0) {
                    if (x - 1 > maxX) {
                        maxX = x - 1;
                    }

                    y++;
                    x = 0;
                }
                continue;
            case '.':
                if (i < boardData.size() - 1) {
                    switch (boardData[i + 1]) {
                        case '\n':
                            break;
                        case ' ': // Empty loc
                        case FIRST_PLA_LOWER: // First player working move
                        case SECOND_PLA_LOWER: // Second player working move
                        case '\'': // Captured loc
                            i++;
                            break;
                        default:
                            throw StringError(string("Unexpected ladder loc type: ") + boardData[i + 1]);
                    }
                }
                x++;
                break;
            case FIRST_PLA_UPPER:
            case SECOND_PLA_UPPER: {
                const Color color = c == FIRST_PLA_UPPER ? C_BLACK : C_WHITE;
                moves.emplace_back(x, y, color);

                if (i < boardData.size() - 1) {
                    switch (boardData[i + 1]) {
                        case '\n':
                            break;
                        case ' ': // Empty loc
                        case '\'': // Captured loc
                            i++;
                            break;
                        default:
                            throw StringError(string("Unexpected ladder loc type: ") + boardData[i + 1]);
                    }
                }

                x++;
                break;
            }
            default:
               throw StringError(string("Unexpected field character: ") + c);
        }
    }

    if (x == 0) {
        y--;
    }

    const auto rules = Rules(
        Rules::START_POS_EMPTY,
        Rules::DEFAULT_DOTS.startPosIsRandom,
        Rules::DEFAULT_DOTS.multiStoneSuicideLegal,
        captureEmptyBases,
        Rules::DEFAULT_DOTS.dotsFreeCapturedDots
        );
    auto field = Board(maxX + 1, y + 1, rules);

    vector<Move> linearMoves;
    linearMoves.reserve(moves.size());

    std::transform(
        moves.begin(),
        moves.end(),
        std::back_inserter(linearMoves),
        [maxX](const XYMove& item) {
            return item.toMove(maxX + 1);
        }
    );

    field.setStonesFailIfNoLibs(linearMoves);
    return field;
}

static void checkLadders(const string& fieldDataWithLaddersInfo, const bool captureEmptyBases = Rules::DEFAULT_DOTS.dotsCaptureEmptyBases) {
    Board field = parseDotsFieldWithLaddersInfo(fieldDataWithLaddersInfo, captureEmptyBases);
    Board::LaddersInfo laddersInfo(field);

    const string actualFieldLadderInfo = playAndDumpLadderInfo(field, XYMove::getNullMove(), laddersInfo);

    EXPECT_EQ_TRIMMED(fieldDataWithLaddersInfo, actualFieldLadderInfo);
}

TEST(LadderTests, MinimalCapturing) {
    checkLadders(
        R"(
.  .  .x .  .
.  X  O' .  X
X  O' O' X  .
.  X  X  .  .
)");
}

TEST(LadderTests, MinimalCapturingRotated) {
    checkLadders(
        R"(
.  X  .  .
.  .  X  .
.x O' O' X
.  X  O' X
.  .  X  .
)");
}

TEST(LadderTests, MinimalCapturingWithEmptyLocs) {
    checkLadders(
        R"(
.  .  .x .  .
.  X  O' .  X
X  .' .' X  .
.  X  X  .  .
)");
}

TEST(LadderTests, IndirectLadder) {
    checkLadders(
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
.  .  .  .  O' X  .  .  .  .  .
.  .  .  X  O' O' X  .  .  .  .
.  .  .  .  X  O' .x .  .  .  .
.  .  .  .  .  X  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
)"
        );
}

TEST(LadderTests, SingleCaptureIsNotLadder) {
    checkLadders(
R"(
.  .  .  .  .
.  .  X  .  .
.  X  O  X  .
.  .  .  .  .
.  .  .  .  .
)");
}

TEST(LadderTests, DoubleAtariIsNotLadder) {
    checkLadders(
R"(
.  .  .  .  .  .
.  X  O  .  .  .
.  .  X  O  .  .
.  .  .  X  .  .
.  .  .  .  .  .
)");
}

TEST(LadderTests, WorkingMoveIsAlsoCapturingMove) {
    checkLadders(
        R"(
.  .  .  X  .  .  .
.  .  X  .  .  .  .
.  X  O' O' .x O  .
.  .  X  X  O  X  .
.  .  .  .  X  .  .
)");
}

TEST(LadderTests, OneWorkingMoveIsAlsoUnrelatedCapturing) {
    checkLadders(
        R"(
.  .  X  .  .  .  .  .
.  .  .  .  X  .  .  .
.  .  .  X  O  X  .  .
.  .  .  .  .  O  O  .
.  .  .x .  .  .  .  .
.  X  O' O' X  .  .  .
.  .  X  X  .  .  .  .
.  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, EmptyTerritoryIsNotAccountable) {
    checkLadders(
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
)");
}

TEST(LadderTests, TwoLaddersAndTworWorkingMoves) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .
.  X  .  .  .  .  X  .
.  .  .  .  .  .  .  .
.  .  .  .x .x .  .  .
.  .  X  O' O' X  .  .
.  .  .  X  X  .  .  .
.  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, TwoUnrelatedLadders) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .  .
.  X  .  .  .  .  .  .  .  X  .
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .x .  .  .  .x .  .  .
.  X  O' O' X  .  X  O' O' X  .
.  .  X  X  .  .  .  X  X  .  .
.  .  .  .  .  .  .  .  .  .  .
)");
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
)"
        );
}

TEST(LadderTests, OppAtariButWorkingLadder) {
    checkLadders(
        R"(
.  .  .  X  .x .  .  .
.  .  X  O' O' .  X  .
.  X  O' O' X  .' X  .
.  .  X  X  O' .' X  .
.  .  .  .  X  X  .  .
)");
}

TEST(LadderTests, CapturedAndWorkingLocsArePresentedAndDifferent) {
    checkLadders(
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

TEST(LadderTests, MoveInsideEmptyTerritory) {
    checkLadders(
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
X  O' O' O' X  .
X  O' X  O' .  .
.  X  .  .x .  .
.  .  .  .  X  .
)");
}

TEST(LadderTests, ComplexAtariWhenInitLocIsCapturedByOppButFinallyFreed) {
    checkLadders(
        R"(
.  .  .  X  X  X  .  .  .
.  .  X  O' O' O' X  .  .
.  X  O' O' X  O' .  .  .
.  X  O' X  .  .x .  .  .
.  .  X  .  .  .  .  X  .
.  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, Rotation) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  X  .  .
.  .  .x .  .  .  .  .  .  .
.  X  O' O' X  .  .  .  .  .
.  .  X  X  .  .  .  .  X  .
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, RotationNotEnoughSpace) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  X  .  .
.  .  .  .  .  .  .  .  .  .
.  X  O  O  X  .  .  .  .  .
.  .  X  X  .  .  .  .  X  .
.  .  .  .  .  .  .  .  .  .
)");
}

// TODO: implement support of dotsCaptureEmptyBases moe
/*TEST(LadderTests, LadderWhenCaptureEmptyBaseIsEnabled) {
    checkLadders(
        R"(
.  .  .  .  .
.  .  .  .  X
X  .  .  X  .
.  X  X  .  .
)",
        true);
}*/

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
    if constexpr (width > height) {
        farMoveX = height - 1;
        farMoveY = farMoveX;
    }
    if constexpr (width < height) {
        farMoveX = width - 1;
        farMoveY = farMoveX;
    }
    farMoveX = width - 2;
    farMoveY = height - 1;
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

TEST(LaddersPerformanceTests, RandomGames) {
    Tests::runDotsStressTestsInternal(
      39,
      32,
      1,
      true,
      Rules::START_POS_CROSS,
      false,
      false,
      0.0f,
      true,
      1.0f,
      1.0f,
      Tests::LADDERS_ON_EACH_MOVE,
      0
    );
}