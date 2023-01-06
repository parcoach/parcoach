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
cl::opt<bool> optDumpSSA("dump-ssa", cl::desc("Dump the all-inclusive SSA"),
                         cl::cat(ParcoachCategory));

cl::opt<std::string> optDumpSSAFunc("dump-ssa-func",
                                    cl::desc("Dump the all-inclusive SSA "
                                             "for a particular function."),
                                    cl::cat(ParcoachCategory));

} // namespace

MemorySSA::MemorySSA(Module &M, Andersen const &PTA, PTACallGraph const &CG,
                     MemReg const &Regions, ModRefAnalysisResult *MRA,
                     ExtInfo const &extInfo, ModuleAnalysisManager &AM)
    : computeMuChiTime(0), computePhiTime(0), renameTime(0),
      computePhiPredicatesTime(0), PTA(PTA), CG(CG), Regions(Regions), MRA(MRA),
      extInfo(extInfo), curDF(NULL), curDT(NULL) {
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

    if (F.isDeclaration())
      continue;

    // errs() << " + Fun: " << counter << " - " << F.getName() << "\n";
    DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
    DominanceFrontier &DF = FAM.getResult<DominanceFrontierAnalysis>(F);
    PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);

    buildSSA(&F, DT, DF, PDT);
    if (optDumpSSA)
      dumpMSSA(&F);
    if (F.getName().equals(optDumpSSAFunc))
      dumpMSSA(&F);
  }
}

void MemorySSA::buildSSA(const Function *F, DominatorTree &DT,
                         DominanceFrontier &DF, PostDominatorTree &PDT) {
  curDF = &DF;
  curDT = &DT;
  curPDT = &PDT;

  usedRegs.clear();
  regDefToBBMap.clear();

  double t1, t2, t3, t4, t5;

  t1 = gettime();

  computeMuChi(F);

  t2 = gettime();

  computePhi(F);

  t3 = gettime();

  rename(F);

  t4 = gettime();

  computePhiPredicates(F);

  t5 = gettime();

  computeMuChiTime += t2 - t1;
  computePhiTime += t3 - t2;
  renameTime += t4 - t3;
  computePhiPredicatesTime += t5 - t4;
}

void MemorySSA::computeMuChi(const Function *F) {
  for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    /* Call Site */
    if (isCallSite(inst)) {
      if (isIntrinsicDbgInst(inst))
        continue;

      CallBase *cs = const_cast<CallBase *>(cast<CallBase>(inst));
      Function *callee = cs->getCalledFunction();

      // indirect call
      if (callee == NULL) {
        for (const Function *mayCallee : CG.getIndirectCallMap().lookup(inst)) {
          if (isIntrinsicDbgFunction(mayCallee))
            continue;

          computeMuChiForCalledFunction(cs, const_cast<Function *>(mayCallee));
          if (mayCallee->isDeclaration())
            createArtificalChiForCalledFunction(cs, mayCallee);
        }
      }

      // direct call
      else {
        if (isIntrinsicDbgFunction(callee))
          continue;

        computeMuChiForCalledFunction(cs, callee);
        if (callee->isDeclaration())
          createArtificalChiForCalledFunction(cs, callee);
      }

      continue;
    }

    /* Load Instruction
     * We insert a Mu function for each region pointed-to by
     * the load instruction.
     */
    if (isa<LoadInst>(inst)) {
      const LoadInst *LI = cast<LoadInst>(inst);
      std::vector<const Value *> ptsSet;
      bool Found = PTA.getPointsToSet(LI->getPointerOperand(), ptsSet);
      assert(Found && "Load not found in MSSA");
      if (!Found)
        continue;
      MemRegVector regs;
      Regions.getValuesRegion(ptsSet, regs);

      for (auto *r : regs) {
        if (MRA->inGlobalKillSet(r))
          continue;

        loadToMuMap[LI].emplace_back(std::make_unique<MSSALoadMu>(r, LI));
        usedRegs.insert(r);
      }

      continue;
    }

    /* Store Instruction
     * We insert a Chi function for each region pointed-to by
     * the store instruction.
     */
    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = cast<StoreInst>(inst);
      std::vector<const Value *> ptsSet;
      bool Found = PTA.getPointsToSet(SI->getPointerOperand(), ptsSet);
      assert(Found && "Store not found in MSSA");
      if (!Found)
        continue;
      MemRegVector regs;
      Regions.getValuesRegion(ptsSet, regs);

      for (auto *r : regs) {
        if (MRA->inGlobalKillSet(r))
          continue;

        storeToChiMap[SI].emplace_back(std::make_unique<MSSAStoreChi>(r, SI));
        usedRegs.insert(r);
        regDefToBBMap[r].insert(inst->getParent());
      }

      continue;
    }
  }

  /* Create an EntryChi and a ReturnMu for each memory region used by the
   * function.
   */
  for (auto *r : usedRegs) {
    auto &Chi =
        funToEntryChiMap[F].emplace_back(std::make_unique<MSSAEntryChi>(r, F));

    funRegToEntryChiMap[F][Chi->region] = Chi.get();
    regDefToBBMap[r].insert(&F->getEntryBlock());
  }

  if (!functionDoesNotRet(F)) {
    for (auto *r : usedRegs) {
      auto &Mu =
          funToReturnMuMap[F].emplace_back(std::make_unique<MSSARetMu>(r, F));
      funRegToReturnMuMap[F][Mu->region] = Mu.get();
    }
  }
}

void MemorySSA::computeMuChiForCalledFunction(CallBase *inst,
                                              Function *callee) {
#if defined(PARCOACH_ENABLE_CUDA) || defined(PARCOACH_ENABLE_OPENMP)
  Collective const *Coll = Collective::find(*callee);
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
    for (auto *r :
         getRange(Regions.getOmpSharedRegions(), inst->getFunction())) {
      callSiteToSyncChiMap[inst].emplace_back(
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

  // If the callee is a declaration (external function), we create a Mu
  // for each pointer argument and a Chi for each modified argument.
  if (callee->isDeclaration()) {
    assert(isa<CallInst>(inst)); // InvokeInst are not handled yet
    CallInst *CI = cast<CallInst>(inst);
    extFuncToCSMap[callee].insert(inst);

    auto const *info = extInfo.getExtModInfo(callee);

    // Mu and Chi for parameters
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

      std::vector<const Value *> ptsSet;
      std::vector<const Value *> argPtsSet;
      bool Found = PTA.getPointsToSet(arg, argPtsSet);
      assert(Found && "arg not found in MSSA");
      if (!Found)
        continue;
      ptsSet.insert(ptsSet.end(), argPtsSet.begin(), argPtsSet.end());

      MemRegVector regs;
      Regions.getValuesRegion(ptsSet, regs);

      // Mus
      for (auto *r : regs) {
        if (MRA->inGlobalKillSet(r))
          continue;

        callSiteToMuMap[inst].emplace_back(
            std::make_unique<MSSAExtCallMu>(r, callee, i));
        usedRegs.insert(r);
      }

      if (!info) {
        continue;
      }

      // Chis
      if (i >= info->nbArgs) {
        assert(callee->isVarArg());
        if (info->argIsMod[info->nbArgs - 1]) {
          for (auto *r : regs) {
            if (MRA->inGlobalKillSet(r))
              continue;

            callSiteToChiMap[inst].emplace_back(
                std::make_unique<MSSAExtCallChi>(r, callee, i, inst));
            regDefToBBMap[r].insert(inst->getParent());
          }
        }
      } else {
        if (info->argIsMod[i]) {
          for (auto *r : regs) {
            if (MRA->inGlobalKillSet(r))
              continue;

            callSiteToChiMap[inst].emplace_back(
                std::make_unique<MSSAExtCallChi>(r, callee, i, inst));
            regDefToBBMap[r].insert(inst->getParent());
          }
        }
      }
    }

    // Chi for return value if it is a pointer
    if (info && info->retIsMod && CI->getType()->isPointerTy()) {
      std::vector<const Value *> ptsSet;
      bool Found = PTA.getPointsToSet(CI, ptsSet);
      assert(Found && "CI not found in MSSA");
      if (!Found)
        return;

      MemRegVector regs;
      Regions.getValuesRegion(ptsSet, regs);

      for (auto *r : regs) {
        if (MRA->inGlobalKillSet(r))
          continue;

        extCallSiteToCallerRetChi[inst].emplace_back(
            std::make_unique<MSSAExtRetCallChi>(r, callee));
        regDefToBBMap[r].insert(inst->getParent());
        usedRegs.insert(r);
      }
    }
  }

  // If the callee is not a declaration we create a Mu(Chi) for each region
  // in Ref(Mod) callee.
  else {
    const Function *caller = inst->getParent()->getParent();
    MemRegSet killSet = MRA->getFuncKill(caller);

    // Create Mu for each region \in ref(callee)
    for (auto *r : MRA->getFuncRef(callee)) {
      if (killSet.find(r) == killSet.end()) {
        callSiteToMuMap[inst].emplace_back(
            std::make_unique<MSSACallMu>(r, callee));
        usedRegs.insert(r);
      }
    }

    // Create Chi for each region \in mod(callee)
    for (auto *r : MRA->getFuncMod(callee)) {
      if (killSet.find(r) == killSet.end()) {
        callSiteToChiMap[inst].emplace_back(
            std::make_unique<MSSACallChi>(r, callee, inst));
        regDefToBBMap[r].insert(inst->getParent());
        usedRegs.insert(r);
      }
    }
  }
}

void MemorySSA::computePhi(const Function *F) {
  // For each memory region used, compute basic blocks where phi must be
  // inserted.
  for (auto *r : usedRegs) {
    std::vector<const BasicBlock *> worklist;
    std::set<const BasicBlock *> domFronPlus;
    std::set<const BasicBlock *> work;

    for (const BasicBlock *X : regDefToBBMap[r]) {
      worklist.push_back(X);
      work.insert(X);
    }

    while (!worklist.empty()) {
      const BasicBlock *X = worklist.back();
      worklist.pop_back();

      auto it = curDF->find(const_cast<BasicBlock *>(X));
      if (it == curDF->end()) {
        errs() << "Error: basic block not in the dom frontier !\n";
        exit(EXIT_FAILURE);
        continue;
      }

      auto domSet = it->second;
      for (auto Y : domSet) {
        if (domFronPlus.find(Y) != domFronPlus.end())
          continue;

        bbToPhiMap[Y].emplace_back(std::make_unique<MSSAPhi>(r));
        domFronPlus.insert(Y);

        if (work.find(Y) != work.end())
          continue;

        work.insert(Y);
        worklist.push_back(Y);
      }
    }
  }
}

void MemorySSA::rename(const Function *F) {
  std::map<MemRegEntry *, unsigned> C;
  std::map<MemRegEntry *, std::vector<MSSAVar *>> S;

  // Initialization:

  // C(*) <- 0
  for (auto *r : usedRegs) {
    C[r] = 0;
  }

  // Compute LHS version for each region.
  for (auto &chi : funToEntryChiMap[F]) {
    chi->var = std::make_unique<MSSAVar>(chi.get(), 0, &F->getEntryBlock());

    S[chi->region].push_back(chi->var.get());
    C[chi->region]++;
  }

  renameBB(F, &F->getEntryBlock(), C, S);
}

void MemorySSA::renameBB(const Function *F, const llvm::BasicBlock *X,
                         std::map<MemRegEntry *, unsigned> &C,
                         std::map<MemRegEntry *, std::vector<MSSAVar *>> &S) {
  // Compute LHS for PHI
  for (auto &phi : bbToPhiMap[X]) {
    auto *V = phi->region;
    unsigned i = C[V];
    phi->var = std::make_unique<MSSAVar>(phi.get(), i, X);
    S[V].push_back(phi->var.get());
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
    const Instruction *inst = &*I;

    if (isCallSite(inst)) {
      CallBase *cs(const_cast<CallBase *>(cast<CallBase>(inst)));

      for (auto &mu : callSiteToMuMap[cs])
        mu->var = S[mu->region].back();

      for (auto &chi : callSiteToSyncChiMap[cs]) {
        auto *V = chi->region;
        unsigned i = C[V];
        chi->var = std::make_unique<MSSAVar>(chi.get(), i, inst->getParent());
        chi->opVar = S[V].back();
        S[V].push_back(chi->var.get());
        C[V]++;
      }

      for (auto &chi : callSiteToChiMap[cs]) {
        auto *V = chi->region;
        unsigned i = C[V];
        chi->var = std::make_unique<MSSAVar>(chi.get(), i, inst->getParent());
        chi->opVar = S[V].back();
        S[V].push_back(chi->var.get());
        C[V]++;
      }

      for (auto &chi : extCallSiteToCallerRetChi[cs]) {
        auto *V = chi->region;
        unsigned i = C[V];
        chi->var = std::make_unique<MSSAVar>(chi.get(), i, inst->getParent());
        chi->opVar = S[V].back();
        S[V].push_back(chi->var.get());
        C[V]++;
      }
    }

    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = cast<StoreInst>(inst);
      for (auto &chi : storeToChiMap[SI]) {
        auto *V = chi->region;
        unsigned i = C[V];
        chi->var = std::make_unique<MSSAVar>(chi.get(), i, inst->getParent());
        chi->opVar = S[V].back();
        S[V].push_back(chi->var.get());
        C[V]++;
      }
    }

    if (isa<LoadInst>(inst)) {
      const LoadInst *LI = cast<LoadInst>(inst);
      for (auto &mu : loadToMuMap[LI])
        mu->var = S[mu->region].back();
    }

    if (isa<ReturnInst>(inst)) {
      for (auto &mu : funToReturnMuMap[F]) {
        mu->var = S[mu->region].back();
      }
    }
  }

  // For each successor Y of X
  //   For each Phi function F in Y
  //     Replace operands V by Vi  where i = Top(S(V))
  for (auto I = succ_begin(X), E = succ_end(X); I != E; ++I) {
    const BasicBlock *Y = *I;
    for (auto &phi : bbToPhiMap[Y]) {
      unsigned index = whichPred(X, Y);
      phi->opsVar[index] = S[phi->region].back();
    }
  }

  // For each successor of X in the dominator tree
  DomTreeNode *DTnode = curDT->getNode(const_cast<BasicBlock *>(X));
  assert(DTnode);
  for (auto I = DTnode->begin(), E = DTnode->end(); I != E; ++I) {
    const BasicBlock *Y = (*I)->getBlock();
    renameBB(F, Y, C, S);
  }

  // For each assignment of A in X
  //   pop(S(A))
  for (auto &phi : bbToPhiMap[X]) {
    auto *V = phi->region;
    S[V].pop_back();
  }
  for (auto I = X->begin(), E = X->end(); I != E; ++I) {
    const Instruction *inst = &*I;

    if (isa<CallInst>(inst)) {
      CallBase *cs(const_cast<CallBase *>(cast<CallBase>(inst)));

      for (auto &chi : callSiteToSyncChiMap[cs]) {
        auto *V = chi->region;
        S[V].pop_back();
      }

      for (auto &chi : callSiteToChiMap[cs]) {
        auto *V = chi->region;
        S[V].pop_back();
      }

      for (auto &chi : extCallSiteToCallerRetChi[cs]) {
        auto *V = chi->region;
        S[V].pop_back();
      }
    }

    if (auto *SI = dyn_cast<StoreInst>(inst)) {
      for (auto &chi : storeToChiMap[SI]) {
        auto *V = chi->region;
        S[V].pop_back();
      }
    }
  }
}

void MemorySSA::computePhiPredicates(const llvm::Function *F) {
  ValueSet preds;

  for (const BasicBlock &bb : *F) {
    for (auto &phi : bbToPhiMap[&bb]) {
      computeMSSAPhiPredicates(phi.get());
    }

    for (const Instruction &inst : bb) {
      const PHINode *phi = dyn_cast<PHINode>(&inst);
      if (!phi)
        continue;
      computeLLVMPhiPredicates(phi);
    }
  }
}

void MemorySSA::computeLLVMPhiPredicates(const llvm::PHINode *phi) {
  // For each argument of the PHINode
  for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
    // Get IPDF
    std::vector<BasicBlock *> IPDF =
        iterated_postdominance_frontier(*curPDT, phi->getIncomingBlock(i));

    for (unsigned n = 0; n < IPDF.size(); ++n) {
      // Push conditions of each BB in the IPDF
      const Instruction *ti = IPDF[n]->getTerminator();
      assert(ti);

      if (isa<BranchInst>(ti)) {
        const BranchInst *bi = cast<BranchInst>(ti);
        assert(bi);

        if (bi->isUnconditional())
          continue;

        const Value *cond = bi->getCondition();

        llvmPhiToPredMap[phi].insert(cond);
      } else if (isa<SwitchInst>(ti)) {
        const SwitchInst *si = cast<SwitchInst>(ti);
        assert(si);
        const Value *cond = si->getCondition();
        llvmPhiToPredMap[phi].insert(cond);
      }
    }
  }
}

void MemorySSA::computeMSSAPhiPredicates(MSSAPhi *phi) {
  // For each argument of the PHINode
  for (auto I : phi->opsVar) {
    MSSAVar *op = I.second;
    // Get IPDF
    std::vector<BasicBlock *> IPDF = iterated_postdominance_frontier(
        *curPDT, const_cast<BasicBlock *>(op->bb));

    for (unsigned n = 0; n < IPDF.size(); ++n) {
      // Push conditions of each BB in the IPDF
      const Instruction *ti = IPDF[n]->getTerminator();
      assert(ti);

      if (isa<BranchInst>(ti)) {
        const BranchInst *bi = cast<BranchInst>(ti);
        assert(bi);

        if (bi->isUnconditional())
          continue;

        const Value *cond = bi->getCondition();

        phi->preds.insert(cond);
      } else if (isa<SwitchInst>(ti)) {
        const SwitchInst *si = cast<SwitchInst>(ti);
        assert(si);
        const Value *cond = si->getCondition();
        phi->preds.insert(cond);
      }
    }
  }
}

unsigned MemorySSA::whichPred(const BasicBlock *pred,
                              const BasicBlock *succ) const {
  unsigned index = 0;
  for (auto I = pred_begin(succ), E = pred_end(succ); I != E; ++I, ++index) {
    if (pred == *I)
      return index;
  }

  return index;
}

void MemorySSA::dumpMSSA(const llvm::Function *F) {
  std::string filename = F->getName().str();
  filename.append("-assa.ll");
  errs() << "Writing '" << filename << "' ...\n";
  std::error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::OF_Text);

  // Function header
  stream << "define " << *F->getReturnType() << " @" << F->getName().str()
         << "(";
  for (const Argument &arg : F->args())
    stream << arg << ", ";
  stream << ") {\n";

  // Dump entry chi
  for (auto &chi : funToEntryChiMap[F])
    stream << chi->region->getName() << chi->var->version << "\n";

  // For each basic block
  for (auto BI = F->begin(), BE = F->end(); BI != BE; ++BI) {
    const BasicBlock *bb = &*BI;

    // BB name
    stream << bb->getName().str() << ":\n";

    // Phi functions
    for (auto &phi : bbToPhiMap[bb]) {
      stream << phi->region->getName() << phi->var->version << " = phi( ";
      for (auto I : phi->opsVar)
        stream << phi->region->getName() << I.second->version << ", ";
      for (const Value *v : phi->preds)
        stream << getValueLabel(v) << ", ";
      stream << ")\n";
    }

    // For each instruction
    for (auto I = bb->begin(), E = bb->end(); I != E; ++I) {
      const Instruction *inst = &*I;

      // LLVM Phi node: print predicates
      if (const PHINode *PHI = dyn_cast<PHINode>(inst)) {
        stream << getValueLabel(PHI) << " = phi(";
        for (const Value *incoming : PHI->incoming_values())
          stream << getValueLabel(incoming) << ", ";
        for (const Value *pred : llvmPhiToPredMap[PHI])
          stream << getValueLabel(pred) << ", ";
        stream << ")\n";
        continue;
      }

      // Load inst
      if (const LoadInst *LI = dyn_cast<LoadInst>(inst)) {
        stream << getValueLabel(LI) << " = mu(";

        for (auto &mu : loadToMuMap[LI])
          stream << mu->region->getName() << mu->var->version << ", ";

        stream << getValueLabel(LI->getPointerOperand()) << ")\n";
        continue;
      }

      // Store inst
      if (auto *SI = dyn_cast<StoreInst>(inst)) {
        for (auto &chi : storeToChiMap[SI]) {
          stream << chi->region->getName() << chi->var->version << " = X("
                 << chi->region->getName() << chi->opVar->version << ", "
                 << getValueLabel(SI->getValueOperand()) << ", "
                 << getValueLabel(SI->getPointerOperand()) << ")\n";
        }
        continue;
      }

      // Call inst
      if (const CallInst *CI = dyn_cast<CallInst>(inst)) {
        if (isIntrinsicDbgInst(CI))
          continue;

        CallInst *cs(const_cast<CallInst *>(CI));
        stream << *CI << "\n";
        for (auto &mu : callSiteToMuMap[cs])
          stream << "  mu(" << mu->region->getName() << mu->var->version
                 << ")\n";

        for (auto &chi : callSiteToChiMap[cs])
          stream << chi->region->getName() << chi->var->version << " = "
                 << "  X(" << chi->region->getName() << chi->opVar->version
                 << ")\n";

        for (auto &chi : callSiteToSyncChiMap[cs])
          stream << chi->region->getName() << chi->var->version << " = "
                 << "  X(" << chi->region->getName() << chi->opVar->version
                 << ")\n";

        for (auto &chi : extCallSiteToCallerRetChi[cs])
          stream << chi->region->getName() << chi->var->version << " = "
                 << "  X(" << chi->region->getName() << chi->opVar->version
                 << ")\n";
        continue;
      }

      stream << *inst << "\n";
    }
  }

  // Dump return mu
  for (auto &mu : funToReturnMuMap[F])
    stream << "  mu(" << mu->region->getName() << mu->var->version << ")\n";

  stream << "}\n";
}

void MemorySSA::createArtificalChiForCalledFunction(
    llvm::CallBase *CS, const llvm::Function *callee) {
  // Not sure if we need mod/ref info here, we can just create entry/exit chi
  // for each pointer arguments and then only connect the exit/return chis of
  // modified arguments in the dep graph.

  // If it is a var arg function, create artificial entry and exit chi for the
  // var arg.
  if (callee->isVarArg()) {
    auto [ItEntry, _] = extCallSiteToVarArgEntryChi[callee].emplace(
        CS, std::make_unique<MSSAExtVarArgChi>(callee));
    auto &EntryChi = ItEntry->second;
    EntryChi->var = std::make_unique<MSSAVar>(EntryChi.get(), 0, nullptr);

    auto [ItOut, __] = extCallSiteToVarArgExitChi[callee].emplace(
        CS, std::make_unique<MSSAExtVarArgChi>(callee));
    auto &OutChi = ItOut->second;
    OutChi->var = std::make_unique<MSSAVar>(EntryChi.get(), 1, nullptr);
    OutChi->opVar = EntryChi->var.get();
  }

  // Create artifical entry and exit chi for each pointer argument.
  unsigned argId = 0;
  for (const Argument &arg : callee->args()) {
    if (!arg.getType()->isPointerTy()) {
      argId++;
      continue;
    }

    auto Chi = std::make_unique<MSSAExtArgChi>(callee, argId);
    auto [ItEntry, EntryInserted] =
        extCallSiteToArgEntryChi[callee][CS].emplace(argId, Chi.get());
    if (EntryInserted) {
      AllocatedArgChi.emplace_back(std::move(Chi));
    }
    auto &EntryChi = ItEntry->second;
    EntryChi->var = std::make_unique<MSSAVar>(EntryChi, 0, nullptr);

    auto ChiOut = std::make_unique<MSSAExtArgChi>(callee, argId);
    auto [ItOut, OutInserted] =
        extCallSiteToArgExitChi[callee][CS].emplace(argId, ChiOut.get());
    if (OutInserted) {
      AllocatedArgChi.emplace_back(std::move(ChiOut));
    }
    auto &ExitChi = ItOut->second;
    ExitChi->var = std::make_unique<MSSAVar>(ExitChi, 1, nullptr);
    ExitChi->opVar = EntryChi->var.get();

    argId++;
  }

  // Create artifical chi for return value if it is a pointer.
  if (callee->getReturnType()->isPointerTy()) {
    auto [ItRet, _] = extCallSiteToCalleeRetChi[callee].emplace(
        CS, std::make_unique<MSSAExtRetChi>(callee));
    auto &RetChi = ItRet->second;
    RetChi->var = std::make_unique<MSSAVar>(RetChi.get(), 0, nullptr);
  }
}

void MemorySSA::printTimers() const {
  errs() << "compute Mu/Chi time : " << computeMuChiTime * 1.0e3 << " ms\n";
  errs() << "compute Phi time : " << computePhiTime * 1.0e3 << " ms\n";
  errs() << "compute Rename Chi time : " << renameTime * 1.0e3 << " ms\n";
  errs() << "compute Phi Predicates time : " << computePhiPredicatesTime * 1.0e3
         << " ms\n";
}

AnalysisKey MemorySSAAnalysis::Key;
MemorySSAAnalysis::Result MemorySSAAnalysis::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("parcoach::MemorySSAAnalysisPass");
  auto const &AA = AM.getResult<AndersenAA>(M);
  auto const &extInfo = AM.getResult<ExtInfoAnalysis>(M);
  auto const &MRA = AM.getResult<ModRefAnalysis>(M);
  auto const &PTACG = AM.getResult<PTACallGraphAnalysis>(M);
  auto const &Regions = AM.getResult<MemRegAnalysis>(M);
  return std::make_unique<MemorySSA>(M, AA, *PTACG, *Regions, MRA.get(),
                                     *extInfo, AM);
}
} // namespace parcoach
