#include "../game/boardhistory.h"

using namespace std;

int BoardHistory::countDotsScoreWhiteMinusBlack(const Board& board, Color area[Board::MAX_ARR_SIZE]) const {
  assert(rules.isDots);
  return board.calculateOwnershipAndWhiteScore(area, C_EMPTY);
}

bool BoardHistory::winOrEffectiveDrawByGrounding(const Board& board, const Player pla, const bool considerDraw) const {
  assert(rules.isDots);

  const float whiteScore = whiteScoreIfGroundingAlive(board);

  return (considerDraw && Global::isZero(whiteScore)) ||
    (pla == P_WHITE && whiteScore > 0.0f) ||
      (pla == P_BLACK && whiteScore < 0.0f);
}

float BoardHistory::whiteScoreIfGroundingAlive(const Board& board, const bool noGroundingCaptures) const {
  assert(rules.isDots);

  const auto extraWhiteScore = whiteBonusScore + whiteHandicapBonusScore + rules.komi;

  if (const float fullWhiteScoreIfBlackGrounds = static_cast<float>(board.whiteScoreIfBlackGrounds) + extraWhiteScore;
     fullWhiteScoreIfBlackGrounds <= 0.0f) {
    // Black already won the game or make a draw by grounding considering white bonus
    if (!noGroundingCaptures && fullWhiteScoreIfBlackGrounds < 0.0f ||
      board.numBlackCaptures - board.numWhiteCaptures == board.whiteScoreIfBlackGrounds && board.blackScoreIfWhiteGrounds + board.whiteScoreIfBlackGrounds == 0)
      return fullWhiteScoreIfBlackGrounds;
    return std::numeric_limits<float>::quiet_NaN();
  }

  if (const float fullBlackScoreIfWhiteGrounds = static_cast<float>(board.blackScoreIfWhiteGrounds) - extraWhiteScore;
     fullBlackScoreIfWhiteGrounds <= 0.0f) {
    // White already won the game or make a draw by grounding considering white bonus
    if (!noGroundingCaptures && fullBlackScoreIfWhiteGrounds < 0.0f ||
      board.numWhiteCaptures - board.numBlackCaptures == board.blackScoreIfWhiteGrounds && board.blackScoreIfWhiteGrounds + board.whiteScoreIfBlackGrounds == 0)
      return -fullBlackScoreIfWhiteGrounds;
    return std::numeric_limits<float>::quiet_NaN();
  }

  return std::numeric_limits<float>::quiet_NaN();
}