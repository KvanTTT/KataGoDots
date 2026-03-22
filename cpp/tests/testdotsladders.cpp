#include "../tests/tests.h"
#include "testdotsutils.h"

#include "../game/graphhash.h"
#include "../program/playutils.h"

using namespace std;
using namespace TestCommon;

static void checkLadder(
  const string& title,
  const string& boardData,
  vector<XYMove> extraMoves,
  const std::optional<string>& expectedDeadAndWorkingDots= std::nullopt,
  const std::optional<string>& expectedDeadDotsMoveAgo = std::nullopt,
  const std::optional<string>& expectedDeadDotsTwoMovesAgo = std::nullopt
) {
    Board dostField = parseDotsFieldDefault(boardData, extraMoves);
    Player nextPlayer = extraMoves.empty() ? P_BLACK : getOpp(extraMoves.back().player);


}

void Tests::runDotsLaddersTests() {
  checkLadder(
    "Basic",
    R"(
........
..xxoo..
.xooxxo.
........
.x....o.
........)",
    {XYMove(3, 3, P_BLACK), XYMove(2, 3, P_WHITE)},
    R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O' O' X  X  O  .
.  .  O' X' .  .  .  .
.  X  .! .  .  .  O  .
.  .  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O' O' X  X  O  .
.  .  .  X  .  .  .  .
.  X  .  .  .  .  O  .
.  .  .  .  .  .  .  .
)",
    R"(
.  .  .  .  .  .  .  .
.  .  X  X  O  O  .  .
.  X  O' O' X' X' O  .
.  .  .  .  .  .  .  .
.  X  .  .  .  .  O  .
.  .  .  .  .  .  .  .
)"
  );
}