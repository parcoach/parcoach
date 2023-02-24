#pragma once

#include "parcoach/CollectiveList.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/ValueMap.h"

#include <vector>

namespace parcoach {

using BBToBBMap = llvm::DenseMap<llvm::BasicBlock *, llvm::BasicBlock *>;
using BBToVectorBBMap =
    llvm::DenseMap<llvm::BasicBlock *, llvm::SmallVector<llvm::BasicBlock *>>;
enum class NodeState {
  UNVISITED = 0,
  PUSHED,
  VISITED,
};

struct LoopAggretationInfo {
  BBToVectorBBMap LoopHeaderToSuccessors;
  BBToBBMap LoopHeaderToIncomingBlock;
  BBToBBMap LoopSuccessorToLoopHeader;
  void clear();
};

using VisitedMapTy = llvm::ValueMap<llvm::BasicBlock *, NodeState>;

// This is a top-down BFS that visit loops from the innermost to the outermost.
// It also computes the successors for each loop "as a whole", as well
// as predecessors (ie: matching loop headers) for these successors.
template <typename Derived> class LoopVisitor {
  Derived &getDerived() { return *static_cast<Derived *>(this); }
  void ComputeReversedMap() {
    for (auto &[Header, Successors] : LAI_.LoopHeaderToSuccessors) {
      for (llvm::BasicBlock *Succ : Successors) {
        assert(LAI_.LoopSuccessorToLoopHeader.count(Succ) == 0 &&
               "Unexpected number of successors");
        LAI_.LoopSuccessorToLoopHeader[Succ] = Header;
      }
    }
  }

  void Visit(llvm::Loop &L, llvm::LoopInfo &LI) {
    for (llvm::Loop *SubLoop : L) {
      Visit(*SubLoop, LI);
    }
    std::deque<llvm::BasicBlock *> Queue;
    VisitedMapTy Visited;
    // Make sure we register our loop.
    auto *Start = L.getHeader();
    LAI_.LoopHeaderToSuccessors[Start] = {};
    Queue.emplace_back(Start);
    Visited[Start] = NodeState::PUSHED;
    llvm::BasicBlock *Incoming, *BackEdge;

    if (!L.getIncomingAndBackEdge(Incoming, BackEdge)) {
      // This is a dead loop, just ignore it.
      return;
    }

    LAI_.LoopHeaderToIncomingBlock[Start] = Incoming;
    while (!Queue.empty()) {
      llvm::BasicBlock *BB = Queue.front();
      Queue.pop_front();

      auto IsDone = [&](llvm::BasicBlock *Succ) {
        return Visited[Succ] == NodeState::VISITED;
      };

      // If the BB is not a loop header, check all predecessors are done!
      if (!LAI_.LoopHeaderToIncomingBlock.count(BB) &&
          !llvm::all_of(llvm::predecessors(BB), IsDone)) {
        Queue.push_back(BB);
        continue;
      }

      Visited[BB] = NodeState::VISITED;
      getDerived().VisitBB(L, BB);
      auto EnqueueBB = [&](llvm::BasicBlock *BB) {
        if (Visited[BB] == NodeState::UNVISITED) {
          if (L.contains(BB)) {
            Visited[BB] = NodeState::PUSHED;
            Queue.emplace_back(BB);
          } else {
            LAI_.LoopHeaderToSuccessors[Start].emplace_back(BB);
          }
        }
      };

      // Try to see if the current BB is a header for a loop other than ours.
      llvm::Loop const *LoopForBB = LI[BB];
      if (BB != Start && LoopForBB && BB == LoopForBB->getHeader()) {
        llvm::for_each(LAI_.LoopHeaderToSuccessors[LoopForBB->getHeader()],
                       EnqueueBB);
        // We just "ate" an inner loop, remove its successors.
        LAI_.LoopHeaderToSuccessors.erase(LoopForBB->getHeader());
      } else {
        llvm::for_each(successors(BB), EnqueueBB);
      }
    }
    getDerived().EndOfLoop(L);
  }

protected:
  LoopAggretationInfo LAI_;

public:
  void Visit(llvm::Function &F, llvm::FunctionAnalysisManager &FAM) {
    LAI_.clear();
    llvm::LoopInfo &LI = FAM.getResult<llvm::LoopAnalysis>(F);
    for (llvm::Loop *L : LI) {
      Visit(*L, LI);
    }
    ComputeReversedMap();
  }
};

template <typename Derived> class CFGVisitor {
  Derived &getDerived() { return *static_cast<Derived *>(this); }

protected:
  llvm::ModuleAnalysisManager &AM;

public:
  CFGVisitor(llvm::ModuleAnalysisManager &MAM) : AM(MAM) {}

  void Visit(llvm::Function &F, LoopAggretationInfo const &Res) {
    VisitedMapTy Visited;

    auto const &PredsMap = Res.LoopSuccessorToLoopHeader;
    auto const &SuccsMap = Res.LoopHeaderToSuccessors;
    auto const &IncomingMap = Res.LoopHeaderToIncomingBlock;

    std::deque<llvm::BasicBlock *> Queue;

    auto HasNoSucc = [](llvm::BasicBlock &BB) {
      return llvm::succ_size(&BB) == 0;
    };
    for (auto &Exit : make_filter_range(F, HasNoSucc)) {
      Visited[&Exit] = NodeState::PUSHED;
      Queue.push_back(&Exit);
    }
    assert(!Queue.empty() && "No exit nodes?!");

    while (!Queue.empty()) {
      llvm::BasicBlock *BB = Queue.front();
      Queue.pop_front();
      auto IsDone = [&](llvm::BasicBlock *Succ) {
        return Visited[Succ] == NodeState::VISITED;
      };
      // Check if this BB belongs to a loop
      bool IsLoopHeader = SuccsMap.count(BB);
      // If visiting a loop header, the succesors are the loop's successors!
      bool AllDone = IsLoopHeader
                         ? llvm::all_of(SuccsMap.find(BB)->second, IsDone)
                         : llvm::all_of(llvm::successors(BB), IsDone);
      // All successors are not visited, postpone the handling.
      if (!AllDone) {
        Queue.push_back(BB);
        continue;
      }

      Visited[BB] = NodeState::VISITED;
      if (IsLoopHeader) {
        getDerived().VisitBB(BB, &Res);
      } else {
        getDerived().VisitBB(BB);
      }
      auto EnqueueBB = [&](llvm::BasicBlock *Current) {
        if (Visited[Current] == NodeState::UNVISITED) {
          Visited[Current] = NodeState::PUSHED;
          Queue.push_back(Current);
        }
      };
      if (IsLoopHeader) {
        // If BB is a loop header, it should have a single incoming edge,
        // add it.
        EnqueueBB(IncomingMap.find(BB)->second);
      } else if (PredsMap.count(BB)) {
        // This BB was a successor of a loop: we want its predecessors to be the
        // header loop itself.
        EnqueueBB(PredsMap.find(BB)->second);
        // We whould have been doing a 1 to 1 replacement, assert that.
        // This will break if a block has a loop pred and a non loop pred,
        // which should not happen.
        assert(pred_size(BB) == 1 && "Unexpected pred size");
      } else {
        // Else just add all preds.
        llvm::for_each(llvm::predecessors(BB), EnqueueBB);
      }
    }
  }
};

} // namespace parcoach
