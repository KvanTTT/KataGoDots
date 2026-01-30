#ifndef GAME_COMMON_H
#define GAME_COMMON_H
#include <cstdint>
#include "../core/global.h"

const std::string DOTS_KEY = "dots";
const std::string DATA_LEN_X_KEY = "dataBoardLenX";
const std::string DATA_LEN_Y_KEY = "dataBoardLenY";
const std::string DOTS_CAPTURE_EMPTY_BASE_KEY = "dotsCaptureEmptyBase";
const std::string DOTS_CAPTURE_EMPTY_BASES_KEY = "dotsCaptureEmptyBases";
const std::string START_POS_KEY = "startPos";
const std::string START_POS_RANDOM_KEY = "startPosIsRandom";
const std::string START_POSES_KEY = "startPoses";
const std::string START_POSES_ARE_RANDOM_KEY = "startPosesAreRandom";
const std::string BOARD_SIZES_KEY = "bSizes";
const std::string BOARD_SIZES_XY_KEY = "bSizesXY";
const std::string DOTS_NOTAGO_STANDARD_RULES = "notago";
const std::string DOTS_BBS_STANDARD_RULES = "bbs";

const std::string KO_RULES_KEY = "koRules";
const std::string SCORING_RULES_KEY = "scoringRules";
const std::string TAX_RULES_KEY = "taxRules";
const std::string HAS_BUTTONS_KEY = "hasButtons";
const std::string FANCY_KOMI_VARYING_KEY = "fancyKomiVarying";
const std::string KOMI_BIG_STD_DEV_PROB_KEY = "komiBigStdevProb";
const std::string KOMI_BIG_STD_DEV_KEY = "komiBigStdev";
const std::string KOMI_BIGGER_STD_DEV_PROB_KEY = "komiBiggerStdevProb";
const std::string KOMI_BIGGER_STD_DEV_KEY = "komiBiggerStdev";

const std::string NO_RESULT_STDEV_KEY = "noResultStdev";
const std::string SEKI_FORK_HACK_PROB_KEY = "sekiForkHackProb";

const std::string BLACK_SCORE_IF_WHITE_GROUNDS_KEY = "blackScoreIfWhiteGrounds";
const std::string WHITE_SCORE_IF_BLACK_GROUNDS_KEY = "whiteScoreIfBlackGrounds";

const std::vector GO_ONLY_KEYS = {
  KO_RULES_KEY,
  SCORING_RULES_KEY,
  TAX_RULES_KEY,
  HAS_BUTTONS_KEY,

  FANCY_KOMI_VARYING_KEY,
  KOMI_BIG_STD_DEV_PROB_KEY,
  KOMI_BIG_STD_DEV_KEY,
  KOMI_BIGGER_STD_DEV_PROB_KEY,
  KOMI_BIGGER_STD_DEV_KEY,

  NO_RESULT_STDEV_KEY,
  SEKI_FORK_HACK_PROB_KEY,
};

const std::vector DOTS_ONLY_KEYS = {
  DOTS_CAPTURE_EMPTY_BASES_KEY
};

const std::string PLAYER1 = "Player1";
const std::string PLAYER2 = "Player2";
const std::string PLAYER1_SHORT = "P1";
const std::string PLAYER2_SHORT = "P2";

const std::string RESIGN_STR = "resign";

// Player
typedef int8_t Player;
static constexpr Player P_BLACK = 1;
static constexpr Player P_WHITE = 2;

//Color of a point on the board
typedef int8_t Color;
static constexpr Color C_EMPTY = 0;
static constexpr Color C_BLACK = 1;
static constexpr Color C_WHITE = 2;
static constexpr Color C_WALL = 3;
static constexpr int NUM_BOARD_COLORS = 4;

typedef int8_t State;

//Location of a point on the board
//(x,y) is represented as (x+1) + (y+1)*(x_size+1)
typedef short Loc;

//Simple structure for storing moves. This is a convenient place to define it.
STRUCT_NAMED_PAIR(Loc,loc,Player,pla,Move);

inline bool movesEqual(const Move& m1, const Move& m2) {
  return m1.loc == m2.loc && m1.pla == m2.pla;
}

#endif
