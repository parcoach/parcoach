#include "parcoach/DepGraphDCF.h"

#include "MSSAMuChi.h"
#include "PTACallGraph.h"
#include "Utils.h"
#include "parcoach/Options.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <fstream>
#include <queue>

#define DEBUG_TYPE "dgdcf"

using namespace llvm;
namespace parcoach {
namespace {
struct FunctionArg {
  std::string Name;
  int Arg;
};

std::vector<FunctionArg> SsaSourceFunctions;
std::vector<FunctionArg> ValueSourceFunctions;
std::vector<char const *> LoadValueSources;
std::vector<FunctionArg> ResetFunctions;
cl::opt<bool> OptWeakUpdate("weak-update", cl::desc("Weak update"),
                            cl::cat(ParcoachCategory));
} // namespace

DepGraphDCF::DepGraphDCF(MemorySSA *Mssa, PTACallGraph const &CG,
                         FunctionAnalysisManager &AM, Module &M,
                         bool ContextInsensitive, bool NoPtrDep, bool NoPred,
                         bool DisablePhiElim)
    : mssa(Mssa), CG(CG), FAM(AM), M(M), ContextInsensitive(ContextInsensitive),
      PDT(nullptr), noPtrDep(NoPtrDep), noPred(NoPred),
      disablePhiElim(DisablePhiElim) {

  if (Options::get().isActivated(Paradigm::MPI)) {
    enableMPI();
  }
#ifdef PARCOACH_ENABLE_OPENMP
  if (Options::get().isActivated(Paradigm::OMP)) {
    enableOMP();
  }
#endif
#ifdef PARCOACH_ENABLE_UPC
  if (Options::get().isActivated(Paradigm::UPC))
    enableUPC();
#endif
#ifdef PARCOACH_ENABLE_CUDA
  if (Options::get().isActivated(Paradigm::CUDA))
    enableCUDA();
#endif
  build();
}

DepGraphDCF::~DepGraphDCF() {}

void DepGraphDCF::build() {
  TimeTraceScope TTS("DepGraphDCF");
  for (Function const &F : M) {
    if (!CG.isReachableFromEntry(F)) {
      continue;
    }

    if (isIntrinsicDbgFunction(&F)) {
      continue;
    }

    buildFunction(&F);
  }

  if (!disablePhiElim) {
    phiElimination();
  }

  // Compute tainted values
  if (ContextInsensitive) {
    computeTaintedValuesContextInsensitive();
  } else {
    computeTaintedValuesContextSensitive();
  }

  LLVM_DEBUG({
    dbgs() << "Tainted values (" << taintedLLVMNodes.size() << "/"
           << taintedSSANodes.size() << "):\n";
    for (auto *V : taintedLLVMNodes) {
      V->print(dbgs());
      dbgs() << "\n";
    }
  });
}

void DepGraphDCF::enableMPI() {
  ResetFunctions.push_back({"MPI_Bcast", 0});
  ResetFunctions.push_back({"MPI_Allgather", 3});
  ResetFunctions.push_back({"MPI_Allgatherv", 3});
  ResetFunctions.push_back({"MPI_Alltoall", 3});
  ResetFunctions.push_back({"MPI_Alltoallv", 4});
  ResetFunctions.push_back({"MPI_Alltoallw", 4});
  ResetFunctions.push_back({"MPI_Allreduce", 1});
  SsaSourceFunctions.push_back({"MPI_Comm_rank", 1});
  SsaSourceFunctions.push_back({"MPI_Group_rank", 1});
}

void DepGraphDCF::enableOMP() {
  ValueSourceFunctions.push_back({"__kmpc_global_thread_num", -1});
  ValueSourceFunctions.push_back({"_omp_get_thread_num", -1});
  ValueSourceFunctions.push_back({"omp_get_thread_num", -1});
}

void DepGraphDCF::enableUPC() { LoadValueSources.push_back("gasneti_mynode"); }

void DepGraphDCF::enableCUDA() {
  // threadIdx.x
  ValueSourceFunctions.push_back({"llvm.nvvm.read.ptx.sreg.tid.x", -1});
  // threadIdx.y
  ValueSourceFunctions.push_back({"llvm.nvvm.read.ptx.sreg.tid.y", -1});
  // threadIdx.z
  ValueSourceFunctions.push_back({"llvm.nvvm.read.ptx.sreg.tid.z", -1});
}

void DepGraphDCF::buildFunction(llvm::Function const *F) {
  TimeTraceScope TTS("BuildGraph");

  curFunc = F;

  if (F->isDeclaration()) {
    PDT = nullptr;
  } else {
    PDT = &FAM.getResult<PostDominatorTreeAnalysis>(*const_cast<Function *>(F));
  }

  visit(*const_cast<Function *>(F));

  // Add entry chi nodes to the graph.
  for (auto const &Chi : getRange(mssa->getFunToEntryChiMap(), F)) {
    assert(Chi && Chi->var);
    funcToSSANodesMap[F].insert(Chi->var.get());
    if (Chi->opVar) {
      funcToSSANodesMap[F].insert(Chi->opVar);
      addEdge(Chi->opVar, Chi->var.get());
    }
  }

  // External functions
  if (F->isDeclaration()) {

    // Add var arg entry and exit chi nodes.
    if (F->isVarArg()) {
      for (auto const &I : getRange(mssa->getExtCSToVArgEntryChi(), F)) {
        MSSAChi *EntryChi = I.second.get();
        assert(EntryChi && EntryChi->var && "cs to vararg not found");
        funcToSSANodesMap[F].emplace(EntryChi->var.get());
      }
      for (auto const &I : getRange(mssa->getExtCSToVArgExitChi(), F)) {
        MSSAChi *ExitChi = I.second.get();
        assert(ExitChi && ExitChi->var);
        funcToSSANodesMap[F].insert(ExitChi->var.get());
        addEdge(ExitChi->opVar, ExitChi->var.get());
      }
    }

    // Add args entry and exit chi nodes for external functions.
    unsigned ArgNo = 0;
    for (Argument const &Arg : F->args()) {
      if (!Arg.getType()->isPointerTy()) {
        ArgNo++;
        continue;
      }

      for (auto const &I : getRange(mssa->getExtCSToArgEntryChi(), F)) {
        MSSAChi *EntryChi = I.second.at(ArgNo);
        assert(EntryChi && EntryChi->var && "cs to arg not found");
        funcToSSANodesMap[F].emplace(EntryChi->var.get());
      }
      for (auto const &I : getRange(mssa->getExtCSToArgExitChi(), F)) {
        MSSAChi *ExitChi = I.second.at(ArgNo);
        assert(ExitChi && ExitChi->var);
        funcToSSANodesMap[F].emplace(ExitChi->var.get());
        addEdge(ExitChi->opVar, ExitChi->var.get());
      }

      ArgNo++;
    }

    // Add retval chi node for external functions
    if (F->getReturnType()->isPointerTy()) {
      for (auto const &I : getRange(mssa->getExtCSToCalleeRetChi(), F)) {
        MSSAChi *RetChi = I.second.get();
        assert(RetChi && RetChi->var);
        funcToSSANodesMap[F].emplace(RetChi->var.get());
      }
    }

    // memcpy
    if (F->getName().find("memcpy") != StringRef::npos) {
      auto CSToArgEntry = mssa->getExtCSToArgEntryChi().lookup(F);
      auto CSToArgExit = mssa->getExtCSToArgExitChi().lookup(F);
      for (auto I : CSToArgEntry) {
        CallBase *CS = I.first;
        MSSAChi *SrcEntryChi = CSToArgEntry[CS][1];
        MSSAChi *DstExitChi = CSToArgExit[CS][0];

        addEdge(SrcEntryChi->var.get(), DstExitChi->var.get());

        // llvm.mempcy instrinsic returns void whereas memcpy returns dst
        if (F->getReturnType()->isPointerTy()) {
          MSSAChi *RetChi{};
          auto It = mssa->getExtCSToCalleeRetChi().find(F);
          if (It != mssa->getExtCSToCalleeRetChi().end()) {
            RetChi = It->second.at(CS).get();
          }
          addEdge(DstExitChi->var.get(), RetChi->var.get());
        }
      }
    }

    // memmove
    else if (F->getName().find("memmove") != StringRef::npos) {
      auto CSToArgEntry = mssa->getExtCSToArgEntryChi().lookup(F);
      auto CSToArgExit = mssa->getExtCSToArgExitChi().lookup(F);
      for (auto I : CSToArgEntry) {
        CallBase *CS = I.first;

        MSSAChi *SrcEntryChi = CSToArgEntry[CS][1];
        MSSAChi *DstExitChi = CSToArgExit[CS][0];

        addEdge(SrcEntryChi->var.get(), DstExitChi->var.get());

        // llvm.memmove instrinsic returns void whereas memmove returns dst
        if (F->getReturnType()->isPointerTy()) {
          MSSAChi *RetChi{};
          auto It = mssa->getExtCSToCalleeRetChi().find(F);
          if (It != mssa->getExtCSToCalleeRetChi().end()) {
            RetChi = It->second.at(CS).get();
          }
          addEdge(DstExitChi->var.get(), RetChi->var.get());
        }
      }
    }

    // memset
    else if (F->getName().find("memset") != StringRef::npos) {
      for (auto const &I : getRange(mssa->getExtCSToArgExitChi(), F)) {
        CallBase *CS = I.first;

        MSSAChi *ArgExitChi = mssa->getExtCSToArgExitChi().lookup(F)[CS][0];
        addEdge(F->getArg(1), ArgExitChi->var.get());

        // llvm.memset instrinsic returns void whereas memset returns dst
        if (F->getReturnType()->isPointerTy()) {
          MSSAChi *RetChi{};
          auto It = mssa->getExtCSToCalleeRetChi().find(F);
          if (It != mssa->getExtCSToCalleeRetChi().end()) {
            RetChi = It->second.at(CS).get();
          }
          addEdge(ArgExitChi->var.get(), RetChi->var.get());
        }
      }
    }

    // Unknown external function, we have to connect every input to every
    // output.
    else {
      for (CallBase *Cs : getRange(mssa->getExtFuncToCSMap(), F)) {
        std::set<MSSAVar *> SsaOutputs;
        std::set<MSSAVar *> SsaInputs;

        // Compute SSA outputs
        auto const &CSToArgExit = mssa->getExtCSToArgExitChi();
        auto const &CSToArgEntry = mssa->getExtCSToArgEntryChi();
        auto IndexToExitChi = CSToArgExit.lookup(F)[Cs];
        for (auto &I : IndexToExitChi) {
          MSSAChi *ArgExitChi = I.second;
          SsaOutputs.emplace(ArgExitChi->var.get());
        }
        if (F->isVarArg()) {
          MSSAChi *VarArgExitChi{};
          auto It = mssa->getExtCSToVArgExitChi().find(F);
          if (It != mssa->getExtCSToVArgExitChi().end()) {
            VarArgExitChi = It->second.at(Cs).get();
          }
          SsaOutputs.emplace(VarArgExitChi->var.get());
        }
        if (F->getReturnType()->isPointerTy()) {
          MSSAChi *RetChi{};
          auto It = mssa->getExtCSToCalleeRetChi().find(F);
          if (It != mssa->getExtCSToCalleeRetChi().end()) {
            RetChi = It->second.at(Cs).get();
          }
          SsaOutputs.emplace(RetChi->var.get());
        }

        // Compute SSA inputs
        auto IndexToEntryChi = CSToArgEntry.lookup(F)[Cs];
        for (auto &I : IndexToEntryChi) {
          MSSAChi *ArgEntryChi = I.second;
          SsaInputs.emplace(ArgEntryChi->var.get());
        }
        if (F->isVarArg()) {
          MSSAChi *VarArgEntryChi{};
          auto It = mssa->getExtCSToVArgEntryChi().find(F);
          if (It != mssa->getExtCSToVArgEntryChi().end()) {
            VarArgEntryChi = It->second.at(Cs).get();
          }
          SsaInputs.emplace(VarArgEntryChi->var.get());
        }

        // Connect SSA inputs to SSA outputs
        for (MSSAVar *In : SsaInputs) {
          for (MSSAVar *Out : SsaOutputs) {
            addEdge(In, Out);
          }
        }

        // Connect LLVM arguments to SSA outputs
        for (Argument const &Arg : F->args()) {
          for (MSSAVar *Out : SsaOutputs) {
            addEdge(&Arg, Out);
          }
        }
      }
    }

    // SSA Source functions
    for (auto const &[Name, ArgNo] : SsaSourceFunctions) {
      if (F->getName() != Name) {
        continue;
      }
      for (auto const &I : getRange(mssa->getExtCSToArgExitChi(), F)) {
        assert(I.second.at(ArgNo));
        ssaSources.emplace(I.second.at(ArgNo)->var.get());
      }
    }
  }
}

void DepGraphDCF::visitBasicBlock(llvm::BasicBlock &BB) {
  // Add MSSA Phi nodes and edges to the graph.
  for (auto const &Phi : getRange(mssa->getBBToPhiMap(), &BB)) {
    assert(Phi && Phi->var);
    funcToSSANodesMap[curFunc].insert(Phi->var.get());
    for (auto I : Phi->opsVar) {
      assert(I.second);
      funcToSSANodesMap[curFunc].insert(I.second);
      addEdge(I.second, Phi->var.get());
    }

    if (!noPred) {
      for (Value const *Pred : Phi->preds) {
        funcToLLVMNodesMap[curFunc].insert(Pred);
        addEdge(Pred, Phi->var.get());
      }
    }
  }
}

void DepGraphDCF::visitAllocaInst(llvm::AllocaInst &I) {
  // Do nothing
}

void DepGraphDCF::visitTerminator(llvm::Instruction &I) {
  // Do nothing
}

void DepGraphDCF::visitCmpInst(llvm::CmpInst &I) {
  // Cmp instruction is a value, connect the result to its operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitUnaryOperator(llvm::UnaryOperator &I) {
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitFreezeInst(llvm::FreezeInst &I) {
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitLoadInst(llvm::LoadInst &LI) {
  // Load inst, connect MSSA mus and the pointer loaded.
  funcToLLVMNodesMap[curFunc].insert(&LI);
  funcToLLVMNodesMap[curFunc].insert(LI.getPointerOperand());

  auto const &MuSetForLoad = getRange(mssa->getLoadToMuMap(), &LI);
  for (auto const &Mu : MuSetForLoad) {
    assert(Mu && Mu->var);
    funcToSSANodesMap[curFunc].emplace(Mu->var);
    addEdge(Mu->var, &LI);
  }

  // Load value rank source
  for (auto const &Name : LoadValueSources) {
    if (LI.getPointerOperand()->getName() == Name) {
      for (auto const &Mu : MuSetForLoad) {
        assert(Mu && Mu->var);
        ssaSources.emplace(Mu->var);
      }

      break;
    }
  }

  if (!noPtrDep) {
    addEdge(LI.getPointerOperand(), &LI);
  }
}

void DepGraphDCF::visitStoreInst(llvm::StoreInst &I) {
  // Store inst
  // For each chi, connect the pointer, the value stored and the MSSA operand.
  for (auto const &Chi : getRange(mssa->getStoreToChiMap(), &I)) {
    assert(Chi && Chi->var && Chi->opVar);
    funcToSSANodesMap[curFunc].emplace(Chi->var.get());
    funcToSSANodesMap[curFunc].emplace(Chi->opVar);
    funcToLLVMNodesMap[curFunc].emplace(I.getPointerOperand());
    funcToLLVMNodesMap[curFunc].emplace(I.getValueOperand());

    addEdge(I.getValueOperand(), Chi->var.get());

    if (OptWeakUpdate) {
      addEdge(Chi->opVar, Chi->var.get());
    }

    if (!noPtrDep) {
      addEdge(I.getPointerOperand(), Chi->var.get());
    }
  }
}

void DepGraphDCF::visitGetElementPtrInst(llvm::GetElementPtrInst &I) {
  // GetElementPtr, connect operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}
void DepGraphDCF::visitPHINode(llvm::PHINode &I) {
  // Connect LLVM Phi, connect operands and predicates.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }

  if (!noPred) {
    for (Value const *V : getRange(mssa->getPhiToPredMap(), &I)) {
      addEdge(V, &I);
      funcToLLVMNodesMap[curFunc].insert(V);
    }
  }
}
void DepGraphDCF::visitCastInst(llvm::CastInst &I) {
  // Cast inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}
void DepGraphDCF::visitSelectInst(llvm::SelectInst &I) {
  // Select inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}
void DepGraphDCF::visitBinaryOperator(llvm::BinaryOperator &I) {
  // Binary op, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitCallInst(llvm::CallInst &CI) {
  /* Building rules for call sites :
   *
   * %c = call f (..., %a, ...)
   * [ mu(..., o1, ...) ]
   * [ ...
   *  o2 = chi(o1)
   *  ... ]
   *
   * define f (..., %b, ...) {
   *  [ ..., o0 = X(o), ... ]
   *
   *  ...
   *
   *  [ ...
   *    mu(on)
   *    ... ]
   *  ret %r
   * }
   *
   * Top-level variables
   *
   * rule1: %a -----> %b
   * rule2: %c <----- %r
   *
   * Address-taken variables
   *
   * rule3: o1 ------> o0 in f
   * rule4: o1 ------> o2
   * rule5: o2 <------ on in f
   */

  if (isIntrinsicDbgInst(&CI)) {
    return;
  }

  connectCSMus(CI);
  connectCSChis(CI);
  connectCSEffectiveParameters(CI);
  connectCSCalledReturnValue(CI);
  connectCSRetChi(CI);

  // Add call node
  funcToCallNodes[curFunc].insert(&CI);

  // Add pred to call edges
  std::set<Value const *> Preds = computeIPDFPredicates(*PDT, CI.getParent());
  for (Value const *Pred : Preds) {
    condToCallEdges[Pred].insert(&CI);
    callsiteToConds[&CI].insert(Pred);
  }

  // Add call to func edge
  Function const *Callee = CI.getCalledFunction();

  // direct call
  if (Callee) {
    callToFuncEdges[&CI] = Callee;
    funcToCallSites[Callee].insert(&CI);

    // Return value source
    for (auto const &[Name, ArgNo] : ValueSourceFunctions) {
      if (Callee->getName() != Name) {
        continue;
      }

      if (ArgNo != -1) {
        continue;
      }

      valueSources.insert(&CI);
    }
  }

  // indirect call
  else {
    for (Function const *MayCallee : getRange(CG.getIndirectCallMap(), &CI)) {
      callToFuncEdges[&CI] = MayCallee;
      funcToCallSites[MayCallee].insert(&CI);

      // Return value source
      for (auto const &[Name, ArgNo] : ValueSourceFunctions) {
        if (MayCallee->getName() != Name) {
          continue;
        }

        if (ArgNo != -1) {
          continue;
        }

        valueSources.insert(&CI);
      }
    }
  }

  // Sync CHI
  for (auto const &Chi : getRange(mssa->getCSToSynChiMap(), &CI)) {
    assert(Chi && Chi->var && Chi->opVar);
    funcToSSANodesMap[curFunc].emplace(Chi->var.get());
    funcToSSANodesMap[curFunc].emplace(Chi->opVar);

    addEdge(Chi->opVar, Chi->var.get());
    taintResetSSANodes.emplace(Chi->var.get());
  }
}

void DepGraphDCF::connectCSMus(llvm::CallInst &I) {
  // Mu of the call site.
  for (auto const &Mu : getRange(mssa->getCSToMuMap(), &I)) {
    assert(Mu && Mu->var);
    funcToSSANodesMap[curFunc].emplace(Mu->var);
    Function const *Called = NULL;

    // External Function, we connect call mu to artifical chi of the external
    // function for each argument.
    if (MSSAExtCallMu *ExtCallMu = dyn_cast<MSSAExtCallMu>(Mu.get())) {
      CallBase *CS(&I);

      Called = ExtCallMu->called;
      unsigned ArgNo = ExtCallMu->argNo;

      // Case where this is a var arg parameter
      if (ArgNo >= Called->arg_size()) {
        assert(Called->isVarArg());

        auto ItMap = mssa->getExtCSToVArgEntryChi().find(Called);
        auto ItEnd = mssa->getExtCSToVArgEntryChi().end();
        MSSAChi *Chi{};
        if (ItMap != ItEnd) {
          Chi = ItMap->second.at(CS).get();
        }
        assert(Chi);
        MSSAVar *Var = Chi->var.get();
        assert(Var);
        funcToSSANodesMap[Called].emplace(Var);
        addEdge(Mu->var, Var); // rule3
      }

      else {
        // rule3
        auto const &CSToArgEntry = mssa->getExtCSToArgEntryChi();
        assert(CSToArgEntry.lookup(Called)[CS].at(ArgNo));
        addEdge(Mu->var, CSToArgEntry.lookup(Called)[CS].at(ArgNo)->var.get());
      }

      continue;
    }

    MSSACallMu *CallMu = cast<MSSACallMu>(Mu.get());
    Called = CallMu->called;

    auto const &FunctionToChi = mssa->getFunRegToEntryChiMap();

    auto It = FunctionToChi.find(Called);
    if (It != FunctionToChi.end()) {
      MSSAChi *EntryChi = It->second.at(Mu->region);
      assert(EntryChi && EntryChi->var && "reg to entrychi not found");
      funcToSSANodesMap[Called].emplace(EntryChi->var.get());
      addEdge(CallMu->var, EntryChi->var.get()); // rule3
    }
  }
}

void DepGraphDCF::connectCSChis(llvm::CallInst &I) {
  // Chi of the callsite.
  for (auto const &Chi : getRange(mssa->getCSToChiMap(), &I)) {
    assert(Chi && Chi->var && Chi->opVar);
    funcToSSANodesMap[curFunc].emplace(Chi->opVar);
    funcToSSANodesMap[curFunc].emplace(Chi->var.get());

    if (OptWeakUpdate) {
      addEdge(Chi->opVar, Chi->var.get()); // rule4
    }

    Function const *Called = NULL;

    // External Function, we connect call chi to artifical chi of the external
    // function for each argument.
    if (MSSAExtCallChi *ExtCallChi = dyn_cast<MSSAExtCallChi>(Chi.get())) {
      CallBase *CS(&I);
      Called = ExtCallChi->called;
      unsigned ArgNo = ExtCallChi->argNo;

      // Case where this is a var arg parameter.
      if (ArgNo >= Called->arg_size()) {
        assert(Called->isVarArg());

        auto ItMap = mssa->getExtCSToVArgExitChi().find(Called);
        auto ItEnd = mssa->getExtCSToVArgExitChi().end();
        MSSAChi *Chi{};
        if (ItMap != ItEnd) {
          Chi = ItMap->second.at(CS).get();
        }
        assert(Chi);
        MSSAVar *Var = Chi->var.get();
        assert(Var);
        funcToSSANodesMap[Called].emplace(Var);
        addEdge(Var, Chi->var.get()); // rule5
      }

      else {
        // rule5
        auto const &CSToArgExit = mssa->getExtCSToArgExitChi();
        assert(CSToArgExit.lookup(Called)[CS].at(ArgNo));
        addEdge(CSToArgExit.lookup(Called)[CS].at(ArgNo)->var.get(),
                Chi->var.get());

        // Reset functions
        for (auto const &[Name, FArgNo] : ResetFunctions) {
          if (Called->getName() != Name) {
            continue;
          }

          if ((int)ArgNo != FArgNo) {
            continue;
          }

          taintResetSSANodes.emplace(Chi->var.get());
        }
      }

      continue;
    }

    MSSACallChi *CallChi = cast<MSSACallChi>(Chi.get());
    Called = CallChi->called;

    auto const &FunctionToMu = mssa->getFunRegToReturnMuMap();
    auto It = FunctionToMu.find(Called);
    if (It != FunctionToMu.end()) {
      MSSAMu *ReturnMu = It->second.at(Chi->region);
      assert(ReturnMu && ReturnMu->var && "entry not found in map");
      funcToSSANodesMap[Called].emplace(ReturnMu->var);
      addEdge(ReturnMu->var, Chi->var.get()); // rule5
    }
  }
}

void DepGraphDCF::connectCSEffectiveParameters(llvm::CallInst &I) {
  // Connect effective parameters to formal parameters.
  Function const *Callee = I.getCalledFunction();

  // direct call
  if (Callee) {
    if (Callee->isDeclaration()) {
      connectCSEffectiveParametersExt(I, Callee);
      return;
    }

    unsigned ArgIdx = 0;
    for (Argument const &Arg : Callee->args()) {
      funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(ArgIdx));
      funcToLLVMNodesMap[Callee].insert(&Arg);

      addEdge(I.getArgOperand(ArgIdx), &Arg); // rule1

      ArgIdx++;
    }
  }

  // indirect call
  else {
    for (Function const *MayCallee : getRange(CG.getIndirectCallMap(), &I)) {
      if (MayCallee->isDeclaration()) {
        connectCSEffectiveParametersExt(I, MayCallee);
        return;
      }

      unsigned ArgIdx = 0;
      for (Argument const &Arg : MayCallee->args()) {
        funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(ArgIdx));
        funcToLLVMNodesMap[Callee].insert(&Arg);

        addEdge(I.getArgOperand(ArgIdx), &Arg); // rule1

        ArgIdx++;
      }
    }
  }
}

void DepGraphDCF::connectCSEffectiveParametersExt(CallInst &I,
                                                  Function const *Callee) {
  CallBase *CS(&I);

  if (Callee->getName().find("memset") != StringRef::npos) {
    MSSAChi *ArgExitChi = mssa->getExtCSToArgExitChi().lookup(Callee)[CS][0];
    Value const *CArg = I.getArgOperand(1);
    assert(CArg);
    funcToLLVMNodesMap[I.getParent()->getParent()].emplace(CArg);
    addEdge(CArg, ArgExitChi->var.get());
  }
}

void DepGraphDCF::connectCSCalledReturnValue(llvm::CallInst &I) {
  // If the function called returns a value, connect the return value to the
  // call value.

  Function const *Callee = I.getCalledFunction();

  // direct call
  if (Callee) {
    if (!Callee->isDeclaration() && !Callee->getReturnType()->isVoidTy()) {
      funcToLLVMNodesMap[curFunc].insert(&I);
      addEdge(getReturnValue(Callee), &I); // rule2
    }
  }

  // indirect call
  else {
    for (Function const *MayCallee : getRange(CG.getIndirectCallMap(), &I)) {
      if (!MayCallee->isDeclaration() &&
          !MayCallee->getReturnType()->isVoidTy()) {
        funcToLLVMNodesMap[curFunc].insert(&I);
        addEdge(getReturnValue(MayCallee), &I); // rule2
      }
    }
  }
}

void DepGraphDCF::connectCSRetChi(llvm::CallInst &I) {
  // External function, if the function called returns a pointer, connect the
  // artifical ret chi to the retcallchi
  // return chi of the caller.

  Function const *Callee = I.getCalledFunction();
  CallBase *CS(&I);

  // direct call
  if (Callee) {
    if (Callee->isDeclaration() && Callee->getReturnType()->isPointerTy()) {
      for (auto const &Chi : getRange(mssa->getExtCSToCallerRetChi(), &I)) {
        assert(Chi && Chi->var && Chi->opVar);
        funcToSSANodesMap[curFunc].emplace(Chi->var.get());
        funcToSSANodesMap[curFunc].emplace(Chi->opVar);

        addEdge(Chi->opVar, Chi->var.get());
        auto ItMap = mssa->getExtCSToCalleeRetChi().find(Callee);
        assert(ItMap != mssa->getExtCSToCalleeRetChi().end());
        addEdge(ItMap->second.at(CS)->var.get(), Chi->var.get());
      }
    }
  }

  // indirect call
  else {
    for (Function const *MayCallee : getRange(CG.getIndirectCallMap(), &I)) {
      if (MayCallee->isDeclaration() &&
          MayCallee->getReturnType()->isPointerTy()) {
        for (auto const &Chi : getRange(mssa->getExtCSToCallerRetChi(), &I)) {
          assert(Chi && Chi->var && Chi->opVar);
          funcToSSANodesMap[curFunc].emplace(Chi->var.get());
          funcToSSANodesMap[curFunc].emplace(Chi->opVar);

          addEdge(Chi->opVar, Chi->var.get());
          auto ItMap = mssa->getExtCSToCalleeRetChi().find(MayCallee);
          assert(ItMap != mssa->getExtCSToCalleeRetChi().end());
          addEdge(ItMap->second.at(CS)->var.get(), Chi->var.get());
        }
      }
    }
  }
}

void DepGraphDCF::visitExtractValueInst(llvm::ExtractValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitExtractElementInst(llvm::ExtractElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitInsertElementInst(llvm::InsertElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitInsertValueInst(llvm::InsertValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitShuffleVectorInst(llvm::ShuffleVectorInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *V : I.operands()) {
    addEdge(V, &I);
    funcToLLVMNodesMap[curFunc].insert(V);
  }
}

void DepGraphDCF::visitInstruction(llvm::Instruction &I) {
  errs() << "Error: Unhandled instruction " << I << "\n";
}

void DepGraphDCF::toDot(StringRef Filename) const {
  errs() << "Writing '" << Filename << "' ...\n";

  // FIXME: restore timer with llvm ones

  std::error_code EC;
  raw_fd_ostream Stream(Filename, EC, sys::fs::OF_Text);

  Stream << "digraph F {\n";
  Stream << "compound=true;\n";
  Stream << "rankdir=LR;\n";

  // dot global LLVM values in a separate cluster
  Stream << "\tsubgraph cluster_globalsvar {\n";
  Stream << "style=filled;\ncolor=lightgrey;\n";
  Stream << "label=< <B>  Global Values </B> >;\n";
  Stream << "node [style=filled,color=white];\n";
  for (Value const &G : M.globals()) {
    Stream << "Node" << ((void *)&G) << " [label=\"" << getValueLabel(&G)
           << "\" " << getNodeStyle(&G) << "];\n";
  }
  Stream << "}\n;";

  for (auto const &F : M) {
    if (isIntrinsicDbgFunction(&F)) {
      continue;
    }

    if (F.isDeclaration()) {
      dotExtFunction(Stream, &F);
    } else {
      dotFunction(Stream, &F);
    }
  }

  // Edges
  for (auto I : llvmToLLVMChildren) {
    Value const *S = I.first;
    for (Value const *D : I.second) {
      Stream << "Node" << ((void *)S) << " -> "
             << "Node" << ((void *)D) << "\n";
    }
  }

  for (auto I : llvmToSSAChildren) {
    Value const *S = I.first;
    for (MSSAVar *D : I.second) {
      Stream << "Node" << ((void *)S) << " -> "
             << "Node" << ((void *)D) << "\n";
    }
  }

  for (auto I : ssaToSSAChildren) {
    MSSAVar *S = I.first;
    for (MSSAVar *D : I.second) {
      Stream << "Node" << ((void *)S) << " -> "
             << "Node" << ((void *)D) << "\n";
    }
  }

  for (auto I : ssaToLLVMChildren) {
    MSSAVar *S = I.first;
    for (Value const *D : I.second) {
      Stream << "Node" << ((void *)S) << " -> "
             << "Node" << ((void *)D) << "\n";
    }
  }

  for (auto I : callToFuncEdges) {
    Value const *Call = I.first;
    Function const *F = I.second;
    Stream << "NodeCall" << ((void *)Call) << " -> "
           << "Node" << ((void *)F) << " [lhead=cluster_" << ((void *)F)
           << "]\n";
  }

  for (auto I : condToCallEdges) {
    Value const *S = I.first;
    for (Value const *Call : I.second) {
      Stream << "Node" << ((void *)S) << " -> "
             << "NodeCall" << ((void *)Call) << "\n";
    }
    /*if (taintedLLVMNodes.count(s) != 0){
            errs() << "DBG: " << s->getName() << " is a tainted condition \n";
            s->dump();
    }*/
  }

  Stream << "}\n";
}

void DepGraphDCF::dotFunction(raw_fd_ostream &Stream, Function const *F) const {
  Stream << "\tsubgraph cluster_" << ((void *)F) << " {\n";
  Stream << "style=filled;\ncolor=lightgrey;\n";
  Stream << "label=< <B>" << F->getName() << "</B> >;\n";
  Stream << "node [style=filled,color=white];\n";

  // Nodes with label
  for (Value const *V : getRange(funcToLLVMNodesMap, F)) {
    if (isa<GlobalValue>(V)) {
      continue;
    }
    Stream << "Node" << ((void *)V) << " [label=\"" << getValueLabel(V) << "\" "
           << getNodeStyle(V) << "];\n";
  }

  for (MSSAVar const *V : getRange(funcToSSANodesMap, F)) {
    Stream << "Node" << ((void *)V) << " [label=\"" << V->getName()
           << "\" shape=diamond " << getNodeStyle(V) << "];\n";
  }

  for (Value const *V : getRange(funcToCallNodes, F)) {
    Stream << "NodeCall" << ((void *)V) << " [label=\"" << getCallValueLabel(V)
           << "\" shape=rectangle " << getCallNodeStyle(V) << "];\n";
  }

  Stream << "Node" << ((void *)F) << " [style=invisible];\n";

  Stream << "}\n";
}

void DepGraphDCF::dotExtFunction(raw_fd_ostream &Stream,
                                 Function const *F) const {
  Stream << "\tsubgraph cluster_" << ((void *)F) << " {\n";
  Stream << "style=filled;\ncolor=lightgrey;\n";
  Stream << "label=< <B>" << F->getName() << "</B> >;\n";
  Stream << "node [style=filled,color=white];\n";

  // Nodes with label
  for (Value const *V : getRange(funcToLLVMNodesMap, F)) {
    Stream << "Node" << ((void *)V) << " [label=\"" << getValueLabel(V) << "\" "
           << getNodeStyle(V) << "];\n";
  }

  for (MSSAVar const *V : getRange(funcToSSANodesMap, F)) {
    Stream << "Node" << ((void *)V) << " [label=\"" << V->getName()
           << "\" shape=diamond " << getNodeStyle(V) << "];\n";
  }

  Stream << "Node" << ((void *)F) << " [style=invisible];\n";

  Stream << "}\n";
}

std::string DepGraphDCF::getNodeStyle(llvm::Value const *V) const {
  if (taintedLLVMNodes.count(V) != 0) {
    return "style=filled, color=red";
  }
  return "style=filled, color=white";
}

std::string DepGraphDCF::getNodeStyle(MSSAVar const *V) const {
  if (taintedSSANodes.count(V) != 0) {
    return "style=filled, color=red";
  }
  return "style=filled, color=white";
}

std::string DepGraphDCF::getNodeStyle(Function const *F) {
  return "style=filled, color=white";
}

std::string DepGraphDCF::getCallNodeStyle(llvm::Value const *V) {
  return "style=filled, color=white";
}

void DepGraphDCF::computeTaintedValuesContextInsensitive() {
#ifndef NDEBUG
  unsigned FuncToLlvmNodesMapSize = funcToLLVMNodesMap.size();
  unsigned FuncToSsaNodesMapSize = funcToSSANodesMap.size();
  unsigned VarArgNodeSize = varArgNodes.size();
  unsigned LlvmToLlvmChildrenSize = llvmToLLVMChildren.size();
  unsigned LlvmToLlvmParentsSize = llvmToLLVMParents.size();
  unsigned LlvmToSsaChildrenSize = llvmToSSAChildren.size();
  unsigned LlvmToSsaParentsSize = llvmToSSAParents.size();
  unsigned SsaToLlvmChildrenSize = ssaToLLVMChildren.size();
  unsigned SsaToLlvmParentsSize = ssaToLLVMParents.size();
  unsigned SsaToSsaChildrenSize = ssaToSSAChildren.size();
  unsigned SsaToSsaParentsSize = ssaToSSAParents.size();
  unsigned FuncToCallNodesSize = funcToCallNodes.size();
  unsigned CallToFuncEdgesSize = callToFuncEdges.size();
  unsigned CondToCallEdgesSize = condToCallEdges.size();
  unsigned FuncToCallSitesSize = funcToCallSites.size();
  unsigned CallsiteToCondsSize = callsiteToConds.size();
#endif

  TimeTraceScope TTS("FloodDep");

  std::queue<MSSAVar *> VarToVisit;
  std::queue<Value const *> ValueToVisit;

  // SSA sources
  for (MSSAVar const *Src : ssaSources) {
    taintedSSANodes.insert(Src);
    VarToVisit.push(const_cast<MSSAVar *>(Src));
  }

  // Value sources
  for (Value const *Src : valueSources) {
    taintedLLVMNodes.insert(Src);
    ValueToVisit.push(Src);
  }

  while (!VarToVisit.empty() || !ValueToVisit.empty()) {
    if (!VarToVisit.empty()) {
      MSSAVar *S = VarToVisit.front();
      VarToVisit.pop();

      if (taintResetSSANodes.find(S) != taintResetSSANodes.end()) {
        continue;
      }

      if (ssaToSSAChildren.find(S) != ssaToSSAChildren.end()) {
        for (MSSAVar *D : ssaToSSAChildren[S]) {
          if (taintedSSANodes.count(D) != 0) {
            continue;
          }

          taintedSSANodes.insert(D);
          VarToVisit.push(D);
        }
      }

      if (ssaToLLVMChildren.find(S) != ssaToLLVMChildren.end()) {
        for (Value const *D : ssaToLLVMChildren[S]) {
          if (taintedLLVMNodes.count(D) != 0) {
            continue;
          }

          taintedLLVMNodes.insert(D);
          ValueToVisit.push(D);
        }
      }
    }

    if (!ValueToVisit.empty()) {
      Value const *S = ValueToVisit.front();
      ValueToVisit.pop();

      if (llvmToLLVMChildren.find(S) != llvmToLLVMChildren.end()) {
        for (Value const *D : llvmToLLVMChildren[S]) {
          if (taintedLLVMNodes.count(D) != 0) {
            continue;
          }

          taintedLLVMNodes.insert(D);
          ValueToVisit.push(D);
        }
      }

      if (llvmToSSAChildren.find(S) != llvmToSSAChildren.end()) {
        for (MSSAVar *D : llvmToSSAChildren[S]) {
          if (taintedSSANodes.count(D) != 0) {
            continue;
          }
          taintedSSANodes.insert(D);
          VarToVisit.push(D);
        }
      }
    }
  }

  for (Value const *V : taintedLLVMNodes) {
    taintedConditions.insert(V);
  }

  assert(FuncToLlvmNodesMapSize == funcToLLVMNodesMap.size());
  assert(FuncToSsaNodesMapSize == funcToSSANodesMap.size());
  assert(VarArgNodeSize == varArgNodes.size());
  assert(LlvmToLlvmChildrenSize == llvmToLLVMChildren.size());
  assert(LlvmToLlvmParentsSize == llvmToLLVMParents.size());
  assert(LlvmToSsaChildrenSize == llvmToSSAChildren.size());
  assert(LlvmToSsaParentsSize == llvmToSSAParents.size());
  assert(SsaToLlvmChildrenSize == ssaToLLVMChildren.size());
  assert(SsaToLlvmParentsSize == ssaToLLVMParents.size());
  assert(SsaToSsaChildrenSize == ssaToSSAChildren.size());
  assert(SsaToSsaParentsSize == ssaToSSAParents.size());
  assert(FuncToCallNodesSize == funcToCallNodes.size());
  assert(CallToFuncEdgesSize == callToFuncEdges.size());
  assert(CondToCallEdgesSize == condToCallEdges.size());
  assert(FuncToCallSitesSize == funcToCallSites.size());
  assert(CallsiteToCondsSize == callsiteToConds.size());
}

bool DepGraphDCF::isTaintedValue(Value const *V) const {
  return taintedConditions.find(V) != taintedConditions.end();
}

void DepGraphDCF::getCallInterIPDF(
    llvm::CallInst const *Call,
    std::set<llvm::BasicBlock const *> &Ipdf) const {
  std::set<llvm::CallInst const *> VisitedCallSites;
  std::queue<CallInst const *> CallsitesToVisit;
  CallsitesToVisit.push(Call);

  while (!CallsitesToVisit.empty()) {
    CallInst const *CS = CallsitesToVisit.front();
    Function *F = const_cast<Function *>(CS->getParent()->getParent());
    CallsitesToVisit.pop();
    VisitedCallSites.insert(CS);

    BasicBlock *BB = const_cast<BasicBlock *>(CS->getParent());
    PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(*F);
    std::vector<BasicBlock *> FuncIpdf =
        iterated_postdominance_frontier(PDT, BB);
    Ipdf.insert(FuncIpdf.begin(), FuncIpdf.end());
    auto It = funcToCallSites.find(F);
    if (It != funcToCallSites.end()) {
      for (Value const *V : It->second) {
        CallInst const *CS2 = cast<CallInst>(V);
        if (VisitedCallSites.count(CS2) != 0) {
          continue;
        }
        CallsitesToVisit.push(CS2);
      }
    }
  }
}

bool DepGraphDCF::areSSANodesEquivalent(MSSAVar *Var1, MSSAVar *Var2) {
  assert(Var1);
  assert(Var2);

  if (Var1->def->type == MSSADef::PHI || Var2->def->type == MSSADef::PHI) {
    return false;
  }

  VarSet IncomingSsAsVar1;
  VarSet IncomingSsAsVar2;

  ValueSet IncomingValuesVar1;
  ValueSet IncomingValuesVar2;

  bool FoundVar1 = false;
  bool FoundVar2 = false;
  FoundVar1 = ssaToSSAChildren.find(Var1) != ssaToSSAChildren.end();
  FoundVar2 = ssaToSSAChildren.find(Var2) != ssaToSSAChildren.end();
  if (FoundVar1 != FoundVar2) {
    return false;
  }

  // Check whether number of edges are the same for both nodes.
  if (FoundVar1 && FoundVar2) {
    if (ssaToSSAChildren[Var1].size() != ssaToSSAChildren[Var2].size()) {
      return false;
    }
  }

  FoundVar1 = ssaToLLVMChildren.find(Var1) != ssaToLLVMChildren.end();
  FoundVar2 = ssaToLLVMChildren.find(Var2) != ssaToLLVMChildren.end();
  if (FoundVar1 != FoundVar2) {
    return false;
  }
  if (FoundVar1 && FoundVar2) {
    if (ssaToLLVMChildren[Var1].size() != ssaToLLVMChildren[Var2].size()) {
      return false;
    }
  }

  FoundVar1 = ssaToSSAParents.find(Var1) != ssaToSSAParents.end();
  FoundVar2 = ssaToSSAParents.find(Var2) != ssaToSSAParents.end();
  if (FoundVar1 != FoundVar2) {
    return false;
  }
  if (FoundVar1 && FoundVar2) {
    if (ssaToSSAParents[Var1].size() != ssaToSSAParents[Var2].size()) {
      return false;
    }
  }

  FoundVar1 = ssaToLLVMParents.find(Var1) != ssaToLLVMParents.end();
  FoundVar2 = ssaToLLVMParents.find(Var2) != ssaToLLVMParents.end();
  if (FoundVar1 != FoundVar2) {
    return false;
  }
  if (FoundVar1 && FoundVar2) {
    if (ssaToLLVMParents[Var1].size() != ssaToLLVMParents[Var2].size()) {
      return false;
    }
  }

  // Check whether outgoing edges are the same for both nodes.
  if (ssaToSSAChildren.find(Var1) != ssaToSSAChildren.end()) {
    for (MSSAVar *V : ssaToSSAChildren[Var1]) {
      if (ssaToSSAChildren[Var2].find(V) == ssaToSSAChildren[Var2].end()) {
        return false;
      }
    }
  }
  if (ssaToLLVMChildren.find(Var1) != ssaToLLVMChildren.end()) {
    for (Value const *V : ssaToLLVMChildren[Var1]) {
      if (ssaToLLVMChildren[Var2].find(V) == ssaToLLVMChildren[Var2].end()) {
        return false;
      }
    }
  }

  // Check whether incoming edges are the same for both nodes.
  if (ssaToSSAParents.find(Var1) != ssaToSSAParents.end()) {
    for (MSSAVar *V : ssaToSSAParents[Var1]) {
      if (ssaToSSAParents[Var2].find(V) == ssaToSSAParents[Var2].end()) {
        return false;
      }
    }
  }
  if (ssaToLLVMParents.find(Var1) != ssaToLLVMParents.end()) {
    for (Value const *V : ssaToLLVMParents[Var1]) {
      if (ssaToLLVMParents[Var2].find(V) == ssaToLLVMParents[Var2].end()) {
        return false;
      }
    }
  }

  return true;
}

void DepGraphDCF::eliminatePhi(MSSAPhi *Phi, std::vector<MSSAVar *> Ops) {
  struct Ssa2SsaEdge {
    Ssa2SsaEdge(MSSAVar *S, MSSAVar *D) : S(S), D(D) {}
    MSSAVar *S;
    MSSAVar *D;
  };
  struct Ssa2LlvmEdge {
    Ssa2LlvmEdge(MSSAVar *S, Value const *D) : S(S), D(D) {}
    MSSAVar *S;
    Value const *D;
  };
  struct Llvm2SsaEdge {
    Llvm2SsaEdge(Value const *S, MSSAVar *D) : S(S), D(D) {}
    Value const *S;
    MSSAVar *D;
  };
  struct Llvm2LlvmEdge {
    Llvm2LlvmEdge(Value const *S, Value const *D) : S(S), D(D) {}
    Value const *S;
    Value const *D;
  };

  // Singlify operands
  std::set<MSSAVar *> OpsSet;
  for (MSSAVar *V : Ops) {
    OpsSet.insert(V);
  }
  Ops.clear();
  for (MSSAVar *V : OpsSet) {
    Ops.push_back(V);
  }

  // Remove links from predicates to PHI
  for (Value const *V : Phi->preds) {
    removeEdge(V, Phi->var.get());
  }

  // Remove links from ops to PHI
  for (MSSAVar *Op : Ops) {
    removeEdge(Op, Phi->var.get());
  }

  // For each outgoing edge from PHI to a SSA node N, connect
  // op1 to N and remove the link from PHI to N.
  {
    std::vector<Ssa2SsaEdge> EdgesToAdd;
    std::vector<Ssa2SsaEdge> EdgesToRemove;
    if (ssaToSSAChildren.find(Phi->var.get()) != ssaToSSAChildren.end()) {
      for (MSSAVar *V : ssaToSSAChildren[Phi->var.get()]) {
        EdgesToAdd.push_back(Ssa2SsaEdge(Ops[0], V));
        EdgesToRemove.push_back(Ssa2SsaEdge(Phi->var.get(), V));

        // If N is a phi replace the phi operand of N with op1
        if (V->def->type == MSSADef::PHI) {
          MSSAPhi *OutPhi = cast<MSSAPhi>(V->def);

          bool Found = false;
          for (auto &Entry : OutPhi->opsVar) {
            if (Entry.second == Phi->var.get()) {
              Found = true;
              Entry.second = Ops[0];
              break;
            }
          }
          if (!Found) {
            continue;
          }
          assert(Found);
        }
      }
    }
    for (Ssa2SsaEdge E : EdgesToAdd) {
      addEdge(E.S, E.D);
    }
    for (Ssa2SsaEdge E : EdgesToRemove) {
      removeEdge(E.S, E.D);
    }
  }

  {
    std::vector<Ssa2LlvmEdge> EdgesToAdd;
    std::vector<Ssa2LlvmEdge> EdgesToRemove;

    // For each outgoing edge from PHI to a LLVM node N, connect
    // connect op1 to N and remove the link from PHI to N.
    if (ssaToLLVMChildren.find(Phi->var.get()) != ssaToLLVMChildren.end()) {
      for (Value const *V : ssaToLLVMChildren[Phi->var.get()]) {
        // addEdge(ops[0], v);
        // removeEdge(phi->var, v);
        EdgesToAdd.push_back(Ssa2LlvmEdge(Ops[0], V));
        EdgesToRemove.push_back(Ssa2LlvmEdge(Phi->var.get(), V));
      }
    }
    for (Ssa2LlvmEdge E : EdgesToAdd) {
      addEdge(E.S, E.D);
    }
    for (Ssa2LlvmEdge E : EdgesToRemove) {
      removeEdge(E.S, E.D);
    }
  }

  // Remove PHI Node
  Function const *F = Phi->var->bb->getParent();
  assert(F);
  auto It = funcToSSANodesMap[F].find(Phi->var.get());
  assert(It != funcToSSANodesMap[F].end());
  funcToSSANodesMap[F].erase(It);

  // Remove edges connected to other operands than op0
  {
    std::vector<Ssa2SsaEdge> ToRemove1;
    std::vector<Ssa2LlvmEdge> ToRemove2;
    std::vector<Llvm2SsaEdge> ToRemove3;
    for (unsigned I = 1; I < Ops.size(); ++I) {
      if (ssaToSSAParents.find(Ops[I]) != ssaToSSAParents.end()) {
        for (MSSAVar *V : ssaToSSAParents[Ops[I]]) {
          ToRemove1.push_back(Ssa2SsaEdge(V, Ops[I]));
        }
      }
      if (ssaToLLVMParents.find(Ops[I]) != ssaToLLVMParents.end()) {
        for (Value const *V : ssaToLLVMParents[Ops[I]]) {
          ToRemove3.push_back(Llvm2SsaEdge(V, Ops[I]));
        }
      }
      if (ssaToSSAChildren.find(Ops[I]) != ssaToSSAChildren.end()) {
        for (MSSAVar *V : ssaToSSAChildren[Ops[I]]) {
          ToRemove1.push_back(Ssa2SsaEdge(Ops[I], V));
        }
      }
      if (ssaToLLVMChildren.find(Ops[I]) != ssaToLLVMChildren.end()) {
        for (Value const *V : ssaToLLVMChildren[Ops[I]]) {
          ToRemove2.push_back(Ssa2LlvmEdge(Ops[I], V));
        }
      }
    }
    for (Ssa2SsaEdge E : ToRemove1) {
      removeEdge(E.S, E.D);
    }
    for (Ssa2LlvmEdge E : ToRemove2) {
      removeEdge(E.S, E.D);
    }
    for (Llvm2SsaEdge E : ToRemove3) {
      removeEdge(E.S, E.D);
    }
  }

  // Remove other operands than op 0 from the graph
  for (unsigned I = 1; I < Ops.size(); ++I) {
    auto It2 = funcToSSANodesMap[F].find(Ops[I]);
    assert(It2 != funcToSSANodesMap[F].end());
    funcToSSANodesMap[F].erase(It2);
  }
}

void DepGraphDCF::phiElimination() {

  TimeTraceScope TTS("PhiElimination");

  // For each function, iterate through its basic block and try to eliminate phi
  // function until reaching a fixed point.
  for (Function const &F : M) {
    bool Changed = true;

    while (Changed) {
      Changed = false;

      for (BasicBlock const &BB : F) {
        for (auto const &Phi : getRange(mssa->getBBToPhiMap(), &BB)) {

          assert(funcToSSANodesMap.find(&F) != funcToSSANodesMap.end());

          // Has the phi node been removed already ?
          if (funcToSSANodesMap[&F].count(Phi->var.get()) == 0) {
            continue;
          }

          // For each phi we test if its operands (chi) are not PHI and
          // are equivalent
          std::vector<MSSAVar *> PhiOperands;
          for (auto J : Phi->opsVar) {
            PhiOperands.push_back(J.second);
          }

          bool CanElim = true;
          for (unsigned I = 0; I < PhiOperands.size() - 1; I++) {
            if (!areSSANodesEquivalent(PhiOperands[I], PhiOperands[I + 1])) {
              CanElim = false;
              break;
            }
          }
          if (!CanElim) {
            continue;
          }

          // PHI Node can be eliminated !
          Changed = true;
          eliminatePhi(Phi.get(), PhiOperands);
        }
      }
    }
  }
}

void DepGraphDCF::addEdge(llvm::Value const *S, llvm::Value const *D) {
  llvmToLLVMChildren[S].insert(D);
  llvmToLLVMParents[D].insert(S);
}

void DepGraphDCF::addEdge(llvm::Value const *S, MSSAVar *D) {
  llvmToSSAChildren[S].insert(D);
  ssaToLLVMParents[D].insert(S);
}

void DepGraphDCF::addEdge(MSSAVar *S, llvm::Value const *D) {
  ssaToLLVMChildren[S].insert(D);
  llvmToSSAParents[D].insert(S);
}

void DepGraphDCF::addEdge(MSSAVar *S, MSSAVar *D) {
  ssaToSSAChildren[S].insert(D);
  ssaToSSAParents[D].insert(S);
}

void DepGraphDCF::removeEdge(llvm::Value const *S, llvm::Value const *D) {
  int N;
  N = llvmToLLVMChildren[S].erase(D);
  assert(N == 1);
  N = llvmToLLVMParents[D].erase(S);
  assert(N == 1);
  (void)N;
}

void DepGraphDCF::removeEdge(llvm::Value const *S, MSSAVar *D) {
  int N;
  N = llvmToSSAChildren[S].erase(D);
  assert(N == 1);
  N = ssaToLLVMParents[D].erase(S);
  assert(N == 1);
  (void)N;
}

void DepGraphDCF::removeEdge(MSSAVar *S, llvm::Value const *D) {
  int N;
  N = ssaToLLVMChildren[S].erase(D);
  assert(N == 1);
  N = llvmToSSAParents[D].erase(S);
  assert(N == 1);
  (void)N;
}

void DepGraphDCF::removeEdge(MSSAVar *S, MSSAVar *D) {
  int N;
  N = ssaToSSAChildren[S].erase(D);
  assert(N == 1);
  N = ssaToSSAParents[D].erase(S);
  assert(N == 1);
  (void)N;
}

void DepGraphDCF::dotTaintPath(Value const *V, StringRef Filename,
                               Instruction const *Collective) const {
  errs() << "Writing '" << Filename << "' ...\n";

  // Parcours en largeur
  unsigned CurDist = 0;
  unsigned CurSize = 128;
  std::vector<std::set<Value const *>> VisitedLlvmNodesByDist;
  std::set<Value const *> VisitedLlvmNodes;
  std::vector<std::set<MSSAVar *>> VisitedSsaNodesByDist;
  std::set<MSSAVar *> VisitedSsaNodes;

  VisitedSsaNodesByDist.resize(CurSize);
  VisitedLlvmNodesByDist.resize(CurSize);

  VisitedLlvmNodes.insert(V);

  for (Value const *P : getRange(llvmToLLVMParents, V)) {
    if (VisitedLlvmNodes.find(P) != VisitedLlvmNodes.end()) {
      continue;
    }

    if (taintedLLVMNodes.find(P) == taintedLLVMNodes.end()) {
      continue;
    }

    VisitedLlvmNodesByDist[CurDist].insert(P);
  }
  for (MSSAVar *P : getRange(llvmToSSAParents, V)) {
    if (VisitedSsaNodes.find(P) != VisitedSsaNodes.end()) {
      continue;
    }

    if (taintedSSANodes.find(P) == taintedSSANodes.end()) {
      continue;
    }

    VisitedSsaNodesByDist[CurDist].insert(P);
  }

  bool Stop = false;
  MSSAVar *SsaRoot = NULL;
  Value const *LlvmRoot = NULL;

  while (true) {
    if (CurDist >= CurSize) {
      CurSize *= 2;
      VisitedLlvmNodesByDist.resize(CurSize);
      VisitedSsaNodesByDist.resize(CurSize);
    }

    // Visit parents of llvm values
    for (Value const *V : VisitedLlvmNodesByDist[CurDist]) {
      if (valueSources.find(V) != valueSources.end()) {
        LlvmRoot = V;
        VisitedLlvmNodes.insert(V);
        errs() << "found a path of size " << CurDist << "\n";
        Stop = true;
        break;
      }

      VisitedLlvmNodes.insert(V);

      for (Value const *P : getRange(llvmToLLVMParents, V)) {
        if (VisitedLlvmNodes.find(P) != VisitedLlvmNodes.end()) {
          continue;
        }

        if (taintedLLVMNodes.find(P) == taintedLLVMNodes.end()) {
          continue;
        }

        VisitedLlvmNodesByDist[CurDist + 1].insert(P);
      }
      for (MSSAVar *P : getRange(llvmToSSAParents, V)) {
        if (VisitedSsaNodes.find(P) != VisitedSsaNodes.end()) {
          continue;
        }

        if (taintedSSANodes.find(P) == taintedSSANodes.end()) {
          continue;
        }

        VisitedSsaNodesByDist[CurDist + 1].insert(P);
      }
    }

    if (Stop) {
      break;
    }

    // Visit parents of ssa variables
    for (MSSAVar *V : VisitedSsaNodesByDist[CurDist]) {
      if (ssaSources.find(V) != ssaSources.end()) {
        SsaRoot = V;
        VisitedSsaNodes.insert(V);
        errs() << "found a path of size " << CurDist << "\n";
        Stop = true;
        break;
      }

      VisitedSsaNodes.insert(V);
      for (Value const *P : getRange(ssaToLLVMParents, V)) {
        if (VisitedLlvmNodes.find(P) != VisitedLlvmNodes.end()) {
          continue;
        }

        if (taintedLLVMNodes.find(P) == taintedLLVMNodes.end()) {
          continue;
        }

        VisitedLlvmNodesByDist[CurDist + 1].insert(P);
      }
      for (MSSAVar *P : getRange(ssaToSSAParents, V)) {
        if (VisitedSsaNodes.find(P) != VisitedSsaNodes.end()) {
          continue;
        }

        if (taintedSSANodes.find(P) == taintedSSANodes.end()) {
          continue;
        }

        VisitedSsaNodesByDist[CurDist + 1].insert(P);
      }

      if (Stop) {
        break;
      }
    }

    if (Stop) {
      break;
    }

    CurDist++;
  }

  assert(Stop);

  std::error_code EC;
  raw_fd_ostream Stream(Filename, EC, sys::fs::OF_Text);

  Stream << "digraph F {\n";
  Stream << "compound=true;\n";
  Stream << "rankdir=LR;\n";

  std::vector<std::string> DebugMsgs;
  std::vector<DGDebugLoc> DebugLocs;

  VisitedSsaNodes.clear();
  VisitedLlvmNodes.clear();

  assert(LlvmRoot || SsaRoot);

  if (SsaRoot) {
    VisitedSsaNodes.insert(SsaRoot);
  } else {
    VisitedLlvmNodes.insert(LlvmRoot);
  }

  std::string TmpStr;
  raw_string_ostream StrStream(TmpStr);

  MSSAVar *LastVar = SsaRoot;
  Value const *LastValue = LlvmRoot;
  DGDebugLoc DL;

  if (LastVar) {
    DebugMsgs.push_back(getStringMsg(LastVar));

    if (getDGDebugLoc(LastVar, DL)) {
      DebugLocs.push_back(DL);
    }
  } else {
    DebugMsgs.push_back(getStringMsg(LastValue));
    if (getDGDebugLoc(LastValue, DL)) {
      DebugLocs.push_back(DL);
    }
  }

  bool LastIsVar = LastVar != NULL;

  // Compute edges of the shortest path to strStream
  for (unsigned I = CurDist - 1; I > 0; I--) {
    bool Found = false;
    if (LastIsVar) {
      for (MSSAVar *V : VisitedSsaNodesByDist[I]) {
        if (count(getRange(ssaToSSAParents, V), LastVar) == 0) {
          continue;
        }

        VisitedSsaNodes.insert(V);
        StrStream << "Node" << ((void *)LastVar) << " -> "
                  << "Node" << ((void *)V) << "\n";
        LastVar = V;
        Found = true;
        DebugMsgs.push_back(getStringMsg(V));
        if (getDGDebugLoc(V, DL)) {
          DebugLocs.push_back(DL);
        }
        break;
      }

      if (Found) {
        continue;
      }

      for (Value const *V : VisitedLlvmNodesByDist[I]) {
        if (count(getRange(llvmToSSAParents, V), LastVar) == 0) {
          continue;
        }

        VisitedLlvmNodes.insert(V);
        StrStream << "Node" << ((void *)LastVar) << " -> "
                  << "Node" << ((void *)V) << "\n";
        LastValue = V;
        LastIsVar = false;
        Found = true;
        DebugMsgs.push_back(getStringMsg(V));
        if (getDGDebugLoc(V, DL)) {
          DebugLocs.push_back(DL);
        }
        break;
      }

      assert(Found);
    }

    // Last visited is a value
    else {
      for (MSSAVar *V : VisitedSsaNodesByDist[I]) {
        if (count(getRange(ssaToLLVMParents, V), LastValue) == 0) {
          continue;
        }

        VisitedSsaNodes.insert(V);
        StrStream << "Node" << ((void *)LastValue) << " -> "
                  << "Node" << ((void *)V) << "\n";
        LastVar = V;
        LastIsVar = true;
        Found = true;
        DebugMsgs.push_back(getStringMsg(V));
        if (getDGDebugLoc(V, DL)) {
          DebugLocs.push_back(DL);
        }
        break;
      }

      if (Found) {
        continue;
      }

      for (Value const *V : VisitedLlvmNodesByDist[I]) {
        if (count(getRange(llvmToLLVMParents, V), LastValue) == 0) {
          continue;
        }

        VisitedLlvmNodes.insert(V);
        StrStream << "Node" << ((void *)LastValue) << " -> "
                  << "Node" << ((void *)V) << "\n";
        LastValue = V;
        LastIsVar = false;
        Found = true;
        DebugMsgs.push_back(getStringMsg(V));
        if (getDGDebugLoc(V, DL)) {
          DebugLocs.push_back(DL);
        }
        break;
      }

      assert(Found);
    }
  }

  // compute visited functions
  std::set<Function const *> VisitedFunctions;
  for (auto I : funcToLLVMNodesMap) {
    for (Value const *V : I.second) {
      if (VisitedLlvmNodes.find(V) != VisitedLlvmNodes.end()) {
        VisitedFunctions.insert(I.first);
      }
    }
  }

  for (auto I : funcToSSANodesMap) {
    for (MSSAVar *V : I.second) {
      if (VisitedSsaNodes.find(V) != VisitedSsaNodes.end()) {
        VisitedFunctions.insert(I.first);
      }
    }
  }

  // Dot visited functions and nodes
  for (Function const *F : VisitedFunctions) {
    Stream << "\tsubgraph cluster_" << ((void *)F) << " {\n";
    Stream << "style=filled;\ncolor=lightgrey;\n";
    Stream << "label=< <B>" << F->getName() << "</B> >;\n";
    Stream << "node [style=filled,color=white];\n";

    for (Value const *V : VisitedLlvmNodes) {
      if (count(getRange(funcToLLVMNodesMap, F), V) == 0) {
        continue;
      }

      Stream << "Node" << ((void *)V) << " [label=\"" << getValueLabel(V)
             << "\" " << getNodeStyle(V) << "];\n";
    }

    for (MSSAVar *V : VisitedSsaNodes) {
      if (count(getRange(funcToSSANodesMap, F), V) == 0) {
        continue;
      }

      Stream << "Node" << ((void *)V) << " [label=\"" << V->getName()
             << "\" shape=diamond " << getNodeStyle(V) << "];\n";
    }

    Stream << "}\n";
  }

  // Dot edges
  Stream << StrStream.str();

  Stream << "}\n";

  for (auto const &Msg : DebugMsgs) {
    Stream << Msg;
  }

  // Write trace
  std::string Trace;
  if (getDebugTrace(DebugLocs, Trace, Collective)) {
    std::string Tracefilename = (Filename + ".trace").str();
    errs() << "Writing '" << Tracefilename << "' ...\n";
    raw_fd_ostream Tracestream(Tracefilename, EC, sys::fs::OF_Text);
    Tracestream << Trace;
  }
}

std::string DepGraphDCF::getStringMsg(Value const *V) {
  std::string Msg;
  Msg.append("# ");
  Msg.append(getValueLabel(V));
  Msg.append(":\n# ");

  DebugLoc Loc = NULL;
  std::string FuncName = "unknown";
  Instruction const *Inst = dyn_cast<Instruction>(V);
  if (Inst) {
    Loc = Inst->getDebugLoc();
    FuncName = Inst->getParent()->getParent()->getName().str();
  }

  Msg.append("function: ");
  Msg.append(FuncName);
  if (Loc) {
    Msg.append(" file ");
    Msg.append(Loc->getFilename().str());
    Msg.append(" line ");
    Msg.append(std::to_string(Loc.getLine()));
  } else {
    Msg.append(" no debug loc");
  }
  Msg.append("\n");

  return Msg;
}

std::string DepGraphDCF::getStringMsg(MSSAVar *V) {
  std::string Msg;
  Msg.append("# ");
  Msg.append(V->getName());
  Msg.append(":\n# ");
  std::string FuncName = "unknown";
  if (V->bb) {
    FuncName = V->bb->getParent()->getName().str();
  }

  MSSADef *Def = V->def;
  assert(Def);

  // Def can be PHI, call, store, chi, entry, extvararg, extarg, extret,
  // extcall, extretcall

  DebugLoc Loc = NULL;

  if (isa<MSSACallChi>(Def)) {
    MSSACallChi *CallChi = cast<MSSACallChi>(Def);
    FuncName = CallChi->inst->getParent()->getParent()->getName().str();
    Loc = CallChi->inst->getDebugLoc();
  } else if (isa<MSSAStoreChi>(Def)) {
    MSSAStoreChi *StoreChi = cast<MSSAStoreChi>(Def);
    FuncName = StoreChi->inst->getParent()->getParent()->getName().str();
    Loc = StoreChi->inst->getDebugLoc();
  } else if (isa<MSSAExtCallChi>(Def)) {
    MSSAExtCallChi *ExtCallChi = cast<MSSAExtCallChi>(Def);
    FuncName = ExtCallChi->inst->getParent()->getParent()->getName().str();
    Loc = ExtCallChi->inst->getDebugLoc();
  } else if (isa<MSSAExtVarArgChi>(Def)) {
    MSSAExtVarArgChi *VarArgChi = cast<MSSAExtVarArgChi>(Def);
    FuncName = VarArgChi->func->getName().str();
  } else if (isa<MSSAExtArgChi>(Def)) {
    MSSAExtArgChi *ExtArgChi = cast<MSSAExtArgChi>(Def);
    FuncName = ExtArgChi->func->getName().str();
  } else if (isa<MSSAExtRetChi>(Def)) {
    MSSAExtRetChi *ExtRetChi = cast<MSSAExtRetChi>(Def);
    FuncName = ExtRetChi->func->getName().str();
  }

  Msg.append("function: ");
  Msg.append(FuncName);

  if (Loc) {
    Msg.append(" file ");
    Msg.append(Loc->getFilename().str());
    Msg.append(" line ");
    Msg.append(std::to_string(Loc.getLine()));
  } else {
    Msg.append(" no debug loc");
  }
  Msg.append("\n");

  return Msg;
}

bool DepGraphDCF::getDGDebugLoc(Value const *V, DGDebugLoc &DL) {
  DL.F = NULL;
  DL.line = -1;
  DL.filename = "unknown";

  DebugLoc Loc = NULL;

  Instruction const *Inst = dyn_cast<Instruction>(V);
  if (Inst) {
    Loc = Inst->getDebugLoc();
    DL.F = Inst->getParent()->getParent();
  }

  if (Loc) {
    DL.filename = Loc->getFilename().str();
    DL.line = Loc->getLine();
  } else {
    return false;
  }

  return DL.F != NULL;
}

bool DepGraphDCF::getDGDebugLoc(MSSAVar *V, DGDebugLoc &DL) {
  DL.F = NULL;
  DL.line = -1;
  DL.filename = "unknown";

  if (V->bb) {
    DL.F = V->bb->getParent();
  }

  MSSADef *Def = V->def;
  assert(Def);

  // Def can be PHI, call, store, chi, entry, extvararg, extarg, extret,
  // extcall, extretcall

  DebugLoc Loc = NULL;

  if (isa<MSSACallChi>(Def)) {
    MSSACallChi *CallChi = cast<MSSACallChi>(Def);
    DL.F = CallChi->inst->getParent()->getParent();
    Loc = CallChi->inst->getDebugLoc();
  } else if (isa<MSSAStoreChi>(Def)) {
    MSSAStoreChi *StoreChi = cast<MSSAStoreChi>(Def);
    DL.F = StoreChi->inst->getParent()->getParent();
    Loc = StoreChi->inst->getDebugLoc();
  } else if (isa<MSSAExtCallChi>(Def)) {
    MSSAExtCallChi *ExtCallChi = cast<MSSAExtCallChi>(Def);
    DL.F = ExtCallChi->inst->getParent()->getParent();
    Loc = ExtCallChi->inst->getDebugLoc();
  } else if (isa<MSSAExtVarArgChi>(Def)) {
    MSSAExtVarArgChi *VarArgChi = cast<MSSAExtVarArgChi>(Def);
    DL.F = VarArgChi->func;
  } else if (isa<MSSAExtArgChi>(Def)) {
    MSSAExtArgChi *ExtArgChi = cast<MSSAExtArgChi>(Def);
    DL.F = ExtArgChi->func;
  } else if (isa<MSSAExtRetChi>(Def)) {
    MSSAExtRetChi *ExtRetChi = cast<MSSAExtRetChi>(Def);
    DL.F = ExtRetChi->func;
  }

  if (Loc) {
    DL.filename = Loc->getFilename().str();
    DL.line = Loc->getLine();
  } else {
    return false;
  }

  return DL.F != NULL;
}

static bool getStrLine(std::ifstream &File, int Line, std::string &Str) {
  // go to line
  File.seekg(std::ios::beg);
  for (int I = 0; I < Line - 1; ++I) {
    File.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }

  getline(File, Str);

  return true;
}

void DepGraphDCF::reorderAndRemoveDup(std::vector<DGDebugLoc> &DLs) {
  std::vector<DGDebugLoc> SameFuncDl;
  std::vector<DGDebugLoc> Res;

  if (DLs.empty()) {
    return;
  }

  Function const *Prev = DLs[0].F;
  while (!DLs.empty()) {
    // pop front
    DGDebugLoc DL = DLs.front();
    DLs.erase(DLs.begin());

    // new function or end
    if (DL.F != Prev || DLs.empty()) {
      if (!DLs.empty()) {
        DLs.insert(DLs.begin(), DL);
      } else {
        SameFuncDl.push_back(DL);
      }

      Prev = DL.F;

      // sort
      std::sort(SameFuncDl.begin(), SameFuncDl.end());

      // remove duplicates
      int LinePrev = -1;
      for (unsigned I = 0; I < SameFuncDl.size(); ++I) {
        if (SameFuncDl[I].line == LinePrev) {
          LinePrev = SameFuncDl[I].line;
          SameFuncDl.erase(SameFuncDl.begin() + I);
          I--;
        } else {
          LinePrev = SameFuncDl[I].line;
        }
      }

      // append to res
      Res.insert(Res.end(), SameFuncDl.begin(), SameFuncDl.end());
      SameFuncDl.clear();
    } else {
      SameFuncDl.push_back(DL);
    }
  }

  DLs.clear();
  DLs.insert(DLs.begin(), Res.begin(), Res.end());
}

bool DepGraphDCF::getDebugTrace(std::vector<DGDebugLoc> &DLs,
                                std::string &Trace,
                                Instruction const *Collective) {
  DGDebugLoc CollectiveLoc;
  if (getDGDebugLoc(Collective, CollectiveLoc)) {
    DLs.push_back(CollectiveLoc);
  }

  Function const *PrevFunc = NULL;
  std::ifstream File;

  reorderAndRemoveDup(DLs);

  for (unsigned I = 0; I < DLs.size(); ++I) {
    std::string Strline;
    Function const *F = DLs[I].F;
    if (!F) {
      return false;
    }

    // new function, print filename and protoype
    if (F != PrevFunc) {
      File.close();
      PrevFunc = F;
      DISubprogram *DI = F->getSubprogram();
      if (!DI) {
        return false;
      }

      std::string Filename = DI->getFilename().str();
      std::string Dir = DI->getDirectory().str();
      std::string Path = Dir + "/" + Filename;
      int Line = DI->getLine();

      File.open(Path, std::ios::in);
      if (!File.good()) {
        errs() << "error opening file: " << Path << "\n";
        return false;
      }

      getStrLine(File, Line, Strline);
      Trace.append("\n" + Filename + "\n");
      Trace.append(Strline);
      Trace.append(" l." + std::to_string(Line) + "\n");
    }

    getStrLine(File, DLs[I].line, Strline);
    Trace.append("...\n" + Strline + " l." + std::to_string(DLs[I].line) +
                 "\n");
  }

  File.close();

  return true;
}

void DepGraphDCF::floodFunction(Function const *F) {
  std::queue<MSSAVar *> VarToVisit;
  std::queue<Value const *> ValueToVisit;

  // 1) taint LLVM and SSA sources
  for (MSSAVar const *S : ssaSources) {
    if (funcToSSANodesMap.find(F) == funcToSSANodesMap.end()) {
      continue;
    }

    if (funcToSSANodesMap[F].find(const_cast<MSSAVar *>(S)) !=
        funcToSSANodesMap[F].end()) {
      taintedSSANodes.insert(S);
    }
  }

  for (Value const *S : valueSources) {
    Instruction const *Inst = dyn_cast<Instruction>(S);
    if (!Inst || Inst->getParent()->getParent() != F) {
      continue;
    }

    taintedLLVMNodes.insert(S);
  }

  // 2) Add tainted variables of the function to the queue.
  if (funcToSSANodesMap.find(F) != funcToSSANodesMap.end()) {
    for (MSSAVar *V : funcToSSANodesMap[F]) {
      if (taintedSSANodes.find(V) != taintedSSANodes.end()) {
        VarToVisit.push(V);
      }
    }
  }
  if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
    for (Value const *V : funcToLLVMNodesMap[F]) {
      if (taintedLLVMNodes.find(V) != taintedLLVMNodes.end()) {
        ValueToVisit.push(V);
      }
    }
  }

  // 3) flood function
  while (!VarToVisit.empty() || !ValueToVisit.empty()) {
    if (!VarToVisit.empty()) {
      MSSAVar *S = VarToVisit.front();
      VarToVisit.pop();

      if (taintResetSSANodes.find(S) != taintResetSSANodes.end()) {
        continue;
      }

      if (ssaToSSAChildren.find(S) != ssaToSSAChildren.end()) {
        for (MSSAVar *D : ssaToSSAChildren[S]) {
          if (funcToSSANodesMap.find(F) == funcToSSANodesMap.end()) {
            continue;
          }

          if (funcToSSANodesMap[F].find(D) == funcToSSANodesMap[F].end()) {
            continue;
          }
          if (taintedSSANodes.count(D) != 0) {
            continue;
          }

          taintedSSANodes.insert(D);
          VarToVisit.push(D);
        }
      }

      if (ssaToLLVMChildren.find(S) != ssaToLLVMChildren.end()) {
        for (Value const *D : ssaToLLVMChildren[S]) {
          if (funcToLLVMNodesMap[F].find(D) == funcToLLVMNodesMap[F].end()) {
            continue;
          }

          if (taintedLLVMNodes.count(D) != 0) {
            continue;
          }

          taintedLLVMNodes.insert(D);
          ValueToVisit.push(D);
        }
      }
    }

    if (!ValueToVisit.empty()) {
      Value const *S = ValueToVisit.front();
      ValueToVisit.pop();

      if (llvmToLLVMChildren.find(S) != llvmToLLVMChildren.end()) {
        for (Value const *D : llvmToLLVMChildren[S]) {
          if (funcToLLVMNodesMap.find(F) == funcToLLVMNodesMap.end()) {
            continue;
          }

          if (funcToLLVMNodesMap[F].find(D) == funcToLLVMNodesMap[F].end()) {
            continue;
          }

          if (taintedLLVMNodes.count(D) != 0) {
            continue;
          }

          taintedLLVMNodes.insert(D);
          ValueToVisit.push(D);
        }
      }

      if (llvmToSSAChildren.find(S) != llvmToSSAChildren.end()) {
        for (MSSAVar *D : llvmToSSAChildren[S]) {
          if (funcToSSANodesMap.find(F) == funcToSSANodesMap.end()) {
            continue;
          }
          if (funcToSSANodesMap[F].find(D) == funcToSSANodesMap[F].end()) {
            continue;
          }

          if (taintedSSANodes.count(D) != 0) {
            continue;
          }
          taintedSSANodes.insert(D);
          VarToVisit.push(D);
        }
      }
    }
  }
}

void DepGraphDCF::floodFunctionFromFunction(Function const *To,
                                            Function const *From) {
  if (funcToSSANodesMap.find(From) != funcToSSANodesMap.end()) {
    for (MSSAVar *S : funcToSSANodesMap[From]) {
      if (taintedSSANodes.find(S) == taintedSSANodes.end()) {
        continue;
      }
      if (taintResetSSANodes.find(S) != taintResetSSANodes.end()) {
        if (ssaToSSAChildren.find(S) != ssaToSSAChildren.end()) {
          for (MSSAVar *D : ssaToSSAChildren[S]) {
            if (funcToSSANodesMap.find(To) == funcToSSANodesMap.end()) {
              continue;
            }
            if (funcToSSANodesMap[To].find(D) == funcToSSANodesMap[To].end()) {
              continue;
            }
            taintedSSANodes.erase(D);
          }
        }

        if (ssaToLLVMChildren.find(S) != ssaToLLVMChildren.end()) {
          for (Value const *D : ssaToLLVMChildren[S]) {
            if (funcToLLVMNodesMap.find(To) == funcToLLVMNodesMap.end()) {
              continue;
            }

            if (funcToLLVMNodesMap[To].find(D) ==
                funcToLLVMNodesMap[To].end()) {
              continue;
            }
            taintedLLVMNodes.erase(D);
          }
        }

        continue;
      }

      if (ssaToSSAChildren.find(S) != ssaToSSAChildren.end()) {
        for (MSSAVar *D : ssaToSSAChildren[S]) {
          if (funcToSSANodesMap.find(To) == funcToSSANodesMap.end()) {
            continue;
          }
          if (funcToSSANodesMap[To].find(D) == funcToSSANodesMap[To].end()) {
            continue;
          }
          taintedSSANodes.insert(D);
        }
      }

      if (ssaToLLVMChildren.find(S) != ssaToLLVMChildren.end()) {
        for (Value const *D : ssaToLLVMChildren[S]) {
          if (funcToLLVMNodesMap.find(To) == funcToLLVMNodesMap.end()) {
            continue;
          }

          if (funcToLLVMNodesMap[To].find(D) == funcToLLVMNodesMap[To].end()) {
            continue;
          }
          taintedLLVMNodes.insert(D);
        }
      }
    }
  }

  if (funcToLLVMNodesMap.find(From) != funcToLLVMNodesMap.end()) {
    for (Value const *S : funcToLLVMNodesMap[From]) {
      if (taintedLLVMNodes.find(S) == taintedLLVMNodes.end()) {
        continue;
      }

      if (llvmToSSAChildren.find(S) != llvmToSSAChildren.end()) {
        for (MSSAVar *D : llvmToSSAChildren[S]) {
          if (funcToSSANodesMap.find(To) == funcToSSANodesMap.end()) {
            continue;
          }
          if (funcToSSANodesMap[To].find(D) == funcToSSANodesMap[To].end()) {
            continue;
          }
          taintedSSANodes.insert(D);
        }
      }

      if (llvmToLLVMChildren.find(S) != llvmToLLVMChildren.end()) {
        for (Value const *D : llvmToLLVMChildren[S]) {
          if (funcToLLVMNodesMap.find(To) == funcToLLVMNodesMap.end()) {
            continue;
          }
          if (funcToLLVMNodesMap[To].find(D) == funcToLLVMNodesMap[To].end()) {
            continue;
          }
          taintedLLVMNodes.insert(D);
        }
      }
    }
  }
}

void DepGraphDCF::resetFunctionTaint(Function const *F) {
  assert(F && CG.isReachableFromEntry(*F));
  if (funcToSSANodesMap.find(F) != funcToSSANodesMap.end()) {
    for (MSSAVar *V : funcToSSANodesMap[F]) {
      if (taintedSSANodes.find(V) != taintedSSANodes.end()) {
        taintedSSANodes.erase(V);
      }
    }
  }

  if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
    for (Value const *V : funcToLLVMNodesMap[F]) {
      if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
        taintedLLVMNodes.erase(V);
      }
    }
  }
}

void DepGraphDCF::computeFunctionCSTaintedConds(llvm::Function const *F) {
  for (BasicBlock const &BB : *F) {
    for (Instruction const &I : BB) {
      if (!isa<CallInst>(I)) {
        continue;
      }

      if (callsiteToConds.find(cast<Value>(&I)) != callsiteToConds.end()) {
        for (Value const *V : callsiteToConds[cast<Value>(&I)]) {
          if (taintedLLVMNodes.find(V) != taintedLLVMNodes.end()) {
            // EMMA : if(v->getName() != "cmp1" && v->getName() != "cmp302"){
            taintedConditions.insert(V);
            // errs() << "EMMA: value tainted: " << v->getName() << "\n";
            //}
          }
        }
      }
    }
  }
}

void DepGraphDCF::computeTaintedValuesContextSensitive() {
#ifndef NDEBUG
  unsigned FuncToLlvmNodesMapSize = funcToLLVMNodesMap.size();
  unsigned FuncToSsaNodesMapSize = funcToSSANodesMap.size();
  unsigned VarArgNodeSize = varArgNodes.size();
  unsigned LlvmToLlvmChildrenSize = llvmToLLVMChildren.size();
  unsigned LlvmToLlvmParentsSize = llvmToLLVMParents.size();
  unsigned LlvmToSsaChildrenSize = llvmToSSAChildren.size();
  unsigned LlvmToSsaParentsSize = llvmToSSAParents.size();
  unsigned SsaToLlvmChildrenSize = ssaToLLVMChildren.size();
  unsigned SsaToLlvmParentsSize = ssaToLLVMParents.size();
  unsigned SsaToSsaChildrenSize = ssaToSSAChildren.size();
  unsigned SsaToSsaParentsSize = ssaToSSAParents.size();
  unsigned FuncToCallNodesSize = funcToCallNodes.size();
  unsigned CallToFuncEdgesSize = callToFuncEdges.size();
  unsigned CondToCallEdgesSize = condToCallEdges.size();
  unsigned FuncToCallSitesSize = funcToCallSites.size();
  unsigned CallsiteToCondsSize = callsiteToConds.size();
#endif

  PTACallGraphNode const *Entry = CG.getEntry();
  if (Entry->getFunction()) {
    computeTaintedValuesCSForEntry(Entry);
  } else {
    for (auto I = Entry->begin(), E = Entry->end(); I != E; ++I) {
      PTACallGraphNode *CalleeNode = I->second;
      computeTaintedValuesCSForEntry(CalleeNode);
    }
  }

  assert(FuncToLlvmNodesMapSize == funcToLLVMNodesMap.size());
  assert(FuncToSsaNodesMapSize == funcToSSANodesMap.size());
  assert(VarArgNodeSize == varArgNodes.size());
  assert(LlvmToLlvmChildrenSize == llvmToLLVMChildren.size());
  assert(LlvmToLlvmParentsSize == llvmToLLVMParents.size());
  assert(LlvmToSsaChildrenSize == llvmToSSAChildren.size());
  assert(LlvmToSsaParentsSize == llvmToSSAParents.size());
  assert(SsaToLlvmChildrenSize == ssaToLLVMChildren.size());
  assert(SsaToLlvmParentsSize == ssaToLLVMParents.size());
  assert(SsaToSsaChildrenSize == ssaToSSAChildren.size());
  assert(SsaToSsaParentsSize == ssaToSSAParents.size());
  assert(FuncToCallNodesSize == funcToCallNodes.size());
  assert(CallToFuncEdgesSize == callToFuncEdges.size());
  assert(CondToCallEdgesSize == condToCallEdges.size());
  assert(FuncToCallSitesSize == funcToCallSites.size());
  assert(CallsiteToCondsSize == callsiteToConds.size());
}

void DepGraphDCF::computeTaintedValuesCSForEntry(
    PTACallGraphNode const *Entry) {
  std::vector<PTACallGraphNode const *> S;

  std::map<PTACallGraphNode const *, std::set<PTACallGraphNode *>>
      Node2VisitedChildrenMap;
  S.push_back(Entry);

  bool GoingDown = true;
  Function const *Prev = NULL;

  while (!S.empty()) {
    PTACallGraphNode const *N = S.back();
    bool FoundChildren = false;

    //    if (N->getFunction())
    //      errs() << "current =" << N->getFunction()->getName() << "\n";

    /*    if (goingDown)
          errs() << "down\n";
        else
          errs() << "up\n";
    */
    if (Prev) {
      if (GoingDown) {
        // errs() << "tainting " << N->getFunction()->getName() << " from "
        // << prev->getName() << "\n";
        floodFunctionFromFunction(N->getFunction(), Prev);

        // errs() << "tainting " << N->getFunction()->getName() << "\n";
        floodFunction(N->getFunction());

        // errs() << "for each call site get PDF+ and save tainted
        // conditions\n";
        computeFunctionCSTaintedConds(N->getFunction());
      } else {
        // errs() << "tainting " << N->getFunction()->getName() << " from "
        //   << prev->getName() << "\n";
        floodFunctionFromFunction(N->getFunction(), Prev);

        // errs() << "tainting " << N->getFunction()->getName() << "\n";
        floodFunction(N->getFunction());

        // errs() << "for each call site get PDF+ and save tainted
        // conditions\n";
        computeFunctionCSTaintedConds(N->getFunction());

        // errs() << "untainting " << prev->getName() << "\n";
        resetFunctionTaint(Prev);
      }
    } else {
      // errs() << "tainting " << N->getFunction()->getName() << "\n";
      floodFunction(N->getFunction());

      LLVM_DEBUG(
          dbgs()
          << "for each call site get PDF+ and save tainted conditions\n");
      computeFunctionCSTaintedConds(N->getFunction());
    }

    // Add first unvisited callee to stack if any
    for (auto I = N->begin(), E = N->end(); I != E; ++I) {
      PTACallGraphNode *CalleeNode = I->second;
      if (Node2VisitedChildrenMap[N].find(CalleeNode) ==
          Node2VisitedChildrenMap[N].end()) {
        FoundChildren = true;
        Node2VisitedChildrenMap[N].insert(CalleeNode);
        if (CalleeNode->getFunction()) {
          S.push_back(CalleeNode);
          break;
        }
      }
    }

    if (!FoundChildren) {
      S.pop_back();
      GoingDown = false;
    } else {
      GoingDown = true;
    }

    Prev = N->getFunction();
  }
}

AnalysisKey DepGraphDCFAnalysis::Key;
DepGraphDCFAnalysis::Result
DepGraphDCFAnalysis::run(Module &M, ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("DepGraphDCFAnalysis");
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto &MSSA = AM.getResult<MemorySSAAnalysis>(M);
  auto const &PTACG = AM.getResult<PTACallGraphAnalysis>(M);
  return std::make_unique<DepGraphDCF>(MSSA.get(), *PTACG, FAM, M,
                                       ContextInsensitive_);
}
} // namespace parcoach
