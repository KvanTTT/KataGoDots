#include <gtest/gtest.h>

#include "testdotsutils.h"
#include "tests.h"
#include "game/board.h"
#include "game/dotsfieldLadders.h"

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

string playAndDumpLaddersInfo(Board& board, const XYMove move, DotsLaddersSolver& solver,
    int& actualBlackScore, int& actualWhiteScore
    ) {
    if (const Loc moveLoc = Location::getLoc(move.x, move.y, board.x_size); moveLoc != Board::NULL_LOC) {
        cout << "Move: " << move.toString() << ". ";
        EXPECT_TRUE(board.playMove(moveLoc, move.player, true));
    }

    uint16_t previousMovesCount = solver.getMovesCount();
    bool firstIteration = previousMovesCount== 0;
    solver.clearMaxDepth();

    const auto workingLadderLocInfos = solver.solve();

    cout << "Total moves count: " << solver.getMovesCount();
    if (!firstIteration) {
        cout << " (+" << solver.getMovesCount() - previousMovesCount << ")";
    }
    cout << ", max depth: " << solver.getMaxDepth();
    cout << '\n';

    std::unordered_set<Loc> blackWorkingLocs;
    std::unordered_set<Loc> whiteWorkingLocs;
    std::unordered_set<Loc> blackCapturedLocs;
    std::unordered_set<Loc> whiteCapturedLocs;

    actualBlackScore = 0;
    actualWhiteScore = 0;

    for (const auto& ladderLocInfo : workingLadderLocInfos) {
        Player ladderPlayer = ladderLocInfo.player;
        assert(ladderPlayer != C_EMPTY && ladderLocInfo.workingLoc != Board::NULL_LOC);

        std::unordered_set<Loc>& workingLocs = ladderPlayer == C_BLACK ? blackWorkingLocs : whiteWorkingLocs;
        if (ladderPlayer == C_BLACK) {
            workingLocs = blackWorkingLocs;
            actualBlackScore += ladderLocInfo.score;
        } else {
            workingLocs = whiteWorkingLocs;
            actualWhiteScore += ladderLocInfo.score;
        }
        workingLocs.insert(ladderLocInfo.workingLoc);

        std::unordered_set<Loc>& capturedLocs = ladderPlayer == C_BLACK ? blackCapturedLocs : whiteCapturedLocs;
        for (const auto& territoryLoc : ladderLocInfo.territoryLocs) {
            capturedLocs.insert(territoryLoc);
        }
    }

    if (solver.getStoreMovesTree()) {
        cout << "SGF: " << solver.toSgf() << '\n';
    }

    return generateBoardStateRepresentation(board, blackWorkingLocs, whiteWorkingLocs, blackCapturedLocs,
                                            whiteCapturedLocs);
}

static void checkLadders(const string& fieldData, const optional<string>& expectedLaddersInfo,
    const optional<int> expectedBlackScore = nullopt, const optional<int> expectedWhiteScore = nullopt
) {
    Board field = parseDotsFieldDefault(fieldData);
    DotsLaddersSolver solver(field);

    int actualBlackScore = 0;
    int actualWhiteScore = 0;
    const string actualFieldLadderInfo = playAndDumpLaddersInfo(field, XYMove::getNullMove(), solver, actualBlackScore, actualWhiteScore);

    const string expectedLaddersInfoString = expectedLaddersInfo.has_value()
        ? expectedLaddersInfo.value()
        : generateBoardRepresentation(field);

    EXPECT_EQ_TRIMMED(expectedLaddersInfoString, actualFieldLadderInfo);
    if (expectedBlackScore.has_value()) {
        EXPECT_EQ(expectedBlackScore, actualBlackScore);
    }
    if (expectedWhiteScore.has_value()) {
        EXPECT_EQ(expectedWhiteScore, actualWhiteScore);
    }
}

static Board parseDotsFieldWithLaddersInfo(const string& boardData, const bool captureEmptyBases, const vector<XYMove>& extraMovesXY,
    vector<Move>& initialMoves,
    unordered_set<Loc>& blackWorkingLocs,
    unordered_set<Loc>& whiteWorkingLocs,
    unordered_set<Loc>& blackCapturedLocs,
    unordered_set<Loc>& whiteCapturedLocs
) {
    int y = 0;
    int x = 0;
    int maxX = 0;

    vector<XYMove> moves;
    vector<XYMove> workingMoves;
    vector<XYMove> capturedMoves;

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
                            i++;
                            break;
                        case FIRST_PLA_LOWER:
                            // First player working move
                            workingMoves.emplace_back(x, y, P_BLACK);
                            i++;
                            break;
                        case SECOND_PLA_LOWER:
                            // Second player working move
                            workingMoves.emplace_back(x, y, P_WHITE);
                            i++;
                            break;
                        case '\'':
                            // Captured loc. Currently, it's not possible to detect the captured color reliably
                            // Use P_BLACK as default
                            capturedMoves.emplace_back(x, y, P_BLACK);
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
                        case ' ':
                            // Empty loc
                            i++;
                            break;
                        case '\'':
                            // Captured loc
                            // Currently it's not possible to detect the captured color reliably
                            // Use P_BLACK as default
                            capturedMoves.emplace_back(x, y, P_BLACK);
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

    initialMoves.reserve(moves.size());

    int x_size = maxX + 1;

    std::transform(
        moves.begin(),
        moves.end(),
        std::back_inserter(initialMoves),
        [x_size](const XYMove& item) {
            return item.toMove(x_size);
        }
    );

    for (const XYMove& workingMove : workingMoves) {
        auto& workingLocs = workingMove.player == P_BLACK ? blackWorkingLocs : whiteWorkingLocs;
        workingLocs.insert(workingMove.toMove(x_size).loc);
    }

    for (const XYMove& capturedMove : capturedMoves) {
        auto& capturedLocs = capturedMove.player == P_BLACK ? blackCapturedLocs : whiteCapturedLocs;
        capturedLocs.insert(capturedMove.toMove(x_size).loc);
    }

    field.setStonesFailIfNoLibs(initialMoves);

    playXYMovesAssumeLegal(field, extraMovesXY);

    return field;
}

static void checkLadders(const string& fieldDataWithLaddersInfo,
    const optional<int> expectedBlackScore = nullopt,
    const optional<int> expectedWhiteScore = nullopt,
    const bool captureEmptyBases = Rules::DEFAULT_DOTS.dotsCaptureEmptyBases,
    const vector<XYMove>& extraMovesXY = {}
    ) {
    vector<Move> initialMoves;
    vector<Move> extraMoves;

    unordered_set<Loc> blackWorkingLocs;
    unordered_set<Loc> whiteWorkingLocs;
    unordered_set<Loc> blackCapturedLocs;
    unordered_set<Loc> whiteCapturedLocs;
    Board field = parseDotsFieldWithLaddersInfo(fieldDataWithLaddersInfo, captureEmptyBases, extraMovesXY, initialMoves,
        blackWorkingLocs, whiteWorkingLocs, blackCapturedLocs, whiteCapturedLocs);

    int x_size = field.x_size;
    std::transform(
        extraMovesXY.begin(),
        extraMovesXY.end(),
        std::back_inserter(extraMoves),
        [x_size](const XYMove& item) -> Move {
            return item.toMove(x_size);
        }
    );

    const string expectedFieldLaddersInfo = generateBoardStateRepresentation(field,
        blackWorkingLocs, whiteWorkingLocs, blackCapturedLocs, whiteCapturedLocs);

    DotsLaddersSolver solver(field, false, initialMoves, extraMoves);

    int actualBlackScore = 0;
    int actualWhiteScore = 0;
    const string actualFieldLaddersInfo = playAndDumpLaddersInfo(field, XYMove::getNullMove(), solver, actualBlackScore, actualWhiteScore);

    EXPECT_EQ_TRIMMED(expectedFieldLaddersInfo, actualFieldLaddersInfo);
    if (expectedBlackScore.has_value()) {
        EXPECT_EQ(expectedBlackScore, actualBlackScore);
    }
    if (expectedWhiteScore.has_value()) {
        EXPECT_EQ(expectedWhiteScore, actualWhiteScore);
    }
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

TEST(LadderTests, IndirectLadder2) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .  .
.  X  .  X  X  X  .  .  .  .  .  .  .
.  .  .  O' O' O' .x .  .  .  .  .  .
.  .  X  X  X  X  X  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .  .
)");
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

TEST(LadderTests, TwoLaddersAndTwoWorkingMoves) {
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

TEST(LadderTests, NotLadderBecauseOfOppAtari2) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  X  .  .
.  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .
.  .  .  X  X  .  .  .  .  .
.  O  X  O  O  O  .  .  .  .
.  .  .  X  O  O  X  .  .  .
.  O  X  O  O  X  .  .  .  .
.  .  .  X  X  .  .  .  .  .
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

TEST(LadderTests, OppAtariButOuterCapture) {
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

TEST(LadderTests, CapturingMoveInsideEmptyTerritory) {
    checkLadders(
        R"(
.  X  X  X  .
X  O' O' O' X
X  O' .  O' X
.  X  O  .x .
.  .  .  .  .
)");
}

TEST(LadderTests, CapturingLocsAreNotDirectlyAdjacentToLastOppLoc) {
    checkLadders(
        R"(
.  X  .x X  .
X  O' .' O' X
X  O' .' O' X
X  O' .' O' X
.  X  O' X  .
.  .  .x .  .
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

// TODO: Consider non-directly adjacent locs
TEST(LadderTests, DISABLED_ConsiderDiagonalConnectedDotOnDefending) {
    checkLadders(
        R"(
.  .  .  .  .  .  .
.  .  X  X  X  .  .
.  X  O' .  O' X  .
.  O  .x .  .x O  .
.  O  .  .  .  O  .
.  .  .  .  .  .  .
)");
}

// TODO: Consider non-directly adjacent locs
TEST(LadderTests, DISABLED_ConsiderAdjLocsOfPreviousDotOnDefending) {
    checkLadders(
        R"(
.  .  .  .  .  .  .
.  .  X  X  X  .  .
.  X  O  .  O' X  .
X  O  O  .  .x O  .
X  O  .  .  .  O  .
.  X  .  .  .  .  .
)");
}

// TODO: Consider non-directly adjacent locs
TEST(LadderTests, DISABLED_ConsiderAdjLocsLevel2OfPreviousDotOnDefending) {
    checkLadders(
        R"(
.  .  .  .  .  .  .
.  .  X  X  X  .  .
.  X  O  .  O' X  .
.  X  O  .  .x O  .
.  .  .  .  .  O  .
.  .  .  .  .  .  .
)");
}

// TODO: Consider non-directly adjacent locs
TEST(LadderTests, DISABLED_ConsiderAdjLocsLevel2OfPreviousDotOnDefending2) {
    checkLadders(
        R"(
.  .  .  .  .  .  .
.  .  X  X  X  .  .
.  X  O  .  O' X  .
.  X  O  .  .x .  .
.  X  O  .  .  .  .
.  X  O  O  .  O  .
.  .  X  X  .  .  .
.  .  .  .  .  .  .
)");
}

// TODO: Consider non-directly adjacent locs
TEST(LadderTests, DISABLED_ConsiderAdjCornerLevel2OfPreviousDotOnDefending2) {
    checkLadders(
        R"(
.  .  .  .  .  .  .
.  .  X  X  X  .  .
.  X  O  .  O' X  .
.  X  O  .  .x .  .
.  X  O  .  .  .  .
.  X  O  .  .  O  .
.  .  X  .  .  .  .
.  .  .  .  .  .  .
)");
}


TEST(LadderTests, LadderCaptureOnOtherSideWhenOppCaptures) {
    checkLadders(
        R"(
.  .  .  .  .  .  .
.  .  .  X  .  .  .
.  .  X  .' X  .  .
.  .x O' O' O' X  .
.  X  .' .' X  O' X
.  X  O' O' O' .  .
.  .  X  X  X  X  .
.  .  .  .  .  .  .
)");
}

TEST(LadderTests, DISABLED_LadderCaptureOnOtherSideWhenOppCapturesOnDiagonal) {
    checkLadders(
        R"(
.  .  .  .  .  .  .
.  .  .  X  .  .  .
.  .  X  .' X  .  .
.  .x O' O' O' X  .
.  X  .' .' X  O  .
.  X  O' O' O' .  X
.  .  X  X  X  X  .
.  .  .  .  .  .  .
)");
}

TEST(LadderTests, LadderCaptureOnOtherSideWhenOppCapturesWithLadder) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .
.  .  .  X  .  .  .  .  .
.  .  X  .' X  X  .  .  .
.  .x O' O' O' .  .  .  .
.  X  .' .' X  O  .  .  .
.  X  O' O' O' X  X  .  .
.  .  X  X  X  X  .  .  .
.  .  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, EmptyBaseAdjacentToRegular) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .
.  .  .  X  O  .  .  .  .
.  .  X  O  X  .  .  .  .
.  .  .  .  O  .  O  .  .
.  .  .  X  .  O  X  .  .
.  .  .  .  X  X  .  .  .
.  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, NotWorkingLadderBecauseOfInternalCapturing) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  X  .  .  .  .  .
.  .  .  .  X  .  X  O  O  .  .
.  .  .  X  O  .  .  X  O  O  .
.  .  .  O  O  .  .  .  X  O  .
.  .  .  O  O  .  .  X  O  O  .
.  .  .  X  X  .  X  O  O  .  .
.  X  .  .  .  X  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, WorkingLadderWithInternalCapturings) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  X  X  .  .  .  .  .  .
.  .  .  .  .  X  .' .' X  O  O  .  .  .
.  .  .  .  X  O' .' .  .  X  O  O  O  .
.  .  .  .x O' O' .  .  .  .  X  O  X  .
.  .  .  .  O' O' .' .  .  X  O  O  O  .
.  .  .  .  X  X  .' .' X  O  O  .  .  .
.  X  .  .  .  .  X  X  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .  .  .
)",
        1,
        0,
        false,
        {XYMove(13, 4, P_WHITE)}
        );
}

// Mark the ladders even it's losing or tied score.
//   * The main thing is to hint the ladder that can be deep that's tough for NN to evaluate.
//   * Hopefully, NN will be able to estimate the losing score by itself because the capturing territory is provided and calculated as minimal.
//   * Currently, there is no channel that would allow marking lost dots for ladders.
//   * Such situations are hopefully very rare.
TEST(LadderTests, WorkingLadderWithInternalCapturingsAndZeroScore) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  X  X  .  .  .  .  .
.  .  .  .  .  X  .' .' X  O  O  .  .
.  .  .  .  X  O' .' .  .  X  O  O  .
.  .  .  .x O' O' .  .  .  .  X  O  .
.  .  .  .  O' .' .' .  .  X  O  O  .
.  .  .  .  X  X  .' .' X  O  O  .  .
.  X  .  .  .  .  X  X  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .  .
)",
        0);
}

TEST(LadderTests, WorkingLadderWithInternalCapturingsAndLosingScore) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  X  X  .  .  .  .  .
.  .  .  .  .  X  .' .' X  O  O  O  .
.  .  .  .  X  O' .' .  .  X  O  X  O
.  .  .  .x O' O' .  .  .  .  X  X  O
.  .  .  .  O' .' .' .  .  X  O  X  O
.  .  .  .  X  X  .' .' X  O  O  O  .
.  X  .  .  .  .  X  X  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .  .
)",
        -3);
}

// TODO: Fix calculation of minimal territory considering ideal pla play
TEST(LadderTests, DISABLED_ComplexInterwoundSurroundings) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .
.  .  .  O  O  .  X  .  .
.  .  O  .  X  .  .  .  .
.  O  X  X  .' .  .  .  .
.  X  O' .' .' O' X  .  .
.  X  O' .' .' X  O  .  .
.  X  O' .' .' O' .x .  .
.  X  O' O' O' X  X  .  .
.  .  X  X  X  .  .  .  .
.  .  .  .  .  .  .  .  .
)");
}

// TODO: Fix calculation of two ladders with single working move but when both of them are inescapable
TEST(LadderTests, DISABLED_WorkingMoveCreatesTwoLadders) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .
.  X  .  .  .  .  .  .
.  .  .  X  .  .  .  .
.  X  O' O' .x .  .  .
.  .  X  X  O' X  .  .
.  .  .  X  O' .  .  .
.  .  .  .  X  .  .  .
.  .  .  .  .  .  X  .
.  .  .  .  .  .  .  .
)");
}

// TODO: implement support of dotsCaptureEmptyBases mode
TEST(LadderTests, DISABLED_LadderWhenCaptureEmptyBaseIsEnabled) {
    checkLadders(
        R"(
.  .  .  .  .
.  .  .  .  X
X  .  .  X  .
.  X  X  .  .
)",
        true);
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
    checkLadders(representationWithEscape, 0, 0);

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
    checkLadders(representationWithCapture, expectedLadder, 2, 0);
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