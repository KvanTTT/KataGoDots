#pragma once
#include <utility>

#include "board.h"

class DotsLaddersEvaluator {
public:
  struct LadderLocInfo {
    static LadderLocInfo createZero(const Player pla) {
      return LadderLocInfo(Board::NULL_LOC, pla, nullptr, nullptr, 0);
    }

    static LadderLocInfo createInfinity(const Player pla) {
      return LadderLocInfo(Board::RESIGN_LOC, pla, nullptr, nullptr, 0);
    }

    static LadderLocInfo create(const Loc workingLoc, const Player pla, const Board& field, const Board::MoveRecord& moveRecord, const int initialWhiteScore) {
      return LadderLocInfo(workingLoc, pla, &field, &moveRecord, initialWhiteScore);
    }

    [[nodiscard]] bool isZero() const {
      return workingLoc == Board::NULL_LOC;
    }

    [[nodiscard]] bool isInfinity() const {
      return workingLoc == Board::RESIGN_LOC;
    }

    // Prioritize the score over territory.
    [[nodiscard]] bool isWorseThan(const LadderLocInfo& other) const {
      if (score < other.score) {
        return true;
      }
      if (score > other.score) {
        return false;
      }

      return territoryLocs.size() < other.territoryLocs.size();
    }

    Loc workingLoc;
    Player player;
    short score;
    std::vector<Loc> territoryLocs;

  private:
    explicit LadderLocInfo(const Loc workingLoc, const Player pla, const Board* field, const Board::MoveRecord* moveRecord, const int initialWhiteScore)
      : workingLoc(workingLoc), player(pla) {
      if (!isZero() && !isInfinity()) {
        assert(!moveRecord->bases.empty());
        for (const auto& base : moveRecord->bases) {
          assert(base.pla == pla);
          if (base.type == Board::Base::Type::EMPTY) continue;
          for (const auto& state: base.rollback_locs_states_captures) {
            territoryLocs.push_back(state.getLoc());
          }
        }
        // Consider the whole board score instead of scoring of the created bases.
        // It provides more refined evaluation.
        score = static_cast<short>(pla == P_BLACK
                 ? field->numWhiteCaptures - field->numBlackCaptures + initialWhiteScore
                 : field->numBlackCaptures - field->numWhiteCaptures - initialWhiteScore);
      } else {
        assert(nullptr == moveRecord);
        score = isInfinity() ? std::numeric_limits<short>::max() : 0;
      }
    }
  };

  enum LadderMoveInfoType : uint8_t {
    EMPTY,
    LADDER,
    CAPTURE,
  };

  explicit DotsLaddersEvaluator() = delete;

  explicit DotsLaddersEvaluator(Board& newBoard) : board(newBoard) {
    chainsData.resize(getMaxArrSize(newBoard.x_size, newBoard.y_size));
    initialWhiteScore = static_cast<short>(newBoard.numBlackCaptures - newBoard.numWhiteCaptures);
    // Reserve memory to get rid of excessive reallocations.
    // The field perimeter size should be enough for most cases.
    walkStack.reserve((newBoard.x_size + newBoard.y_size) * 2);
  }

  std::vector<LadderLocInfo> evaluate();
  [[nodiscard]] uint16_t getMovesCount() const { return movesCounter; }
  [[nodiscard]] uint16_t getMaxDepth() const { return maxDepth; }
  [[nodiscard]] uint16_t getCurrentDepth() const { return currentDepth; }
  [[nodiscard]] short getInitialWhiteScore() const { return initialWhiteScore; }
  void clearMaxDepth() { maxDepth = 0; }

private:
  bool isRelevantCapturing(const Board::MoveRecord& moveRecord, Loc initLoc, Player pla);

  LadderLocInfo iterateForPlayer(Loc initLoc, Loc loc, Player pla);

  LadderLocInfo iterateAdjLocs(Loc initLoc, Loc loc, Player pla, bool requireAtLeastTwoUnconnectedDotsForLadder);

  LadderLocInfo iterateForOpp(Loc initLoc, Loc loc, Player pla);

  void initializeOpponentChain(Player pla, const Board::MoveRecord& capturingMoveRecord);

  Board::MoveRecord playAndExtendChain(const Loc loc, const Player pla) {
    const Board::MoveRecord moveRecord = play(loc, pla);
    createAndPushChainInfo(loc, pla);
    return moveRecord;
  }

  Board::MoveRecord play(const Loc loc, const Player pla) {
    const auto moveRecord = board.playMoveRecordedDots(loc, pla);

    movesCounter++;
    currentDepth++;
    if (currentDepth > maxDepth) {
      maxDepth = currentDepth;
    }

    return moveRecord;
  }

  void reduceChainAndUndo(const Board::MoveRecord& moveRecord) {
    popChainInfo(moveRecord.pla);
    undo(moveRecord);
  }

  void undo(const Board::MoveRecord& moveRecord) {
    board.undoDots(moveRecord);
    currentDepth--;
  }

  void createAndPushChainInfo(Loc loc, Player pla);

  void createAndPushChainInfo(const std::vector<Loc>& locs, Player pla);

  void popChainInfo(Player pla);

  [[nodiscard]] std::vector<Loc> extractCaptureLocs(Player pla) const;

  [[nodiscard]] Color getChainColor(const Loc loc) const {
    const auto color = static_cast<Color>(chainsData[loc] & 0b11);
    assert(color != C_WALL && "Chains can't have colors of both player");
    return color;
  }

  void setChainPlayer(const Loc loc, const Player pla) {
    chainsData[loc] = static_cast<char>(chainsData[loc] | pla);
  }

  void resetChainPlayer(const Loc loc) {
    chainsData[loc] = static_cast<char>(chainsData[loc] & ~0b11);
  }

  [[nodiscard]] Color getMaybeCaptureColor(const Loc loc) const {
    return static_cast<Color>(chainsData[loc] >> 2 & 0b11);
  }

  [[nodiscard]] bool alreadyMaybeCapture(const Loc loc, const Player pla) const {
    return (chainsData[loc] >> 2 & pla) != 0;
  }

  void setMaybeCapturePlayer(const Loc loc, const Player pla) {
    chainsData[loc] = static_cast<char>(chainsData[loc] | pla << 2);
  }

  void unsetMaybeCapturePlayer(const Loc loc, const Player pla) {
    chainsData[loc] = static_cast<char>(chainsData[loc] & ~(pla << 2));
  }

  void setInitTerritory(const Loc loc) {
    chainsData[loc] = static_cast<char>(chainsData[loc] | 0b10000);
    initTerritory.push_back(loc);
  }

  void resetInitTerritory() {
    assert(currentDepth == 1);
    assert(!initTerritory.empty());
    for (const Loc loc : initTerritory) {
      chainsData[loc] = static_cast<char>(chainsData[loc] & ~0b10000);
    }
    initTerritory.clear();
  }

  [[nodiscard]] bool isInitTerritory(const Loc loc) const {
    return (chainsData[loc] & 0b10000) != 0;
  }

  [[nodiscard]] bool maybeChainCaptureLoc(const Loc loc, const Player pla) const {
    return getChainCaptureLocType(loc, pla, false) == CAPTURE;
  }

  [[nodiscard]] LadderMoveInfoType getChainCaptureLocType(Loc loc, Player pla, bool requireAtLeastTwoUnconnectedDotsForLadder) const;

  [[nodiscard]] std::string debugChainsData() const;

  uint16_t currentDepth = 0;
  uint16_t maxDepth = 0;
  uint16_t movesCounter = 0;
  short initialWhiteScore = 0;

  struct ChainsInfo {
    uint16_t newChainLocsCount;
    uint16_t newMaybeCaptureLocsCount;
  };

  Board& board;
  std::vector<char> chainsData;
  std::vector<Loc> walkStack;
  std::vector<Loc> initTerritory;
  std::vector<Loc> firstPlaChainLocs;
  std::vector<Loc> secondPlaChainLocs;
  std::vector<Loc> firstPlaMaybeCaptureLocs;
  std::vector<Loc> secondPlaMaybeCaptureLocs;
  std::vector<ChainsInfo> firstPlaChainInfos;
  std::vector<ChainsInfo> secondPlaChainInfos;
};