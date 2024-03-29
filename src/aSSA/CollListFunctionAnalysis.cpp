#include "parcoach/CollListFunctionAnalysis.h"

#include "parcoach/CollectiveList.h"
#include "parcoach/DepGraphDCF.h"
#include "parcoach/MPICommAnalysis.h"
#include "parcoach/Options.h"

#include "PTACallGraph.h"
#include "Utils.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/InstIterator.h"

#define DEBUG_TYPE "coll-list-func"

using namespace llvm;

namespace parcoach {
namespace {

struct CollListCFGVisitor : CFGVisitor<CollListCFGVisitor> {
  using CFGVisitor::CFGVisitor;
  CollListCFGVisitor(ModuleAnalysisManager &AM, PTACallGraph const &CG,
                     SmallPtrSetImpl<Value *> const &Comms)
      : CFGVisitor(AM), PTACG(CG), Communicators(Comms){};
  PTACallGraph const &PTACG;
  SmallPtrSetImpl<Value *> const &Communicators;
  CollectiveList::CommToBBToCollListMap CollListsPerComm;

  template <typename SuccessorsRange>
  CollectiveList compute(CollectiveList::BBToCollListMap &Lists,
                         SuccessorsRange Successors, BasicBlock *BB,
                         bool IsLoopHeader, Value *Comm) {
    bool IsNAVS = CollectiveList::NeighborsAreNAVS(
        Lists, BB, Successors.begin(), Successors.end());
    LLVM_DEBUG(dbgs() << "NAVS computed from succ: " << IsNAVS << "\n");
    CollectiveList Current{};

    if (IsLoopHeader) {
      if (IsNAVS) {
        Current.extendWith<NavsElement>();
      } else {
        // Push the original loop coll set
        Current.extendWith(Lists[BB]);
      }
    } else {
      Current = CollectiveList::CreateFromBB(Lists, PTACG, IsNAVS, *BB, Comm);
    }

    LLVM_DEBUG(dbgs() << "Current CollectiveList: " << Current.toString()
                      << "\n");

    if (!IsNAVS) {
      // If we're looking at the exit node it doesn't have any successor.
      if (!empty(Successors)) {
        assert(Lists.count(*Successors.begin()) &&
               "Successor should have been computed already.");
        CollectiveList const &SuccSet = Lists[*Successors.begin()];
        Current.extendWith(SuccSet);
      }
    }
    return Current;
  }

  void visitBB(BasicBlock *BB,
               LoopAggretationInfo const *LoopAnalysisResult = nullptr) {
    LLVM_DEBUG({
      dbgs() << "CFGVisitor::visitBB::";
      if (LoopAnalysisResult) {
        dbgs() << "header::";
      }
      BB->printAsOperand(dbgs());
      dbgs() << "\n";
    });
    for (Value *Comm : Communicators) {
      auto &Lists = CollListsPerComm[Comm];
      CollectiveList Current;
      if (LoopAnalysisResult) {
        assert(Lists[BB].getLoopHeader() &&
               "Visiting header which is not a loop?!");
        auto const &Successors =
            LoopAnalysisResult->LoopHeaderToSuccessors.find(BB)->second;
        Lists[BB] =
            compute(Lists, make_range(Successors.begin(), Successors.end()), BB,
                    true, Comm);
      } else {
        Lists[BB] = compute(Lists, successors(BB), BB, false, Comm);
      }
    }
  }

  void Visit(Function &F, LoopCFGInfo const &LoopAnalysisResult) {
    for (auto const &[Comm, Lists] : LoopAnalysisResult.CommToBBToCollList) {
      for (auto const &[BB, Colls] : Lists) {
        CollListsPerComm[Comm].insert({BB, Colls});
      }
    }
    CFGVisitor::Visit(F, LoopAnalysisResult.LAI);
    LLVM_DEBUG({
      dbgs() << "CollLists per communicator at end of function:\n";
      for (auto const &[Comm, Lists] : CollListsPerComm) {
        dbgs() << "comm:";
        Comm->print(dbgs());
        dbgs() << "\n";
        for (auto const &[BB, Set] : Lists) {
          BB->printAsOperand(dbgs());
          dbgs() << ": " << Set.toString() << "\n";
        }
        dbgs() << "-----\n";
      }
    });
  }
};

void checkWarnings(
    Function &F, CollectiveList::CommToBBToCollListMap const &CollListsPerComm,
    DepGraphDCF const &DG, WarningCollection &Warnings,
    FunctionAnalysisManager &FAM, bool EmitDotDG) {
  llvm::LoopInfo &LI = FAM.getResult<llvm::LoopAnalysis>(F);
  auto IsaDirectCallToCollective = [](Instruction const &I) {
    if (CallInst const *CI = dyn_cast<CallInst>(&I)) {
      Function const *F = CI->getCalledFunction();
      if (F) {
        return Collective::isCollective(*F);
      }
    }
    return false;
  };
  auto Candidates =
      make_filter_range(instructions(F), IsaDirectCallToCollective);
  for (Instruction &I : Candidates) {
    CallInst &CI = cast<CallInst>(I);
    Function &F = *CI.getCalledFunction();
    Collective const *Coll = Collective::find(F);
    assert(Coll && "Coll expected to be not null because of filter");
    // Get conditionals from the callsite
    std::set<BasicBlock const *> CallIpdf;
    DG.getCallInterIPDF(&CI, CallIpdf);
    LLVM_DEBUG(dbgs() << "Call to " << Coll->Name << "\n");
    LLVM_DEBUG(dbgs() << "callIPDF size: " << CallIpdf.size() << "\n");
    Value *CommForCollective{};

    if (auto const *MPIColl = dyn_cast<MPICollective>(Coll)) {
      CommForCollective = MPIColl->getCommunicator(CI);
    }

    Warning::ConditionalsContainerTy Conditionals;
    for (BasicBlock const *BB : CallIpdf) {
      LLVM_DEBUG({
        BB->printAsOperand(dbgs());
        dbgs() << " is on path\n";
      });
      Loop const *L = LI[BB];
      // Is this node detected as potentially dangerous by parcoach?
      bool HasNAVS = false;
      for (auto const &[Comm, Lists] : CollListsPerComm) {
        // At this point:
        //   - we have a non-null comm, we'll use it for colllists
        //   - we have a null comm: either it's for non-mpi collectives, or
        //   it's a mpi finalize, in both cases we want to check colllists for
        //   all communicators.
        if (!CommForCollective || Comm == CommForCollective) {
          CollectiveList const &CL =
              L ? Lists.find(L->getHeader())->second : Lists.find(BB)->second;
          LLVM_DEBUG(dbgs() << "CL for BB on IPDF: " << CL.toString() << "\n");
          HasNAVS |= CL.navs();
        }
      }

      if (!HasNAVS) {
        continue;
      }

      // Is this condition tainted?
      Value const *Cond = getBasicBlockCond(BB);

      if (!Cond || !DG.isTaintedValue(Cond)) {
        LLVM_DEBUG(dbgs() << "Not tainted\n");
        continue;
      }

      DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
      Instruction const *Inst = BB->getTerminator();
      DebugLoc Loc = Inst->getDebugLoc();
      Conditionals.push_back(Loc);

      if (EmitDotDG) {
        std::string Dotfilename("taintedpath-");
        std::string Filename = Loc->getFilename().str();
        size_t LastposSlash = Filename.find_last_of('/');
        if (LastposSlash != std::string::npos) {
          Filename = Filename.substr(LastposSlash + 1, Filename.size());
        }
        Dotfilename.append(Filename).append("-");
        Dotfilename.append(std::to_string(Loc.getLine())).append(".dot");
        DG.dotTaintPath(Cond, Dotfilename, &CI);
      }
    } // END FOR

    if (Conditionals.empty()) {
      continue;
    }
    Warnings.insert(
        {&CI, Warning(F, CI.getDebugLoc(), std::move(Conditionals))});
  }
}
} // namespace

llvm::AnalysisKey CollListFunctionAnalysis::Key;
llvm::AnalysisKey CollectiveAnalysis::Key;

CollectiveAnalysis::Result
CollectiveAnalysis::run(llvm::Module &M,
                        llvm::ModuleAnalysisManager &AM) const {
  TimeTraceScope TTS("ParcoachCollectiveAnalysis");
  PTACallGraph const &PTACG = *AM.getResult<PTACallGraphAnalysis>(M);
  auto &DG = AM.getResult<DepGraphDCFAnalysis>(M);
  auto &BBToCollList = AM.getResult<CollListFunctionAnalysis>(M);
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  WarningCollection Result;
  scc_iterator<PTACallGraph const *> CgSccIter = scc_begin(&PTACG);
  while (!CgSccIter.isAtEnd()) {
    auto const &NodeVec = *CgSccIter;
    for (PTACallGraphNode const *Node : NodeVec) {
      Function *F = Node->getFunction();
      if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(*F)) {
        continue;
      }
      checkWarnings(*F, *BBToCollList, *DG, Result, FAM, EmitDotDG_);
    }
    ++CgSccIter;
  }
  return std::make_unique<WarningCollection>(std::move(Result));
}

CollListFunctionAnalysis::Result
CollListFunctionAnalysis::run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("CollListFunctionAnalysis");
  auto const &Res = AM.getResult<PTACallGraphAnalysis>(M);
  PTACallGraph const &PTACG = *Res;
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  scc_iterator<PTACallGraph const *> CgSccIter = scc_begin(&PTACG);
  auto Comms = AM.getResult<MPICommAnalysis>(M);
  if (Comms.empty()) {
    // We're likely checking collectives other than MPI, insert a null comm.
    Comms.insert(nullptr);
  }
  CollListCFGVisitor Visitor(AM, PTACG, Comms);
  // This loop "analysis" actually uses the PTACG to build collective list
  // for indirect calls!
  CollListLoopAnalysis LoopAnalysis(PTACG, Comms);
  while (!CgSccIter.isAtEnd()) {
    auto const &NodeVec = *CgSccIter;
    for (PTACallGraphNode const *Node : NodeVec) {
      Function *F = Node->getFunction();
      if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(*F)) {
        continue;
      }
      // FIXME: we should definitely share the visitor's BBToCollList with the
      // loop analysis!
      LoopCFGInfo Info = LoopAnalysis.run(*F, FAM);
      LLVM_DEBUG({
        dbgs() << "LoopCFGInfo for " << F->getName() << ":\n";
        Info.dump();
      });
      Visitor.Visit(*F, Info);
    }
    ++CgSccIter;
  }
  return std::make_unique<CollectiveList::CommToBBToCollListMap>(
      std::move(Visitor.CollListsPerComm));
}
} // namespace parcoach
