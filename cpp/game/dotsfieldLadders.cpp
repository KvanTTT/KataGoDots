#include "dotsfieldLadders.h"

#include <iomanip>

#include "board.h"

using namespace std;

vector<DotsLaddersSolver::LadderLocInfo> DotsLaddersSolver::solve() {
  vector<LadderLocInfo> result;

  if (board.is_finished) {
    return result; // Ladders are not relevant when the game is finished (by grounding or resignation)
  }

  for (int y = 0; y < board.y_size; y++) {
    for (int x = 0; x < board.x_size; x++) {
      const Loc loc = Location::getLoc(x, y, board.x_size);

      const State state = board.getState(loc);
      // Optimization: perform fast check to avoid more heavy checks and extra allocations.
      // The most frequent case is the case when the location is empty.
      if (const Color activeColor = getActiveColor(state); activeColor != C_EMPTY) continue;
      const Color emptyTerritoryColor = getEmptyTerritoryColor(state);
      const Color colorsOfPotentialCapturing = board.getColorsOfPotentialCapturing(loc, 1);

      const auto addLadderMoveInfo = [&](const Player pla) {
        if (!(colorsOfPotentialCapturing & pla) || emptyTerritoryColor == pla || (emptyTerritoryColor != C_EMPTY && !board.wouldBeCaptureDots(loc, pla))) {
          return;
        }

        if (const auto ladderLocInfo = startLadder(loc, pla); !ladderLocInfo.isZero()) {
          result.push_back(ladderLocInfo);
        }
      };

      addLadderMoveInfo(P_BLACK);
      //addLadderMoveInfo(P_WHITE); TODO: uncomment after current tests suite with single color become robust
    }
  }

  return result;
}

DotsLaddersSolver::LadderLocInfo DotsLaddersSolver::startLadder(const Loc newInitLoc, const Player pla) {
  initLoc = newInitLoc;
  attacker = pla;
  defender = getOpp(pla);
  auto result = iterateForAttacker(initLoc);
  if (!result.isZero()) {
    // Finally, we are interested in the loc that starts the ladder but not in the loc that forms the final capture.
    result.workingLoc = initLoc;
  }
  initLoc = Board::NULL_LOC;
  attacker = C_EMPTY;
  defender = C_EMPTY;
  return result;
}

DotsLaddersSolver::LadderLocInfo DotsLaddersSolver::iterateForAttacker(const Loc loc) {
  const auto moveRecordForLoc = play(loc, attacker);

  if (isRelevantCapturing(moveRecordForLoc)) {
    const auto result = LadderLocInfo::create(loc, attacker, board, moveRecordForLoc, getInitialWhiteScore());
    undo(moveRecordForLoc);
    return result;
  }

  createAndPushChainInfo(loc, attacker);
  const auto captureLocs = extractCaptureLocs(attacker);

  auto ladderLocInfo = LadderLocInfo::createInfinity(attacker);
  bool ladderLocInfoInitialized = false;

  for (const Loc captureLoc : captureLocs) {
    const auto maybeCapturingMoveRecord = play(captureLoc, attacker);
    const bool ladderCapturingIsFound = isRelevantCapturing(maybeCapturingMoveRecord);
    undo(maybeCapturingMoveRecord);

    if (ladderCapturingIsFound) {
      const bool initDefenderChain = getCurrentDepth() == 1;

      if (initDefenderChain) {
        initializeDefenderChain(maybeCapturingMoveRecord);
      }

      if (const auto attackerLadderLocInfo = iterateForDefender(captureLoc);
        attackerLadderLocInfo.isWorseThan(ladderLocInfo)
      ) {
        ladderLocInfo = attackerLadderLocInfo;
        ladderLocInfoInitialized = true;
      }

      if (initDefenderChain) {
        popChainInfo(defender);
        resetInitTerritory();
      }
    }
  }

  reduceChainAndUndo(moveRecordForLoc);

  if (!ladderLocInfoInitialized) {
    assert(ladderLocInfo.isInfinity());
    ladderLocInfo = LadderLocInfo::createZero(attacker);
  }

  return ladderLocInfo;
}

DotsLaddersSolver::LadderLocInfo DotsLaddersSolver::iterateForDefender(const Loc loc) {
  auto attackerLadderLocInfo = LadderLocInfo::createInfinity(attacker);

  // Try capturing part of pla surrounding at first.
  const auto& defenderCurrentCaptureLocs = getDefenderCurrentCaptureLocs();
  for (const auto& defenderCaptureLoc : defenderCurrentCaptureLocs) {
    if (const Color colorAtDefenderCaptureLoc = board.getColor(defenderCaptureLoc); colorAtDefenderCaptureLoc != C_EMPTY) {
      assert(colorAtDefenderCaptureLoc == attacker);
      continue;
    }

    const auto potentialDefendCaptureMoveRecord = play(defenderCaptureLoc, defender);

    bool potentialDefendCaptureMoveIsFound = false;

    for (const auto& defenderBase : potentialDefendCaptureMoveRecord.bases) {
      if (defenderBase.type != Board::Base::Type::NORMAL) continue;
      const auto& defenderBaseStates = defenderBase.rollback_locs_states_captures;
      potentialDefendCaptureMoveIsFound = std::any_of(defenderBaseStates.begin(), defenderBaseStates.end(),
        [&](const auto& state) {
          return getChainColor(state.getLoc()) == attacker;
      });
      if (potentialDefendCaptureMoveIsFound) break;
    }

    if (potentialDefendCaptureMoveIsFound) {
      createAndPushChainInfo(defenderCaptureLoc, defender);

      // Try to continue the ladder (only captures or strictly connecting locs are relevant).
      const auto defenderCapturingLadderLocInfo = iterateAdjLocsForAttacker(defenderCaptureLoc, true);
      if (defenderCapturingLadderLocInfo.isWorseThan(attackerLadderLocInfo)) {
        attackerLadderLocInfo = defenderCapturingLadderLocInfo;
      }

      popChainInfo(defender);
    }

    undo(potentialDefendCaptureMoveRecord);

    // Optimization:
    // At this site it's assumed that the defending capture is successful for defender player, and it breaks part of the pla ladder chain.
    // It means it doesn't make sense to continue the ladder because it's already broken.
    if (attackerLadderLocInfo.isZero()) {
      return attackerLadderLocInfo;
    }
  }

  // Try defending at second
  const auto defendMove = playAndExtendChain(loc, defender);
  LadderLocInfo ladderLocInfoAfterDefending = iterateAdjLocsForAttacker(loc, false);
  reduceChainAndUndo(defendMove);

  if (!ladderLocInfoAfterDefending.isZero()) {
    // Optimization: don't run iteration over adjacent defender locs to calculate the worst capturing for pla considering defender ideal play.
    // Instead, assume that the failed defending loc is actually a capturing loc for pla.
    // It should form the base(s) with the worst possible evaluation in terms of score and territory.
    const auto maybeWorstSurroundMoveRecordForPla = play(loc, attacker);
    const auto maybeWorseResultIfConsiderIdealDefenderPlay = LadderLocInfo::create(
      loc, attacker, board, maybeWorstSurroundMoveRecordForPla, getInitialWhiteScore()
    );
    undo(maybeWorstSurroundMoveRecordForPla);

    if (maybeWorseResultIfConsiderIdealDefenderPlay.isWorseThan(ladderLocInfoAfterDefending)) {
      ladderLocInfoAfterDefending = maybeWorseResultIfConsiderIdealDefenderPlay;
    }
  }

  if (ladderLocInfoAfterDefending.isWorseThan(attackerLadderLocInfo)) {
    attackerLadderLocInfo = ladderLocInfoAfterDefending;
  }

  return attackerLadderLocInfo;
}

DotsLaddersSolver::LadderLocInfo DotsLaddersSolver::iterateAdjLocsForAttacker(const Loc loc,
                                                                         const bool requireAtLeastTwoUnconnectedDotsForLadder
) {
  array<pair<LadderMoveInfoType, Loc>, 16> actualLocs{};
  int actualLocsSize = 0;

  // Prioritize captures to get rid of calculating useless ladders if a capture move found.
  auto insertCaptureBeforeLadders = [&](const Loc captureLoc) {
    const auto end = actualLocs.begin() + actualLocsSize;
    const auto insertPos = std::find_if(actualLocs.begin(), end, [](const auto& item) {
      return item.first == LADDER;
    });
    std::move_backward(insertPos, end, end + 1);
    *insertPos = make_pair(CAPTURE, captureLoc);
    actualLocsSize++;
  };

  // Make sure the color of the last location is defender to traverse its strongly and indirectly adjacent locs to find maybe capture ladder pla locs.
  assert(board.getColor(loc) == defender);

  auto checkAndAddAdjacentLoc = [&](const Loc directlyAdjLoc, const Loc indirectlyAdjLoc) {
    // The adjacent loc should be empty and have at least one connection with the player chain (otherwise it can't create ladders).
    if (const auto chainCaptureLocType = getChainCaptureLocType(directlyAdjLoc, attacker, requireAtLeastTwoUnconnectedDotsForLadder, true);
      chainCaptureLocType == LADDER
    ) {
      actualLocs[actualLocsSize++] = make_pair(chainCaptureLocType, directlyAdjLoc);
    } else if (chainCaptureLocType == CAPTURE) {
      insertCaptureBeforeLadders(directlyAdjLoc);
    }

    // Handle special cases when the capturing loc isn't directly adjacent to last defender loc:
    //
    // .  X  X  X  .
    // X  O' O' O' X
    // X  O' .  O' X
    // .  X  O  .x .
    // .  .  .  .  .
    //
    // .  X  .  X  .
    // X  O  .  O  X
    // X  O  .  O  X
    // X  O  .  O  X
    // .  X  O  X  .
    // .  .  .  .  .
    //
    // Check the color of the direct adjacent loc at first because it's not safe to use locs outside the field borders + side locs.
    // If the color isn't empty, it's safe to calculate the non-directly adjacent loc because it means the directlyAdjLoc is always
    // within real borders and its adjacent locs are always legal.
    if (board.getColor(directlyAdjLoc) == defender && maybeChainCaptureLoc(indirectlyAdjLoc, attacker, true)) {
      insertCaptureBeforeLadders(indirectlyAdjLoc);
    }
  };

  const Loc xm1yLoc = Location::xm1y(loc);
  checkAndAddAdjacentLoc(xm1yLoc, Location::xm1y(xm1yLoc));

  const Loc xym1Loc = Location::xym1(loc, board.x_size);
  checkAndAddAdjacentLoc(xym1Loc, Location::xym1(xym1Loc, board.x_size));

  const Loc xp1yLoc = Location::xp1y(loc);
  checkAndAddAdjacentLoc(xp1yLoc, Location::xp1y(xp1yLoc));

  const Loc xyp1Loc = Location::xyp1(loc, board.x_size);
  checkAndAddAdjacentLoc(xyp1Loc, Location::xyp1(xyp1Loc, board.x_size));

  for (int i = 0; i < actualLocsSize; i++) {
    const Loc actualLoc = actualLocs[i].second;
    if (const auto ladderLocInfo = iterateForAttacker(actualLoc); !ladderLocInfo.isZero()) {
      // Optimization: returns capturing as soon as it's found.
      // However, it's not completely clear if it makes sense to continue bypassing (at least on strictly capturing locs)
      // and maximize the result.
      return ladderLocInfo;
    }
  }

  return LadderLocInfo::createZero(attacker);
}

bool DotsLaddersSolver::isRelevantCapturing(const Board::MoveRecord& moveRecord) {
  const int depth = getCurrentDepth();
  if (depth <= 1) {
    return false;
  }

  // Init loc is expected to be contained at least in one base
  if (!std::any_of(moveRecord.bases.begin(), moveRecord.bases.end(), [&](const Board::Base& base) {
    assert(base.pla == attacker);
    if (base.type == Board::Base::Type::EMPTY) return false;
    assert(base.type == Board::Base::Type::NORMAL);
    // Filter out unrelated surroundings using checking of initLoc inclusion
    return contains(base.surrounding_locs, initLoc) || std::any_of(base.rollback_locs_states_captures.begin(), base.rollback_locs_states_captures.end(), [&](const auto& state) {
      return state.getLoc() == initLoc;
    });
  })) {
    return false;
  }

  bool result = false;
  for (const auto& base : moveRecord.bases) {
    if (base.type == Board::Base::Type::EMPTY) continue;

    for (const auto& rollback_locs_states_capture: base.rollback_locs_states_captures) {
      const Loc baseLoc = rollback_locs_states_capture.getLoc();
      if (depth == 2) {
        setInitTerritory(baseLoc);
        result = true;
      } else {
        if (isInitTerritory(baseLoc)) {
          return true;
        }
      }
    }
  }
  return result;
}

void DotsLaddersSolver::initializeDefenderChain(const Board::MoveRecord& capturingMoveRecord) {
  vector<Loc> defenderInitChainLocs;
  for (const auto& base : capturingMoveRecord.bases) {
    if (base.type == Board::Base::Type::EMPTY) continue;
    for (const auto& surrounding_loc : base.surrounding_locs) {
      board.forEachAdjacent(surrounding_loc, [&](const Loc adjLoc) {
        if (board.getColor(adjLoc) == defender) {
          defenderInitChainLocs.push_back(adjLoc);
        }
      });
    }
  }
  createAndPushChainInfo(defenderInitChainLocs, defender);
}

void DotsLaddersSolver::createAndPushChainInfo(const Loc loc, const Player pla) {
  vector<Loc> locs(1);
  locs[0] = loc;
  createAndPushChainInfo(locs, pla);
}

void DotsLaddersSolver::createAndPushChainInfo(const vector<Loc>& locs, const Player pla) {
  vector<ChainsInfo>& chainInfos = pla == attacker ? attackerChainInfos : defenderChainInfos;
  vector<Loc>& chainLocs = pla == attacker ? attackerChainLocs : defenderChainLocs;
  vector<Loc>& maybeCaptureLocs = pla == attacker ? attackerMaybeCaptureLocs : defenderMaybeCaptureLocs;
  chainInfos.emplace_back();
  auto& [newChainLocsCount, newMaybeCaptureLocsCount] = chainInfos.back();

  std::vector<Loc> newChainLocs;

  assert(C_WALL != pla);
  assert(walkStack.empty());

  for (const Loc loc : locs) {
    assert(board.getColor(loc) == pla);
    walkStack.push_back(loc);
  }

  while (!walkStack.empty()) {
    const Loc currentLoc = walkStack.back();
    walkStack.pop_back();

    if (const State adjState = board.getState(currentLoc); getActiveColor(adjState) == pla && !isTerritory(adjState) && getChainColor(currentLoc) != pla) {
      setChainPlayer(currentLoc, pla);
      chainLocs.push_back(currentLoc);
      newChainLocs.push_back(currentLoc);
      newChainLocsCount++;

      for (const short adj_offset : board.adj_offsets) {
        walkStack.push_back(static_cast<Loc>(currentLoc + adj_offset));
      }
    }
  }

  for (const Loc newChainLoc : newChainLocs) {
    for (const short adj_offset : board.adj_offsets) {
      const Loc adjLocForChainLoc = static_cast<Loc>(newChainLoc + adj_offset);
      if (!isVisited(adjLocForChainLoc)) {
        if (!alreadyMaybeCapture(adjLocForChainLoc, pla) && maybeChainCaptureLoc(adjLocForChainLoc, pla, false)) {
          setMaybeCapturePlayer(adjLocForChainLoc, pla);
          maybeCaptureLocs.push_back(adjLocForChainLoc);
          newMaybeCaptureLocsCount++;
        }
        setVisited(adjLocForChainLoc);
        walkStack.push_back(adjLocForChainLoc);
      }
    }
  }

  for (const Loc adjLocForChainLoc : walkStack) {
    resetVisited(adjLocForChainLoc);
  }
  walkStack.clear();

  if (pla == defender) {
    defenderCaptureLocs.push_back(extractCaptureLocs(pla));
  }
}

void DotsLaddersSolver::popChainInfo(const Player pla) {
  vector<ChainsInfo>& chainInfos = pla == attacker ? attackerChainInfos : defenderChainInfos;
  vector<Loc>& chainLocs = pla == attacker ? attackerChainLocs : defenderChainLocs;
  vector<Loc>& maybeCaptureLocs = pla == attacker ? attackerMaybeCaptureLocs : defenderMaybeCaptureLocs;
  const auto& [newChainLocsCount, newMaybeCaptureLocsCount] = chainInfos.back();

  assert(newChainLocsCount > 0);
  const int chainLocsSize = static_cast<int>(chainLocs.size());
  for (int i = chainLocsSize - 1; i >= chainLocsSize - newChainLocsCount; i--) {
    const Loc chainLoc = chainLocs[i];
    assert(board.getColor(chainLoc) == pla);
    resetChainPlayer(chainLoc);
  }
  chainLocs.resize(chainLocs.size() - newChainLocsCount);

  const int lastMaybeCaptureSize = static_cast<int>(maybeCaptureLocs.size());
  for (int i = lastMaybeCaptureSize - 1; i >= lastMaybeCaptureSize - newMaybeCaptureLocsCount ; i--) {
    const Loc maybeCaptureLoc = maybeCaptureLocs[i];
    assert(board.getColor(maybeCaptureLoc) == C_EMPTY);
    unsetMaybeCapturePlayer(maybeCaptureLoc, pla);
  }
  maybeCaptureLocs.resize(maybeCaptureLocs.size() - newMaybeCaptureLocsCount);

  chainInfos.pop_back();

  if (pla == defender) {
    defenderCaptureLocs.pop_back();
  }
}

std::vector<Loc>& DotsLaddersSolver::getDefenderCurrentCaptureLocs() {
  return defenderCaptureLocs.back();
}

std::vector<Loc> DotsLaddersSolver::extractCaptureLocs(const Player pla) const {
  const vector<Loc>& maybeCaptureLocs = pla == attacker ? attackerMaybeCaptureLocs : defenderMaybeCaptureLocs;

  vector<Loc> result;
  result.reserve(maybeCaptureLocs.size());
  for (const Loc maybeCaptureLoc : maybeCaptureLocs) {
    if (maybeChainCaptureLoc(maybeCaptureLoc, pla, true)) {
      result.push_back(maybeCaptureLoc);
    }
  }
  return result;
}

DotsLaddersSolver::LadderMoveInfoType DotsLaddersSolver::getChainCaptureLocType(const Loc loc, const Player pla,
  const bool requireAtLeastTwoUnconnectedDotsForLadder, const bool ignoreEmptyBaseLocs) const {
  const State state = board.getState(loc);
  if (getActiveColor(state) != C_EMPTY) {
    return EMPTY;
  }
  if (ignoreEmptyBaseLocs) {
    // We should consider moves into empty territory as legal in case of calculating maybe capturing locs.
    // Because such locs might become useful (non-suicidal) after some sequence of moves.
    // The maybe capturing locs is actually an iteratively calculated sequence of locs that could include false-positives.
    if (const Color emptyTerritoryColor = getEmptyTerritoryColor(state);
      emptyTerritoryColor == pla || (emptyTerritoryColor != C_EMPTY && !board.wouldBeCaptureDots(loc, pla))
    ) {
      return EMPTY;
    }
  }

  int unconnectedLocsSize = 0;
  const std::array<Loc, 4> unconnectedLocs = board.getUnconnectedLocations(loc, pla, unconnectedLocsSize);
  int chainConnectionLocsSize = 0;
  for (int i = 0; i < unconnectedLocsSize; i++) {
    if (getChainColor(unconnectedLocs[i]) == pla) {
      chainConnectionLocsSize++;
    }
  }

  if (chainConnectionLocsSize >= 2) {
    return CAPTURE;
  }

  // The requireAtLeastTwoUnconnectedDotsForLadder is actual when defender succeeds by counter-capturing:
  // In this case it's known that pla dot always should connect a chain to proceed with the ladder.
  // Unfortunately, we can't rely on the ladder's chain because it might not be complete:
  // we check empty locs *before* dot placement, but the chain is fully-formed *after* the dot placement.
  return chainConnectionLocsSize == 1 && (!requireAtLeastTwoUnconnectedDotsForLadder || unconnectedLocsSize >= 2)
           ? LADDER
           : EMPTY;
}

std::string DotsLaddersSolver::debugChainsData() const {
  std::ostringstream stream;
  for (int y = 0; y < board.y_size; y++) {
    if (y == 0) {
      stream << "    ";
      for (int x = 0; x < board.x_size; x++) {
        stream << std::left << std::setw(4) << x;
      }
      stream << '\n';
    }

    for (int x = 0; x < board.x_size; x++) {
      if (x == 0) {
        stream << std::left << std::setw(4) << y;
      }

      const Loc loc = Location::getLoc(x, y, board.x_size);

      const Color chainColor = getChainColor(loc);
      Color maybeCaptureColor = getMaybeCaptureColor(loc);

      stream << PlayerIO::colorToChar(chainColor);
      std::string maybeCaptureString;
      switch (maybeCaptureColor) {
        case C_EMPTY:
          maybeCaptureString = "  ";
          break;
        case C_BLACK:
          maybeCaptureString = "x ";
          break;
        case C_WHITE:
          maybeCaptureString = "o ";
          break;
        case C_WALL:
          maybeCaptureString = "xo";
          break;
        default:
          ASSERT_UNREACHABLE;
      }
      stream << maybeCaptureString;

      if (x < board.x_size - 1) {
        stream << ' ';
      }
    }
    stream << std::endl;
  }
  return stream.str();
}

