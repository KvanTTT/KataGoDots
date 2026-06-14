#include "dotsfieldLadders.h"
#include "board.h"

using namespace std;

vector<DotsLaddersEvaluator::LadderLocInfo> DotsLaddersEvaluator::evaluate() {
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

        if (const auto ladderLocInfo = iterateForPlayer(loc, loc, pla); !ladderLocInfo.isZero()) {
          result.push_back(ladderLocInfo);
        }
      };

      addLadderMoveInfo(P_BLACK);
      //addLadderMoveInfo(P_WHITE); TODO: uncomment after current tests suite with single color become robust
    }
  }

  return result;
}

DotsLaddersEvaluator::LadderLocInfo DotsLaddersEvaluator::iterateForPlayer(const Loc initLoc, const Loc loc, const Player pla) {
  const auto moveRecordForLoc = play(loc, pla);

  if (isRelevantCapturing(moveRecordForLoc, initLoc, pla)) {
    const auto result = LadderLocInfo::create(loc, pla, board, moveRecordForLoc, getInitialWhiteScore());
    undo(moveRecordForLoc);
    return result;
  }

  vector<Loc> plaChainNewLocs;
  vector<Loc> plaChainMainCaptureLocs;
  const auto maybeCaptureLocs = extendChain(loc, pla, plaChainNewLocs, plaChainMainCaptureLocs);

  auto ladderLocInfo = LadderLocInfo::createInfinity(pla);
  bool ladderLocInfoInitialized = false;

  for (const Loc maybeCaptureLoc : maybeCaptureLocs) {
    if (!maybeChainCaptureLoc(maybeCaptureLoc, pla)) {
      continue;
    }

    const auto maybeCapturingMoveRecord = play(maybeCaptureLoc, pla);
    const bool ladderCapturingIsFound = isRelevantCapturing(maybeCapturingMoveRecord, initLoc, pla);
    undo(maybeCapturingMoveRecord);

    if (ladderCapturingIsFound) {
      const bool initializeOppChain = getCurrentDepth() == 1;
      vector<Loc> oppChainNewLocs;
      vector<Loc> oppChainNewMaybeCaptureLocs;

      if (initializeOppChain) {
        initializeOpponentChain(pla, maybeCapturingMoveRecord, oppChainNewLocs, oppChainNewMaybeCaptureLocs);
      }

      if (const auto oppLadderLocInfo = iterateForOpp(initLoc, maybeCaptureLoc, pla);
        oppLadderLocInfo.isWorseThan(ladderLocInfo)
      ) {
        ladderLocInfo = oppLadderLocInfo;
        ladderLocInfoInitialized = true;
      }

      if (initializeOppChain) {
        reduceChain(getOpp(pla), oppChainNewLocs, oppChainNewMaybeCaptureLocs);
        resetInitTerritory();
      }
    }
  }

  reduceChainAndUndo(moveRecordForLoc, plaChainNewLocs, plaChainMainCaptureLocs);

  if (!ladderLocInfoInitialized) {
    assert(ladderLocInfo.isInfinity());
    ladderLocInfo = LadderLocInfo::createZero(pla);
  } else if (!ladderLocInfo.isZero() && getCurrentDepth() == 0) {
    // Finally, we are interested in the loc that starts the ladder but not in the loc that forms the final capture.
    ladderLocInfo.workingLoc = initLoc;
  }

  return ladderLocInfo;
}

DotsLaddersEvaluator::LadderLocInfo DotsLaddersEvaluator::iterateForOpp(const Loc initLoc, const Loc loc, const Player pla) {
  const Player opp = getOpp(pla);
  auto oppLadderLocInfo = LadderLocInfo::createInfinity(pla);

  // Try capturing part of pla surrounding at first.
  const auto maybeCaptureLocs = getMaybeCaptureLocs(opp);
  for (const auto& oppMaybeCaptureLoc : maybeCaptureLocs) {
    if (!maybeChainCaptureLoc(oppMaybeCaptureLoc, opp)) {
      continue;
    }

    const auto potentialDefendCapturingMoveRecord = play(oppMaybeCaptureLoc, opp);

    for (const auto& oppBase : potentialDefendCapturingMoveRecord.bases) {
      assert(oppBase.pla == opp);
      if (oppBase.type == Board::Base::Type::EMPTY) continue;
      assert(oppBase.type == Board::Base::Type::NORMAL);

      const auto& oppBaseStates = oppBase.rollback_locs_states_captures;
      const bool potentialDefendCaptureMoveIsFound = std::any_of(oppBaseStates.begin(), oppBaseStates.end(),
        [&](const auto& state) {
          return getChainColor(state.getLoc()) == pla;
      });

      if (potentialDefendCaptureMoveIsFound) {
        vector<Loc> oppChainNewLocsAfterCapturing;
        vector<Loc> oppChainNewMaybeCaptureLocsAfterCapturing;
        extendChain(oppMaybeCaptureLoc, opp, oppChainNewLocsAfterCapturing, oppChainNewMaybeCaptureLocsAfterCapturing);

        // Try to continue the ladder (only captures or strictly connecting locs are relevant).
        const auto oppCapturingLadderLocInfo = iterateAdjLocs(initLoc, oppMaybeCaptureLoc, pla, true);
        if (oppCapturingLadderLocInfo.isWorseThan(oppLadderLocInfo)) {
          oppLadderLocInfo = oppCapturingLadderLocInfo;
        }

        reduceChain(opp, oppChainNewLocsAfterCapturing, oppChainNewMaybeCaptureLocsAfterCapturing);

        if (oppLadderLocInfo.isZero()) {
          break; // The successful opp defending capture is found -> exit
        }
      }
    }

    undo(potentialDefendCapturingMoveRecord);

    // Optimization:
    // At this site it's assumed that the defending capture is successful for opp player, and it breaks part of the pla ladder chain.
    // It means it doesn't make sense to continue the ladder because it's already broken.
    if (oppLadderLocInfo.isZero()) {
      return oppLadderLocInfo;
    }
  }

  // Try defending at second
  vector<Loc> oppChainNewLocsAfterDefending;
  vector<Loc> oppChainNewMaybeCaptureLocsAfterDefending;
  const auto defendMove = playAndExtendChain(loc, opp, oppChainNewLocsAfterDefending, oppChainNewMaybeCaptureLocsAfterDefending);

  LadderLocInfo ladderLocInfoAfterDefending = iterateAdjLocs(initLoc, loc, pla, false);
  reduceChainAndUndo(defendMove, oppChainNewLocsAfterDefending, oppChainNewMaybeCaptureLocsAfterDefending);

  if (!ladderLocInfoAfterDefending.isZero()) {
    // Optimization: don't run iteration over adjacent opp locs to calculate the worst capturing for pla considering opp ideal play.
    // Instead, assume that the failed defending opposite loc is actually a capturing loc for pla.
    // It should form the base(s) with the worst possible evaluation in terms of score and territory.
    const auto maybeWorstSurroundMoveRecordForPla = play(loc, pla);
    const auto maybeWorseResultIfConsiderIdealOppPlay = LadderLocInfo::create(
      loc, pla, board, maybeWorstSurroundMoveRecordForPla, getInitialWhiteScore()
    );
    undo(maybeWorstSurroundMoveRecordForPla);

    if (maybeWorseResultIfConsiderIdealOppPlay.isWorseThan(ladderLocInfoAfterDefending)) {
      ladderLocInfoAfterDefending = maybeWorseResultIfConsiderIdealOppPlay;
    }
  }

  if (ladderLocInfoAfterDefending.isWorseThan(oppLadderLocInfo)) {
    oppLadderLocInfo = ladderLocInfoAfterDefending;
  }

  return oppLadderLocInfo;
}

DotsLaddersEvaluator::LadderLocInfo DotsLaddersEvaluator::iterateAdjLocs(const Loc initLoc, const Loc loc, const Player pla,
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

  // Make sure the color of the last location is opp to traverse its strongly and indirectly adjacent locs to find maybe capture ladder pla locs.
  const Player opp = getOpp(pla);
  assert(board.getColor(loc) == getOpp(pla));

  auto checkAndAddAdjacentLoc = [&](const Loc directlyAdjLoc, const Loc indirectlyAdjLoc) {
    // The adjacent loc should be empty and have at least one connection with the player chain (otherwise it can't create ladders).
    if (const auto chainCaptureLocType = getChainCaptureLocType(directlyAdjLoc, pla, requireAtLeastTwoUnconnectedDotsForLadder);
      chainCaptureLocType == LADDER
    ) {
      actualLocs[actualLocsSize++] = make_pair(chainCaptureLocType, directlyAdjLoc);
    } else if (chainCaptureLocType == CAPTURE) {
      insertCaptureBeforeLadders(directlyAdjLoc);
    }

    // Handle special cases when the capturing loc isn't directly adjacent to last opp loc:
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
    if (board.getColor(directlyAdjLoc) == opp && maybeChainCaptureLoc(indirectlyAdjLoc, pla)) {
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
    if (const auto ladderLocInfo = iterateForPlayer(initLoc, actualLoc, pla); !ladderLocInfo.isZero()) {
      // Optimization: returns capturing as soon as it's found.
      // However, it's not completely clear if it makes sense to continue bypassing (at least on strictly capturing locs)
      // and maximize the result.
      return ladderLocInfo;
    }
  }

  return LadderLocInfo::createZero(pla);
}

bool DotsLaddersEvaluator::isRelevantCapturing(const Board::MoveRecord& moveRecord, const Loc initLoc, const Player pla) {
  const int depth = getCurrentDepth();
  if (depth <= 1) {
    return false;
  }

  // Init loc is expected to be contained at least in one base
  if (!std::any_of(moveRecord.bases.begin(), moveRecord.bases.end(), [&](const Board::Base& base) {
    assert(base.pla == pla);
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

void DotsLaddersEvaluator::initializeOpponentChain(const Player pla,
  const Board::MoveRecord& capturingMoveRecord, vector<Loc>& oppChainNewLocs, vector<Loc>& oppChainNewMaybeCaptureLocs
  ) {
  const Player opp = getOpp(pla);
  for (const auto& base : capturingMoveRecord.bases) {
    if (base.type == Board::Base::Type::EMPTY) continue;
    for (const auto& surrounding_loc : base.surrounding_locs) {
      board.forEachAdjacent(surrounding_loc, [&](const Loc adjLoc) {
        if (board.getColor(adjLoc) == opp) {
          extendChain(adjLoc, opp, oppChainNewLocs, oppChainNewMaybeCaptureLocs);
        }
      });
    }
  }
}

const std::vector<Loc>& DotsLaddersEvaluator::extendChain(const Loc loc, const Player pla,
  std::vector<Loc>& newChainLocs, std::vector<Loc>& newMaybeCaptureLocs) {
  auto& maybeCaptureLocs = pla == P_BLACK ? firstPlaMaybeCaptureLocs : secondPlaMaybeCaptureLocs;

  assert(C_WALL != pla && board.getColor(loc) == pla);
  assert(walkStack.empty());
  walkStack.push_back(loc);

  while (!walkStack.empty()) {
    const Loc currentLoc = walkStack.back();
    walkStack.pop_back();

    if (const State adjState = board.getState(currentLoc); getActiveColor(adjState) == pla && !isTerritory(adjState) && getChainColor(currentLoc) != pla) {
      setChainPlayer(currentLoc, pla);
      newChainLocs.push_back(currentLoc);

      for (const short adj_offset : board.adj_offsets) {
        walkStack.push_back(static_cast<Loc>(currentLoc + adj_offset));
      }
    }
  }

  for (const Loc newChainLoc : newChainLocs) {
    for (const short adj_offset_for_adj_loc : board.adj_offsets) {
      if (const Loc adjLocForChainLoc = static_cast<Loc>(newChainLoc + adj_offset_for_adj_loc);
        !alreadyMaybeCapture(adjLocForChainLoc, pla) && maybeChainCaptureLoc(adjLocForChainLoc, pla)
      ) {
        setMaybeCapturePlayer(adjLocForChainLoc, pla);
        maybeCaptureLocs.push_back(adjLocForChainLoc);
        newMaybeCaptureLocs.push_back(adjLocForChainLoc);
      }
    }
  }

  return maybeCaptureLocs;
}

void DotsLaddersEvaluator::reduceChain(const Player pla, const std::vector<Loc>& newChainLocs,
                              const std::vector<Loc>& newMaybeCapturingLocs) {
  auto& maybeCaptureLocs = pla == P_BLACK ? firstPlaMaybeCaptureLocs : secondPlaMaybeCaptureLocs;

  assert(!newChainLocs.empty());
  for (const Loc newChainLoc : newChainLocs) {
    assert(board.getColor(newChainLoc) == pla);
    resetChainPlayer(newChainLoc);
  }

  for (const Loc newMaybeCapturingLoc : newMaybeCapturingLocs) {
    assert(board.getColor(newMaybeCapturingLoc) == C_EMPTY);
    unsetMaybeCapturePlayer(newMaybeCapturingLoc, pla);
  }

  const auto sizeBefore = maybeCaptureLocs.size();

  maybeCaptureLocs.erase(
    std::remove_if(maybeCaptureLocs.begin(), maybeCaptureLocs.end(), [&](const int value) {
      return std::find(newMaybeCapturingLocs.begin(), newMaybeCapturingLocs.end(), value) != newMaybeCapturingLocs.end();
    }),
    maybeCaptureLocs.end()
  );

  assert(sizeBefore - newMaybeCapturingLocs.size() == maybeCaptureLocs.size());
}

DotsLaddersEvaluator::LadderMoveInfoType DotsLaddersEvaluator::getChainCaptureLocType(const Loc loc, const Player pla,
                                                                    const bool requireAtLeastTwoUnconnectedDotsForLadder) const {
  const State state = board.getState(loc);
  if (getActiveColor(state) != C_EMPTY) {
    return EMPTY;
  }
  if (const Color emptyTerritoryColor = getEmptyTerritoryColor(state);
    emptyTerritoryColor == pla || (emptyTerritoryColor != C_EMPTY && !board.wouldBeCaptureDots(loc, pla))
  ) {
    return EMPTY;
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

  // The requireAtLeastTwoUnconnectedDotsForLadder is actual when opp defends by counter-capturing:
  // In this case it's known that pla dot always should connect a chain to proceed with the ladder.
  // Unfortunately, we can't rely on the ladder's chain because it might not be complete:
  // we check empty locs *before* dot placement, but the chain is fully-formed *after* the dot placement.
  return chainConnectionLocsSize == 1 && (!requireAtLeastTwoUnconnectedDotsForLadder || unconnectedLocsSize >= 2)
           ? LADDER
           : EMPTY;
}

std::string DotsLaddersEvaluator::debugChainsData() const {
  std::ostringstream stream;
  for (int y = 0; y < board.y_size; y++) {
    for (int x = 0; x < board.x_size; x++) {
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

