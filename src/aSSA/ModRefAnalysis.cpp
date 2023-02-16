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
cl::opt<bool> optDumpModRef("dump-modref",
                            cl::desc("Dump the mod/ref analysis"),
                            cl::cat(ParcoachCategory));
#endif

} // namespace
ModRefAnalysisResult::ModRefAnalysisResult(PTACallGraph const &CG,
                                           Andersen const &PTA,
                                           ExtInfo const &extInfo,
                                           MemReg const &Regions, Module &M)
    : CG(CG), PTA(PTA), extInfo(extInfo), Regions(Regions) {
  analyze(M);
}

ModRefAnalysisResult::~ModRefAnalysisResult() {}

void ModRefAnalysisResult::visitAllocaInst(AllocaInst &I) {
  auto *r = Regions.getValueRegion(&I);
  assert(r);
  funcLocalMap[curFunc].insert(r);
}

void ModRefAnalysisResult::visitLoadInst(LoadInst &I) {
  std::vector<Value const *> ptsSet;
  bool Found = PTA.getPointsToSet(I.getPointerOperand(), ptsSet);
  // FIXME: should this be an actual error?
  assert(Found && "Load not found");
  if (!Found)
    return;
  MemRegVector regs;
  Regions.getValuesRegion(ptsSet, regs);

  for (auto *r : regs) {
    if (globalKillSet.find(r) != globalKillSet.end())
      continue;
    funcRefMap[curFunc].insert(r);
  }
}

void ModRefAnalysisResult::visitStoreInst(StoreInst &I) {
  std::vector<Value const *> ptsSet;
  bool Found = PTA.getPointsToSet(I.getPointerOperand(), ptsSet);
  // FIXME: should this be an actual error?
  assert(Found && "Store not found");
  if (!Found)
    return;
  MemRegVector regs;
  Regions.getValuesRegion(ptsSet, regs);

  for (auto *r : regs) {
    if (globalKillSet.find(r) != globalKillSet.end())
      continue;
    funcModMap[curFunc].insert(r);
  }
}

void ModRefAnalysisResult::visitCallBase(CallBase &CB) {
  // For each external function called, add the region of each pointer
  // parameters passed to the function to the ref set of the called
  // function. Regions are added to the Mod set only if the parameter is
  // modified in the callee.

  CallInst *CI = cast<CallInst>(&CB);
  Function const *callee = CI->getCalledFunction();
#if defined(PARCOACH_ENABLE_CUDA) || defined(PARCOACH_ENABLE_OPENMP)
  Collective const *Coll = callee ? Collective::find(*callee) : nullptr;
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
    for (auto *r : getRange(Regions.getOmpSharedRegions(), CI->getFunction())) {
      if (globalKillSet.find(r) != globalKillSet.end())
        continue;
      funcModMap[curFunc].insert(r);
    }
  }
#endif

  // indirect call
  if (!callee) {
    bool mayCallExternalFunction = false;
    for (Function const *mayCallee : CG.getIndirectCallMap().lookup(CI)) {
      if (mayCallee->isDeclaration() && !isIntrinsicDbgFunction(mayCallee)) {
        mayCallExternalFunction = true;
        break;
      }
    }
    if (!mayCallExternalFunction)
      return;
  }

  // direct call
  else {
    if (!callee->isDeclaration())
      return;

    if (isIntrinsicDbgFunction(callee))
      return;
  }

  for (unsigned i = 0; i < CI->arg_size(); ++i) {
    Value *arg = CI->getArgOperand(i);
    if (arg->getType()->isPointerTy() == false)
      continue;

    // Case where argument is a inttoptr cast (e.g. MPI_IN_PLACE)
    ConstantExpr *ce = dyn_cast<ConstantExpr>(arg);
    if (ce) {
      Instruction *inst = ce->getAsInstruction();
      assert(inst);
      bool isAIntToPtr = isa<IntToPtrInst>(inst);
      inst->deleteValue();
      if (isAIntToPtr) {
        continue;
      }
    }

    std::vector<Value const *> argPtsSet;

    bool Found = PTA.getPointsToSet(arg, argPtsSet);
    assert(Found && "arg not found in ModRefAnalysisResult");
    if (!Found) {
      continue;
    }
    MemRegVector regs;
    Regions.getValuesRegion(argPtsSet, regs);

    for (auto *r : regs) {
      if (globalKillSet.find(r) != globalKillSet.end())
        continue;
      funcRefMap[curFunc].insert(r);
    }

    // direct call
    if (callee) {
      auto const *info = extInfo.getExtModInfo(callee);

      if (info) {
        // Variadic argument
        if (i >= info->NbArgs) {
          // errs() << "Function: " << callee->getName() << " in " <<
          // callee->getParent()->getName() << "\n";
          assert(callee->isVarArg());

          if (info->ArgIsMod[info->NbArgs - 1]) {
            for (auto *r : regs) {
              if (globalKillSet.find(r) != globalKillSet.end())
                continue;
              funcModMap[curFunc].insert(r);
            }
          }
        } else {
          // Normal argument
          if (info->ArgIsMod[i]) {
            for (auto *r : regs) {
              if (globalKillSet.find(r) != globalKillSet.end())
                continue;
              funcModMap[curFunc].insert(r);
            }
          }
        }
      }
    } else { // indirect call
      for (Function const *mayCallee : CG.getIndirectCallMap().lookup(CI)) {
        if (!mayCallee->isDeclaration() || isIntrinsicDbgFunction(mayCallee))
          continue;

        auto const *info = extInfo.getExtModInfo(mayCallee);
        if (!info) {
          continue;
        }

        // Variadic argument
        if (i >= info->NbArgs) {
          assert(mayCallee->isVarArg());

          if (info->ArgIsMod[info->NbArgs - 1]) {
            for (auto *r : regs) {
              if (globalKillSet.find(r) != globalKillSet.end())
                continue;
              funcModMap[curFunc].insert(r);
            }
          }
        }

        // Normal argument
        else {
          if (info->ArgIsMod[i]) {
            for (auto *r : regs) {
              if (globalKillSet.find(r) != globalKillSet.end())
                continue;
              funcModMap[curFunc].insert(r);
            }
          }
        }
      }
    }
  }

  // Compute mof/ref for return value if it is a pointer.
  if (callee) {
    auto const *info = extInfo.getExtModInfo(callee);

    if (callee->getReturnType()->isPointerTy()) {
      std::vector<Value const *> retPtsSet;
      bool Found = PTA.getPointsToSet(CI, retPtsSet);
      assert(Found && "callee not found in ModRefAnalysisResult");
      if (!Found) {
        return;
      }

      MemRegVector regs;
      Regions.getValuesRegion(retPtsSet, regs);
      for (auto *r : regs) {
        if (globalKillSet.find(r) != globalKillSet.end())
          continue;
        funcRefMap[curFunc].insert(r);
      }

      if (info && info->RetIsMod) {
        for (auto *r : regs) {
          if (globalKillSet.find(r) != globalKillSet.end())
            continue;
          funcModMap[curFunc].insert(r);
        }
      }
    }
  }

  else {
    for (Function const *mayCallee : CG.getIndirectCallMap().lookup(CI)) {
      if (!mayCallee->isDeclaration() || isIntrinsicDbgFunction(mayCallee))
        continue;

      auto const *info = extInfo.getExtModInfo(mayCallee);

      if (mayCallee->getReturnType()->isPointerTy()) {
        std::vector<Value const *> retPtsSet;
        bool Found = PTA.getPointsToSet(CI, retPtsSet);
        assert(Found && "CI not found in ModRefAnalysisResult");
        if (!Found) {
          continue;
        }
        MemRegVector regs;
        Regions.getValuesRegion(retPtsSet, regs);
        for (auto *r : regs) {
          if (globalKillSet.find(r) != globalKillSet.end())
            continue;
          funcRefMap[curFunc].insert(r);
        }

        if (info && info->RetIsMod) {
          for (auto *r : regs) {
            if (globalKillSet.find(r) != globalKillSet.end())
              continue;
            funcModMap[curFunc].insert(r);
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
  std::vector<Value const *> allocSites;
  PTA.getAllAllocationSites(allocSites);
  for (Value const *v : allocSites) {
    Instruction const *inst = dyn_cast<Instruction>(v);
    if (!inst)
      continue;
    if (CG.isReachableFromEntry(*inst->getParent()->getParent()))
      continue;
    globalKillSet.insert(Regions.getValueRegion(v));
  }

  // First compute the mod/ref sets of each function from its load/store
  // instructions and calls to external functions.

  for (Function &F : M) {
    if (!CG.isReachableFromEntry(F))
      continue;

    curFunc = &F;
    visit(&F);
  }

  // Then iterate through the PTACallGraph with an SCC iterator
  // and add mod/ref sets from callee to caller.
  scc_iterator<PTACallGraph const *> cgSccIter = scc_begin(&CG);
  while (!cgSccIter.isAtEnd()) {

    auto const &nodeVec = *cgSccIter;

    // For each function in the SCC compute kill sets
    // from callee not in the SCC and update mod/ref sets accordingly.
    for (PTACallGraphNode const *node : nodeVec) {
      Function const *F = node->getFunction();
      if (F == NULL)
        continue;

      for (auto it : *node) {
        Function const *callee = it.second->getFunction();
        if (callee == NULL || F == callee)
          continue;

        // If callee is not in the scc
        // kill(F) = kill(F) U kill(callee) U local(callee)
        if (find(nodeVec.begin(), nodeVec.end(), it.second) == nodeVec.end()) {
          for (auto *r : funcLocalMap[callee]) {
            funcKillMap[F].insert(r);
          }

          // Here we have to use a vector to store regions we want to add into
          // the funcKillMap because iterators in a DenseMap are invalidated
          // whenever an insertion occurs unlike map.
          MemRegVector killToAdd;
          for (auto *r : funcKillMap[callee]) {
            killToAdd.push_back(r);
          }
          for (auto *r : killToAdd) {
            funcKillMap[F].insert(r);
          }
        }
      }

      // Mod(F) = Mod(F) \ kill(F)
      // Ref(F) = Ref(F) \ kill(F)
      MemRegVector toRemove;
      for (auto *r : funcModMap[F]) {
        if (funcKillMap[F].find(r) != funcKillMap[F].end())
          toRemove.push_back(r);
      }
      for (auto *r : toRemove) {
        funcModMap[F].erase(r);
      }
      toRemove.clear();
      for (auto *r : funcRefMap[F]) {
        if (funcKillMap[F].find(r) != funcKillMap[F].end())
          toRemove.push_back(r);
      }
      for (auto *r : toRemove) {
        funcRefMap[F].erase(r);
      }
    }

    // For each function in the SCC, update mod/ref sets until reaching a fixed
    // point.
    bool changed = true;

    while (changed) {
      changed = false;

      for (PTACallGraphNode const *node : nodeVec) {
        Function const *F = node->getFunction();
        if (F == NULL)
          continue;

        unsigned modSize = funcModMap[F].size();
        unsigned refSize = funcRefMap[F].size();

        for (auto it : *node) {
          Function const *callee = it.second->getFunction();
          if (callee == NULL || F == callee)
            continue;

          // Mod(caller) = Mod(caller) U (Mod(callee) \ Kill(caller)
          // Ref(caller) = Ref(caller) U (Ref(callee) \ Kill(caller)
          MemRegSet modToAdd;
          MemRegSet refToAdd;
          modToAdd.insert(funcModMap[callee].begin(), funcModMap[callee].end());
          refToAdd.insert(funcRefMap[callee].begin(), funcRefMap[callee].end());
          for (auto *r : modToAdd) {
            if (funcKillMap[F].find(r) == funcKillMap[F].end())
              funcModMap[F].insert(r);
          }
          for (auto *r : refToAdd) {
            if (funcKillMap[F].find(r) == funcKillMap[F].end())
              funcRefMap[F].insert(r);
          }
        }

        if (funcModMap[F].size() > modSize || funcRefMap[F].size() > refSize)
          changed = true;
      }
    }

    ++cgSccIter;
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
  scc_iterator<PTACallGraph const *> cgSccIter = scc_begin(&CG);
  while (!cgSccIter.isAtEnd()) {
    auto const &nodeVec = *cgSccIter;

    for (PTACallGraphNode const *node : nodeVec) {
      Function *F = node->getFunction();
      if (F == NULL || isIntrinsicDbgFunction(F))
        continue;

      errs() << "Mod/Ref for function " << F->getName() << ":\n";
      errs() << "Mod(";
      for (auto *r : getFuncMod(F))
        errs() << r->getName() << ", ";
      errs() << ")\n";
      errs() << "Ref(";
      for (auto *r : getFuncRef(F))
        errs() << r->getName() << ", ";
      errs() << ")\n";
      errs() << "Local(";
      for (auto *r : funcLocalMap.lookup(F))
        errs() << r->getName() << ", ";
      errs() << ")\n";
      errs() << "Kill(";
      for (auto *r : getFuncKill(F))
        errs() << r->getName() << ", ";
      errs() << ")\n";
    }

    ++cgSccIter;
  }
  errs() << "GlobalKill(";
  for (auto *r : globalKillSet) {
    errs() << r->getName() << ", ";
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
  if (optDumpModRef) {
    MRA->dump();
  }
#endif
  return MRA;
}

} // namespace parcoach
