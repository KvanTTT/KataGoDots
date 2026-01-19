#include "../neuralnet/nninputs.h"

using namespace std;

void NNInputs::fillRowV7Dots(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN_X);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN_Y);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill_n(rowBin, NUM_FEATURES_SPATIAL_V7_DOTS * nnXLen * nnYLen,false);
  std::fill_n(rowGlobal, NUM_FEATURES_GLOBAL_V7_DOTS, 0.0f);

  const Player pla = nextPlayer;
  const Player opp = getOpp(pla);
  const int xSize = board.x_size;
  const int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    featureStride = 1;
    posStride = NUM_FEATURES_SPATIAL_V7_DOTS;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  const Rules& rules = hist.rules;

  vector<Color> captures;
  vector<Color> bases;
  board.calculateOneMoveCaptureAndBasePositionsForDots(captures, bases);
  [[maybe_unused]] int deadDotsCount = 0;

  auto setSpatial = [&](const int pos, const DotsSpatialFeature spatialFeature) {
    setRowBin(rowBin, pos, static_cast<int>(spatialFeature), 1.0f, posStride, featureStride);
  };

  auto setGlobal = [&](const DotsGlobalFeature globalFeature, const float value = 1.0f) {
    rowGlobal[static_cast<int>(globalFeature)] = value;
  };

  bool hasLegalNonGroundMoves = false;

  for(int y = 0; y<ySize; y++) {
    for(int x = 0; x<xSize; x++) {
      const int pos = NNPos::xyToPos(x,y,nnXLen);
      const Loc loc = Location::getLoc(x,y,xSize);

      setSpatial(pos, DotsSpatialFeature::OnBoard_0);

      const State state = board.getState(loc);
      const Color activeColor = getActiveColor(state);
      const Color placedColor = getPlacedDotColor(state);

      hasLegalNonGroundMoves = hasLegalNonGroundMoves ||
        (activeColor == C_EMPTY && !board.isIllegalSuicide(loc, nextPlayer, rules.multiStoneSuicideLegal));

      if (activeColor == pla)
        setSpatial(pos, DotsSpatialFeature::PlayerActive_1);
      else if (activeColor == opp)
        setSpatial(pos, DotsSpatialFeature::PlayerOppActive_2);
      else
        assert(C_EMPTY == activeColor);

      if (placedColor == pla)
        setSpatial(pos, DotsSpatialFeature::PlayerPlaced_3);
      else if (placedColor == opp)
        setSpatial(pos, DotsSpatialFeature::PlayerOppPlaced_4);
      else
        assert(C_EMPTY == placedColor);

      if (activeColor != C_EMPTY && placedColor != C_EMPTY && placedColor != activeColor) {
        // Needed for more correct score calculation, but probably it's redundant considering placed dots
        setSpatial(pos, DotsSpatialFeature::DeadDots_5);
        deadDotsCount++;
      }

      if (isGrounded(state)) {
        setSpatial(pos, DotsSpatialFeature::Grounded_8);
      }

      const Color captureColor = captures[loc];
      if ((pla & captureColor) != 0) {
        setSpatial(pos, DotsSpatialFeature::PlayerCaptures_18);
      }
      if ((opp & captureColor) != 0) {
        setSpatial(pos, DotsSpatialFeature::PlayerOppCaptures_19);
      }

      const Color baseColor = bases[loc];
      if ((pla & baseColor) != 0) {
        setSpatial(pos, DotsSpatialFeature::PlayerSurroundings_20);
      }
      if ((opp & baseColor) != 0) {
        setSpatial(pos, DotsSpatialFeature::PlayerOppSurroundings_21);
      }

      // TODO: Set up history and ladder features, https://github.com/KvanTTT/KataGoDots/issues/3
    }
  }

  assert(deadDotsCount == board.numBlackCaptures + board.numWhiteCaptures);

  int maxTurnsOfHistoryToInclude = 5;
  const vector<Move>& moveHistory = hist.moveHistory;
  if (!hasLegalNonGroundMoves) {
    // Don't include history for non-resultative games:
    //   * There are no legal non-ground moves to play
    //   * The number of moves exceeds the max limit (not yet implemented).
    //     Although, it shouldn't happen in any Dots game (grounding should happen before).
    maxTurnsOfHistoryToInclude = 0;
  }
  const int amountOfHistoryToTryToUse = std::min(maxTurnsOfHistoryToInclude, hist.numApproxValidTurnsThisPhase);
  const int moveHistoryLen = static_cast<int>(moveHistory.size());
  assert(moveHistoryLen >= hist.numApproxValidTurnsThisPhase);

  int numTurnsOfHistoryIncluded = 0; // TODO: it will be used for ladders, https://github.com/KvanTTT/KataGoDots/issues/3
  Player currentPla = opp;
  for (int i = 0; i < amountOfHistoryToTryToUse; i++) {
    const int index = moveHistoryLen - i - 1;
    if (index < 0) break;

    const Move& prevMove = moveHistory[index];

    // History is only actual for strict moves order (colors should be alternating)
    if (prevMove.pla != currentPla) break;

    const Loc prevLoc = prevMove.loc;

    // Grounding is not expected since it's always an ending move.
#ifdef NDEBUG
    // Check for null and ground move just in case
    if (prevLoc == Board::NULL_LOC || prevLoc == Board::PASS_LOC) continue;
#else
    assert(prevLoc != Board::NULL_LOC && prevLoc != Board::PASS_LOC);
#endif

    numTurnsOfHistoryIncluded++;

    const int histPos = NNPos::locToPos(prevLoc, xSize, nnXLen, nnYLen);
    setRowBin(rowBin, histPos, static_cast<int>(DotsSpatialFeature::Prev1Loc_9) + i, 1.0f, posStride, featureStride);

    currentPla = getOpp(currentPla);
  }

  //Komi and any score adjustments
  float selfKomi = hist.currentSelfKomi(nextPlayer,nnInputParams.drawEquivalentWinsForWhite);
  const auto bArea = static_cast<float>(xSize * ySize);
  //Bound komi just in case
  if(selfKomi > bArea+NNPos::KOMI_CLIP_RADIUS)
    selfKomi = bArea+NNPos::KOMI_CLIP_RADIUS;
  if(selfKomi < -bArea-NNPos::KOMI_CLIP_RADIUS)
    selfKomi = -bArea-NNPos::KOMI_CLIP_RADIUS;
  setGlobal(DotsGlobalFeature::Komi_5, selfKomi / NNPos::KOMI_CLIP_RADIUS);

  if (rules.multiStoneSuicideLegal) {
    setGlobal(DotsGlobalFeature::Suicide_8);
  }

  if (rules.dotsCaptureEmptyBases) {
    setGlobal(DotsGlobalFeature::CaptureEmpty_17);
  }

  if (hist.winOrEffectiveDrawByGrounding(board, pla)) {
    // Train to better understand grounding
    setGlobal(DotsGlobalFeature::WinByGrounding_14);
  }

  setGlobal(DotsGlobalFeature::FieldSizeKomiParity_18, 0.0f); // TODO: implement later
}