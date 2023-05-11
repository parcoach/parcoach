#include "PTACallGraph.h"
#include "Utils.h"

#include "parcoach/Collectives.h"
#include "parcoach/ModRefAnalysis.h"
#include "parcoach/Options.h"

#include "llvm/ADT/SCCIterator.h"

using namespace llvm;

namespace parcoach {
namespace {
#ifndef NDEBUG
cl::opt<bool> OptDumpModRef("dump-modref",
                            cl::desc("Dump the mod/ref analysis"),
                            cl::cat(ParcoachCategory));
#endif

} // namespace
ModRefAnalysisResult::ModRefAnalysisResult(PTACallGraph const &CG,
                                           Andersen const &PTA,
                                           ExtInfo const &ExtInfo,
                                           MemReg const &Regions, Module &M)
    : CG(CG), PTA(PTA), extInfo(ExtInfo), Regions(Regions) {
  analyze(M);
}

ModRefAnalysisResult::~ModRefAnalysisResult() {}

void ModRefAnalysisResult::visitAllocaInst(AllocaInst &I) {
  auto *R = Regions.getValueRegion(&I);
  assert(R);
  funcLocalMap[curFunc].insert(R);
}

void ModRefAnalysisResult::visitLoadInst(LoadInst &I) {
  std::vector<Value const *> PtsSet;
  bool Found = PTA.getPointsToSet(I.getPointerOperand(), PtsSet);
  // FIXME: should this be an actual error?
  assert(Found && "Load not found");
  if (!Found) {
    return;
  }
  MemRegVector Regs;
  Regions.getValuesRegion(PtsSet, Regs);

  for (auto *R : Regs) {
    if (globalKillSet.find(R) != globalKillSet.end()) {
      continue;
    }
    funcRefMap[curFunc].insert(R);
  }
}

void ModRefAnalysisResult::visitStoreInst(StoreInst &I) {
  std::vector<Value const *> PtsSet;
  bool Found = PTA.getPointsToSet(I.getPointerOperand(), PtsSet);
  // FIXME: should this be an actual error?
  assert(Found && "Store not found");
  if (!Found) {
    return;
  }
  MemRegVector Regs;
  Regions.getValuesRegion(PtsSet, Regs);

  for (auto *R : Regs) {
    if (globalKillSet.find(R) != globalKillSet.end()) {
      continue;
    }
    funcModMap[curFunc].insert(R);
  }
}

void ModRefAnalysisResult::visitCallBase(CallBase &CB) {
  // For each external function called, add the region of each pointer
  // parameters passed to the function to the ref set of the called
  // function. Regions are added to the Mod set only if the parameter is
  // modified in the callee.

  CallInst *CI = cast<CallInst>(&CB);
  Function const *Callee = CI->getCalledFunction();
#if defined(PARCOACH_ENABLE_CUDA) || defined(PARCOACH_ENABLE_OPENMP)
  Collective const *Coll = Callee ? Collective::find(*Callee) : nullptr;
#endif

#ifdef PARCOACH_ENABLE_CUDA
  // In CUDA after a synchronization, all region in shared memory are written.
  if (Coll && isa<CudaCollective>(Coll) && Coll->Name == "llvm.nvvm.barrier0") {
    for (auto *r : Regions.getCudaSharedRegions()) {
      if (globalKillSet.find(r) != globalKillSet.end()) {
        continue;
      }
      funcModMap[curFunc].insert(r);
    }
  }
#endif

#ifdef PARCOACH_ENABLE_OPENMP
  // In OpenMP after a synchronization, all region in shared memory are written.
  if (Coll && isa<OMPCollective>(Coll) && Coll->Name == "__kmpc_barrier") {
    for (auto *R : getRange(Regions.getOmpSharedRegions(), CI->getFunction())) {
      if (globalKillSet.find(R) != globalKillSet.end()) {
        continue;
      }
      funcModMap[curFunc].insert(R);
    }
  }
#endif

  // indirect call
  if (!Callee) {
    bool MayCallExternalFunction = false;
    for (Function const *MayCallee : CG.getIndirectCallMap().lookup(CI)) {
      if (MayCallee->isDeclaration() && !isIntrinsicDbgFunction(MayCallee)) {
        MayCallExternalFunction = true;
        break;
      }
    }
    if (!MayCallExternalFunction) {
      return;
    }
  }

  // direct call
  else {
    if (!Callee->isDeclaration()) {
      return;
    }

    if (isIntrinsicDbgFunction(Callee)) {
      return;
    }
  }

  for (unsigned I = 0; I < CI->arg_size(); ++I) {
    Value *Arg = CI->getArgOperand(I);
    if (!Arg->getType()->isPointerTy()) {
      continue;
    }

    // Case where argument is a inttoptr cast (e.g. MPI_IN_PLACE)
    ConstantExpr *Ce = dyn_cast<ConstantExpr>(Arg);
    if (Ce) {
      Instruction *Inst = Ce->getAsInstruction();
      assert(Inst);
      bool IsAIntToPtr = isa<IntToPtrInst>(Inst);
      Inst->deleteValue();
      if (IsAIntToPtr) {
        continue;
      }
    }

    std::vector<Value const *> ArgPtsSet;

    bool Found = PTA.getPointsToSet(Arg, ArgPtsSet);
    assert(Found && "arg not found in ModRefAnalysisResult");
    if (!Found) {
      continue;
    }
    MemRegVector Regs;
    Regions.getValuesRegion(ArgPtsSet, Regs);

    for (auto *R : Regs) {
      if (globalKillSet.find(R) != globalKillSet.end()) {
        continue;
      }
      funcRefMap[curFunc].insert(R);
    }

    // direct call
    if (Callee) {
      auto const *Info = extInfo.getExtModInfo(Callee);

      if (Info) {
        // Variadic argument
        if (I >= Info->NbArgs) {
          // errs() << "Function: " << callee->getName() << " in " <<
          // callee->getParent()->getName() << "\n";
          assert(Callee->isVarArg());

          if (Info->ArgIsMod[Info->NbArgs - 1]) {
            for (auto *R : Regs) {
              if (globalKillSet.find(R) != globalKillSet.end()) {
                continue;
              }
              funcModMap[curFunc].insert(R);
            }
          }
        } else {
          // Normal argument
          if (Info->ArgIsMod[I]) {
            for (auto *R : Regs) {
              if (globalKillSet.find(R) != globalKillSet.end()) {
                continue;
              }
              funcModMap[curFunc].insert(R);
            }
          }
        }
      }
    } else { // indirect call
      for (Function const *MayCallee : CG.getIndirectCallMap().lookup(CI)) {
        if (!MayCallee->isDeclaration() || isIntrinsicDbgFunction(MayCallee)) {
          continue;
        }

        auto const *Info = extInfo.getExtModInfo(MayCallee);
        if (!Info) {
          continue;
        }

        // Variadic argument
        if (I >= Info->NbArgs) {
          assert(MayCallee->isVarArg());

          if (Info->ArgIsMod[Info->NbArgs - 1]) {
            for (auto *R : Regs) {
              if (globalKillSet.find(R) != globalKillSet.end()) {
                continue;
              }
              funcModMap[curFunc].insert(R);
            }
          }
        }

        // Normal argument
        else {
          if (Info->ArgIsMod[I]) {
            for (auto *R : Regs) {
              if (globalKillSet.find(R) != globalKillSet.end()) {
                continue;
              }
              funcModMap[curFunc].insert(R);
            }
          }
        }
      }
    }
  }

  // Compute mof/ref for return value if it is a pointer.
  if (Callee) {
    auto const *Info = extInfo.getExtModInfo(Callee);

    if (Callee->getReturnType()->isPointerTy()) {
      std::vector<Value const *> RetPtsSet;
      bool Found = PTA.getPointsToSet(CI, RetPtsSet);
      assert(Found && "callee not found in ModRefAnalysisResult");
      if (!Found) {
        return;
      }

      MemRegVector Regs;
      Regions.getValuesRegion(RetPtsSet, Regs);
      for (auto *R : Regs) {
        if (globalKillSet.find(R) != globalKillSet.end()) {
          continue;
        }
        funcRefMap[curFunc].insert(R);
      }

      if (Info && Info->RetIsMod) {
        for (auto *R : Regs) {
          if (globalKillSet.find(R) != globalKillSet.end()) {
            continue;
          }
          funcModMap[curFunc].insert(R);
        }
      }
    }
  }

  else {
    for (Function const *MayCallee : CG.getIndirectCallMap().lookup(CI)) {
      if (!MayCallee->isDeclaration() || isIntrinsicDbgFunction(MayCallee)) {
        continue;
      }

      auto const *Info = extInfo.getExtModInfo(MayCallee);

      if (MayCallee->getReturnType()->isPointerTy()) {
        std::vector<Value const *> RetPtsSet;
        bool Found = PTA.getPointsToSet(CI, RetPtsSet);
        assert(Found && "CI not found in ModRefAnalysisResult");
        if (!Found) {
          continue;
        }
        MemRegVector Regs;
        Regions.getValuesRegion(RetPtsSet, Regs);
        for (auto *R : Regs) {
          if (globalKillSet.find(R) != globalKillSet.end()) {
            continue;
          }
          funcRefMap[curFunc].insert(R);
        }

        if (Info && Info->RetIsMod) {
          for (auto *R : Regs) {
            if (globalKillSet.find(R) != globalKillSet.end()) {
              continue;
            }
            funcModMap[curFunc].insert(R);
          }
        }
      }
    }
  }
}

void ModRefAnalysisResult::analyze(Module &M) {
  TimeTraceScope TTS("ModRefAnalysis");
  // Compute global kill set containing regions whose allocation sites are
  // in functions not reachable from prog entry.
  std::vector<Value const *> AllocSites;
  PTA.getAllAllocationSites(AllocSites);
  for (Value const *V : AllocSites) {
    Instruction const *Inst = dyn_cast<Instruction>(V);
    if (!Inst) {
      continue;
    }
    if (CG.isReachableFromEntry(*Inst->getParent()->getParent())) {
      continue;
    }
    globalKillSet.insert(Regions.getValueRegion(V));
  }

  // First compute the mod/ref sets of each function from its load/store
  // instructions and calls to external functions.

  for (Function &F : M) {
    if (!CG.isReachableFromEntry(F)) {
      continue;
    }

    curFunc = &F;
    visit(&F);
  }

  // Then iterate through the PTACallGraph with an SCC iterator
  // and add mod/ref sets from callee to caller.
  scc_iterator<PTACallGraph const *> CgSccIter = scc_begin(&CG);
  while (!CgSccIter.isAtEnd()) {

    auto const &NodeVec = *CgSccIter;

    // For each function in the SCC compute kill sets
    // from callee not in the SCC and update mod/ref sets accordingly.
    for (PTACallGraphNode const *Node : NodeVec) {
      Function const *F = Node->getFunction();
      if (F == NULL) {
        continue;
      }

      for (auto It : *Node) {
        Function const *Callee = It.second->getFunction();
        if (Callee == NULL || F == Callee) {
          continue;
        }

        // If callee is not in the scc
        // kill(F) = kill(F) U kill(callee) U local(callee)
        if (find(NodeVec.begin(), NodeVec.end(), It.second) == NodeVec.end()) {
          for (auto *R : funcLocalMap[Callee]) {
            funcKillMap[F].insert(R);
          }

          // Here we have to use a vector to store regions we want to add into
          // the funcKillMap because iterators in a DenseMap are invalidated
          // whenever an insertion occurs unlike map.
          MemRegVector KillToAdd;
          for (auto *R : funcKillMap[Callee]) {
            KillToAdd.push_back(R);
          }
          for (auto *R : KillToAdd) {
            funcKillMap[F].insert(R);
          }
        }
      }

      // Mod(F) = Mod(F) \ kill(F)
      // Ref(F) = Ref(F) \ kill(F)
      MemRegVector ToRemove;
      for (auto *R : funcModMap[F]) {
        if (funcKillMap[F].find(R) != funcKillMap[F].end()) {
          ToRemove.push_back(R);
        }
      }
      for (auto *R : ToRemove) {
        funcModMap[F].erase(R);
      }
      ToRemove.clear();
      for (auto *R : funcRefMap[F]) {
        if (funcKillMap[F].find(R) != funcKillMap[F].end()) {
          ToRemove.push_back(R);
        }
      }
      for (auto *R : ToRemove) {
        funcRefMap[F].erase(R);
      }
    }

    // For each function in the SCC, update mod/ref sets until reaching a fixed
    // point.
    bool Changed = true;

    while (Changed) {
      Changed = false;

      for (PTACallGraphNode const *Node : NodeVec) {
        Function const *F = Node->getFunction();
        if (F == NULL) {
          continue;
        }

        unsigned ModSize = funcModMap[F].size();
        unsigned RefSize = funcRefMap[F].size();

        for (auto It : *Node) {
          Function const *Callee = It.second->getFunction();
          if (Callee == NULL || F == Callee) {
            continue;
          }

          // Mod(caller) = Mod(caller) U (Mod(callee) \ Kill(caller)
          // Ref(caller) = Ref(caller) U (Ref(callee) \ Kill(caller)
          MemRegSet ModToAdd;
          MemRegSet RefToAdd;
          ModToAdd.insert(funcModMap[Callee].begin(), funcModMap[Callee].end());
          RefToAdd.insert(funcRefMap[Callee].begin(), funcRefMap[Callee].end());
          for (auto *R : ModToAdd) {
            if (funcKillMap[F].find(R) == funcKillMap[F].end()) {
              funcModMap[F].insert(R);
            }
          }
          for (auto *R : RefToAdd) {
            if (funcKillMap[F].find(R) == funcKillMap[F].end()) {
              funcRefMap[F].insert(R);
            }
          }
        }

        if (funcModMap[F].size() > ModSize || funcRefMap[F].size() > RefSize) {
          Changed = true;
        }
      }
    }

    ++CgSccIter;
  }
}

MemRegSet ModRefAnalysisResult::getFuncMod(Function const *F) const {
  return funcModMap.lookup(F);
}

MemRegSet ModRefAnalysisResult::getFuncRef(Function const *F) const {
  return funcRefMap.lookup(F);
}

MemRegSet ModRefAnalysisResult::getFuncKill(Function const *F) const {
  return funcKillMap.lookup(F);
}

bool ModRefAnalysisResult::inGlobalKillSet(MemRegEntry *R) const {
  return globalKillSet.count(R) > 0;
}

#ifndef NDEBUG
void ModRefAnalysisResult::dump() const {
  scc_iterator<PTACallGraph const *> CgSccIter = scc_begin(&CG);
  while (!CgSccIter.isAtEnd()) {
    auto const &NodeVec = *CgSccIter;

    for (PTACallGraphNode const *Node : NodeVec) {
      Function *F = Node->getFunction();
      if (F == NULL || isIntrinsicDbgFunction(F)) {
        continue;
      }

      errs() << "Mod/Ref for function " << F->getName() << ":\n";
      errs() << "Mod(";
      for (auto *R : getFuncMod(F)) {
        errs() << R->getName() << ", ";
      }
      errs() << ")\n";
      errs() << "Ref(";
      for (auto *R : getFuncRef(F)) {
        errs() << R->getName() << ", ";
      }
      errs() << ")\n";
      errs() << "Local(";
      for (auto *R : funcLocalMap.lookup(F)) {
        errs() << R->getName() << ", ";
      }
      errs() << ")\n";
      errs() << "Kill(";
      for (auto *R : getFuncKill(F)) {
        errs() << R->getName() << ", ";
      }
      errs() << ")\n";
    }

    ++CgSccIter;
  }
  errs() << "GlobalKill(";
  for (auto *R : globalKillSet) {
    errs() << R->getName() << ", ";
  }
  errs() << ")\n";
}
#endif

AnalysisKey ModRefAnalysis::Key;
ModRefAnalysis::Result ModRefAnalysis::run(Module &M,
                                           ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("ModRefAnalysisPass");
  auto &AA = AM.getResult<AndersenAA>(M);
  auto &PTA = AM.getResult<PTACallGraphAnalysis>(M);
  auto &ExtInfo = AM.getResult<ExtInfoAnalysis>(M);
  auto &Regions = AM.getResult<MemRegAnalysis>(M);
  auto MRA =
      std::make_unique<ModRefAnalysisResult>(*PTA, AA, *ExtInfo, *Regions, M);
#ifndef NDEBUG
  // FIXME: migrating this anywhere should work, we should make
  // the omp thingy an analysis pass.
  if (OptDumpModRef) {
    MRA->dump();
  }
#endif
  return MRA;
}

} // namespace parcoach
