#include "../game/boardhistory.h"

using namespace std;

int BoardHistory::countDotsScoreWhiteMinusBlack(const Board& board, Color area[Board::MAX_ARR_SIZE]) {
  return board.calculateOwnershipAndWhiteScore(area, C_EMPTY);
}

bool BoardHistory::isGroundReasonable(const Board& board) const {
  return !std::isnan(whiteScoreIfGroundingAlive(board));
}

bool BoardHistory::isResignReasonable(const Board& board, const Player pla) const {
  const float whiteScore = whiteScoreIfGroundingAlive(board);
  return (pla == P_BLACK && whiteScore > 0.0f) || (pla == P_WHITE && whiteScore < 0.0f);
}

bool BoardHistory::isNotCapturingGroundingAlive(const Board& board, const Player pla) const {
  return !std::isnan(whiteScoreIfNotCapturingGroundingAlive(board, pla));
}

float BoardHistory::whiteScoreIfGroundingAlive(const Board& board) const {
  return whiteScoreIfGroundingAlive(board, C_EMPTY);
}

float BoardHistory::whiteScoreIfAllDotsAreGrounded(const Board& board) const {
  return whiteScoreIfGroundingAlive(board, C_WALL);
}

float BoardHistory::whiteScoreIfNotCapturingGroundingAlive(const Board& board, const Player pla) const {
  return whiteScoreIfGroundingAlive(board, pla);
}

float BoardHistory::whiteScoreIfGroundingAlive(const Board& board, const Color groundColor, const Board::CapturesAndTerritoriesInfos* capturesAndTerritoriesInfos) const {
  if (!rules.isDots) return std::numeric_limits<float>::quiet_NaN();

  const auto completeWhiteBonus = getCompleteWhiteBonus();

  const int blackWhiteCapturesDiff = board.numBlackCaptures - board.numWhiteCaptures;

  int normBlackScoreIfWhiteGrounds = board.blackScoreIfWhiteGrounds;
  int normWhiteScoreIfBlackGrounds = board.whiteScoreIfBlackGrounds;

  if (capturesAndTerritoriesInfos != nullptr) {
    handleEffectivelyGroundedEmptyBases(board, capturesAndTerritoriesInfos, normBlackScoreIfWhiteGrounds, normWhiteScoreIfBlackGrounds);
  }

  if (normBlackScoreIfWhiteGrounds == -normWhiteScoreIfBlackGrounds) {
    // All dots are grounded -> draw or win by extra bonus
    assert(normWhiteScoreIfBlackGrounds == blackWhiteCapturesDiff);
    return static_cast<float>(blackWhiteCapturesDiff) + completeWhiteBonus;
  }

  // In case of non-capturing grounding, the winner still can ground if only all its dots are grounded (ungrounded opp dots don't matter)
  if (const float fullWhiteScoreIfBlackGrounds = static_cast<float>(normWhiteScoreIfBlackGrounds) + completeWhiteBonus;
     fullWhiteScoreIfBlackGrounds < 0.0F) {
    // Black already won the game by grounding considering white extra bonus
    if (groundColor == C_EMPTY || (blackWhiteCapturesDiff == normWhiteScoreIfBlackGrounds && groundColor == P_BLACK)) {
      return fullWhiteScoreIfBlackGrounds;
    }
  } else if (const float fullBlackScoreIfWhiteGrounds = static_cast<float>(normBlackScoreIfWhiteGrounds) - completeWhiteBonus;
     fullBlackScoreIfWhiteGrounds < 0.0F) {
    // White already won the game by grounding considering white extra bonus
    if (groundColor == C_EMPTY || (-blackWhiteCapturesDiff == normBlackScoreIfWhiteGrounds && groundColor == P_WHITE)) {
      return -fullBlackScoreIfWhiteGrounds;
    }
  }

  return std::numeric_limits<float>::quiet_NaN();
}

void BoardHistory::handleEffectivelyGroundedEmptyBases(const Board& board, const Board::CapturesAndTerritoriesInfos* capturesAndTerritoriesInfos, int& normBlackScoreIfWhiteGrounds, int& normWhiteScoreIfBlackGrounds) {
  const int x_size = board.x_size;
  const int y_size = board.y_size;

  vector<char> marked((x_size + 1) * (y_size + 1));

  // Firstly, filter out empty bases that can be broken
  // Store this info into marked vector (the `true` values prevent the wave from being propagated)
  for (const auto* baseInfo : capturesAndTerritoriesInfos->getBaseInfos()) {
    // Only normal bases can break empty territories
    if (baseInfo->type != Board::Base::Type::NORMAL) {
      continue;
    }

    const Loc captureLoc = baseInfo->captureLoc;
    assert(captureLoc != Board::NULL_LOC);

    // Make sure the capture location is inside an empty territory
    const Color emptyTerritoryColorAtLoc = getEmptyTerritoryColor(board.getState(captureLoc));
    if (emptyTerritoryColorAtLoc == C_EMPTY) {
      continue;
    }

    // Find and handle empty territories adjacent to the capture location
    marked[captureLoc] = true;
    board.forEachAdjacent(captureLoc, [&](const Loc adjLoc) {
      const auto* capturesAndTerritoriesInfoAtAdjLoc = capturesAndTerritoriesInfos->at(adjLoc);
      if (capturesAndTerritoriesInfoAtAdjLoc == nullptr) {
        return;
      }

      const auto* emptyBaseInfo = capturesAndTerritoriesInfoAtAdjLoc->getZeroMoveEmptyBaseInfo();
      if (emptyBaseInfo == nullptr) {
        return;
      }

      assert(emptyBaseInfo->player == emptyTerritoryColorAtLoc);

      for (const Loc territoryLoc : emptyBaseInfo->territory) {
        marked[territoryLoc] = true;
      }
    });
  }

  vector<Move> groundedViaEmptyBasesDots;
  vector<Loc> walkStack;

  // Secondly, collect dots that form empty bases and can't be surrounded (considering the bases that can be broken using marked vector)
  for (int y = 0; y < y_size; y++) {
    for (int x = 0; x < x_size; x++) {
      const Loc loc = Location::getLoc(x, y, x_size);

      if (const State state = board.getState(loc); isGrounded(state)) {
        assert(!marked[loc]);
        const Color activeColor = getActiveColor(state);
        assert(activeColor == C_BLACK || activeColor == C_WHITE);

        walkStack.clear();
        walkStack.push_back(loc);
        while (!walkStack.empty()) {
          const Loc currentLoc = walkStack.back();
          walkStack.pop_back();

          board.forEachAdjacent(currentLoc, [&](const Loc adjLoc) {
            if (const State adjState = board.getState(adjLoc);
              !isGrounded(adjState) &&
              // The wave can propagate both through empty territory and active dots
              (getActiveColor(adjState) == activeColor || getEmptyTerritoryColor(adjState) == activeColor) &&
              !marked[adjLoc]
            ) {
              walkStack.push_back(adjLoc);
              marked[adjLoc] = true;
              // The only placed dots can affect score
              if (getPlacedDotColor(adjState) != C_EMPTY) {
                groundedViaEmptyBasesDots.emplace_back(adjLoc, activeColor);
              }
            }
          });
        }
      }
    }
  }

  // Finally, normalize the score in case of grounding considering the dots collected above
  for (const Move& move : groundedViaEmptyBasesDots) {
    const auto* capturesAndTerritoriesInfo = capturesAndTerritoriesInfos->at(move.loc);
    if (const Player player = move.pla; capturesAndTerritoriesInfo == nullptr || !capturesAndTerritoriesInfo->hasAnyTerritory(getOpp(player))) {
      if (player == P_BLACK) {
        normWhiteScoreIfBlackGrounds--;
      } else {
        normBlackScoreIfWhiteGrounds--;
      }
    }
  }
}

bool BoardHistory::isReasonableForDots(const Loc loc, const Board& board, const Color currentPla) {
  assert(loc != Board::PASS_LOC);
  return isReasonableForDots(Location::getX(loc, board.x_size), Location::getY(loc, board.x_size), loc, board, currentPla, nullptr);
}

bool BoardHistory::isReasonableForDots(
  const int x,
  const int y,
  const Loc loc,
  const Board& board,
  const Color currentPla,
  const Board::CapturesAndTerritoriesInfos* capturesAndTerritoriesInfos) {

  if((x == 0 || x == board.x_size - 1) && (y == 0 || y == board.y_size - 1)) {
    // Drop corner locs because they are never beneficial
    return false;
  }

  // Drop locs with already placed (or just surrounded) dots
  if(board.getColor(loc) != C_EMPTY)
    return false;

  // Check reasonability that depends on capture/territory status.
  // For instance, it doesn't make sense to play under atari if this loc can be captured by the next opp move.
  // Also, always choose territories with max square.
  if (capturesAndTerritoriesInfos) {
    if (const auto* captureAndTerritoryInfo = capturesAndTerritoriesInfos->at(loc);
       captureAndTerritoryInfo && !captureAndTerritoryInfo->isReasonableMove(currentPla)) {
      return false;
    }
  }

  return true;
}