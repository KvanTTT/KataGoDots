#include "board.h"

using namespace std;

vector<const Board::LadderMoveInfo*> Board::iterDotsLadders(LaddersCache& cachedLadderMoveInfos) {
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

        if (const LadderMoveInfo* ladderMoveInfo = ladderStart(loc, pla, cachedLadderMoveInfos); ladderMoveInfo->isLadder(pla)) {
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

const Board::LadderMoveInfo* Board::ladderStart(const Loc initLoc, const Player pla, LaddersCache& cache) {
  unordered_set<Loc> plaChainLocs;
  unordered_set<Loc> plaChainAdjLocs;
  unordered_set<Loc> oppChainLocs;
  unordered_set<Loc> oppChainAdjLocs;
  unordered_set<Loc> oppInitCaptures;

  return ladderIterPla(initLoc, initLoc, NULL_LOC, pla, 1,
                       plaChainLocs, plaChainAdjLocs, oppChainLocs, oppChainAdjLocs, oppInitCaptures, cache);
}

const Board::LadderMoveInfo* Board::ladderIterPla(const Loc initLoc, const Loc loc, const Loc prevLoc,
                                                  const Player pla, const uint16_t depth,
                                                  unordered_set<Loc>& plaChainLocs, unordered_set<Loc>& plaChainAdjLocs,
                                                  unordered_set<Loc>& oppChainLocs, unordered_set<Loc>& oppChainAdjLocs,
                                                  unordered_set<Loc>& oppInitCaptures,
                                                  LaddersCache& cache) {
  const MoveRecord moveRecordForLoc = playMoveRecordedDots(loc, pla);
  cache.recalcMaxDepth(depth);
  cache.incMovesCount();

  if (const LadderMoveInfo* calculatedResult = cache.get(pos_hash, pla); calculatedResult != nullptr) {
    auto* result = calculatedResult->type == LadderMoveInfo::LADDER && calculatedResult->workingMove != loc
      ? cache.put(pos_hash, LadderMoveInfo::createLadder(loc, *calculatedResult))
      : calculatedResult;
    undoDots(moveRecordForLoc);
    return result;
  }

  if (ladderIsRelevantCapturing(moveRecordForLoc, initLoc, pla, depth, oppInitCaptures)) {
    const LadderMoveInfo* result = cache.put(pos_hash, LadderMoveInfo::createCapture(moveRecordForLoc.bases));
    undoDots(moveRecordForLoc);
    return result;
  }

  vector<Loc> plaChainNewLocs;
  vector<Loc> plaChainAdjNewLocs;
  appendAllDiagonallyConnectedDots(loc, pla, plaChainLocs, plaChainAdjLocs, plaChainNewLocs, plaChainAdjNewLocs);

  auto worstResult = LadderMoveInfo::createEmpty(pla);

  for (const Loc plaChainAdjLoc : plaChainAdjLocs) {
    if (!ladderIsRelevantLadderLoc(plaChainAdjLoc, pla, true, plaChainLocs)) {
      continue;
    }

    const MoveRecord secondMoveRecord = playMoveRecordedDots(plaChainAdjLoc, pla);
    cache.incMovesCount();

    bool potentialCapturingIsFound = ladderIsRelevantCapturing(secondMoveRecord, initLoc, pla, depth + 1, oppInitCaptures);
    undoDots(secondMoveRecord);

    if (potentialCapturingIsFound) {
      if (const LadderMoveInfo* adjLocLadderMoveInfo = ladderIterOpp(initLoc, plaChainAdjLoc, loc, prevLoc, pla, depth + 1,
        plaChainLocs, plaChainAdjLocs, oppChainLocs, oppChainAdjLocs, oppInitCaptures, cache);
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

  cleanUpChainAndAdjLocs(plaChainLocs, plaChainAdjLocs, plaChainNewLocs, plaChainAdjNewLocs);

  const LadderMoveInfo* result = cache.put(pos_hash, worstResult);
  undoDots(moveRecordForLoc);
  return result;
}

const Board::LadderMoveInfo* Board::ladderIterOpp(const Loc initLoc, const Loc loc, Loc prevLoc, Loc prevPrevLoc,
                                                  const Player pla, const uint16_t depth, unordered_set<Loc>& plaChainLocs, unordered_set<Loc>& plaChainAdjLocs,
                                                  unordered_set<Loc>& oppChainLocs, unordered_set<Loc>& oppChainAdjLocs, unordered_set<Loc>& oppInitCaptures, LaddersCache& cache
) {
  const Player opp = getOpp(pla);

  const LadderMoveInfo* worstFoundLadderOrCaptureOrNull = nullptr;

  // Try capturing part of pla surrounding at first.
  assert(!oppInitCaptures.empty());
  if (depth == 2) {
    oppChainLocs.clear();
    oppChainAdjLocs.clear();
    vector<Loc> oppChainNewLocs;
    vector<Loc> oppChainNewAdjLocs;
    appendAllDiagonallyConnectedDots(*oppInitCaptures.begin(), opp, oppChainLocs, oppChainAdjLocs, oppChainNewLocs, oppChainNewAdjLocs);
  }

  for (const auto& oppChainAdjLoc : oppChainAdjLocs) {
    if (!ladderIsRelevantLadderLoc(oppChainAdjLoc, opp, true, oppChainLocs)) {
      continue;
    }

    MoveRecord potentialDefendCapturingMoveRecord = playMoveRecordedDots(oppChainAdjLoc, opp);
    cache.incMovesCount();

    for (const Base& base : potentialDefendCapturingMoveRecord.bases) {
      assert(base.pla == opp);
      if (base.type == Base::Type::EMPTY) continue;
      assert(base.type == Base::Type::NORMAL);

      bool potentialDefendCaptureMoveIsFound = false;
      for (const auto& rollback_locs_states_capture: base.rollback_locs_states_captures) {
        if (plaChainLocs.find(rollback_locs_states_capture.getLoc()) != plaChainLocs.end()) {
          potentialDefendCaptureMoveIsFound = true;
          break;
        }
      }

      if (potentialDefendCaptureMoveIsFound) {
        vector<Loc> oppChainNewLocs;
        vector<Loc> oppChainNewAdjLocs;
        appendAllDiagonallyConnectedDots(oppChainAdjLoc, opp, oppChainLocs, oppChainAdjLocs, oppChainNewLocs, oppChainNewAdjLocs);

        // Try to continue the ladder
        const LadderMoveInfo* foundLadderFromOppCaptureMoveOrNull =
          ladderIterAdjLocs(initLoc, oppChainAdjLoc, prevLoc, prevPrevLoc, pla, depth, plaChainLocs, plaChainAdjLocs, oppChainLocs, oppChainAdjLocs, oppInitCaptures, cache);

        if (foundLadderFromOppCaptureMoveOrNull != nullptr && foundLadderFromOppCaptureMoveOrNull->isLadderOrCapture(pla)) {
          if (foundLadderFromOppCaptureMoveOrNull->isWorseThan(worstFoundLadderOrCaptureOrNull)) {
            worstFoundLadderOrCaptureOrNull = foundLadderFromOppCaptureMoveOrNull;
          }
        } else {
          undoDots(potentialDefendCapturingMoveRecord);
          cleanUpChainAndAdjLocs(oppChainLocs, oppChainAdjLocs, oppChainNewLocs, oppChainNewAdjLocs);
          return nullptr;
        }

        cleanUpChainAndAdjLocs(oppChainLocs, oppChainAdjLocs, oppChainNewLocs, oppChainNewAdjLocs);
      }
    }

    undoDots(potentialDefendCapturingMoveRecord);
  }

  // Try defending at second
  const MoveRecord& defendMove = playMoveRecordedDots(loc, opp);

  vector<Loc> oppChainNewLocs;
  vector<Loc> oppChainNewAdjLocs;
  appendAllDiagonallyConnectedDots(loc, opp, oppChainLocs, oppChainAdjLocs, oppChainNewLocs, oppChainNewAdjLocs);

  cache.recalcMaxDepth(depth);
  cache.incMovesCount();

  if (const LadderMoveInfo* foundLadderFromDefendMoveOrNull =
    ladderIterAdjLocs(initLoc, loc, prevLoc, prevPrevLoc, pla, depth, plaChainLocs, plaChainAdjLocs, oppChainLocs, oppChainAdjLocs, oppInitCaptures, cache);
    foundLadderFromDefendMoveOrNull != nullptr && foundLadderFromDefendMoveOrNull->isLadderOrCapture(pla)
  ) {
    if (foundLadderFromDefendMoveOrNull->isWorseThan(worstFoundLadderOrCaptureOrNull)) {
      worstFoundLadderOrCaptureOrNull = foundLadderFromDefendMoveOrNull;
    }
  } else {
    worstFoundLadderOrCaptureOrNull = nullptr;
  }

  undoDots(defendMove);

  cleanUpChainAndAdjLocs(oppChainLocs, oppChainAdjLocs, oppChainNewLocs, oppChainNewAdjLocs);

  return worstFoundLadderOrCaptureOrNull;
}

const Board::LadderMoveInfo* Board::ladderIterAdjLocs(
  const Loc initLoc,
  const Loc loc,
  const Loc prevLoc,
  const Loc prevPrevLoc,
  const Player pla,
  const uint16_t depth,
  unordered_set<Loc>& plaChainLocs,
  unordered_set<Loc>& plaChainAdjLocs,
  unordered_set<Loc>& oppChainLocs,
  unordered_set<Loc>& oppChainAdjLocs,
  unordered_set<Loc>& oppInitCaptures,
  LaddersCache& cache
) {
  array<Loc, 16> actualLocs{};
  int actualLocsSize = 0;

  // Make sure the color of the last location is opp to traverse its adjacent locs to find maybe capture of ladder pla locs.
  // The adjacent loc should be empty and have at least one connection with the player chain (otherwise it can't create ladders).
  assert(getColor(loc) == getOpp(pla));
  for (const int adj_offset : adj_offsets) {
    if (const Loc adjLoc = static_cast<Loc>(loc + adj_offset); ladderIsRelevantLadderLoc(adjLoc, pla, false, plaChainLocs)) {
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

      if (ladderIsRelevantLadderLoc(adjPrevLoc, pla, true, plaChainLocs)) {
        actualLocs[actualLocsSize++] = adjPrevLoc;
      }
    }
  }

  for (int i = 0; i < actualLocsSize; i++) {
    const Loc actualLoc = actualLocs[i];

    if (const LadderMoveInfo* ladderMoveInfoAtAdjLoc =
          ladderIterPla(initLoc, actualLoc, prevLoc, pla, depth + 1, plaChainLocs, plaChainAdjLocs, oppChainLocs, oppChainAdjLocs, oppInitCaptures, cache);
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

bool Board::ladderIsRelevantLadderLoc(const Loc loc, const Player pla, const bool oneMoveCapturing, unordered_set<Loc>& chainLocs) const {
  const State state = getState(loc);
  if (getActiveColor(state) != C_EMPTY) {
    return false;
  }
  if (const Color emptyTerritoryColor = getEmptyTerritoryColor(state);
    emptyTerritoryColor == pla || (emptyTerritoryColor != C_EMPTY && !wouldBeCaptureDots(loc, pla))
  ) {
    return false;
  }

  int unconnectedLocsSize = 0;
  const array<Loc, 4> unconnectedLocs = getUnconnectedLocations(loc, pla, unconnectedLocsSize);
  int chainConnectionLocsSize = 0;
  for (int i = 0; i < unconnectedLocsSize; i++) {
    if (chainLocs.find(unconnectedLocs[i]) != chainLocs.end())
      chainConnectionLocsSize++;
  }

  if (oneMoveCapturing) {
    return chainConnectionLocsSize >= 2;
  }

  if (chainConnectionLocsSize < 1) {
    return false;
  }

  return true;
}

void Board::appendAllDiagonallyConnectedDots(const Loc loc, const Player pla,
  unordered_set<Loc>& chain, unordered_set<Loc>& chainAdjLocs,
  vector<Loc>& newChainLocs, vector<Loc>& newAdjLocs
  ) {
  vector<Loc> localWalkStack;
  localWalkStack.push_back(loc);

  while (!localWalkStack.empty()) {
    const Loc currentLoc = localWalkStack.back();
    localWalkStack.pop_back();

    if (const State adjState = getState(currentLoc); getActiveColor(adjState) == pla && !isTerritory(adjState) && chain.find(currentLoc) == chain.end()) {
      chain.insert(currentLoc);
      newChainLocs.push_back(currentLoc);

      for (const short adj_offset_for_adj_loc : adj_offsets) {
        if (const Loc adjLocForChainLoc = static_cast<Loc>(currentLoc + adj_offset_for_adj_loc);
          chainAdjLocs.find(adjLocForChainLoc) == chainAdjLocs.end() &&
          ladderIsRelevantLadderLoc(adjLocForChainLoc, pla, true, chain)
        ) {
          chainAdjLocs.insert(adjLocForChainLoc);
          newAdjLocs.push_back(adjLocForChainLoc);
        }
      }

      for (const short adj_offset : adj_offsets) {
        localWalkStack.push_back(static_cast<Loc>(currentLoc + adj_offset));
      }
    }
  }
}

void Board::cleanUpChainAndAdjLocs(unordered_set<Loc>& chainLocs, unordered_set<Loc>& chainAdjLocs, const vector<Loc>& chainNewLocs, const vector<Loc>& chainAdjNewLocs) {
  for (Loc newChainLoc : chainNewLocs) {
    const auto erased = chainLocs.erase(newChainLoc);
    assert(erased == 1 && "Placed or connected locs are always expected to be erased");
  }

  for (Loc newChainAdjLoc : chainAdjNewLocs) {
    const auto erased = chainAdjLocs.erase(newChainAdjLoc);
    assert(erased == 1 && "Placed or connected adjacent locs are always expected to be erased");
  }
}