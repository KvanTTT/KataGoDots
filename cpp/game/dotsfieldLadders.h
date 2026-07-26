#pragma once
#include <memory>
#include <ostream>
#include <utility>

#include "board.h"

class DotsLaddersSolver {
public:
  struct LadderLocInfo {
    static LadderLocInfo createZero(const Player pla) {
      return LadderLocInfo(Board::NULL_LOC, pla, nullptr, 0);
    }

    static LadderLocInfo create(const Loc workingLoc, const Player pla, const Board::MoveRecord& moveRecord, const int whiteScoreDiff) {
      return LadderLocInfo(workingLoc, pla, &moveRecord, whiteScoreDiff);
    }

    [[nodiscard]] bool isZero() const {
      return workingLoc == Board::NULL_LOC;
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
    explicit LadderLocInfo(const Loc workingLoc, const Player pla, const Board::MoveRecord* moveRecord, const int whiteScoreDiff)
      : workingLoc(workingLoc), player(pla) {
      if (!isZero()) {
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
        score = static_cast<short>(pla == P_BLACK ? -whiteScoreDiff : whiteScoreDiff);
      } else {
        assert(nullptr == moveRecord);
        score = 0;
      }
    }
  };

  enum LadderMoveInfoType : uint8_t {
    EMPTY,
    LADDER,
    CAPTURE,
  };

  enum LadderMoveType : uint8_t {
    INVALID,
    CAPTURE_FOUND,
    FALSE_CAPTURE,
    DEFEND_CAPTURE,
    DEFEND_FALSE_CAPTURE,
    DEFEND_MOVE,
    LADDER_SUCCEEDED,
    LADDER_FAILED,
    LADDER_SUCCEEDED_STOP,
    LADDER_FAILED_STOP,
  };

  explicit DotsLaddersSolver() = delete;

  explicit DotsLaddersSolver(Board& newBoard, const bool newStoreMovesTree = false,
    const std::vector<Move>& newInitialMoves = {}, const std::vector<Move>& newExtraMoves = {}) :
      board(newBoard), storeMovesTree(newStoreMovesTree), initialMoves(newInitialMoves), extraMoves(newExtraMoves) {
    chainsData.resize(getMaxArrSize(newBoard.x_size, newBoard.y_size));
    initialWhiteScore = static_cast<short>(newBoard.numBlackCaptures - newBoard.numWhiteCaptures);
    // Reserve memory to get rid of excessive reallocations.
    // The field perimeter size should be enough for most cases.
    walkStack.reserve((newBoard.x_size + newBoard.y_size) * 2);

    movesTreeRoot = std::make_unique<MoveTreeNode>();
    currentMovesTreeNode = movesTreeRoot.get();
  }

  std::vector<LadderLocInfo> solve();

  [[nodiscard]] const LadderLocInfo& zero() const {
    return attacker == P_BLACK ? zeroBlack : zeroWhite;
  }

  [[nodiscard]] uint16_t getMovesCount() const { return movesCounter; }
  [[nodiscard]] uint16_t getMaxDepth() const { return maxDepth; }
  [[nodiscard]] uint16_t getCurrentDepth() const { return currentDepth; }
  [[nodiscard]] short getWhiteScoreDiff() const { return board.numBlackCaptures - board.numWhiteCaptures - initialWhiteScore; }
  void clearMaxDepth() { maxDepth = 0; }
  [[nodiscard]] bool getStoreMovesTree() const { return storeMovesTree; }
  [[nodiscard]] std::string toSgf() const;

private:
  bool isRelevantCapturing(const Board::MoveRecord& moveRecord);

  LadderLocInfo startLadder(Loc newInitLoc, Player pla);

  LadderLocInfo iterateForAttacker(Loc loc);

  LadderLocInfo iterateAttackerLocsAfterDefending(
    const std::vector<std::pair<LadderMoveInfoType, Loc>>& attackerLocsToCheckAfterDefending);

  // Returns true if the passed move record surrounds an attacker loc
  bool isDefendCapture(const Board::MoveRecord& potentialDefendCaptureMoveRecord);

  LadderLocInfo iterateForDefender(Loc loc);

  void appendAttackerLocsToCheck(std::vector<std::pair<LadderMoveInfoType, Loc>>& attackerLocsToCheck,
                                 Loc defenderLastMoveLoc, bool defenderCapture) const;

  void initializeDefenderChain(const Board::MoveRecord& capturingMoveRecord);

  Board::MoveRecord playAndExtendChain(const Loc loc, const Player pla) {
    const Board::MoveRecord moveRecord = play(loc, pla);
    createAndPushChainInfo(loc, pla);
    return moveRecord;
  }

  Board::MoveRecord play(const Loc loc, const Player pla) {
    assert(board.getColor(loc) == C_EMPTY);
    const auto moveRecord = board.playMoveRecordedDots(loc, pla);

    movesCounter++;
    currentDepth++;
    if (currentDepth > maxDepth) {
      maxDepth = currentDepth;
    }

    if (storeMovesTree) {
      auto child = std::make_unique<MoveTreeNode>();
      child->loc = loc;
      child->pla = pla;
      child->number = getMovesCount();

      movesTreeStack.push_back(currentMovesTreeNode);
      currentMovesTreeNode->children.push_back(std::move(child));
      currentMovesTreeNode = currentMovesTreeNode->children.back().get();
    }

    return moveRecord;
  }

  void reduceChainAndUndo(const Board::MoveRecord& moveRecord, const LadderMoveType moveType, const LadderLocInfo* result = nullptr) {
    popChainInfo(moveRecord.pla);
    undo(moveRecord, moveType, result);
  }

  void undo(const Board::MoveRecord& moveRecord, const LadderMoveType moveType, const LadderLocInfo* result = nullptr) {
    board.undoDots(moveRecord);

    if (storeMovesTree) {
      currentMovesTreeNode->moveType = moveType;
      if (result != nullptr) {
        currentMovesTreeNode->score = result->score;
        currentMovesTreeNode->territory = result->territoryLocs.size();
      }
      currentMovesTreeNode = movesTreeStack.back();
      movesTreeStack.pop_back();
    }

    currentDepth--;
  }

  void createAndPushChainInfo(Loc loc, Player pla);

  void createAndPushChainInfo(const std::vector<Loc>& locs, Player pla);

  void popChainInfo(Player pla);

  std::vector<Loc>& getDefenderCurrentCaptureLocs();

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
      chainsData[loc] = static_cast<char>(chainsData[loc] & ~0b1'0000);
    }
    initTerritory.clear();
  }

  [[nodiscard]] bool isInitTerritory(const Loc loc) const {
    return (chainsData[loc] & 0b1'0000) != 0;
  }

  void setVisited(const Loc loc) {
    chainsData[loc] = static_cast<char>(chainsData[loc] | 0b1000'0000);
  }

  [[nodiscard]] bool isVisited(const Loc loc) {
    return (chainsData[loc] & 0b1000'0000) != 0;
  }

  void resetVisited(const Loc loc) {
    chainsData[loc] = static_cast<char>(chainsData[loc] & ~0b1000'0000);
  }

  [[nodiscard]] bool maybeChainCaptureLoc(const Loc loc, const Player pla, const bool ignoreEmptyBaseLocs) const {
    return getChainCaptureLocType(loc, pla, false, ignoreEmptyBaseLocs) == CAPTURE;
  }

  [[nodiscard]] LadderMoveInfoType getChainCaptureLocType(Loc loc, Player pla, bool requireAtLeastTwoUnconnectedDotsForLadder, bool ignoreEmptyBaseLocs) const;

  [[nodiscard]] std::string debugChainsData() const;

  // A node of the tree of explored moves, kept only when storeMovesTree is true.
  // The root node (movesTreeRoot) is synthetic and has loc == Board::NULL_LOC.
  struct MoveTreeNode {
    Loc loc = Board::NULL_LOC;
    Player pla = C_EMPTY;
    LadderMoveType moveType = INVALID;
    short score = 0;
    short territory = 0;
    int number = 0;
    std::vector<std::unique_ptr<MoveTreeNode>> children;
  };

  static void writeSgfCoord(std::ostream& out, Loc loc, int xSize);

  static void writeMovesTreeSgf(std::ostream& out, const MoveTreeNode* node, int xSize);

  Player attacker{};
  Player defender{};
  Loc initLoc{};

  uint16_t currentDepth = 0;
  uint16_t maxDepth = 0;
  uint16_t movesCounter = 0;
  short initialWhiteScore = 0;

  struct ChainsInfo {
    uint16_t newChainLocsCount;
    uint16_t newMaybeCaptureLocsCount;
  };

  Board& board;
  LadderLocInfo zeroBlack = LadderLocInfo::createZero(P_BLACK);
  LadderLocInfo zeroWhite = LadderLocInfo::createZero(P_WHITE);
  bool storeMovesTree;
  std::vector<Move> initialMoves;
  std::vector<Move> extraMoves;

  std::vector<char> chainsData;
  std::vector<Loc> walkStack;
  std::vector<Loc> initTerritory;
  std::vector<Loc> attackerChainLocs;
  std::vector<Loc> defenderChainLocs;
  std::vector<Loc> attackerMaybeCaptureLocs;
  std::vector<Loc> defenderMaybeCaptureLocs;
  std::vector<ChainsInfo> attackerChainInfos;
  std::vector<ChainsInfo> defenderChainInfos;
  std::vector<std::vector<Loc>> defenderCaptureLocs;

  // Tree of explored moves, kept only when storeMovesTree is true, for later dumping via toSgf().
  // currentMovesTreeNode is the node for the current position; movesTreeStack holds its ancestors
  // so that undo() can restore the cursor without discarding already-explored sibling branches.
  std::unique_ptr<MoveTreeNode> movesTreeRoot;
  std::vector<MoveTreeNode*> movesTreeStack;
  MoveTreeNode* currentMovesTreeNode = nullptr;
};