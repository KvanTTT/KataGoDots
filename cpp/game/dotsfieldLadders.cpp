#include "board.h"

using namespace std;

vector<Board::LadderLocInfo> Board::iterDotsLadders(LaddersInfo& laddersInfo) {
  vector<LadderLocInfo> result;

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

        if (ladderIterPla(loc, loc, pla, laddersInfo)) {
          result.emplace_back(loc, pla, laddersInfo.getInitFinalTerritory());
        }
      };

      addLadderMoveInfo(P_BLACK);
      //addLadderMoveInfo(P_WHITE); TODO: uncomment after current tests suite with single color become robust
    }
  }

  return result;
}

bool Board::ladderIterPla(const Loc initLoc, const Loc loc, const Player pla, LaddersInfo& laddersInfo) {
  const MoveRecord moveRecordForLoc = laddersInfo.play(loc, pla);

  if (const bool* calculatedResult = laddersInfo.get(pos_hash, pla); calculatedResult != nullptr) {
    laddersInfo.undo(moveRecordForLoc);
    return *calculatedResult;
  }

  if (ladderIsRelevantCapturing(moveRecordForLoc, initLoc, pla, laddersInfo)) {
    laddersInfo.put(pos_hash, pla, true);
    laddersInfo.undo(moveRecordForLoc);
    return true;
  }

  vector<Loc> plaChainNewLocs;
  vector<Loc> plaChainMainCaptureLocs;
  const auto maybeCaptureLocs = laddersInfo.extendChain(loc, pla, plaChainNewLocs, plaChainMainCaptureLocs);

  bool success = false;

  for (const Loc maybeCaptureLoc : maybeCaptureLocs) {
    if (!laddersInfo.maybeChainCaptureLoc(maybeCaptureLoc, pla)) {
      continue;
    }

    const MoveRecord secondMoveRecord = laddersInfo.play(maybeCaptureLoc, pla);
    const bool ladderCapturingIsFound = ladderIsRelevantCapturing(secondMoveRecord, initLoc, pla, laddersInfo);
    laddersInfo.undo(secondMoveRecord);

    if (ladderCapturingIsFound) {
      const bool initializeOppChain = laddersInfo.getCurrentDepth() == 1;
      vector<Loc> oppChainNewLocs;
      vector<Loc> oppChainNewMaybeCaptureLocs;

      if (initializeOppChain) {
        const Player opp = getOpp(pla);
        laddersInfo.extendChain(laddersInfo.getFirstInitTerritoryLoc(opp), opp, oppChainNewLocs, oppChainNewMaybeCaptureLocs);
      }

      success = success | ladderIterOpp(initLoc, maybeCaptureLoc, loc, pla, laddersInfo);

      if (initializeOppChain) {
        laddersInfo.reduceChain(getOpp(pla), oppChainNewLocs, oppChainNewMaybeCaptureLocs);
        laddersInfo.handleAndResetInitTerritory(success);
      } else if (success) {
        break;
      }
    }
  }

  laddersInfo.put(pos_hash, pla, success);
  laddersInfo.reduceChainAndUndo(moveRecordForLoc, plaChainNewLocs, plaChainMainCaptureLocs);

  return success;
}

bool Board::ladderIterOpp(const Loc initLoc, const Loc loc, const Loc prevLoc, const Player pla,
                          LaddersInfo& laddersInfo
) {
  const Player opp = getOpp(pla);

  // Try capturing part of pla surrounding at first.
  const auto maybeCaptureLocs = laddersInfo.getMaybeCaptureLocs(opp);
  for (const auto& oppMaybeCaptureLoc : maybeCaptureLocs) {
    if (!laddersInfo.maybeChainCaptureLoc(oppMaybeCaptureLoc, opp)) {
      continue;
    }

    MoveRecord potentialDefendCapturingMoveRecord = laddersInfo.play(oppMaybeCaptureLoc, opp);

    bool breakingCaptureFound = false;
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
        breakingCaptureFound = breakingCaptureFound | !ladderIterAdjLocs(initLoc, oppMaybeCaptureLoc, prevLoc, pla, laddersInfo);

        laddersInfo.reduceChain(opp, oppChainNewLocsAfterCapturing, oppChainNewMaybeCaptureLocsAfterCapturing);
      }
    }

    laddersInfo.undo(potentialDefendCapturingMoveRecord);

    if (breakingCaptureFound) {
      return false;
    }
  }

  // Try defending at second
  vector<Loc> oppChainNewLocsAfterDefending;
  vector<Loc> oppChainNewMaybeCaptureLocsAfterDefending;
  const MoveRecord defendMove = laddersInfo.playAndExtendChain(loc, opp, oppChainNewLocsAfterDefending, oppChainNewMaybeCaptureLocsAfterDefending);

  const bool success = ladderIterAdjLocs(initLoc, loc, prevLoc, pla, laddersInfo);

  laddersInfo.reduceChainAndUndo(defendMove, oppChainNewLocsAfterDefending, oppChainNewMaybeCaptureLocsAfterDefending);

  return success;
}

bool Board::ladderIterAdjLocs(const Loc initLoc, const Loc loc, const Loc prevLoc, const Player pla, LaddersInfo& laddersInfo
) {
  array<pair<LaddersInfo::LadderMoveInfoType, Loc>, 16> actualLocs{};
  int actualLocsSize = 0;

  // Prioritize captures to get rid of calculating useless ladders if a capture move found.
  auto insertCaptureBeforeLadders = [&](const Loc captureLoc) {
    const auto end = actualLocs.begin() + actualLocsSize;
    const auto insertPos = std::find_if(actualLocs.begin(), end, [](const auto& item) {
      return item.first == LaddersInfo::LadderMoveInfoType::LADDER;
    });
    std::move_backward(insertPos, end, end + 1);
    *insertPos = make_pair(LaddersInfo::LadderMoveInfoType::CAPTURE, captureLoc);
    actualLocsSize++;
  };

  // Make sure the color of the last location is opp to traverse its adjacent locs to find maybe capture of ladder pla locs.
  // The adjacent loc should be empty and have at least one connection with the player chain (otherwise it can't create ladders).
  assert(getColor(loc) == getOpp(pla));
  for (const int adj_offset : adj_offsets) {
    const Loc adjLoc = static_cast<Loc>(loc + adj_offset);
    if (const auto chainCaptureLocType = laddersInfo.getChainCaptureLocType(adjLoc, pla);
      chainCaptureLocType == LaddersInfo::LadderMoveInfoType::LADDER
    ) {
      actualLocs[actualLocsSize++] = make_pair(chainCaptureLocType, adjLoc);
    } else if (chainCaptureLocType == LaddersInfo::LadderMoveInfoType::CAPTURE) {
      insertCaptureBeforeLadders(adjLoc);
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
        std::find_if(actualLocs.begin(), end,   [adjPrevLoc](const auto& item) {
          return item.second == adjPrevLoc;
        }) != end
      ) {
        continue;
      }

      if (laddersInfo.maybeChainCaptureLoc(adjPrevLoc, pla)) {
        insertCaptureBeforeLadders(adjPrevLoc);
      }
    }
  }

  for (int i = 0; i < actualLocsSize; i++) {
    if (const Loc actualLoc = actualLocs[i].second; ladderIterPla(initLoc, actualLoc, pla, laddersInfo)) {
      return true;
    }
  }

  return false;
}

bool Board::ladderIsRelevantCapturing(const MoveRecord& moveRecord, const Loc initLoc, const Player pla, LaddersInfo& laddersInfo) {
  const int depth = laddersInfo.getCurrentDepth();
  if (depth <= 1) {
    return false;
  }

  // Init loc is expected to be contained at least in one base
  if (!std::any_of(moveRecord.bases.begin(), moveRecord.bases.end(), [&](const Base& base) {
    assert(base.pla == pla);
    if (base.type == Base::Type::EMPTY) return false;
    assert(base.type == Base::Type::NORMAL);
    return contains(base.surrounding_locs, initLoc);
  })) {
    return false;
  }

  bool result = false;
  for (const Base& base : moveRecord.bases) {
    if (base.type == Base::Type::EMPTY) continue;

    for (const auto& rollback_locs_states_capture: base.rollback_locs_states_captures) {
      const Loc baseLoc = rollback_locs_states_capture.getLoc();
      if (depth == 2) {
        laddersInfo.setInitTerritory(baseLoc);
        result = true;
      } else {
        if (laddersInfo.isInitTerritory(baseLoc)) {
          return true;
        }
      }
    }
  }
  return result;
}

