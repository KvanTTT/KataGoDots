#include "board.h"

using namespace std;

vector<const Board::LadderMoveInfo*> Board::iterDotsLadders(LaddersInfo& laddersInfo) {
  vector<const LadderMoveInfo*> result;

  if (is_finished) {
    return result; // Ladders are not relevant when the game is finished (by grounding or resignation)
  }

  for (int y = 0; y < y_size; y++) {
    for (int x = 0; x < x_size; x++) {
      const Loc loc = Location::getLoc(x, y, x_size);

      const State state = getState(loc);
      // Optimization: perform fast check to avoid more heavy checks and extra allocations.
      // The most frequent case is the case when the location is empty.
      if (const Color activeColor = getActiveColor(state); activeColor != C_EMPTY) continue;
      const Color emptyTerritoryColor = getEmptyTerritoryColor(state);
      const Color colorsOfPotentialCapturing = getColorsOfPotentialCapturing(loc, 1);

      const auto addLadderMoveInfo = [&](const Player pla) {
        if (!(colorsOfPotentialCapturing & pla) || emptyTerritoryColor == pla || (emptyTerritoryColor != C_EMPTY && !wouldBeCaptureDots(loc, pla))) {
          return;
        }

        if (const LadderMoveInfo* ladderMoveInfo = ladderStart(loc, pla, laddersInfo); ladderMoveInfo->isLadder(pla)) {
          assert(ladderMoveInfo->workingMove != NULL_LOC);

          result.push_back(ladderMoveInfo);
        }
      };

      addLadderMoveInfo(P_BLACK);
      //addLadderMoveInfo(P_WHITE); TODO: uncomment after tests become robust
    }
  }

  return result;
}

const Board::LadderMoveInfo* Board::ladderStart(const Loc initLoc, const Player pla, LaddersInfo& cache) {
  unordered_set<Loc> oppInitCaptures;

  return ladderIterPla(initLoc, initLoc, pla, oppInitCaptures, cache);
}

const Board::LadderMoveInfo* Board::ladderIterPla(const Loc initLoc, const Loc loc,
                                                  const Player pla,
                                                  unordered_set<Loc>& oppInitCaptures,
                                                  LaddersInfo& laddersInfo) {
  const MoveRecord moveRecordForLoc = laddersInfo.play(loc, pla);

  if (const LadderMoveInfo* calculatedResult = laddersInfo.get(pos_hash, pla); calculatedResult != nullptr) {
    auto* result = calculatedResult->type == LadderMoveInfo::LADDER && calculatedResult->workingMove != loc
      ? laddersInfo.put(pos_hash, LadderMoveInfo::createLadder(loc, *calculatedResult))
      : calculatedResult;
    laddersInfo.undo(moveRecordForLoc);
    return result;
  }

  if (ladderIsRelevantCapturing(moveRecordForLoc, initLoc, pla, laddersInfo.getCurrentDepth(), oppInitCaptures)) {
    const LadderMoveInfo* result = laddersInfo.put(pos_hash, LadderMoveInfo::createCapture(moveRecordForLoc.bases));
    laddersInfo.undo(moveRecordForLoc);
    return result;
  }

  vector<Loc> plaChainNewLocs;
  vector<Loc> plaChainMainCaptureLocs;
  const auto maybeCaptureLocs = laddersInfo.extendChain(loc, pla, plaChainNewLocs, plaChainMainCaptureLocs);

  auto worstResult = LadderMoveInfo::createEmpty(pla);

  for (const Loc maybeCaptureLoc : maybeCaptureLocs) {
    if (!laddersInfo.maybeChainCaptureLoc(maybeCaptureLoc, pla)) {
      continue;
    }

    const MoveRecord secondMoveRecord = laddersInfo.play(maybeCaptureLoc, pla);
    bool ladderCapturingIsFound = ladderIsRelevantCapturing(secondMoveRecord, initLoc, pla, laddersInfo.getCurrentDepth(), oppInitCaptures);
    laddersInfo.undo(secondMoveRecord);

    if (ladderCapturingIsFound) {
      if (const LadderMoveInfo* adjLocLadderMoveInfo = ladderIterOpp(initLoc, maybeCaptureLoc, loc, pla, oppInitCaptures, laddersInfo);
          adjLocLadderMoveInfo != nullptr &&
          adjLocLadderMoveInfo->isLadderOrCapture(pla)
      ) {
        // TODO: implement more robust comparison
        if (worstResult.type == LadderMoveInfo::EMPTY || adjLocLadderMoveInfo->territoryLocs.size() < worstResult.territoryLocs.size()) {
          if (adjLocLadderMoveInfo->isLadder(pla)) {
            worstResult = LadderMoveInfo::createLadder(loc, secondMoveRecord.bases);
          } else {
            worstResult = LadderMoveInfo::createLadder(loc, *adjLocLadderMoveInfo);
          }
        }
      }
    }
  }

  const LadderMoveInfo* result = laddersInfo.put(pos_hash, worstResult);
  laddersInfo.reduceChainAndUndo(moveRecordForLoc, plaChainNewLocs, plaChainMainCaptureLocs);

  return result;
}

const Board::LadderMoveInfo* Board::ladderIterOpp(const Loc initLoc, const Loc loc, const Loc prevLoc,
                                                  const Player pla,
                                                  unordered_set<Loc>& oppInitCaptures, LaddersInfo& laddersInfo
) {
  const Player opp = getOpp(pla);

  const LadderMoveInfo* worstFoundLadderOrCaptureOrNull = nullptr;

  vector<Loc> oppChainNewLocs;
  vector<Loc> oppChainNewMaybeCaptureLocs;

  assert(!oppInitCaptures.empty());
  bool initializeOppChain = laddersInfo.getCurrentDepth() == 1;
  if (initializeOppChain) {
    // TODO: check for empty opp chain
    laddersInfo.extendChain(*oppInitCaptures.begin(), opp, oppChainNewLocs, oppChainNewMaybeCaptureLocs);
  }

  // Try capturing part of pla surrounding at first.
  bool breakingCaptureFound = false;
  const auto maybeCaptureLocs = laddersInfo.getMaybeCaptureLocs(opp);
  for (const auto& oppMaybeCaptureLoc : maybeCaptureLocs) {
    if (breakingCaptureFound || !laddersInfo.maybeChainCaptureLoc(oppMaybeCaptureLoc, opp)) {
      continue;
    }

    MoveRecord potentialDefendCapturingMoveRecord = laddersInfo.play(oppMaybeCaptureLoc, opp);

    for (const Base& oppBase : potentialDefendCapturingMoveRecord.bases) {
      assert(oppBase.pla == opp);
      if (breakingCaptureFound || oppBase.type == Base::Type::EMPTY) continue;
      assert(oppBase.type == Base::Type::NORMAL);

      bool potentialDefendCaptureMoveIsFound = false;
      for (const auto& rollback_locs_states_capture: oppBase.rollback_locs_states_captures) {
        if (laddersInfo.getChainColor(rollback_locs_states_capture.getLoc()) == pla) {
          potentialDefendCaptureMoveIsFound = true;
          break;
        }
      }

      if (potentialDefendCaptureMoveIsFound) {
        vector<Loc> oppChainNewLocsAfterCapturing;
        vector<Loc> oppChainNewMaybeCaptureLocsAfterCapturing;
        laddersInfo.extendChain(oppMaybeCaptureLoc, opp, oppChainNewLocsAfterCapturing, oppChainNewMaybeCaptureLocsAfterCapturing);

        // Try to continue the ladder
        const LadderMoveInfo* foundLadderFromOppCaptureMoveOrNull =
          ladderIterAdjLocs(initLoc, oppMaybeCaptureLoc, prevLoc, pla, oppInitCaptures, laddersInfo);

        if (foundLadderFromOppCaptureMoveOrNull != nullptr && foundLadderFromOppCaptureMoveOrNull->isLadderOrCapture(pla)) {
          if (foundLadderFromOppCaptureMoveOrNull->isWorseThan(worstFoundLadderOrCaptureOrNull)) {
            worstFoundLadderOrCaptureOrNull = foundLadderFromOppCaptureMoveOrNull;
          }
        } else {
          worstFoundLadderOrCaptureOrNull = nullptr;
          breakingCaptureFound = true;
        }

        laddersInfo.reduceChain(opp, oppChainNewLocsAfterCapturing, oppChainNewMaybeCaptureLocsAfterCapturing);
      }
    }

    laddersInfo.undo(potentialDefendCapturingMoveRecord);
  }

  if (!breakingCaptureFound) {
    // Try defending at second
    vector<Loc> oppChainNewLocsAfterDefending;
    vector<Loc> oppChainNewMaybeCaptureLocsAfterDefending;
    const MoveRecord defendMove = laddersInfo.playAndExtendChain(loc, opp, oppChainNewLocsAfterDefending, oppChainNewMaybeCaptureLocsAfterDefending);

    if (const LadderMoveInfo* foundLadderFromDefendMoveOrNull =
      ladderIterAdjLocs(initLoc, loc, prevLoc, pla, oppInitCaptures, laddersInfo);
      foundLadderFromDefendMoveOrNull != nullptr && foundLadderFromDefendMoveOrNull->isLadderOrCapture(pla)
    ) {
      if (foundLadderFromDefendMoveOrNull->isWorseThan(worstFoundLadderOrCaptureOrNull)) {
        worstFoundLadderOrCaptureOrNull = foundLadderFromDefendMoveOrNull;
      }
    } else {
      worstFoundLadderOrCaptureOrNull = nullptr;
    }

    laddersInfo.reduceChainAndUndo(defendMove, oppChainNewLocsAfterDefending, oppChainNewMaybeCaptureLocsAfterDefending);
  }

  if (initializeOppChain) {
    laddersInfo.reduceChain(opp, oppChainNewLocs, oppChainNewMaybeCaptureLocs);
  }

  return worstFoundLadderOrCaptureOrNull;
}

const Board::LadderMoveInfo* Board::ladderIterAdjLocs(
  const Loc initLoc,
  const Loc loc,
  const Loc prevLoc,
  const Player pla,
  unordered_set<Loc>& oppInitCaptures,
  LaddersInfo& laddersInfo
) {
  array<Loc, 16> actualLocs{};
  int actualLocsSize = 0;

  // Make sure the color of the last location is opp to traverse its adjacent locs to find maybe capture of ladder pla locs.
  // The adjacent loc should be empty and have at least one connection with the player chain (otherwise it can't create ladders).
  assert(getColor(loc) == getOpp(pla));
  for (const int adj_offset : adj_offsets) {
    if (const Loc adjLoc = static_cast<Loc>(loc + adj_offset); laddersInfo.maybeChainCaptureLoc(adjLoc, pla, false)) {
      actualLocs[actualLocsSize++] = adjLoc;
    }
  }

  // Handle a special case when the surrounding can go through empty territory that's not directly adjacent to last opp location:
  //
  // .  X  X  X  .
  // X  O' O' O' X
  // X  O' .  O' X
  // .  X  O  .x .
  // .  .  .  .  .
  //
  // Make sure the color of prevLoc is pla but don't use the assertion on active color because the prevLoc can already be captured by opp player
  assert(getPlacedColor(prevLoc) == pla);
  if (getColor(prevLoc) == pla) {
    for (const int adj_offset : adj_offsets) {
      const Loc adjPrevLoc = static_cast<Loc>(prevLoc + adj_offset);

      if (const auto end = actualLocs.begin() + actualLocsSize;
        std::find(actualLocs.begin(), end, adjPrevLoc) != end) {
        continue;
      }

      if (laddersInfo.maybeChainCaptureLoc(adjPrevLoc, pla)) {
        actualLocs[actualLocsSize++] = adjPrevLoc;
      }
    }
  }

  for (int i = 0; i < actualLocsSize; i++) {
    const Loc actualLoc = actualLocs[i];

    if (const LadderMoveInfo* ladderMoveInfoAtAdjLoc =
          ladderIterPla(initLoc, actualLoc, pla, oppInitCaptures, laddersInfo);
      ladderMoveInfoAtAdjLoc->isLadderOrCapture(pla)
    ) {
      return ladderMoveInfoAtAdjLoc;
    }
  }

  return nullptr;
}

bool Board::ladderIsRelevantCapturing(const MoveRecord& moveRecord, const Loc initLoc, const Player pla,
                                      const int depth, unordered_set<Loc>& oppInitCaptures) {
  if (depth <= 1) {
    assert(oppInitCaptures.empty());
    return false;
  }

  for (const Base& base : moveRecord.bases) {
    assert(base.pla == pla);
    if (base.type == Base::Type::EMPTY) continue;
    assert(base.type == Base::Type::NORMAL);

    if (!contains(base.surrounding_locs, initLoc)) continue; // Init loc is expected to be contained

    if (depth == 2) {
      oppInitCaptures.clear();
      for (const auto& rollback_locs_states_capture: base.rollback_locs_states_captures) {
        if (getPlacedDotColor(rollback_locs_states_capture.getState()) == getOpp(pla)) {
          oppInitCaptures.insert(rollback_locs_states_capture.getLoc());
        }
      }
      assert(!oppInitCaptures.empty());
      return true;
    }

    assert(!oppInitCaptures.empty());
    for (const auto& rollback_locs_states_capture: base.rollback_locs_states_captures) {
      if (oppInitCaptures.find(rollback_locs_states_capture.getLoc()) != oppInitCaptures.end()) {
        return true;
      }
    }
  }
  return false;
}

