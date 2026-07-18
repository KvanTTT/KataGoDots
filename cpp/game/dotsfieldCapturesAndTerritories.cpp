#include "board.h"

using namespace std;

Board::BaseInfo::BaseInfo(const Base& base, const Loc newCaptureLoc) {
  player = base.pla;
  captureLoc = newCaptureLoc;
  type = base.type;
  removed = false;
  territory.reserve(base.rollback_locs_states_captures.size());

  for(const auto& rollback_locs_states_capture: base.rollback_locs_states_captures) {
    territory.insert(rollback_locs_states_capture.getLoc());
  }
}

Board::BaseInfo::RelationType Board::BaseInfo::getRelationTo(const Base& other, const Loc otherCaptureLoc) const {
  int commonLocsCount = 0;

  bool otherContainsCaptureLoc = false;
  for (const auto& rollback_locs_states_capture : other.rollback_locs_states_captures) {
    Loc otherTerritoryLoc = rollback_locs_states_capture.getLoc();
    otherContainsCaptureLoc = otherContainsCaptureLoc || otherTerritoryLoc == captureLoc;
    if (territory.find(otherTerritoryLoc) != territory.end()) {
      commonLocsCount++;
    }
  }

  const bool containsCaptureLoc = territory.find(otherCaptureLoc) != territory.end();

  if (commonLocsCount == 0) {
    assert(player != other.pla && "Bases of same color can have only SUPERSET or SUBSET relation");
  } else {
    // Overlapping -> relation must be SUPERSET or SUBSET
    if (player == other.pla) {
      if (territory.size() > commonLocsCount) {
        assert(other.rollback_locs_states_captures.size() == commonLocsCount);
        return RelationType::SUPERSET;
      }

      assert(
        other.rollback_locs_states_captures.size() > commonLocsCount &&
        territory.size() == commonLocsCount &&
        "Surrounding locs sets of same color should be either subsets or supersets but they neither equal nor different"
      );
      return RelationType::SUBSET;
    }

    if (other.rollback_locs_states_captures.size() == commonLocsCount) {
      return RelationType::SUPERSET;
    }

    if (territory.size() == commonLocsCount) {
      return RelationType::SUBSET;
    }
  }

  return containsCaptureLoc && otherContainsCaptureLoc
    ? RelationType::INNER_CAPTURE
    : RelationType::OUTER_CAPTURE_OR_UNRELATED;
}

void Board::CaptureAndTerritoryInfos::addCaptureInfo(BaseInfo* newBaseInfo) {
  [[maybe_unused]] bool added = false;
  assert(newBaseInfo != nullptr && "Attempt to add null base info");
  for (BaseInfo*& baseInfo : captureBaseInfos) {
    assert(baseInfo != newBaseInfo && "Attempt to add an existing base info");
    if (!added && baseInfo == nullptr) {
      baseInfo = newBaseInfo;
      added = true;
    }
  }
  assert(added && "Too many capture locs, max is 4");
}

void Board::CaptureAndTerritoryInfos::addTerritoryInfo(BaseInfo* newBaseInfo) {
  [[maybe_unused]] bool added = false;
  assert(newBaseInfo != nullptr && "Attempt to add null base info");
  for (BaseInfo*& baseInfo : territoryBaseInfos) {
    assert(baseInfo != newBaseInfo && "Attempt to add an existing base info");
    if (!added && baseInfo == nullptr) {
      baseInfo = newBaseInfo;
      added = true;
    }
  }
  assert(added && "Too many capture locs, max is 2");
}

bool Board::CaptureAndTerritoryInfos::removeCaptureAndTerritoryInfos(BaseInfo* baseInfoToRemove) {
  bool captureBaseInfoRemoved = false;
  assert(baseInfoToRemove != nullptr && "Attempt to remove null base info");
  for (BaseInfo*& baseInfo : captureBaseInfos) {
    if (baseInfo == baseInfoToRemove) {
      assert(!captureBaseInfoRemoved && "Capture base info is already removed");
      baseInfo = nullptr;
      captureBaseInfoRemoved = true;
    }
  }
  bool territoryBaseInfoRemoved = false;
  for (BaseInfo*& baseInfo : territoryBaseInfos) {
    if (baseInfo == baseInfoToRemove) {
      assert(!territoryBaseInfoRemoved && "Capture base info is already removed");
      baseInfo = nullptr;
      territoryBaseInfoRemoved = true;
    }
  }
  baseInfoToRemove->removed = true;
  return captureBaseInfoRemoved || territoryBaseInfoRemoved;
}

Color Board::CaptureAndTerritoryInfos::getOneMoveCaptureColor() const {
  Color result = C_EMPTY;
  for (const BaseInfo* baseInfo : captureBaseInfos) {
    if (baseInfo != nullptr && baseInfo->type == Base::Type::NORMAL) {
      result = static_cast<Color>(result | baseInfo->player);
    }
  }
  return result;
}

Color Board::CaptureAndTerritoryInfos::getOneMoveEmptyCaptureColor() const {
  Color result = C_EMPTY;
  for (const BaseInfo* baseInfo : captureBaseInfos) {
    if (baseInfo != nullptr && baseInfo->type == Base::Type::EMPTY) {
      result = static_cast<Color>(result | baseInfo->player);
    }
  }
  return result;
}

Color Board::CaptureAndTerritoryInfos::getOneMoveTerritoryColor(const Color activeColorAtLoc) const {
  Color result = C_EMPTY;
  for (const BaseInfo* baseInfo : territoryBaseInfos) {
    if (baseInfo != nullptr && baseInfo->type == Base::Type::NORMAL && activeColorAtLoc != baseInfo->player) {
      result = static_cast<Color>(result | baseInfo->player);
    }
  }
  return result;
}

Player Board::CaptureAndTerritoryInfos::getOneMoveEmptyTerritoryPlayer(const Color activeColorAtLoc) const {
  Player result = C_EMPTY;
  for (const BaseInfo* baseInfo : territoryBaseInfos) {
    if (baseInfo != nullptr && baseInfo->type == Base::Type::EMPTY && activeColorAtLoc != baseInfo->player) {
      assert(result == C_EMPTY && "One move empty territory color can be only single");
      result = baseInfo->player;
    }
  }
  return result;
}

Player Board::CaptureAndTerritoryInfos::getZeroMoveEmptyTerritoryPlayer(const Color activeColorAtLoc) const {
  const BaseInfo* result = getZeroMoveEmptyBaseInfo();
  return result == nullptr || result->player == activeColorAtLoc ? C_EMPTY : result->player;
}

Board::BaseInfo* Board::CaptureAndTerritoryInfos::getZeroMoveEmptyBaseInfo() const {
  BaseInfo* result = nullptr;
  for (BaseInfo* baseInfo : territoryBaseInfos) {
    if (baseInfo != nullptr && baseInfo->type == Base::Type::SUICIDAL) {
      assert(result == nullptr && "Zero move territory color can be only single");
      result = baseInfo;
    }
  }
  return result;
}

bool Board::CaptureAndTerritoryInfos::hasAnyTerritory(const Player pla) const {
  for (const BaseInfo* baseInfo : territoryBaseInfos) {
    if (baseInfo != nullptr && baseInfo->player == pla) {
      return true;
    }
  }
  return false;
}

bool Board::CaptureAndTerritoryInfos::isReasonableMove(const Player currentPla) const {
  // First priority: check for real captures
  for (const BaseInfo* captureBaseInfo : captureBaseInfos) {
    if (captureBaseInfo == nullptr) continue;

    assert(captureBaseInfo->type != Base::Type::SUICIDAL && "Suicidal bases mustn't have capturing locations");

    if (captureBaseInfo->type == Base::Type::NORMAL && captureBaseInfo->player == currentPla) {
      // Always reasonable if it captures anything
      // Own capturing moves don't overlap with own territory thanks to the preliminary refinement
      return true;
    }
  }

  bool reasonable = false;
  bool territoryExists = false;

  // Second priority
  for (const BaseInfo* baseInfo : territoryBaseInfos) {
    if (baseInfo == nullptr) continue;

    if (baseInfo->type == Base::Type::NORMAL) {
      // Never reasonable: placing into a territory that can be surrounded by the next move
      return false;
    }

    // Otherwise, it's reasonable if only it's a territory of the current player
    // In most cases such moves are useless;
    // however, unfortunately they can't be just dropped because enclosure ungrounded dots might prevent grounding.
    assert(!territoryExists || baseInfo->player == currentPla);
    reasonable = baseInfo->player == currentPla;

    territoryExists = true;
  }

  return !territoryExists || reasonable;
}

Board::CapturesAndTerritoriesInfos::CapturesAndTerritoriesInfos(const int size) {
  capturesAndTerritoriesInfos.resize(size);
}

Board::CaptureAndTerritoryInfos* Board::CapturesAndTerritoriesInfos::at(const int index) const {
  return capturesAndTerritoriesInfos[index];
}

Board::CaptureAndTerritoryInfos& Board::CapturesAndTerritoriesInfos::getOrPut(const int index) {
  CaptureAndTerritoryInfos* result = capturesAndTerritoriesInfos[index];
  if(result == nullptr) {
    result = new CaptureAndTerritoryInfos();
    capturesAndTerritoriesInfos[index] = result;
  }
  return *result;
}

Board::CaptureAndTerritoryInfos* Board::CapturesAndTerritoriesInfos::put(const int index) {
  assert(nullptr == capturesAndTerritoriesInfos[index]);
  auto* result = new CaptureAndTerritoryInfos();
  capturesAndTerritoriesInfos[index] = result;
  return result;
}

Board::BaseInfo* Board::CapturesAndTerritoriesInfos::addBaseInfo(const Base& base, const Loc captureLoc) {
  const auto baseInfo = new BaseInfo(base, captureLoc);
  baseInfos.push_back(baseInfo);
  return baseInfo;
}

const std::vector<Board::BaseInfo*>& Board::CapturesAndTerritoriesInfos::getBaseInfos() const {
  return baseInfos;
}

Board::CapturesAndTerritoriesInfos::~CapturesAndTerritoriesInfos() {
  for(const auto& baseInfo: baseInfos) {
    delete baseInfo;
  }
  baseInfos.clear();
  for(const auto& captureAndTerritoryInfos: capturesAndTerritoriesInfos) {
    delete captureAndTerritoryInfos;
  }
  capturesAndTerritoriesInfos.clear();
}

void Board::recalculateCapturesAndTerritories(
  const Player pla,
  const Loc loc,
  CapturesAndTerritoriesInfos& capturesAndTerritoriesInfos) const {

  CaptureAndTerritoryInfos* captureAndTerritoryInfoAtCaptureLoc = capturesAndTerritoriesInfos.at(loc);

  // Optimization: if the dot is placed into own territory it's expected that a larger and more optimal surrounding exists
  // with corresponding (more outer) capturing location
  if (captureAndTerritoryInfoAtCaptureLoc && captureAndTerritoryInfoAtCaptureLoc->hasAnyTerritory(pla)) {
    return;
  }

  const MoveRecord moveRecord = const_cast<Board*>(this)->playMoveRecordedDots(loc, pla);

  for (Base const& base: moveRecord.bases) {
    const bool isSuicidal = base.type == Base::Type::SUICIDAL;

    if (captureAndTerritoryInfoAtCaptureLoc == nullptr) {
      captureAndTerritoryInfoAtCaptureLoc = capturesAndTerritoriesInfos.put(loc);
    }

    if (isSuicidal && captureAndTerritoryInfoAtCaptureLoc->hasAnyTerritory(base.pla)) {
      // Optimization: don't recalculate same or more inner territory on each suicidal move
      // It differs from the above one, because the suicide can be determined only after the placement
      continue;
    }

    unordered_set<BaseInfo*> overlappedBaseInfos;

    for (auto const& rollback_locs_states_capture: base.rollback_locs_states_captures) {
      const Loc territoryLoc = rollback_locs_states_capture.getLoc();
      for (BaseInfo* territoryBaseInfo : capturesAndTerritoriesInfos.getOrPut(territoryLoc).territoryBaseInfos) {
        if (territoryBaseInfo != nullptr) {
          overlappedBaseInfos.insert(territoryBaseInfo);
        }
      }
    }

    unordered_set<BaseInfo*> baseInfosToRemove;

    bool shouldAddNewBaseInfo = true;
    for (BaseInfo* overlappedBaseInfo : overlappedBaseInfos) {
      const BaseInfo::RelationType relationType = overlappedBaseInfo->getRelationTo(base, loc);
      if (relationType == BaseInfo::RelationType::SUPERSET) {
        // It's expected that the supersets are only actual for opp surroundings
        // Because own surroundings should be filtered out at the beginning of the method
        assert(overlappedBaseInfo->player != base.pla);
        // Optimization: superset supersedes the subset -> break the loop, because further traversal doesn't make sense
        shouldAddNewBaseInfo = false;
        break;
      }
      if (relationType == BaseInfo::RelationType::SUBSET) {
        // Remove the subset. Don't break the loop because multiple subsets are legal.
        baseInfosToRemove.insert(overlappedBaseInfo);
      }
    }

    BaseInfo* newBaseInfo = shouldAddNewBaseInfo
      ? capturesAndTerritoriesInfos.addBaseInfo(base, loc)
      : nullptr;

    for (auto const& rollback_locs_states_capture: base.rollback_locs_states_captures) {
      const Loc territoryLoc = rollback_locs_states_capture.getLoc();
      auto* captureAndTerritoryInfoAtTerritoryLoc = capturesAndTerritoriesInfos.at(territoryLoc);

      for (auto& baseInfoToRemove: baseInfosToRemove) {
        captureAndTerritoryInfoAtTerritoryLoc->removeCaptureAndTerritoryInfos(baseInfoToRemove);
      }

      if (newBaseInfo != nullptr) {
        captureAndTerritoryInfoAtTerritoryLoc->addTerritoryInfo(newBaseInfo);
      }
    }

    for (auto& baseInfoToRemove: baseInfosToRemove) {
      captureAndTerritoryInfoAtCaptureLoc->removeCaptureAndTerritoryInfos(baseInfoToRemove);
    }

    if (newBaseInfo != nullptr && !isSuicidal) {
      captureAndTerritoryInfoAtCaptureLoc->addCaptureInfo(newBaseInfo);
    }
  }

  const_cast<Board*>(this)->undoDots(moveRecord);
}

Board::CapturesAndTerritoriesInfos Board::calculateCapturesAndTerritoriesColorsForDots() const {
  CapturesAndTerritoriesInfos capturesAndTerritoriesInfos(getMaxArrSize(x_size, y_size));

  // Don't calculate anything if the game is already over (finished) because it doesn't look correct
  if (is_finished) {
    return capturesAndTerritoriesInfos;
  }

  for (int y = 0; y < y_size; y++) {
    for (int x = 0; x < x_size; x++) {
      const Loc loc = Location::getLoc(x, y, x_size);

      const State state = getState(loc);
      // Optimization: perform fast check to avoid more heavy checks and extra allocations.
      // The most frequent case is the case when the location is empty.
      if (getActiveColor(state) != C_EMPTY) continue;

      const Color emptyTerritoryColor = getEmptyTerritoryColor(state);
      const Color colorsOfPotentialCapturing = getColorsOfPotentialCapturing(loc);

      // It doesn't make sense to calculate capturing when the dot placed into own empty territory
      // Also, consider only potentially unconnected locs because `playMoveRecordedDots` with `undo` calls are expensive
      if (emptyTerritoryColor == P_WHITE || (emptyTerritoryColor == C_EMPTY && colorsOfPotentialCapturing & C_BLACK)) {
        recalculateCapturesAndTerritories(P_BLACK, loc, capturesAndTerritoriesInfos);
      }

      if (emptyTerritoryColor == P_BLACK || (emptyTerritoryColor == C_EMPTY && colorsOfPotentialCapturing & C_WHITE)) {
        recalculateCapturesAndTerritories(P_WHITE, loc, capturesAndTerritoriesInfos);
      }
    }
  }

  return capturesAndTerritoriesInfos;
}