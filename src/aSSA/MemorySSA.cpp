#include "parcoach/MemorySSA.h"

#include "MSSAMuChi.h"
#include "PTACallGraph.h"
#include "Utils.h"
#include "parcoach/Collectives.h"
#include "parcoach/MemoryRegion.h"
#include "parcoach/ModRefAnalysis.h"
#include "parcoach/Options.h"

#include "llvm/IR/InstIterator.h"
#include "llvm/Support/FileSystem.h"

#define DEBUG_TYPE "mssa"

using namespace llvm;

namespace parcoach {
namespace {
cl::opt<bool> OptDumpSsa("dump-ssa", cl::desc("Dump the all-inclusive SSA"),
                         cl::cat(ParcoachCategory));

cl::opt<std::string> OptDumpSsaFunc("dump-ssa-func",
                                    cl::desc("Dump the all-inclusive SSA "
                                             "for a particular function."),
                                    cl::cat(ParcoachCategory));

} // namespace

MemorySSA::MemorySSA(Module &M, Andersen const &PTA, PTACallGraph const &CG,
                     MemReg const &Regions, ModRefAnalysisResult *MRA,
                     ExtInfo const &ExtInfo, ModuleAnalysisManager &AM)
    : PTA(PTA), CG(CG), Regions(Regions), MRA(MRA), extInfo(ExtInfo),
      curDF(NULL), curDT(NULL) {
  buildSSA(M, AM);
}

MemorySSA::~MemorySSA() {}

void MemorySSA::buildSSA(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("MemorySSA");
  // Get an inner FunctionAnalysisManager from the module one.
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  for (Function &F : M) {
    if (!CG.isReachableFromEntry(F)) {
      // errs() << F.getName() << " is not reachable from entry\n";

      continue;
    }

    if (isIntrinsicDbgFunction(&F)) {
      continue;
    }

    if (F.isDeclaration()) {
      continue;
    }

    // errs() << " + Fun: " << counter << " - " << F.getName() << "\n";
    DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
    DominanceFrontier &DF = FAM.getResult<DominanceFrontierAnalysis>(F);
    PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);

    buildSSA(&F, DT, DF, PDT);
    if (OptDumpSsa) {
      dumpMSSA(&F);
    }
    if (F.getName().equals(OptDumpSsaFunc)) {
      dumpMSSA(&F);
    }
  }
}

void MemorySSA::buildSSA(Function const *F, DominatorTree &DT,
                         DominanceFrontier &DF, PostDominatorTree &PDT) {
  curDF = &DF;
  curDT = &DT;
  curPDT = &PDT;

  usedRegs.clear();
  regDefToBBMap.clear();

  {
    TimeTraceScope TTS("parcoach::MemorySSA::ComputeMuChi");
    computeMuChi(F);
  }

  {
    TimeTraceScope TTS("parcoach::MemorySSA::ComputePhi");
    computePhi(F);
  }

  {
    TimeTraceScope TTS("parcoach::MemorySSA::Rename");
    rename(F);
  }

  {
    TimeTraceScope TTS("parcoach::MemorySSA::ComputePhiPredicates");
    computePhiPredicates(F);
  }
}

void MemorySSA::computeMuChi(Function const *F) {
  for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction const *Inst = &*I;

    /* Call Site */
    if (isCallSite(Inst)) {
      if (isIntrinsicDbgInst(Inst)) {
        continue;
      }

      CallBase *Cs = const_cast<CallBase *>(cast<CallBase>(Inst));
      Function *Callee = Cs->getCalledFunction();

      // indirect call
      if (Callee == NULL) {
        for (Function const *MayCallee : CG.getIndirectCallMap().lookup(Inst)) {
          if (isIntrinsicDbgFunction(MayCallee)) {
            continue;
          }

          computeMuChiForCalledFunction(Cs, const_cast<Function *>(MayCallee));
          if (MayCallee->isDeclaration()) {
            createArtificalChiForCalledFunction(Cs, MayCallee);
          }
        }
      }

      // direct call
      else {
        if (isIntrinsicDbgFunction(Callee)) {
          continue;
        }

        computeMuChiForCalledFunction(Cs, Callee);
        if (Callee->isDeclaration()) {
          createArtificalChiForCalledFunction(Cs, Callee);
        }
      }

      continue;
    }

    /* Load Instruction
     * We insert a Mu function for each region pointed-to by
     * the load instruction.
     */
    if (isa<LoadInst>(Inst)) {
      LoadInst const *LI = cast<LoadInst>(Inst);
      std::vector<Value const *> PtsSet;
      bool Found = PTA.getPointsToSet(LI->getPointerOperand(), PtsSet);
      assert(Found && "Load not found in MSSA");
      if (!Found) {
        continue;
      }
      MemRegVector Regs;
      Regions.getValuesRegion(PtsSet, Regs);

      for (auto *R : Regs) {
        if (MRA->inGlobalKillSet(R)) {
          continue;
        }

        loadToMuMap[LI].emplace_back(std::make_unique<MSSALoadMu>(R, LI));
        usedRegs.insert(R);
      }

      continue;
    }

    /* Store Instruction
     * We insert a Chi function for each region pointed-to by
     * the store instruction.
     */
    if (isa<StoreInst>(Inst)) {
      StoreInst const *SI = cast<StoreInst>(Inst);
      std::vector<Value const *> PtsSet;
      bool Found = PTA.getPointsToSet(SI->getPointerOperand(), PtsSet);
      assert(Found && "Store not found in MSSA");
      if (!Found) {
        continue;
      }
      MemRegVector Regs;
      Regions.getValuesRegion(PtsSet, Regs);

      for (auto *R : Regs) {
        if (MRA->inGlobalKillSet(R)) {
          continue;
        }

        storeToChiMap[SI].emplace_back(std::make_unique<MSSAStoreChi>(R, SI));
        usedRegs.insert(R);
        regDefToBBMap[R].insert(Inst->getParent());
      }

      continue;
    }
  }

  /* Create an EntryChi and a ReturnMu for each memory region used by the
   * function.
   */
  for (auto *R : usedRegs) {
    auto &Chi =
        funToEntryChiMap[F].emplace_back(std::make_unique<MSSAEntryChi>(R, F));

    funRegToEntryChiMap[F][Chi->region] = Chi.get();
    regDefToBBMap[R].insert(&F->getEntryBlock());
  }

  if (!functionDoesNotRet(F)) {
    for (auto *R : usedRegs) {
      auto &Mu =
          funToReturnMuMap[F].emplace_back(std::make_unique<MSSARetMu>(R, F));
      funRegToReturnMuMap[F][Mu->region] = Mu.get();
    }
  }
}

void MemorySSA::computeMuChiForCalledFunction(CallBase *Inst,
                                              Function *Callee) {
#if defined(PARCOACH_ENABLE_CUDA) || defined(PARCOACH_ENABLE_OPENMP)
  Collective const *Coll = Collective::find(*Callee);
#endif
#ifdef PARCOACH_ENABLE_CUDA
  // If the called function is a CUDA synchronization, create an artificial CHI
  // for each shared region.
  if (Coll && isa<CudaCollective>(Coll) && Coll->Name == "llvm.nvvm.barrier0") {
    for (auto *r : Regions.getCudaSharedRegions()) {
      callSiteToSyncChiMap[inst].emplace(
          std::make_unique<MSSASyncChi>(r, inst));
      regDefToBBMap[r].insert(inst->getParent());
      usedRegs.insert(r);
    }
    // NOTE (PV): I removed a return here because we want each arg of the
    // barrier to go through the muchi parameters thingy!
    // Otherwise a used region may not appear in the mu map and would lead
    // to breaking an assert!
  }
#endif

#ifdef PARCOACH_ENABLE_OPENMP
  // If the called function is an OMP barrier, create an artificial CHI
  // for each shared region.
  if (Coll && isa<OMPCollective>(Coll) && Coll->Name == "__kmpc_barrier") {
    for (auto *R :
         getRange(Regions.getOmpSharedRegions(), Inst->getFunction())) {
      callSiteToSyncChiMap[Inst].emplace_back(
          std::make_unique<MSSASyncChi>(R, Inst));
      regDefToBBMap[R].insert(Inst->getParent());
      usedRegs.insert(R);
    }
    // NOTE (PV): I removed a return here because we want each arg of the
    // barrier to go through the muchi parameters thingy!
    // Otherwise a used region may not appear in the mu map and would lead
    // to breaking an assert!
  }
#endif

  // If the callee is a declaration (external function), we create a Mu
  // for each pointer argument and a Chi for each modified argument.
  if (Callee->isDeclaration()) {
    assert(isa<CallInst>(Inst)); // InvokeInst are not handled yet
    CallInst *CI = cast<CallInst>(Inst);
    extFuncToCSMap[Callee].insert(Inst);

    auto const *Info = extInfo.getExtModInfo(Callee);

    // Mu and Chi for parameters
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

      std::vector<Value const *> PtsSet;
      std::vector<Value const *> ArgPtsSet;
      bool Found = PTA.getPointsToSet(Arg, ArgPtsSet);
      assert(Found && "arg not found in MSSA");
      if (!Found) {
        continue;
      }
      PtsSet.insert(PtsSet.end(), ArgPtsSet.begin(), ArgPtsSet.end());

      MemRegVector Regs;
      Regions.getValuesRegion(PtsSet, Regs);

      // Mus
      for (auto *R : Regs) {
        if (MRA->inGlobalKillSet(R)) {
          continue;
        }

        callSiteToMuMap[Inst].emplace_back(
            std::make_unique<MSSAExtCallMu>(R, Callee, I));
        usedRegs.insert(R);
      }

      if (!Info) {
        continue;
      }

      // Chis
      if (I >= Info->NbArgs) {
        assert(Callee->isVarArg());
        if (Info->ArgIsMod[Info->NbArgs - 1]) {
          for (auto *R : Regs) {
            if (MRA->inGlobalKillSet(R)) {
              continue;
            }

            callSiteToChiMap[Inst].emplace_back(
                std::make_unique<MSSAExtCallChi>(R, Callee, I, Inst));
            regDefToBBMap[R].insert(Inst->getParent());
          }
        }
      } else {
        if (Info->ArgIsMod[I]) {
          for (auto *R : Regs) {
            if (MRA->inGlobalKillSet(R)) {
              continue;
            }

            callSiteToChiMap[Inst].emplace_back(
                std::make_unique<MSSAExtCallChi>(R, Callee, I, Inst));
            regDefToBBMap[R].insert(Inst->getParent());
          }
        }
      }
    }

    // Chi for return value if it is a pointer
    if (Info && Info->RetIsMod && CI->getType()->isPointerTy()) {
      std::vector<Value const *> PtsSet;
      bool Found = PTA.getPointsToSet(CI, PtsSet);
      assert(Found && "CI not found in MSSA");
      if (!Found) {
        return;
      }

      MemRegVector Regs;
      Regions.getValuesRegion(PtsSet, Regs);

      for (auto *R : Regs) {
        if (MRA->inGlobalKillSet(R)) {
          continue;
        }

        extCallSiteToCallerRetChi[Inst].emplace_back(
            std::make_unique<MSSAExtRetCallChi>(R, Callee));
        regDefToBBMap[R].insert(Inst->getParent());
        usedRegs.insert(R);
      }
    }
  }

  // If the callee is not a declaration we create a Mu(Chi) for each region
  // in Ref(Mod) callee.
  else {
    Function const *Caller = Inst->getParent()->getParent();
    MemRegSet KillSet = MRA->getFuncKill(Caller);

    // Create Mu for each region \in ref(callee)
    for (auto *R : MRA->getFuncRef(Callee)) {
      if (KillSet.find(R) == KillSet.end()) {
        callSiteToMuMap[Inst].emplace_back(
            std::make_unique<MSSACallMu>(R, Callee));
        usedRegs.insert(R);
      }
    }

    // Create Chi for each region \in mod(callee)
    for (auto *R : MRA->getFuncMod(Callee)) {
      if (KillSet.find(R) == KillSet.end()) {
        callSiteToChiMap[Inst].emplace_back(
            std::make_unique<MSSACallChi>(R, Callee, Inst));
        regDefToBBMap[R].insert(Inst->getParent());
        usedRegs.insert(R);
      }
    }
  }
}

void MemorySSA::computePhi(Function const *F) {
  // For each memory region used, compute basic blocks where phi must be
  // inserted.
  for (auto *R : usedRegs) {
    std::vector<BasicBlock const *> Worklist;
    std::set<BasicBlock const *> DomFronPlus;
    std::set<BasicBlock const *> Work;

    for (BasicBlock const *X : regDefToBBMap[R]) {
      Worklist.push_back(X);
      Work.insert(X);
    }

    while (!Worklist.empty()) {
      BasicBlock const *X = Worklist.back();
      Worklist.pop_back();

      auto It = curDF->find(const_cast<BasicBlock *>(X));
      if (It == curDF->end()) {
        errs() << "Error: basic block not in the dom frontier !\n";
        exit(EXIT_FAILURE);
        continue;
      }

      auto DomSet = It->second;
      for (auto *Y : DomSet) {
        if (DomFronPlus.find(Y) != DomFronPlus.end()) {
          continue;
        }

        bbToPhiMap[Y].emplace_back(std::make_unique<MSSAPhi>(R));
        DomFronPlus.insert(Y);

        if (Work.find(Y) != Work.end()) {
          continue;
        }

        Work.insert(Y);
        Worklist.push_back(Y);
      }
    }
  }
}

void MemorySSA::rename(Function const *F) {
  std::map<MemRegEntry *, unsigned> C;
  std::map<MemRegEntry *, std::vector<MSSAVar *>> S;

  // Initialization:

  // C(*) <- 0
  for (auto *R : usedRegs) {
    C[R] = 0;
  }

  // Compute LHS version for each region.
  for (auto &Chi : funToEntryChiMap[F]) {
    Chi->var = std::make_unique<MSSAVar>(Chi.get(), 0, &F->getEntryBlock());

    S[Chi->region].push_back(Chi->var.get());
    C[Chi->region]++;
  }

  renameBB(F, &F->getEntryBlock(), C, S);
}

void MemorySSA::renameBB(Function const *F, llvm::BasicBlock const *X,
                         std::map<MemRegEntry *, unsigned> &C,
                         std::map<MemRegEntry *, std::vector<MSSAVar *>> &S) {
  // Compute LHS for PHI
  for (auto &Phi : bbToPhiMap[X]) {
    auto *V = Phi->region;
    unsigned I = C[V];
    Phi->var = std::make_unique<MSSAVar>(Phi.get(), I, X);
    S[V].push_back(Phi->var.get());
    C[V]++;
  }

  // For each ordinary assignment A do
  //   For each variable V in RHS(A)
  //     Replace use of V by use of Vi where i = Top(S(V))
  //   Let V be LHS(A)
  //   i <- C(V)
  //   replace V by Vi
  //   push i onto S(V)
  //   C(V) <- i + 1
  for (auto I = X->begin(), E = X->end(); I != E; ++I) {
    Instruction const *Inst = &*I;

    if (isCallSite(Inst)) {
      CallBase *Cs(const_cast<CallBase *>(cast<CallBase>(Inst)));

      for (auto &Mu : callSiteToMuMap[Cs]) {
        Mu->var = S[Mu->region].back();
      }

      for (auto &Chi : callSiteToSyncChiMap[Cs]) {
        auto *V = Chi->region;
        unsigned I = C[V];
        Chi->var = std::make_unique<MSSAVar>(Chi.get(), I, Inst->getParent());
        Chi->opVar = S[V].back();
        S[V].push_back(Chi->var.get());
        C[V]++;
      }

      for (auto &Chi : callSiteToChiMap[Cs]) {
        auto *V = Chi->region;
        unsigned I = C[V];
        Chi->var = std::make_unique<MSSAVar>(Chi.get(), I, Inst->getParent());
        Chi->opVar = S[V].back();
        S[V].push_back(Chi->var.get());
        C[V]++;
      }

      for (auto &Chi : extCallSiteToCallerRetChi[Cs]) {
        auto *V = Chi->region;
        unsigned I = C[V];
        Chi->var = std::make_unique<MSSAVar>(Chi.get(), I, Inst->getParent());
        Chi->opVar = S[V].back();
        S[V].push_back(Chi->var.get());
        C[V]++;
      }
    }

    if (isa<StoreInst>(Inst)) {
      StoreInst const *SI = cast<StoreInst>(Inst);
      for (auto &Chi : storeToChiMap[SI]) {
        auto *V = Chi->region;
        unsigned I = C[V];
        Chi->var = std::make_unique<MSSAVar>(Chi.get(), I, Inst->getParent());
        Chi->opVar = S[V].back();
        S[V].push_back(Chi->var.get());
        C[V]++;
      }
    }

    if (isa<LoadInst>(Inst)) {
      LoadInst const *LI = cast<LoadInst>(Inst);
      for (auto &Mu : loadToMuMap[LI]) {
        Mu->var = S[Mu->region].back();
      }
    }

    if (isa<ReturnInst>(Inst)) {
      for (auto &Mu : funToReturnMuMap[F]) {
        Mu->var = S[Mu->region].back();
      }
    }
  }

  // For each successor Y of X
  //   For each Phi function F in Y
  //     Replace operands V by Vi  where i = Top(S(V))
  for (auto I = succ_begin(X), E = succ_end(X); I != E; ++I) {
    BasicBlock const *Y = *I;
    for (auto &Phi : bbToPhiMap[Y]) {
      unsigned Index = whichPred(X, Y);
      Phi->opsVar[Index] = S[Phi->region].back();
    }
  }

  // For each successor of X in the dominator tree
  DomTreeNode *DTnode = curDT->getNode(const_cast<BasicBlock *>(X));
  assert(DTnode);
  for (auto I = DTnode->begin(), E = DTnode->end(); I != E; ++I) {
    BasicBlock const *Y = (*I)->getBlock();
    renameBB(F, Y, C, S);
  }

  // For each assignment of A in X
  //   pop(S(A))
  for (auto &Phi : bbToPhiMap[X]) {
    auto *V = Phi->region;
    S[V].pop_back();
  }
  for (auto I = X->begin(), E = X->end(); I != E; ++I) {
    Instruction const *Inst = &*I;

    if (isa<CallInst>(Inst)) {
      CallBase *Cs(const_cast<CallBase *>(cast<CallBase>(Inst)));

      for (auto &Chi : callSiteToSyncChiMap[Cs]) {
        auto *V = Chi->region;
        S[V].pop_back();
      }

      for (auto &Chi : callSiteToChiMap[Cs]) {
        auto *V = Chi->region;
        S[V].pop_back();
      }

      for (auto &Chi : extCallSiteToCallerRetChi[Cs]) {
        auto *V = Chi->region;
        S[V].pop_back();
      }
    }

    if (auto const *SI = dyn_cast<StoreInst>(Inst)) {
      for (auto &Chi : storeToChiMap[SI]) {
        auto *V = Chi->region;
        S[V].pop_back();
      }
    }
  }
}

void MemorySSA::computePhiPredicates(llvm::Function const *F) {
  ValueSet Preds;

  for (BasicBlock const &BB : *F) {
    for (auto &Phi : bbToPhiMap[&BB]) {
      computeMSSAPhiPredicates(Phi.get());
    }

    for (Instruction const &Inst : BB) {
      PHINode const *Phi = dyn_cast<PHINode>(&Inst);
      if (!Phi) {
        continue;
      }
      computeLLVMPhiPredicates(Phi);
    }
  }
}

void MemorySSA::computeLLVMPhiPredicates(llvm::PHINode const *Phi) {
  // For each argument of the PHINode
  for (unsigned I = 0; I < Phi->getNumIncomingValues(); ++I) {
    // Get IPDF
    std::vector<BasicBlock *> IPDF =
        iterated_postdominance_frontier(*curPDT, Phi->getIncomingBlock(I));

    for (unsigned N = 0; N < IPDF.size(); ++N) {
      // Push conditions of each BB in the IPDF
      Instruction const *Ti = IPDF[N]->getTerminator();
      assert(Ti);

      if (isa<BranchInst>(Ti)) {
        BranchInst const *BI = cast<BranchInst>(Ti);
        assert(BI);

        if (BI->isUnconditional()) {
          continue;
        }

        Value const *Cond = BI->getCondition();

        llvmPhiToPredMap[Phi].insert(Cond);
      } else if (isa<SwitchInst>(Ti)) {
        SwitchInst const *SI = cast<SwitchInst>(Ti);
        assert(SI);
        Value const *Cond = SI->getCondition();
        llvmPhiToPredMap[Phi].insert(Cond);
      }
    }
  }
}

void MemorySSA::computeMSSAPhiPredicates(MSSAPhi *Phi) {
  // For each argument of the PHINode
  for (auto I : Phi->opsVar) {
    MSSAVar *Op = I.second;
    // Get IPDF
    std::vector<BasicBlock *> IPDF = iterated_postdominance_frontier(
        *curPDT, const_cast<BasicBlock *>(Op->bb));

    for (unsigned N = 0; N < IPDF.size(); ++N) {
      // Push conditions of each BB in the IPDF
      Instruction const *Ti = IPDF[N]->getTerminator();
      assert(Ti);

      if (isa<BranchInst>(Ti)) {
        BranchInst const *BI = cast<BranchInst>(Ti);
        assert(BI);

        if (BI->isUnconditional()) {
          continue;
        }

        Value const *Cond = BI->getCondition();

        Phi->preds.insert(Cond);
      } else if (isa<SwitchInst>(Ti)) {
        SwitchInst const *SI = cast<SwitchInst>(Ti);
        assert(SI);
        Value const *Cond = SI->getCondition();
        Phi->preds.insert(Cond);
      }
    }
  }
}

unsigned MemorySSA::whichPred(BasicBlock const *Pred, BasicBlock const *Succ) {
  unsigned Index = 0;
  for (auto I = pred_begin(Succ), E = pred_end(Succ); I != E; ++I, ++Index) {
    if (Pred == *I) {
      return Index;
    }
  }

  return Index;
}

void MemorySSA::dumpMSSA(llvm::Function const *F) {
  std::string Filename = F->getName().str();
  Filename.append("-assa.ll");
  errs() << "Writing '" << Filename << "' ...\n";
  std::error_code EC;
  raw_fd_ostream Stream(Filename, EC, sys::fs::OF_Text);

  // Function header
  Stream << "define " << *F->getReturnType() << " @" << F->getName().str()
         << "(";
  for (Argument const &Arg : F->args()) {
    Stream << Arg << ", ";
  }
  Stream << ") {\n";

  // Dump entry chi
  for (auto &Chi : funToEntryChiMap[F]) {
    Stream << Chi->region->getName() << Chi->var->version << "\n";
  }

  // For each basic block
  for (auto const &BB : *F) {

    // BB name
    Stream << BB.getName().str() << ":\n";

    // Phi functions
    for (auto &Phi : bbToPhiMap[&BB]) {
      Stream << Phi->region->getName() << Phi->var->version << " = phi( ";
      for (auto I : Phi->opsVar) {
        Stream << Phi->region->getName() << I.second->version << ", ";
      }
      for (Value const *V : Phi->preds) {
        Stream << getValueLabel(V) << ", ";
      }
      Stream << ")\n";
    }

    // For each instruction
    for (auto const &Inst : BB) {

      // LLVM Phi node: print predicates
      if (PHINode const *PHI = dyn_cast<PHINode>(&Inst)) {
        Stream << getValueLabel(PHI) << " = phi(";
        for (Value const *Incoming : PHI->incoming_values()) {
          Stream << getValueLabel(Incoming) << ", ";
        }
        for (Value const *Pred : llvmPhiToPredMap[PHI]) {
          Stream << getValueLabel(Pred) << ", ";
        }
        Stream << ")\n";
        continue;
      }

      // Load inst
      if (LoadInst const *LI = dyn_cast<LoadInst>(&Inst)) {
        Stream << getValueLabel(LI) << " = mu(";

        for (auto &Mu : loadToMuMap[LI]) {
          Stream << Mu->region->getName() << Mu->var->version << ", ";
        }

        Stream << getValueLabel(LI->getPointerOperand()) << ")\n";
        continue;
      }

      // Store inst
      if (auto const *SI = dyn_cast<StoreInst>(&Inst)) {
        for (auto &Chi : storeToChiMap[SI]) {
          Stream << Chi->region->getName() << Chi->var->version << " = X("
                 << Chi->region->getName() << Chi->opVar->version << ", "
                 << getValueLabel(SI->getValueOperand()) << ", "
                 << getValueLabel(SI->getPointerOperand()) << ")\n";
        }
        continue;
      }

      // Call inst
      if (CallInst const *CI = dyn_cast<CallInst>(&Inst)) {
        if (isIntrinsicDbgInst(CI)) {
          continue;
        }

        CallInst *Cs(const_cast<CallInst *>(CI));
        Stream << *CI << "\n";
        for (auto &Mu : callSiteToMuMap[Cs]) {
          Stream << "  mu(" << Mu->region->getName() << Mu->var->version
                 << ")\n";
        }

        for (auto &Chi : callSiteToChiMap[Cs]) {
          Stream << Chi->region->getName() << Chi->var->version << " = "
                 << "  X(" << Chi->region->getName() << Chi->opVar->version
                 << ")\n";
        }

        for (auto &Chi : callSiteToSyncChiMap[Cs]) {
          Stream << Chi->region->getName() << Chi->var->version << " = "
                 << "  X(" << Chi->region->getName() << Chi->opVar->version
                 << ")\n";
        }

        for (auto &Chi : extCallSiteToCallerRetChi[Cs]) {
          Stream << Chi->region->getName() << Chi->var->version << " = "
                 << "  X(" << Chi->region->getName() << Chi->opVar->version
                 << ")\n";
        }
        continue;
      }

      Stream << Inst << "\n";
    }
  }

  // Dump return mu
  for (auto &Mu : funToReturnMuMap[F]) {
    Stream << "  mu(" << Mu->region->getName() << Mu->var->version << ")\n";
  }

  Stream << "}\n";
}

void MemorySSA::createArtificalChiForCalledFunction(
    llvm::CallBase *CB, llvm::Function const *Callee) {
  // Not sure if we need mod/ref info here, we can just create entry/exit chi
  // for each pointer arguments and then only connect the exit/return chis of
  // modified arguments in the dep graph.

  // If it is a var arg function, create artificial entry and exit chi for the
  // var arg.
  if (Callee->isVarArg()) {
    auto [ItEntry, _] = extCallSiteToVarArgEntryChi[Callee].emplace(
        CB, std::make_unique<MSSAExtVarArgChi>(Callee));
    auto &EntryChi = ItEntry->second;
    EntryChi->var = std::make_unique<MSSAVar>(EntryChi.get(), 0, nullptr);

    auto [ItOut, __] = extCallSiteToVarArgExitChi[Callee].emplace(
        CB, std::make_unique<MSSAExtVarArgChi>(Callee));
    auto &OutChi = ItOut->second;
    OutChi->var = std::make_unique<MSSAVar>(EntryChi.get(), 1, nullptr);
    OutChi->opVar = EntryChi->var.get();
  }

  // Create artifical entry and exit chi for each pointer argument.
  unsigned ArgId = 0;
  for (Argument const &Arg : Callee->args()) {
    if (!Arg.getType()->isPointerTy()) {
      ArgId++;
      continue;
    }

    auto Chi = std::make_unique<MSSAExtArgChi>(Callee, ArgId);
    auto [ItEntry, EntryInserted] =
        extCallSiteToArgEntryChi[Callee][CB].emplace(ArgId, Chi.get());
    if (EntryInserted) {
      AllocatedArgChi.emplace_back(std::move(Chi));
    }
    auto &EntryChi = ItEntry->second;
    EntryChi->var = std::make_unique<MSSAVar>(EntryChi, 0, nullptr);

    auto ChiOut = std::make_unique<MSSAExtArgChi>(Callee, ArgId);
    auto [ItOut, OutInserted] =
        extCallSiteToArgExitChi[Callee][CB].emplace(ArgId, ChiOut.get());
    if (OutInserted) {
      AllocatedArgChi.emplace_back(std::move(ChiOut));
    }
    auto &ExitChi = ItOut->second;
    ExitChi->var = std::make_unique<MSSAVar>(ExitChi, 1, nullptr);
    ExitChi->opVar = EntryChi->var.get();

    ArgId++;
  }

  // Create artifical chi for return value if it is a pointer.
  if (Callee->getReturnType()->isPointerTy()) {
    auto [ItRet, _] = extCallSiteToCalleeRetChi[Callee].emplace(
        CB, std::make_unique<MSSAExtRetChi>(Callee));
    auto &RetChi = ItRet->second;
    RetChi->var = std::make_unique<MSSAVar>(RetChi.get(), 0, nullptr);
  }
}

AnalysisKey MemorySSAAnalysis::Key;
MemorySSAAnalysis::Result MemorySSAAnalysis::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("parcoach::MemorySSAAnalysisPass");
  auto const &AA = AM.getResult<AndersenAA>(M);
  auto const &ExtInfo = AM.getResult<ExtInfoAnalysis>(M);
  auto const &MRA = AM.getResult<ModRefAnalysis>(M);
  auto const &PTACG = AM.getResult<PTACallGraphAnalysis>(M);
  auto const &Regions = AM.getResult<MemRegAnalysis>(M);
  return std::make_unique<MemorySSA>(M, AA, *PTACG, *Regions, MRA.get(),
                                     *ExtInfo, AM);
}
} // namespace parcoach
