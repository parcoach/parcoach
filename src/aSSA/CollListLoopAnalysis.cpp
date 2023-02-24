#include "parcoach/CollListLoopAnalysis.h"

#include "PTACallGraph.h"
#include "Utils.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

#define DEBUG_TYPE "coll-list-loop"

using namespace llvm;

namespace parcoach {
namespace {
struct CollListLoopVisitor : LoopVisitor<CollListLoopVisitor> {
  CollListLoopVisitor(PTACallGraph const &CG,
                      SmallPtrSetImpl<Value *> const &Comms)
      : PTACG(CG), Communicators(Comms){};
  PTACallGraph const &PTACG;
  SmallPtrSetImpl<Value *> const &Communicators;
  CollectiveList::CommToBBToCollListMap CollListsPerComm;
  void VisitBB(Loop &L, BasicBlock *BB) {
    LLVM_DEBUG({
      dbgs() << "VisitBB in loop:";
      BB->printAsOperand(dbgs());
      dbgs() << "\n";
    });
    if (BB == L.getHeader()) {
      for (Value *Comm : Communicators) {
        CollListsPerComm[Comm].insert({BB, CollectiveList()});
      }
      return;
    }
    for (Value *Comm : Communicators) {
      // assert(CollListsPerComm.count(Comm) && "Predecessor must be computed");
      auto &Lists = CollListsPerComm[Comm];
      auto ItSet = Lists.find(BB);
      if (ItSet != Lists.end()) {
        // It's the header of a nested loop.
        LLVM_DEBUG({
          dbgs() << "Using already inserted set: " << ItSet->second.toString()
                 << "\n";
        });
        return;
      }
      CollectiveList const &PredSet = Lists.lookup(*pred_begin(BB));
      bool IsNAVS = !LAI_.LoopHeaderToIncomingBlock.count(BB) &&
                    CollectiveList::NeighborsAreNAVS(Lists, BB, pred_begin(BB),
                                                     pred_end(BB));
      CollectiveList Current =
          CollectiveList::CreateFromBB(Lists, PTACG, IsNAVS, *BB, Comm);
      if (!IsNAVS) {
        Current.prependWith(PredSet);
      }
      LLVM_DEBUG({
        dbgs() << "Comm::Insert: " << Current.toString() << " for bb ";
        BB->printAsOperand(dbgs());
        dbgs() << " and comm ";
        Comm->print(dbgs());
        dbgs() << "\n";
      });
      Lists.insert({BB, std::move(Current)});
    }
  }

  void EndOfLoop(Loop &L) {
    BasicBlock *Incoming, *BackEdge;
    L.getIncomingAndBackEdge(Incoming, BackEdge);
    for (Value *Comm : Communicators) {
      auto &Lists = CollListsPerComm[Comm];
      assert(Lists.count(L.getHeader()) && Lists[L.getHeader()].empty() &&
             "header not empty?!");
      Lists[L.getHeader()] = CollectiveList(L.getHeader(), Lists[BackEdge]);
    }
  }

  LoopCFGInfo Visit(Function &F, FunctionAnalysisManager &FAM) {
    LoopVisitor<CollListLoopVisitor>::Visit(F, FAM);
    return {std::move(LAI_), std::move(CollListsPerComm)};
  }
};
} // namespace

LoopCFGInfo CollListLoopAnalysis::run(Function &F,
                                      FunctionAnalysisManager &FAM) {
  TimeTraceScope TTS("CollListLoopAnalysis");
  CollListLoopVisitor Visitor = CollListLoopVisitor(PTACG, Communicators);
  LLVM_DEBUG(
      { dbgs() << "Running CollListLoopAnalysis on " << F.getName() << "\n"; });
  return Visitor.Visit(F, FAM);
}

#ifndef NDEBUG
void LoopCFGInfo::dump() const {
  LLVM_DEBUG({
    dbgs() << "header to successorsMap:\n";
    for (auto &[L, Successors] : LAI.LoopHeaderToSuccessors) {
      dbgs() << "LHeader: ";
      L->printAsOperand(dbgs());
      dbgs() << ", succ: ";
      for (auto *BB : Successors) {
        BB->printAsOperand(dbgs());
        dbgs() << ", ";
      }
      dbgs() << "\n";
    }
    dbgs() << "Successor to loop header:\n";
    for (auto const &[BB, Pred] : LAI.LoopSuccessorToLoopHeader) {
      dbgs() << "BB: ";
      BB->printAsOperand(dbgs());
      dbgs() << ", preds: ";
      Pred->printAsOperand(dbgs());
      dbgs() << "\n";
    }
    dbgs() << "Header to incoming:\n";
    for (auto const &[BB, Incoming] : LAI.LoopHeaderToIncomingBlock) {
      dbgs() << "BB: ";
      BB->printAsOperand(dbgs());
      dbgs() << ", Incoming: ";
      Incoming->printAsOperand(dbgs());
      dbgs() << "\n";
    }
    dbgs() << "Coll List per Comm:\n";
    for (auto const &[Comm, ListPerBB] : CommToBBToCollList) {
      dbgs() << "Communicator: ";
      Comm->print(dbgs());
      dbgs() << "\n";
      for (auto const &[BB, List] : ListPerBB) {
        BB->printAsOperand(dbgs());
        dbgs() << ": " << List.toString() << "\n";
      }
      dbgs() << "------------\n";
    }
  });
}
#endif

} // namespace parcoach
