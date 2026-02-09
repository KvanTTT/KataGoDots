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

float BoardHistory::whiteScoreIfGroundingAlive(const Board& board, const Color groundColor) const {
  assert(rules.isDots);

  const auto extraWhiteScore = whiteBonusScore + whiteHandicapBonusScore + rules.komi;

  const int blackWhiteCapturesDiff = board.numBlackCaptures - board.numWhiteCaptures;

  if (board.blackScoreIfWhiteGrounds == -board.whiteScoreIfBlackGrounds) {
    // All dots are grounded -> draw or win by extra bonus
    assert(board.whiteScoreIfBlackGrounds == blackWhiteCapturesDiff);
    return static_cast<float>(blackWhiteCapturesDiff) + extraWhiteScore;
  }

  // In case of non-capturing grounding, the winner still can ground if only all its dots are grounded (ungrounded opp dots don't matter)
  if (const float fullWhiteScoreIfBlackGrounds = static_cast<float>(board.whiteScoreIfBlackGrounds) + extraWhiteScore;
     fullWhiteScoreIfBlackGrounds < 0.0F) {
    // Black already won the game by grounding considering white extra bonus
    if (groundColor == C_EMPTY || (blackWhiteCapturesDiff == board.whiteScoreIfBlackGrounds && groundColor == P_BLACK)) {
      return fullWhiteScoreIfBlackGrounds;
    }
  } else if (const float fullBlackScoreIfWhiteGrounds = static_cast<float>(board.blackScoreIfWhiteGrounds) - extraWhiteScore;
     fullBlackScoreIfWhiteGrounds < 0.0F) {
    // White already won the game by grounding considering white extra bonus
    if (groundColor == C_EMPTY || (-blackWhiteCapturesDiff == board.blackScoreIfWhiteGrounds && groundColor == P_WHITE)) {
      return -fullBlackScoreIfWhiteGrounds;
    }
  }

  return std::numeric_limits<float>::quiet_NaN();
}