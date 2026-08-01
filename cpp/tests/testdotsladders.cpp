#include <gtest/gtest.h>

#include <algorithm>

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

    vector<string> items;
    int maxItemLength = 2;

    for (int y = 0; y < board.y_size; y++) {
        for (int x = 0; x < board.x_size; x++) {
            const Loc loc = Location::getLoc(x, y, board.x_size);

            std::ostringstream stream;
            stream << PlayerIO::stateToChar(board.getState(loc), true);

            int itemLength = 1;
            if (blackWorkingLocs.find(loc) != blackWorkingLocs.end()) {
                stream << 'x';
                itemLength++;
            }
            if (whiteWorkingLocs.find(loc) != whiteWorkingLocs.end()) {
                stream << 'o';
                itemLength++;
            }
            if (blackCapturedLocs.find(loc) != blackCapturedLocs.end()) {
                stream << '\'';
                itemLength++;
            }
            if (whiteCapturedLocs.find(loc) != whiteCapturedLocs.end()) {
                stream << '\'';
                itemLength++;
            }

            maxItemLength = std::max(itemLength, maxItemLength);

            items.emplace_back(stream.str());
        }
    }

    std::ostringstream result;
    for (int i = 0; i < items.size(); i++) {
        const auto& item = items[i];
        result << item;
        if (int x = i % board.x_size; x < board.x_size - 1) {
            result << string(static_cast<int>(maxItemLength - item.length() + 1), ' ');
        } else {
            result << '\n';
        }
    }

    return result.str();
}

static string playAndDumpLaddersInfo(Board& board, const XYMove move, DotsLaddersSolver& solver) {
    if (const Loc moveLoc = Location::getLoc(move.x, move.y, board.x_size); moveLoc != Board::NULL_LOC) {
        cout << "Move: " << move.toString() << ". ";
        EXPECT_TRUE(board.playMove(moveLoc, move.player, true));
    }

    uint16_t previousMovesCount = solver.getMovesCount();
    bool firstIteration = previousMovesCount== 0;
    solver.clearMaxDepth();

    Board fieldCopy = board;
    solver.solve(board.calculateCapturesAndTerritoriesColorsForDots());
    testAssert(fieldCopy.isEqualForTesting(board));

    cout << "Total moves count: " << solver.getMovesCount();
    if (!firstIteration) {
        cout << " (+" << solver.getMovesCount() - previousMovesCount << ")";
    }
    cout << ", max depth: " << solver.getMaxDepth();
    cout << ", cache hits: " << solver.getCacheHitsCount() << ", cache size: " << solver.getCacheSize();
    cout << '\n';

    std::unordered_set<Loc> blackWorkingLocs;
    std::unordered_set<Loc> whiteWorkingLocs;
    std::unordered_set<Loc> blackCapturedLocs;
    std::unordered_set<Loc> whiteCapturedLocs;

    for (int y = 0; y < board.y_size; y++) {
        for (int x = 0; x < board.x_size; x++) {
            const Loc loc = Location::getLoc(x, y, board.x_size);

            Color capturedColor = solver.getCapturedColor(loc);
            if (capturedColor & C_BLACK) {
                blackCapturedLocs.insert(loc);
            }
            if (capturedColor & C_WHITE) {
                whiteCapturedLocs.insert(loc);
            }

            Color workingLocColor = solver.getWorkingColor(loc);
            if (workingLocColor & C_BLACK) {
                blackWorkingLocs.insert(loc);
            }
            if (workingLocColor & C_WHITE) {
                whiteWorkingLocs.insert(loc);
            }
        }
    }

    if (solver.getStoreMovesTree()) {
        cout << "SGF: " << solver.toSgf() << '\n';
    }

    return generateBoardStateRepresentation(board, blackWorkingLocs, whiteWorkingLocs, blackCapturedLocs,
                                            whiteCapturedLocs);
}

static void checkLadders(
    const int width,
    const int height,
    const vector<Move>& moves,
    const unordered_set<Loc>& blackWorkingLocs,
    const unordered_set<Loc>& whiteWorkingLocs,
    const unordered_set<Loc>& blackCapturedLocs,
    const unordered_set<Loc>& whiteCapturedLocs,
    const uint16_t maxMovesCount = 65535U
) {
    Board field(width, height, Rules::DEFAULT_DOTS);
    for (const Move& move : moves) {
        field.playMove(move.loc, move.pla, true);
    }

    const string expectedLaddersInfo = generateBoardStateRepresentation(field,
     blackWorkingLocs,
     whiteWorkingLocs,
     blackCapturedLocs,
     whiteCapturedLocs
     );

    DotsLaddersSolver solver(field, false, moves, {}, maxMovesCount);

    const string actualFieldLadderInfo = playAndDumpLaddersInfo(field, XYMove::getNullMove(), solver);

    EXPECT_EQ_TRIMMED(expectedLaddersInfo, actualFieldLadderInfo);
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
            case '.': {
                bool markerEnd = false;
                while (!markerEnd) {
                    if (i < boardData.size() - 1) {
                        switch (const char nextChar = boardData[i + 1]) {
                            case '\n':
                                markerEnd = true;
                                break;
                            case ' ': // Empty loc
                                i++;
                                markerEnd = true;
                                break;
                            case FIRST_PLA_LOWER:
                            case SECOND_PLA_LOWER:
                                workingMoves.emplace_back(x, y, nextChar == FIRST_PLA_LOWER ? P_BLACK : P_WHITE);
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
                }
                x++;
                break;
            }
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

    const string actualFieldLaddersInfo = playAndDumpLaddersInfo(field, XYMove::getNullMove(), solver);

    EXPECT_EQ_TRIMMED(expectedFieldLaddersInfo, actualFieldLaddersInfo);
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

TEST(LadderTests, CapturingMoveInEmptyTerritory) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .
.  .  X  .  X  .  .  .
.  X  .' .  O  .  .  .
.  X  O' O' .x O  O  .
.  .  X  X  O  X  .  .
.  .  .  .  X  .  .  .
.  .  .  .  .  .  .  .
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
.  .  .o X  X  .  .  .  .  .
.  O  X' O  O  O  .  .  .  .
.  .  .  X' O  O  X  .  .  .
.  O  X' O  O  X  .  .  .  .
.  .  .o X  X  .  .  .  .  .
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

// TODO: Consider non-directly adjacent locs
TEST(LadderTests, DISABLED_ConsiderAdjCornerLoc) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .
.  .  .  X  X  X  X  .  .  .  .  .
.  .  X  O  O  .  O' X  .  .  .  .
.  .  X  O  .  .  .x .  .  .  .  .
.  .  X  O  .  .  .  .  .  .  .  .
.  .  X  O  .  .  .  .  .  .  .  .
.  .  .  X  .  .  .  .  .  X  .  .
.  .  .  .  .  .  .  .  .  .  .  .
.  .  .  .  .  .  .  .  .  .  .  .
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
    // Ladders wins with +1 black score (5 visible white dots - 4 captured black dots during ladder solving)
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
    // Ladders wins with 0 black score (4 visible white dots - 4 captured black dots during ladder solving)
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
)");
}

TEST(LadderTests, WorkingLadderWithInternalCapturingsAndLosingScore) {
    // Ladders wins with -3 black score (4 visible white dots - 6 captured black dots during ladder solving)
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
)");
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

TEST(LadderTests, IndirectlyAdjacentLocIsLadderButNotCapture) {
    checkLadders(
        R"(
.  .  .  .  .  .  .
.  .  .  .  .  .  .
.  .  .x O  .  X  .
.  X  O' .  X  .  .
.  X  O' O' X  .  .
.  .  X  X  .  .  .
.  .  .  .  .  .  .
)");
}

TEST(LadderTests, TwoColorsSimple) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O' O' X' X' O  .
.  .  .  .x .o .  .  .
.  X  .  .  .  .  O  .
.  .  .  .  .  .  .  .
)"
        );
}

TEST(LadderTests, IgnoreWorkingMovesWhenTheyHitCaptureLocations) {
    checkLadders(
        R"(
.  X  X  X  X  .
X  .' .' .' .' .x
X  O' .' .' .' .x
.  X  X  X  X  .
        )");

    checkLadders(
R"(
.  X  X  X  X  .
.x .' .' .' .' X
.x .' .' .' O' X
.  X  X  X  X  .
    )");

    checkLadders(
    R"(
.  .  .  .  .x .  .  .
.  X  X  X  .' X  X  .
X  .' .' .' .' O' .' X
X  O' .' .' .' .' .' X
.  X  X  X  .x X  X  .
        )");

    checkLadders(
R"(
.   X   X   .   O   O   .
X   .'  .'  .xo .'  X'  O
X   O'  .'  .xo .'  .'  O
.   X   X   .   O   O   .
        )");

    checkLadders(
R"(
.   X   X   O   O   O   .
X   .'  .o' .x' .'  X'  O
X   O'  .o' .x' .'  .'  O
.   X   X   O   O   O   .
)");

    checkLadders(
    R"(
.   .   .   .   .   .   .   .   .   .   .   .
.   .   .   .   .   .   .   .   .   .   .   .
.   .   .   O   O   O   X   X   X   .   .   .
.   .   O   .'  .'  X'  O'  .'  .'  X   .   .
.   .   .   O   .'  .'  O'  .'  X   .   .   .
.   .   .   O   .'  X'  .o' .'  X   .   .   .
.   .   .   O   .'  .'  .xo .'  X   .   .   .
.   .   .   .   O   O   .   X   .   .   .   .
.   .   .   .   .   .   .   .   .   .   .   .
.   .   .   .   .   .   .   .   .   .   .   .
)");

    checkLadders(
R"(
.  .  .  .  .x .x .  .  .  .
.  .  .  X  .' .' X  .  .  .
.  .  X  .' .' .' .' X  .  .
.  X  .' .' .' .' .' .' X  .
.  X  .' X' O' O' X' .' X  .
.  X  .' .' X' X' .' .' X  .
.  .  X  .' .' .' .' X  .  .
.  .  .  X  X  X  X  .  .  .
)");

    checkLadders(
R"(
.  .  .  X  X  X  X  .  .  .
.  .  X  .' .' .' .' X  .  .
.  X  .' .' .' .' .' .' X  .
.  X  .' X' O' O' X' .' X  .
.  X  .' .' X' X' .' .' X  .
.  .  X  .' .' .' .' X  .  .
.  .  .  X  .' .' X  .  .  .
.  .  .  .  .x .x .  .  .  .
)");
}

TEST(LadderTests, IgnoreLaddersUnderOneMoveCapturing) {
    checkLadders(
        R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  X  X  .  X  .  .  .
.  .  X  .  .  .  .  X  .  .
.  X  .  .  .  .  .  .  X  .
.  X  .  X  O  O  X  .  X  .
.  X  .  .  X  X  .  .  X  .
.  .  X  .  .  .  .  X  .  .
.  .  .  X  X  X  X  .  .  .
.  .  .  .  .  .  .  .  .  .
)");

    checkLadders(
    R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  X  X  X  X  .  .  .
.  .  X  .  .  .  .  X  .  .
.  X  .  .  .  .  .  .  X  .
.  X  .  X  O  O  X  .  X  .
.  X  .  .  X  X  .  .  X  .
.  .  X  .  .  .  .  X  .  .
.  .  .  X  X  .  X  .  .  .
.  .  .  .  .  .  .  .  .  .
)");

    checkLadders(
    R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  O  O  .  O  .  .  .
.  .  O  .  .  .  .  O  .  .
.  O  .  .  .  .  .  .  O  .
.  O  .  X  O  O  X  .  O  .
.  O  .  .  X  X  .  .  O  .
.  .  O  .  .  .  .  O  .  .
.  .  .  O  O  O  O  .  .  .
.  .  .  .  .  .  .  .  .  .
)");

    checkLadders(
    R"(
.  .  .  .  .  .  .  .  .  .
.  .  .  O  O  O  O  .  .  .
.  .  O  .  .  .  .  O  .  .
.  O  .  .  .  .  .  .  O  .
.  O  .  X  O  O  X  .  O  .
.  O  .  .  X  X  .  .  O  .
.  .  O  .  .  .  .  O  .  .
.  .  .  O  O  .  O  .  .  .
.  .  .  .  .  .  .  .  .  .
)");
}

TEST(LadderTests, LadderWhenCaptureEmptyBaseIsEnabled) {
    checkLadders(
        R"(
.  .x .x .
X  .' .' X
.  X  X  .
)", true);

    checkLadders(
    R"(
.  .  .  .  .
.  .x O  .  X
X  .' .  X  .
.  X  X  .  .
)", true);
}

// Check the following pattern scaled to max len:
//      0  1  2  3  4  .....  MAX
//   0  .  X  X  .  .      .  .
//   1  X  O' O' X  .      .  .
//   2  .  .x .  .  .      .  .
//                    ...
//      .  .  .  .  .      .  .
// MAX  .  .  .  .  .      X  .
TEST(LadderTests, StressSimple) {
    constexpr int width = Board::MAX_LEN_X;
    constexpr int height = Board::MAX_LEN_Y;

    cout << "Size: " << width << " x " << height << '\n';
    // Ladder start
    vector moves = {
        Move(Location::getLoc(0, 1, width), P_BLACK),
        Move(Location::getLoc(1, 1, width), P_WHITE),
        Move(Location::getLoc(1, 0, width), P_BLACK),
        Move(Location::getLoc(2, 1, width), P_WHITE),
        Move(Location::getLoc(2, 0, width), P_BLACK),
        Move(Location::getLoc(3, 1, width), P_BLACK),
    };

    cout << "Check escaping:" << '\n';
    checkLadders(width, height, moves, {}, {}, {}, {});

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
    moves.emplace_back(Location::getLoc(farMoveX, farMoveY, width), P_BLACK);

    cout << "Check capturing:" << '\n';
    checkLadders(width, height, moves,
{Location::getLoc(1, 2, width)},
{},
{Location::getLoc(1, 1, width), Location::getLoc(2, 1, width)},
{}
    );
}

static void stressHugeNumberOfTurns(const int width, const int maxMovesCount, const bool shouldHitTurnsLimit) {
    constexpr int height = Board::MAX_LEN_Y;

    cout << "Size: " << width << " x " << height << '\n';
    vector<Move> moves;

    unordered_set<Loc> expectedBlackCapturedLocs;

    for (int x = 0; x < width; x++) {
        if (x > 0 && x < width - 2) {
            moves.emplace_back(Location::getLoc(x, 0, width), P_BLACK);
        }

        Color secondLineColor;
        if (x > 0 && x < width - 2) {
            secondLineColor = P_WHITE;
        } else if (x == 0 || x == width - 2) {
            secondLineColor = P_BLACK;
        } else {
            secondLineColor = C_EMPTY;
        }
        if (secondLineColor != C_EMPTY) {
            moves.emplace_back(Location::getLoc(x, 1, width), secondLineColor);
            if (secondLineColor == P_WHITE && !shouldHitTurnsLimit) {
                expectedBlackCapturedLocs.insert(Location::getLoc(x, 1, width));
            }
        }

        Color thirdLineColor;
        if (x < width - 2) {
            thirdLineColor = x % 2 == 0 ? P_BLACK : P_WHITE;
        } else if (x == width - 2) {
            thirdLineColor = C_EMPTY;
        } else {
            thirdLineColor = P_BLACK;
        }
        if (thirdLineColor != C_EMPTY) {
            moves.emplace_back(Location::getLoc(x, 2, width), thirdLineColor);
            if (thirdLineColor == P_WHITE && !shouldHitTurnsLimit) {
                expectedBlackCapturedLocs.insert(Location::getLoc(x, 2, width));
            }
        }

        if (x < width - 3 && x % 2 == 1) {
            moves.emplace_back(Location::getLoc(x, 3, width), P_BLACK);
        }
    }

    unordered_set<Loc> expectedBlackWorkingLocs;
    if (!shouldHitTurnsLimit) {
        expectedBlackWorkingLocs.insert(Location::getLoc(width - 3, 3, width));
    }

    cout << "Check " << (shouldHitTurnsLimit ? "turns limit" : "capturing") << ":\n";
    checkLadders(
        width,
        height,
        moves,
        expectedBlackWorkingLocs,
        {},
        expectedBlackCapturedLocs,
        {},
        maxMovesCount
    );
}

// Check the following pattern scaled to max len:
//      0  1  2  3                   MAX
//   0  .  X  X  X  ...  X  X  X  .  .
//   1  X  O  O  O  ...  O  O  O  X  .
//   2  X  O  X  O  ...  O  X  O  .  X
//   3  .  X  .  X  ...  X  .  .  .  .
//   4  .  .  .  .  ...  .  .  .  .  .
//                  ...
// MAX  .  .  .  .  ...  .  .  .  .  .
//
// That's ended up with big triangular capture if turns limit isn't hit.
TEST(LadderTests, StressHitsMaxTurnsLimit) {
    stressHugeNumberOfTurns(10, 100, true);
    stressHugeNumberOfTurns(10, 10000, false);
    stressHugeNumberOfTurns(30, std::numeric_limits<uint16_t>::max(), true);
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