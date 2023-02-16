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
struct functionArg {
  std::string name;
  int arg;
};

std::vector<functionArg> ssaSourceFunctions;
std::vector<functionArg> valueSourceFunctions;
std::vector<char const *> loadValueSources;
std::vector<functionArg> resetFunctions;
cl::opt<bool> optWeakUpdate("weak-update", cl::desc("Weak update"),
                            cl::cat(ParcoachCategory));
} // namespace

DepGraphDCF::DepGraphDCF(MemorySSA *mssa, PTACallGraph const &CG,
                         FunctionAnalysisManager &AM, Module &M,
                         bool ContextInsensitive, bool noPtrDep, bool noPred,
                         bool disablePhiElim)
    : mssa(mssa), CG(CG), FAM(AM), M(M), ContextInsensitive(ContextInsensitive),
      PDT(nullptr), noPtrDep(noPtrDep), noPred(noPred),
      disablePhiElim(disablePhiElim) {

  if (Options::get().isActivated(Paradigm::MPI))
    enableMPI();
#ifdef PARCOACH_ENABLE_OPENMP
  if (Options::get().isActivated(Paradigm::OMP))
    enableOMP();
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
    if (!CG.isReachableFromEntry(F))
      continue;

    if (isIntrinsicDbgFunction(&F))
      continue;

    buildFunction(&F);
  }

  if (!disablePhiElim)
    phiElimination();

  // Compute tainted values
  if (ContextInsensitive)
    computeTaintedValuesContextInsensitive();
  else
    computeTaintedValuesContextSensitive();

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
  resetFunctions.push_back({"MPI_Bcast", 0});
  resetFunctions.push_back({"MPI_Allgather", 3});
  resetFunctions.push_back({"MPI_Allgatherv", 3});
  resetFunctions.push_back({"MPI_Alltoall", 3});
  resetFunctions.push_back({"MPI_Alltoallv", 4});
  resetFunctions.push_back({"MPI_Alltoallw", 4});
  resetFunctions.push_back({"MPI_Allreduce", 1});
  ssaSourceFunctions.push_back({"MPI_Comm_rank", 1});
  ssaSourceFunctions.push_back({"MPI_Group_rank", 1});
}

void DepGraphDCF::enableOMP() {
  valueSourceFunctions.push_back({"__kmpc_global_thread_num", -1});
  valueSourceFunctions.push_back({"_omp_get_thread_num", -1});
  valueSourceFunctions.push_back({"omp_get_thread_num", -1});
}

void DepGraphDCF::enableUPC() { loadValueSources.push_back("gasneti_mynode"); }

void DepGraphDCF::enableCUDA() {
  // threadIdx.x
  valueSourceFunctions.push_back({"llvm.nvvm.read.ptx.sreg.tid.x", -1});
  // threadIdx.y
  valueSourceFunctions.push_back({"llvm.nvvm.read.ptx.sreg.tid.y", -1});
  // threadIdx.z
  valueSourceFunctions.push_back({"llvm.nvvm.read.ptx.sreg.tid.z", -1});
}

void DepGraphDCF::buildFunction(llvm::Function const *F) {
  TimeTraceScope TTS("BuildGraph");

  curFunc = F;

  if (F->isDeclaration())
    PDT = nullptr;
  else
    PDT = &FAM.getResult<PostDominatorTreeAnalysis>(*const_cast<Function *>(F));

  visit(*const_cast<Function *>(F));

  // Add entry chi nodes to the graph.
  for (auto &chi : getRange(mssa->getFunToEntryChiMap(), F)) {
    assert(chi && chi->var);
    funcToSSANodesMap[F].insert(chi->var.get());
    if (chi->opVar) {
      funcToSSANodesMap[F].insert(chi->opVar);
      addEdge(chi->opVar, chi->var.get());
    }
  }

  // External functions
  if (F->isDeclaration()) {

    // Add var arg entry and exit chi nodes.
    if (F->isVarArg()) {
      for (auto &I : getRange(mssa->getExtCSToVArgEntryChi(), F)) {
        MSSAChi *entryChi = I.second.get();
        assert(entryChi && entryChi->var && "cs to vararg not found");
        funcToSSANodesMap[F].emplace(entryChi->var.get());
      }
      for (auto &I : getRange(mssa->getExtCSToVArgExitChi(), F)) {
        MSSAChi *exitChi = I.second.get();
        assert(exitChi && exitChi->var);
        funcToSSANodesMap[F].insert(exitChi->var.get());
        addEdge(exitChi->opVar, exitChi->var.get());
      }
    }

    // Add args entry and exit chi nodes for external functions.
    unsigned argNo = 0;
    for (Argument const &arg : F->args()) {
      if (!arg.getType()->isPointerTy()) {
        argNo++;
        continue;
      }

      for (auto const &I : getRange(mssa->getExtCSToArgEntryChi(), F)) {
        MSSAChi *entryChi = I.second.at(argNo);
        assert(entryChi && entryChi->var && "cs to arg not found");
        funcToSSANodesMap[F].emplace(entryChi->var.get());
      }
      for (auto const &I : getRange(mssa->getExtCSToArgExitChi(), F)) {
        MSSAChi *exitChi = I.second.at(argNo);
        assert(exitChi && exitChi->var);
        funcToSSANodesMap[F].emplace(exitChi->var.get());
        addEdge(exitChi->opVar, exitChi->var.get());
      }

      argNo++;
    }

    // Add retval chi node for external functions
    if (F->getReturnType()->isPointerTy()) {
      for (auto &I : getRange(mssa->getExtCSToCalleeRetChi(), F)) {
        MSSAChi *retChi = I.second.get();
        assert(retChi && retChi->var);
        funcToSSANodesMap[F].emplace(retChi->var.get());
      }
    }

    // memcpy
    if (F->getName().find("memcpy") != StringRef::npos) {
      auto CSToArgEntry = mssa->getExtCSToArgEntryChi().lookup(F);
      auto CSToArgExit = mssa->getExtCSToArgExitChi().lookup(F);
      for (auto I : CSToArgEntry) {
        CallBase *CS = I.first;
        MSSAChi *srcEntryChi = CSToArgEntry[CS][1];
        MSSAChi *dstExitChi = CSToArgExit[CS][0];

        addEdge(srcEntryChi->var.get(), dstExitChi->var.get());

        // llvm.mempcy instrinsic returns void whereas memcpy returns dst
        if (F->getReturnType()->isPointerTy()) {
          MSSAChi *retChi{};
          auto It = mssa->getExtCSToCalleeRetChi().find(F);
          if (It != mssa->getExtCSToCalleeRetChi().end()) {
            retChi = It->second.at(CS).get();
          }
          addEdge(dstExitChi->var.get(), retChi->var.get());
        }
      }
    }

    // memmove
    else if (F->getName().find("memmove") != StringRef::npos) {
      auto CSToArgEntry = mssa->getExtCSToArgEntryChi().lookup(F);
      auto CSToArgExit = mssa->getExtCSToArgExitChi().lookup(F);
      for (auto I : CSToArgEntry) {
        CallBase *CS = I.first;

        MSSAChi *srcEntryChi = CSToArgEntry[CS][1];
        MSSAChi *dstExitChi = CSToArgExit[CS][0];

        addEdge(srcEntryChi->var.get(), dstExitChi->var.get());

        // llvm.memmove instrinsic returns void whereas memmove returns dst
        if (F->getReturnType()->isPointerTy()) {
          MSSAChi *retChi{};
          auto It = mssa->getExtCSToCalleeRetChi().find(F);
          if (It != mssa->getExtCSToCalleeRetChi().end()) {
            retChi = It->second.at(CS).get();
          }
          addEdge(dstExitChi->var.get(), retChi->var.get());
        }
      }
    }

    // memset
    else if (F->getName().find("memset") != StringRef::npos) {
      for (auto &I : getRange(mssa->getExtCSToArgExitChi(), F)) {
        CallBase *CS = I.first;

        MSSAChi *argExitChi = mssa->getExtCSToArgExitChi().lookup(F)[CS][0];
        addEdge(F->getArg(1), argExitChi->var.get());

        // llvm.memset instrinsic returns void whereas memset returns dst
        if (F->getReturnType()->isPointerTy()) {
          MSSAChi *retChi{};
          auto It = mssa->getExtCSToCalleeRetChi().find(F);
          if (It != mssa->getExtCSToCalleeRetChi().end()) {
            retChi = It->second.at(CS).get();
          }
          addEdge(argExitChi->var.get(), retChi->var.get());
        }
      }
    }

    // Unknown external function, we have to connect every input to every
    // output.
    else {
      for (CallBase *cs : getRange(mssa->getExtFuncToCSMap(), F)) {
        std::set<MSSAVar *> ssaOutputs;
        std::set<MSSAVar *> ssaInputs;

        // Compute SSA outputs
        auto const &CSToArgExit = mssa->getExtCSToArgExitChi();
        auto const &CSToArgEntry = mssa->getExtCSToArgEntryChi();
        auto IndexToExitChi = CSToArgExit.lookup(F)[cs];
        for (auto &I : IndexToExitChi) {
          MSSAChi *argExitChi = I.second;
          ssaOutputs.emplace(argExitChi->var.get());
        }
        if (F->isVarArg()) {
          MSSAChi *varArgExitChi{};
          auto It = mssa->getExtCSToVArgExitChi().find(F);
          if (It != mssa->getExtCSToVArgExitChi().end()) {
            varArgExitChi = It->second.at(cs).get();
          }
          ssaOutputs.emplace(varArgExitChi->var.get());
        }
        if (F->getReturnType()->isPointerTy()) {
          MSSAChi *retChi{};
          auto It = mssa->getExtCSToCalleeRetChi().find(F);
          if (It != mssa->getExtCSToCalleeRetChi().end()) {
            retChi = It->second.at(cs).get();
          }
          ssaOutputs.emplace(retChi->var.get());
        }

        // Compute SSA inputs
        auto IndexToEntryChi = CSToArgEntry.lookup(F)[cs];
        for (auto &I : IndexToEntryChi) {
          MSSAChi *argEntryChi = I.second;
          ssaInputs.emplace(argEntryChi->var.get());
        }
        if (F->isVarArg()) {
          MSSAChi *varArgEntryChi{};
          auto It = mssa->getExtCSToVArgEntryChi().find(F);
          if (It != mssa->getExtCSToVArgEntryChi().end()) {
            varArgEntryChi = It->second.at(cs).get();
          }
          ssaInputs.emplace(varArgEntryChi->var.get());
        }

        // Connect SSA inputs to SSA outputs
        for (MSSAVar *in : ssaInputs) {
          for (MSSAVar *out : ssaOutputs) {
            addEdge(in, out);
          }
        }

        // Connect LLVM arguments to SSA outputs
        for (Argument const &arg : F->args()) {
          for (MSSAVar *out : ssaOutputs) {
            addEdge(&arg, out);
          }
        }
      }
    }

    // SSA Source functions
    for (unsigned i = 0; i < ssaSourceFunctions.size(); ++i) {
      if (!F->getName().equals(ssaSourceFunctions[i].name))
        continue;
      unsigned argNo = ssaSourceFunctions[i].arg;
      for (auto &I : getRange(mssa->getExtCSToArgExitChi(), F)) {
        assert(I.second.at(argNo));
        ssaSources.emplace(I.second.at(argNo)->var.get());
      }
    }
  }
}

void DepGraphDCF::visitBasicBlock(llvm::BasicBlock &BB) {
  // Add MSSA Phi nodes and edges to the graph.
  for (auto &phi : getRange(mssa->getBBToPhiMap(), &BB)) {
    assert(phi && phi->var);
    funcToSSANodesMap[curFunc].insert(phi->var.get());
    for (auto I : phi->opsVar) {
      assert(I.second);
      funcToSSANodesMap[curFunc].insert(I.second);
      addEdge(I.second, phi->var.get());
    }

    if (!noPred) {
      for (Value const *pred : phi->preds) {
        funcToLLVMNodesMap[curFunc].insert(pred);
        addEdge(pred, phi->var.get());
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

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitUnaryOperator(llvm::UnaryOperator &I) {
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitFreezeInst(llvm::FreezeInst &I) {
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitLoadInst(llvm::LoadInst &I) {
  // Load inst, connect MSSA mus and the pointer loaded.
  funcToLLVMNodesMap[curFunc].insert(&I);
  funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());

  auto const &MuSetForLoad = getRange(mssa->getLoadToMuMap(), &I);
  for (auto &mu : MuSetForLoad) {
    assert(mu && mu->var);
    funcToSSANodesMap[curFunc].emplace(mu->var);
    addEdge(mu->var, &I);
  }

  // Load value rank source
  for (unsigned i = 0; i < loadValueSources.size(); i++) {
    if (I.getPointerOperand()->getName().equals(loadValueSources[i])) {
      for (auto &mu : MuSetForLoad) {
        assert(mu && mu->var);
        ssaSources.emplace(mu->var);
      }

      break;
    }
  }

  if (!noPtrDep)
    addEdge(I.getPointerOperand(), &I);
}

void DepGraphDCF::visitStoreInst(llvm::StoreInst &I) {
  // Store inst
  // For each chi, connect the pointer, the value stored and the MSSA operand.
  for (auto &chi : getRange(mssa->getStoreToChiMap(), &I)) {
    assert(chi && chi->var && chi->opVar);
    funcToSSANodesMap[curFunc].emplace(chi->var.get());
    funcToSSANodesMap[curFunc].emplace(chi->opVar);
    funcToLLVMNodesMap[curFunc].emplace(I.getPointerOperand());
    funcToLLVMNodesMap[curFunc].emplace(I.getValueOperand());

    addEdge(I.getValueOperand(), chi->var.get());

    if (optWeakUpdate)
      addEdge(chi->opVar, chi->var.get());

    if (!noPtrDep)
      addEdge(I.getPointerOperand(), chi->var.get());
  }
}

void DepGraphDCF::visitGetElementPtrInst(llvm::GetElementPtrInst &I) {
  // GetElementPtr, connect operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void DepGraphDCF::visitPHINode(llvm::PHINode &I) {
  // Connect LLVM Phi, connect operands and predicates.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }

  if (!noPred) {
    for (Value const *v : getRange(mssa->getPhiToPredMap(), &I)) {
      addEdge(v, &I);
      funcToLLVMNodesMap[curFunc].insert(v);
    }
  }
}
void DepGraphDCF::visitCastInst(llvm::CastInst &I) {
  // Cast inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void DepGraphDCF::visitSelectInst(llvm::SelectInst &I) {
  // Select inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void DepGraphDCF::visitBinaryOperator(llvm::BinaryOperator &I) {
  // Binary op, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitCallInst(llvm::CallInst &I) {
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

  if (isIntrinsicDbgInst(&I))
    return;

  connectCSMus(I);
  connectCSChis(I);
  connectCSEffectiveParameters(I);
  connectCSCalledReturnValue(I);
  connectCSRetChi(I);

  // Add call node
  funcToCallNodes[curFunc].insert(&I);

  // Add pred to call edges
  std::set<Value const *> preds = computeIPDFPredicates(*PDT, I.getParent());
  for (Value const *pred : preds) {
    condToCallEdges[pred].insert(&I);
    callsiteToConds[&I].insert(pred);
  }

  // Add call to func edge
  Function const *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    callToFuncEdges[&I] = callee;
    funcToCallSites[callee].insert(&I);

    // Return value source
    for (unsigned i = 0; i < valueSourceFunctions.size(); ++i) {
      if (!callee->getName().equals(valueSourceFunctions[i].name))
        continue;

      int argNo = valueSourceFunctions[i].arg;
      if (argNo != -1)
        continue;

      valueSources.insert(&I);
    }
  }

  // indirect call
  else {
    for (Function const *mayCallee : getRange(CG.getIndirectCallMap(), &I)) {
      callToFuncEdges[&I] = mayCallee;
      funcToCallSites[mayCallee].insert(&I);

      // Return value source
      for (unsigned i = 0; i < valueSourceFunctions.size(); ++i) {
        if (!mayCallee->getName().equals(valueSourceFunctions[i].name))
          continue;

        int argNo = valueSourceFunctions[i].arg;
        if (argNo != -1)
          continue;

        valueSources.insert(&I);
      }
    }
  }

  // Sync CHI
  for (auto &chi : getRange(mssa->getCSToSynChiMap(), &I)) {
    assert(chi && chi->var && chi->opVar);
    funcToSSANodesMap[curFunc].emplace(chi->var.get());
    funcToSSANodesMap[curFunc].emplace(chi->opVar);

    addEdge(chi->opVar, chi->var.get());
    taintResetSSANodes.emplace(chi->var.get());
  }
}

void DepGraphDCF::connectCSMus(llvm::CallInst &I) {
  // Mu of the call site.
  for (auto &mu : getRange(mssa->getCSToMuMap(), &I)) {
    assert(mu && mu->var);
    funcToSSANodesMap[curFunc].emplace(mu->var);
    Function const *called = NULL;

    // External Function, we connect call mu to artifical chi of the external
    // function for each argument.
    if (MSSAExtCallMu *extCallMu = dyn_cast<MSSAExtCallMu>(mu.get())) {
      CallBase *CS(&I);

      called = extCallMu->called;
      unsigned argNo = extCallMu->argNo;

      // Case where this is a var arg parameter
      if (argNo >= called->arg_size()) {
        assert(called->isVarArg());

        auto ItMap = mssa->getExtCSToVArgEntryChi().find(called);
        auto ItEnd = mssa->getExtCSToVArgEntryChi().end();
        MSSAChi *chi{};
        if (ItMap != ItEnd) {
          chi = ItMap->second.at(CS).get();
        }
        assert(chi);
        MSSAVar *var = chi->var.get();
        assert(var);
        funcToSSANodesMap[called].emplace(var);
        addEdge(mu->var, var); // rule3
      }

      else {
        // rule3
        auto const &CSToArgEntry = mssa->getExtCSToArgEntryChi();
        assert(CSToArgEntry.lookup(called)[CS].at(argNo));
        addEdge(mu->var, CSToArgEntry.lookup(called)[CS].at(argNo)->var.get());
      }

      continue;
    }

    MSSACallMu *callMu = cast<MSSACallMu>(mu.get());
    called = callMu->called;

    auto const &FunctionToChi = mssa->getFunRegToEntryChiMap();

    auto it = FunctionToChi.find(called);
    if (it != FunctionToChi.end()) {
      MSSAChi *entryChi = it->second.at(mu->region);
      assert(entryChi && entryChi->var && "reg to entrychi not found");
      funcToSSANodesMap[called].emplace(entryChi->var.get());
      addEdge(callMu->var, entryChi->var.get()); // rule3
    }
  }
}

void DepGraphDCF::connectCSChis(llvm::CallInst &I) {
  // Chi of the callsite.
  for (auto &chi : getRange(mssa->getCSToChiMap(), &I)) {
    assert(chi && chi->var && chi->opVar);
    funcToSSANodesMap[curFunc].emplace(chi->opVar);
    funcToSSANodesMap[curFunc].emplace(chi->var.get());

    if (optWeakUpdate)
      addEdge(chi->opVar, chi->var.get()); // rule4

    Function const *called = NULL;

    // External Function, we connect call chi to artifical chi of the external
    // function for each argument.
    if (MSSAExtCallChi *extCallChi = dyn_cast<MSSAExtCallChi>(chi.get())) {
      CallBase *CS(&I);
      called = extCallChi->called;
      unsigned argNo = extCallChi->argNo;

      // Case where this is a var arg parameter.
      if (argNo >= called->arg_size()) {
        assert(called->isVarArg());

        auto ItMap = mssa->getExtCSToVArgExitChi().find(called);
        auto ItEnd = mssa->getExtCSToVArgExitChi().end();
        MSSAChi *chi{};
        if (ItMap != ItEnd) {
          chi = ItMap->second.at(CS).get();
        }
        assert(chi);
        MSSAVar *var = chi->var.get();
        assert(var);
        funcToSSANodesMap[called].emplace(var);
        addEdge(var, chi->var.get()); // rule5
      }

      else {
        // rule5
        auto const &CSToArgExit = mssa->getExtCSToArgExitChi();
        assert(CSToArgExit.lookup(called)[CS].at(argNo));
        addEdge(CSToArgExit.lookup(called)[CS].at(argNo)->var.get(),
                chi->var.get());

        // Reset functions
        for (unsigned i = 0; i < resetFunctions.size(); ++i) {
          if (!called->getName().equals(resetFunctions[i].name))
            continue;

          if ((int)argNo != resetFunctions[i].arg)
            continue;

          taintResetSSANodes.emplace(chi->var.get());
        }
      }

      continue;
    }

    MSSACallChi *callChi = cast<MSSACallChi>(chi.get());
    called = callChi->called;

    auto const &FunctionToMu = mssa->getFunRegToReturnMuMap();
    auto it = FunctionToMu.find(called);
    if (it != FunctionToMu.end()) {
      MSSAMu *returnMu = it->second.at(chi->region);
      assert(returnMu && returnMu->var && "entry not found in map");
      funcToSSANodesMap[called].emplace(returnMu->var);
      addEdge(returnMu->var, chi->var.get()); // rule5
    }
  }
}

void DepGraphDCF::connectCSEffectiveParameters(llvm::CallInst &I) {
  // Connect effective parameters to formal parameters.
  Function const *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    if (callee->isDeclaration()) {
      connectCSEffectiveParametersExt(I, callee);
      return;
    }

    unsigned argIdx = 0;
    for (Argument const &arg : callee->args()) {
      funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(argIdx));
      funcToLLVMNodesMap[callee].insert(&arg);

      addEdge(I.getArgOperand(argIdx), &arg); // rule1

      argIdx++;
    }
  }

  // indirect call
  else {
    for (Function const *mayCallee : getRange(CG.getIndirectCallMap(), &I)) {
      if (mayCallee->isDeclaration()) {
        connectCSEffectiveParametersExt(I, mayCallee);
        return;
      }

      unsigned argIdx = 0;
      for (Argument const &arg : mayCallee->args()) {
        funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(argIdx));
        funcToLLVMNodesMap[callee].insert(&arg);

        addEdge(I.getArgOperand(argIdx), &arg); // rule1

        argIdx++;
      }
    }
  }
}

void DepGraphDCF::connectCSEffectiveParametersExt(CallInst &I,
                                                  Function const *callee) {
  CallBase *CS(&I);

  if (callee->getName().find("memset") != StringRef::npos) {
    MSSAChi *argExitChi = mssa->getExtCSToArgExitChi().lookup(callee)[CS][0];
    Value const *cArg = I.getArgOperand(1);
    assert(cArg);
    funcToLLVMNodesMap[I.getParent()->getParent()].emplace(cArg);
    addEdge(cArg, argExitChi->var.get());
  }
}

void DepGraphDCF::connectCSCalledReturnValue(llvm::CallInst &I) {
  // If the function called returns a value, connect the return value to the
  // call value.

  Function const *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    if (!callee->isDeclaration() && !callee->getReturnType()->isVoidTy()) {
      funcToLLVMNodesMap[curFunc].insert(&I);
      addEdge(getReturnValue(callee), &I); // rule2
    }
  }

  // indirect call
  else {
    for (Function const *mayCallee : getRange(CG.getIndirectCallMap(), &I)) {
      if (!mayCallee->isDeclaration() &&
          !mayCallee->getReturnType()->isVoidTy()) {
        funcToLLVMNodesMap[curFunc].insert(&I);
        addEdge(getReturnValue(mayCallee), &I); // rule2
      }
    }
  }
}

void DepGraphDCF::connectCSRetChi(llvm::CallInst &I) {
  // External function, if the function called returns a pointer, connect the
  // artifical ret chi to the retcallchi
  // return chi of the caller.

  Function const *callee = I.getCalledFunction();
  CallBase *CS(&I);

  // direct call
  if (callee) {
    if (callee->isDeclaration() && callee->getReturnType()->isPointerTy()) {
      for (auto &chi : getRange(mssa->getExtCSToCallerRetChi(), &I)) {
        assert(chi && chi->var && chi->opVar);
        funcToSSANodesMap[curFunc].emplace(chi->var.get());
        funcToSSANodesMap[curFunc].emplace(chi->opVar);

        addEdge(chi->opVar, chi->var.get());
        auto ItMap = mssa->getExtCSToCalleeRetChi().find(callee);
        assert(ItMap != mssa->getExtCSToCalleeRetChi().end());
        addEdge(ItMap->second.at(CS)->var.get(), chi->var.get());
      }
    }
  }

  // indirect call
  else {
    for (Function const *mayCallee : getRange(CG.getIndirectCallMap(), &I)) {
      if (mayCallee->isDeclaration() &&
          mayCallee->getReturnType()->isPointerTy()) {
        for (auto &chi : getRange(mssa->getExtCSToCallerRetChi(), &I)) {
          assert(chi && chi->var && chi->opVar);
          funcToSSANodesMap[curFunc].emplace(chi->var.get());
          funcToSSANodesMap[curFunc].emplace(chi->opVar);

          addEdge(chi->opVar, chi->var.get());
          auto ItMap = mssa->getExtCSToCalleeRetChi().find(mayCallee);
          assert(ItMap != mssa->getExtCSToCalleeRetChi().end());
          addEdge(ItMap->second.at(CS)->var.get(), chi->var.get());
        }
      }
    }
  }
}

void DepGraphDCF::visitExtractValueInst(llvm::ExtractValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitExtractElementInst(llvm::ExtractElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitInsertElementInst(llvm::InsertElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitInsertValueInst(llvm::InsertValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitShuffleVectorInst(llvm::ShuffleVectorInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (Value const *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void DepGraphDCF::visitInstruction(llvm::Instruction &I) {
  errs() << "Error: Unhandled instruction " << I << "\n";
}

void DepGraphDCF::toDot(StringRef filename) const {
  errs() << "Writing '" << filename << "' ...\n";

  // FIXME: restore timer with llvm ones

  std::error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::OF_Text);

  stream << "digraph F {\n";
  stream << "compound=true;\n";
  stream << "rankdir=LR;\n";

  // dot global LLVM values in a separate cluster
  stream << "\tsubgraph cluster_globalsvar {\n";
  stream << "style=filled;\ncolor=lightgrey;\n";
  stream << "label=< <B>  Global Values </B> >;\n";
  stream << "node [style=filled,color=white];\n";
  for (Value const &g : M.globals()) {
    stream << "Node" << ((void *)&g) << " [label=\"" << getValueLabel(&g)
           << "\" " << getNodeStyle(&g) << "];\n";
  }
  stream << "}\n;";

  for (auto I = M.begin(), E = M.end(); I != E; ++I) {
    Function const *F = &*I;
    if (isIntrinsicDbgFunction(F))
      continue;

    if (F->isDeclaration())
      dotExtFunction(stream, F);
    else
      dotFunction(stream, F);
  }

  // Edges
  for (auto I : llvmToLLVMChildren) {
    Value const *s = I.first;
    for (Value const *d : I.second) {
      stream << "Node" << ((void *)s) << " -> "
             << "Node" << ((void *)d) << "\n";
    }
  }

  for (auto I : llvmToSSAChildren) {
    Value const *s = I.first;
    for (MSSAVar *d : I.second) {
      stream << "Node" << ((void *)s) << " -> "
             << "Node" << ((void *)d) << "\n";
    }
  }

  for (auto I : ssaToSSAChildren) {
    MSSAVar *s = I.first;
    for (MSSAVar *d : I.second) {
      stream << "Node" << ((void *)s) << " -> "
             << "Node" << ((void *)d) << "\n";
    }
  }

  for (auto I : ssaToLLVMChildren) {
    MSSAVar *s = I.first;
    for (Value const *d : I.second) {
      stream << "Node" << ((void *)s) << " -> "
             << "Node" << ((void *)d) << "\n";
    }
  }

  for (auto I : callToFuncEdges) {
    Value const *call = I.first;
    Function const *f = I.second;
    stream << "NodeCall" << ((void *)call) << " -> "
           << "Node" << ((void *)f) << " [lhead=cluster_" << ((void *)f)
           << "]\n";
  }

  for (auto I : condToCallEdges) {
    Value const *s = I.first;
    for (Value const *call : I.second) {
      stream << "Node" << ((void *)s) << " -> "
             << "NodeCall" << ((void *)call) << "\n";
    }
    /*if (taintedLLVMNodes.count(s) != 0){
            errs() << "DBG: " << s->getName() << " is a tainted condition \n";
            s->dump();
    }*/
  }

  stream << "}\n";
}

void DepGraphDCF::dotFunction(raw_fd_ostream &stream, Function const *F) const {
  stream << "\tsubgraph cluster_" << ((void *)F) << " {\n";
  stream << "style=filled;\ncolor=lightgrey;\n";
  stream << "label=< <B>" << F->getName() << "</B> >;\n";
  stream << "node [style=filled,color=white];\n";

  // Nodes with label
  for (Value const *v : getRange(funcToLLVMNodesMap, F)) {
    if (isa<GlobalValue>(v))
      continue;
    stream << "Node" << ((void *)v) << " [label=\"" << getValueLabel(v) << "\" "
           << getNodeStyle(v) << "];\n";
  }

  for (MSSAVar const *v : getRange(funcToSSANodesMap, F)) {
    stream << "Node" << ((void *)v) << " [label=\"" << v->getName()
           << "\" shape=diamond " << getNodeStyle(v) << "];\n";
  }

  for (Value const *v : getRange(funcToCallNodes, F)) {
    stream << "NodeCall" << ((void *)v) << " [label=\"" << getCallValueLabel(v)
           << "\" shape=rectangle " << getCallNodeStyle(v) << "];\n";
  }

  stream << "Node" << ((void *)F) << " [style=invisible];\n";

  stream << "}\n";
}

void DepGraphDCF::dotExtFunction(raw_fd_ostream &stream,
                                 Function const *F) const {
  stream << "\tsubgraph cluster_" << ((void *)F) << " {\n";
  stream << "style=filled;\ncolor=lightgrey;\n";
  stream << "label=< <B>" << F->getName() << "</B> >;\n";
  stream << "node [style=filled,color=white];\n";

  // Nodes with label
  for (Value const *v : getRange(funcToLLVMNodesMap, F)) {
    stream << "Node" << ((void *)v) << " [label=\"" << getValueLabel(v) << "\" "
           << getNodeStyle(v) << "];\n";
  }

  for (MSSAVar const *v : getRange(funcToSSANodesMap, F)) {
    stream << "Node" << ((void *)v) << " [label=\"" << v->getName()
           << "\" shape=diamond " << getNodeStyle(v) << "];\n";
  }

  stream << "Node" << ((void *)F) << " [style=invisible];\n";

  stream << "}\n";
}

std::string DepGraphDCF::getNodeStyle(llvm::Value const *v) const {
  if (taintedLLVMNodes.count(v) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}

std::string DepGraphDCF::getNodeStyle(MSSAVar const *v) const {
  if (taintedSSANodes.count(v) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}

std::string DepGraphDCF::getNodeStyle(Function const *f) const {
  return "style=filled, color=white";
}

std::string DepGraphDCF::getCallNodeStyle(llvm::Value const *v) const {
  return "style=filled, color=white";
}

void DepGraphDCF::computeTaintedValuesContextInsensitive() {
#ifndef NDEBUG
  unsigned funcToLLVMNodesMapSize = funcToLLVMNodesMap.size();
  unsigned funcToSSANodesMapSize = funcToSSANodesMap.size();
  unsigned varArgNodeSize = varArgNodes.size();
  unsigned llvmToLLVMChildrenSize = llvmToLLVMChildren.size();
  unsigned llvmToLLVMParentsSize = llvmToLLVMParents.size();
  unsigned llvmToSSAChildrenSize = llvmToSSAChildren.size();
  unsigned llvmToSSAParentsSize = llvmToSSAParents.size();
  unsigned ssaToLLVMChildrenSize = ssaToLLVMChildren.size();
  unsigned ssaToLLVMParentsSize = ssaToLLVMParents.size();
  unsigned ssaToSSAChildrenSize = ssaToSSAChildren.size();
  unsigned ssaToSSAParentsSize = ssaToSSAParents.size();
  unsigned funcToCallNodesSize = funcToCallNodes.size();
  unsigned callToFuncEdgesSize = callToFuncEdges.size();
  unsigned condToCallEdgesSize = condToCallEdges.size();
  unsigned funcToCallSitesSize = funcToCallSites.size();
  unsigned callsiteToCondsSize = callsiteToConds.size();
#endif

  TimeTraceScope TTS("FloodDep");

  std::queue<MSSAVar *> varToVisit;
  std::queue<Value const *> valueToVisit;

  // SSA sources
  for (MSSAVar const *src : ssaSources) {
    taintedSSANodes.insert(src);
    varToVisit.push(const_cast<MSSAVar *>(src));
  }

  // Value sources
  for (Value const *src : valueSources) {
    taintedLLVMNodes.insert(src);
    valueToVisit.push(src);
  }

  while (varToVisit.size() > 0 || valueToVisit.size() > 0) {
    if (varToVisit.size() > 0) {
      MSSAVar *s = varToVisit.front();
      varToVisit.pop();

      if (taintResetSSANodes.find(s) != taintResetSSANodes.end())
        continue;

      if (ssaToSSAChildren.find(s) != ssaToSSAChildren.end()) {
        for (MSSAVar *d : ssaToSSAChildren[s]) {
          if (taintedSSANodes.count(d) != 0)
            continue;

          taintedSSANodes.insert(d);
          varToVisit.push(d);
        }
      }

      if (ssaToLLVMChildren.find(s) != ssaToLLVMChildren.end()) {
        for (Value const *d : ssaToLLVMChildren[s]) {
          if (taintedLLVMNodes.count(d) != 0)
            continue;

          taintedLLVMNodes.insert(d);
          valueToVisit.push(d);
        }
      }
    }

    if (valueToVisit.size() > 0) {
      Value const *s = valueToVisit.front();
      valueToVisit.pop();

      if (llvmToLLVMChildren.find(s) != llvmToLLVMChildren.end()) {
        for (Value const *d : llvmToLLVMChildren[s]) {
          if (taintedLLVMNodes.count(d) != 0)
            continue;

          taintedLLVMNodes.insert(d);
          valueToVisit.push(d);
        }
      }

      if (llvmToSSAChildren.find(s) != llvmToSSAChildren.end()) {
        for (MSSAVar *d : llvmToSSAChildren[s]) {
          if (taintedSSANodes.count(d) != 0)
            continue;
          taintedSSANodes.insert(d);
          varToVisit.push(d);
        }
      }
    }
  }

  for (Value const *v : taintedLLVMNodes) {
    taintedConditions.insert(v);
  }

  assert(funcToLLVMNodesMapSize == funcToLLVMNodesMap.size());
  assert(funcToSSANodesMapSize == funcToSSANodesMap.size());
  assert(varArgNodeSize == varArgNodes.size());
  assert(llvmToLLVMChildrenSize == llvmToLLVMChildren.size());
  assert(llvmToLLVMParentsSize == llvmToLLVMParents.size());
  assert(llvmToSSAChildrenSize == llvmToSSAChildren.size());
  assert(llvmToSSAParentsSize == llvmToSSAParents.size());
  assert(ssaToLLVMChildrenSize == ssaToLLVMChildren.size());
  assert(ssaToLLVMParentsSize == ssaToLLVMParents.size());
  assert(ssaToSSAChildrenSize == ssaToSSAChildren.size());
  assert(ssaToSSAParentsSize == ssaToSSAParents.size());
  assert(funcToCallNodesSize == funcToCallNodes.size());
  assert(callToFuncEdgesSize == callToFuncEdges.size());
  assert(condToCallEdgesSize == condToCallEdges.size());
  assert(funcToCallSitesSize == funcToCallSites.size());
  assert(callsiteToCondsSize == callsiteToConds.size());
}

bool DepGraphDCF::isTaintedValue(Value const *v) const {
  return taintedConditions.find(v) != taintedConditions.end();
}

void DepGraphDCF::getCallInterIPDF(
    llvm::CallInst const *call,
    std::set<llvm::BasicBlock const *> &ipdf) const {
  std::set<llvm::CallInst const *> visitedCallSites;
  std::queue<CallInst const *> callsitesToVisit;
  callsitesToVisit.push(call);

  while (callsitesToVisit.size() > 0) {
    CallInst const *CS = callsitesToVisit.front();
    Function *F = const_cast<Function *>(CS->getParent()->getParent());
    callsitesToVisit.pop();
    visitedCallSites.insert(CS);

    BasicBlock *BB = const_cast<BasicBlock *>(CS->getParent());
    PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(*F);
    std::vector<BasicBlock *> funcIPDF =
        iterated_postdominance_frontier(PDT, BB);
    ipdf.insert(funcIPDF.begin(), funcIPDF.end());
    auto It = funcToCallSites.find(F);
    if (It != funcToCallSites.end()) {
      for (Value const *v : It->second) {
        CallInst const *CS2 = cast<CallInst>(v);
        if (visitedCallSites.count(CS2) != 0)
          continue;
        callsitesToVisit.push(CS2);
      }
    }
  }
}

bool DepGraphDCF::areSSANodesEquivalent(MSSAVar *var1, MSSAVar *var2) {
  assert(var1);
  assert(var2);

  if (var1->def->type == MSSADef::PHI || var2->def->type == MSSADef::PHI)
    return false;

  VarSet incomingSSAsVar1;
  VarSet incomingSSAsVar2;

  ValueSet incomingValuesVar1;
  ValueSet incomingValuesVar2;

  bool foundVar1 = false, foundVar2 = false;
  foundVar1 = ssaToSSAChildren.find(var1) != ssaToSSAChildren.end();
  foundVar2 = ssaToSSAChildren.find(var2) != ssaToSSAChildren.end();
  if (foundVar1 != foundVar2)
    return false;

  // Check whether number of edges are the same for both nodes.
  if (foundVar1 && foundVar2) {
    if (ssaToSSAChildren[var1].size() != ssaToSSAChildren[var2].size())
      return false;
  }

  foundVar1 = ssaToLLVMChildren.find(var1) != ssaToLLVMChildren.end();
  foundVar2 = ssaToLLVMChildren.find(var2) != ssaToLLVMChildren.end();
  if (foundVar1 != foundVar2)
    return false;
  if (foundVar1 && foundVar2) {
    if (ssaToLLVMChildren[var1].size() != ssaToLLVMChildren[var2].size())
      return false;
  }

  foundVar1 = ssaToSSAParents.find(var1) != ssaToSSAParents.end();
  foundVar2 = ssaToSSAParents.find(var2) != ssaToSSAParents.end();
  if (foundVar1 != foundVar2)
    return false;
  if (foundVar1 && foundVar2) {
    if (ssaToSSAParents[var1].size() != ssaToSSAParents[var2].size())
      return false;
  }

  foundVar1 = ssaToLLVMParents.find(var1) != ssaToLLVMParents.end();
  foundVar2 = ssaToLLVMParents.find(var2) != ssaToLLVMParents.end();
  if (foundVar1 != foundVar2)
    return false;
  if (foundVar1 && foundVar2) {
    if (ssaToLLVMParents[var1].size() != ssaToLLVMParents[var2].size())
      return false;
  }

  // Check whether outgoing edges are the same for both nodes.
  if (ssaToSSAChildren.find(var1) != ssaToSSAChildren.end()) {
    for (MSSAVar *v : ssaToSSAChildren[var1]) {
      if (ssaToSSAChildren[var2].find(v) == ssaToSSAChildren[var2].end())
        return false;
    }
  }
  if (ssaToLLVMChildren.find(var1) != ssaToLLVMChildren.end()) {
    for (Value const *v : ssaToLLVMChildren[var1]) {
      if (ssaToLLVMChildren[var2].find(v) == ssaToLLVMChildren[var2].end())
        return false;
    }
  }

  // Check whether incoming edges are the same for both nodes.
  if (ssaToSSAParents.find(var1) != ssaToSSAParents.end()) {
    for (MSSAVar *v : ssaToSSAParents[var1]) {
      if (ssaToSSAParents[var2].find(v) == ssaToSSAParents[var2].end())
        return false;
    }
  }
  if (ssaToLLVMParents.find(var1) != ssaToLLVMParents.end()) {
    for (Value const *v : ssaToLLVMParents[var1]) {
      if (ssaToLLVMParents[var2].find(v) == ssaToLLVMParents[var2].end())
        return false;
    }
  }

  return true;
}

void DepGraphDCF::eliminatePhi(MSSAPhi *phi, std::vector<MSSAVar *> ops) {
  struct ssa2SSAEdge {
    ssa2SSAEdge(MSSAVar *s, MSSAVar *d) : s(s), d(d) {}
    MSSAVar *s;
    MSSAVar *d;
  };
  struct ssa2LLVMEdge {
    ssa2LLVMEdge(MSSAVar *s, Value const *d) : s(s), d(d) {}
    MSSAVar *s;
    Value const *d;
  };
  struct llvm2SSAEdge {
    llvm2SSAEdge(Value const *s, MSSAVar *d) : s(s), d(d) {}
    Value const *s;
    MSSAVar *d;
  };
  struct llvm2LLVMEdge {
    llvm2LLVMEdge(Value const *s, Value const *d) : s(s), d(d) {}
    Value const *s;
    Value const *d;
  };

  // Singlify operands
  std::set<MSSAVar *> opsSet;
  for (MSSAVar *v : ops)
    opsSet.insert(v);
  ops.clear();
  for (MSSAVar *v : opsSet)
    ops.push_back(v);

  // Remove links from predicates to PHI
  for (Value const *v : phi->preds)
    removeEdge(v, phi->var.get());

  // Remove links from ops to PHI
  for (MSSAVar *op : ops)
    removeEdge(op, phi->var.get());

  // For each outgoing edge from PHI to a SSA node N, connect
  // op1 to N and remove the link from PHI to N.
  {
    std::vector<ssa2SSAEdge> edgesToAdd;
    std::vector<ssa2SSAEdge> edgesToRemove;
    if (ssaToSSAChildren.find(phi->var.get()) != ssaToSSAChildren.end()) {
      for (MSSAVar *v : ssaToSSAChildren[phi->var.get()]) {
        edgesToAdd.push_back(ssa2SSAEdge(ops[0], v));
        edgesToRemove.push_back(ssa2SSAEdge(phi->var.get(), v));

        // If N is a phi replace the phi operand of N with op1
        if (v->def->type == MSSADef::PHI) {
          MSSAPhi *outPHI = cast<MSSAPhi>(v->def);

          bool found = false;
          for (auto I = outPHI->opsVar.begin(), E = outPHI->opsVar.end();
               I != E; ++I) {
            if (I->second == phi->var.get()) {
              found = true;
              I->second = ops[0];
              break;
            }
          }
          if (!found)
            continue;
          assert(found);
        }
      }
    }
    for (ssa2SSAEdge e : edgesToAdd)
      addEdge(e.s, e.d);
    for (ssa2SSAEdge e : edgesToRemove)
      removeEdge(e.s, e.d);
  }

  {
    std::vector<ssa2LLVMEdge> edgesToAdd;
    std::vector<ssa2LLVMEdge> edgesToRemove;

    // For each outgoing edge from PHI to a LLVM node N, connect
    // connect op1 to N and remove the link from PHI to N.
    if (ssaToLLVMChildren.find(phi->var.get()) != ssaToLLVMChildren.end()) {
      for (Value const *v : ssaToLLVMChildren[phi->var.get()]) {
        // addEdge(ops[0], v);
        // removeEdge(phi->var, v);
        edgesToAdd.push_back(ssa2LLVMEdge(ops[0], v));
        edgesToRemove.push_back(ssa2LLVMEdge(phi->var.get(), v));
      }
    }
    for (ssa2LLVMEdge e : edgesToAdd)
      addEdge(e.s, e.d);
    for (ssa2LLVMEdge e : edgesToRemove)
      removeEdge(e.s, e.d);
  }

  // Remove PHI Node
  Function const *F = phi->var->bb->getParent();
  assert(F);
  auto it = funcToSSANodesMap[F].find(phi->var.get());
  assert(it != funcToSSANodesMap[F].end());
  funcToSSANodesMap[F].erase(it);

  // Remove edges connected to other operands than op0
  {
    std::vector<ssa2SSAEdge> toRemove1;
    std::vector<ssa2LLVMEdge> toRemove2;
    std::vector<llvm2SSAEdge> toRemove3;
    for (unsigned i = 1; i < ops.size(); ++i) {
      if (ssaToSSAParents.find(ops[i]) != ssaToSSAParents.end()) {
        for (MSSAVar *v : ssaToSSAParents[ops[i]])
          toRemove1.push_back(ssa2SSAEdge(v, ops[i]));
      }
      if (ssaToLLVMParents.find(ops[i]) != ssaToLLVMParents.end()) {
        for (Value const *v : ssaToLLVMParents[ops[i]])
          toRemove3.push_back(llvm2SSAEdge(v, ops[i]));
      }
      if (ssaToSSAChildren.find(ops[i]) != ssaToSSAChildren.end()) {
        for (MSSAVar *v : ssaToSSAChildren[ops[i]])
          toRemove1.push_back(ssa2SSAEdge(ops[i], v));
      }
      if (ssaToLLVMChildren.find(ops[i]) != ssaToLLVMChildren.end()) {
        for (Value const *v : ssaToLLVMChildren[ops[i]])
          toRemove2.push_back(ssa2LLVMEdge(ops[i], v));
      }
    }
    for (ssa2SSAEdge e : toRemove1)
      removeEdge(e.s, e.d);
    for (ssa2LLVMEdge e : toRemove2)
      removeEdge(e.s, e.d);
    for (llvm2SSAEdge e : toRemove3)
      removeEdge(e.s, e.d);
  }

  // Remove other operands than op 0 from the graph
  for (unsigned i = 1; i < ops.size(); ++i) {
    auto it2 = funcToSSANodesMap[F].find(ops[i]);
    assert(it2 != funcToSSANodesMap[F].end());
    funcToSSANodesMap[F].erase(it2);
  }
}

void DepGraphDCF::phiElimination() {

  TimeTraceScope TTS("PhiElimination");

  // For each function, iterate through its basic block and try to eliminate phi
  // function until reaching a fixed point.
  for (Function const &F : M) {
    bool changed = true;

    while (changed) {
      changed = false;

      for (BasicBlock const &BB : F) {
        for (auto &phi : getRange(mssa->getBBToPhiMap(), &BB)) {

          assert(funcToSSANodesMap.find(&F) != funcToSSANodesMap.end());

          // Has the phi node been removed already ?
          if (funcToSSANodesMap[&F].count(phi->var.get()) == 0)
            continue;

          // For each phi we test if its operands (chi) are not PHI and
          // are equivalent
          std::vector<MSSAVar *> phiOperands;
          for (auto J : phi->opsVar)
            phiOperands.push_back(J.second);

          bool canElim = true;
          for (unsigned i = 0; i < phiOperands.size() - 1; i++) {
            if (!areSSANodesEquivalent(phiOperands[i], phiOperands[i + 1])) {
              canElim = false;
              break;
            }
          }
          if (!canElim)
            continue;

          // PHI Node can be eliminated !
          changed = true;
          eliminatePhi(phi.get(), phiOperands);
        }
      }
    }
  }
}

void DepGraphDCF::addEdge(llvm::Value const *s, llvm::Value const *d) {
  llvmToLLVMChildren[s].insert(d);
  llvmToLLVMParents[d].insert(s);
}

void DepGraphDCF::addEdge(llvm::Value const *s, MSSAVar *d) {
  llvmToSSAChildren[s].insert(d);
  ssaToLLVMParents[d].insert(s);
}

void DepGraphDCF::addEdge(MSSAVar *s, llvm::Value const *d) {
  ssaToLLVMChildren[s].insert(d);
  llvmToSSAParents[d].insert(s);
}

void DepGraphDCF::addEdge(MSSAVar *s, MSSAVar *d) {
  ssaToSSAChildren[s].insert(d);
  ssaToSSAParents[d].insert(s);
}

void DepGraphDCF::removeEdge(llvm::Value const *s, llvm::Value const *d) {
  int n;
  n = llvmToLLVMChildren[s].erase(d);
  assert(n == 1);
  n = llvmToLLVMParents[d].erase(s);
  assert(n == 1);
  (void)n;
}

void DepGraphDCF::removeEdge(llvm::Value const *s, MSSAVar *d) {
  int n;
  n = llvmToSSAChildren[s].erase(d);
  assert(n == 1);
  n = ssaToLLVMParents[d].erase(s);
  assert(n == 1);
  (void)n;
}

void DepGraphDCF::removeEdge(MSSAVar *s, llvm::Value const *d) {
  int n;
  n = ssaToLLVMChildren[s].erase(d);
  assert(n == 1);
  n = llvmToSSAParents[d].erase(s);
  assert(n == 1);
  (void)n;
}

void DepGraphDCF::removeEdge(MSSAVar *s, MSSAVar *d) {
  int n;
  n = ssaToSSAChildren[s].erase(d);
  assert(n == 1);
  n = ssaToSSAParents[d].erase(s);
  assert(n == 1);
  (void)n;
}

void DepGraphDCF::dotTaintPath(Value const *v, StringRef filename,
                               Instruction const *collective) const {
  errs() << "Writing '" << filename << "' ...\n";

  // Parcours en largeur
  unsigned curDist = 0;
  unsigned curSize = 128;
  std::vector<std::set<Value const *>> visitedLLVMNodesByDist;
  std::set<Value const *> visitedLLVMNodes;
  std::vector<std::set<MSSAVar *>> visitedSSANodesByDist;
  std::set<MSSAVar *> visitedSSANodes;

  visitedSSANodesByDist.resize(curSize);
  visitedLLVMNodesByDist.resize(curSize);

  visitedLLVMNodes.insert(v);

  for (Value const *p : getRange(llvmToLLVMParents, v)) {
    if (visitedLLVMNodes.find(p) != visitedLLVMNodes.end())
      continue;

    if (taintedLLVMNodes.find(p) == taintedLLVMNodes.end())
      continue;

    visitedLLVMNodesByDist[curDist].insert(p);
  }
  for (MSSAVar *p : getRange(llvmToSSAParents, v)) {
    if (visitedSSANodes.find(p) != visitedSSANodes.end())
      continue;

    if (taintedSSANodes.find(p) == taintedSSANodes.end())
      continue;

    visitedSSANodesByDist[curDist].insert(p);
  }

  bool stop = false;
  MSSAVar *ssaRoot = NULL;
  Value const *llvmRoot = NULL;

  while (true) {
    if (curDist >= curSize) {
      curSize *= 2;
      visitedLLVMNodesByDist.resize(curSize);
      visitedSSANodesByDist.resize(curSize);
    }

    // Visit parents of llvm values
    for (Value const *v : visitedLLVMNodesByDist[curDist]) {
      if (valueSources.find(v) != valueSources.end()) {
        llvmRoot = v;
        visitedLLVMNodes.insert(v);
        errs() << "found a path of size " << curDist << "\n";
        stop = true;
        break;
      }

      visitedLLVMNodes.insert(v);

      for (Value const *p : getRange(llvmToLLVMParents, v)) {
        if (visitedLLVMNodes.find(p) != visitedLLVMNodes.end())
          continue;

        if (taintedLLVMNodes.find(p) == taintedLLVMNodes.end())
          continue;

        visitedLLVMNodesByDist[curDist + 1].insert(p);
      }
      for (MSSAVar *p : getRange(llvmToSSAParents, v)) {
        if (visitedSSANodes.find(p) != visitedSSANodes.end())
          continue;

        if (taintedSSANodes.find(p) == taintedSSANodes.end())
          continue;

        visitedSSANodesByDist[curDist + 1].insert(p);
      }
    }

    if (stop)
      break;

    // Visit parents of ssa variables
    for (MSSAVar *v : visitedSSANodesByDist[curDist]) {
      if (ssaSources.find(v) != ssaSources.end()) {
        ssaRoot = v;
        visitedSSANodes.insert(v);
        errs() << "found a path of size " << curDist << "\n";
        stop = true;
        break;
      }

      visitedSSANodes.insert(v);
      for (Value const *p : getRange(ssaToLLVMParents, v)) {
        if (visitedLLVMNodes.find(p) != visitedLLVMNodes.end())
          continue;

        if (taintedLLVMNodes.find(p) == taintedLLVMNodes.end())
          continue;

        visitedLLVMNodesByDist[curDist + 1].insert(p);
      }
      for (MSSAVar *p : getRange(ssaToSSAParents, v)) {
        if (visitedSSANodes.find(p) != visitedSSANodes.end())
          continue;

        if (taintedSSANodes.find(p) == taintedSSANodes.end())
          continue;

        visitedSSANodesByDist[curDist + 1].insert(p);
      }

      if (stop)
        break;
    }

    if (stop)
      break;

    curDist++;
  }

  assert(stop);

  std::error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::OF_Text);

  stream << "digraph F {\n";
  stream << "compound=true;\n";
  stream << "rankdir=LR;\n";

  std::vector<std::string> debugMsgs;
  std::vector<DGDebugLoc> debugLocs;

  visitedSSANodes.clear();
  visitedLLVMNodes.clear();

  assert(llvmRoot || ssaRoot);

  if (ssaRoot)
    visitedSSANodes.insert(ssaRoot);
  else
    visitedLLVMNodes.insert(llvmRoot);

  std::string tmpStr;
  raw_string_ostream strStream(tmpStr);

  MSSAVar *lastVar = ssaRoot;
  Value const *lastValue = llvmRoot;
  DGDebugLoc DL;

  if (lastVar) {
    debugMsgs.push_back(getStringMsg(lastVar));

    if (getDGDebugLoc(lastVar, DL))
      debugLocs.push_back(DL);
  } else {
    debugMsgs.push_back(getStringMsg(lastValue));
    if (getDGDebugLoc(lastValue, DL))
      debugLocs.push_back(DL);
  }

  bool lastIsVar = lastVar != NULL;

  // Compute edges of the shortest path to strStream
  for (unsigned i = curDist - 1; i > 0; i--) {
    bool found = false;
    if (lastIsVar) {
      for (MSSAVar *v : visitedSSANodesByDist[i]) {
        if (count(getRange(ssaToSSAParents, v), lastVar) == 0)
          continue;

        visitedSSANodes.insert(v);
        strStream << "Node" << ((void *)lastVar) << " -> "
                  << "Node" << ((void *)v) << "\n";
        lastVar = v;
        found = true;
        debugMsgs.push_back(getStringMsg(v));
        if (getDGDebugLoc(v, DL))
          debugLocs.push_back(DL);
        break;
      }

      if (found)
        continue;

      for (Value const *v : visitedLLVMNodesByDist[i]) {
        if (count(getRange(llvmToSSAParents, v), lastVar) == 0)
          continue;

        visitedLLVMNodes.insert(v);
        strStream << "Node" << ((void *)lastVar) << " -> "
                  << "Node" << ((void *)v) << "\n";
        lastValue = v;
        lastIsVar = false;
        found = true;
        debugMsgs.push_back(getStringMsg(v));
        if (getDGDebugLoc(v, DL))
          debugLocs.push_back(DL);
        break;
      }

      assert(found);
    }

    // Last visited is a value
    else {
      for (MSSAVar *v : visitedSSANodesByDist[i]) {
        if (count(getRange(ssaToLLVMParents, v), lastValue) == 0)
          continue;

        visitedSSANodes.insert(v);
        strStream << "Node" << ((void *)lastValue) << " -> "
                  << "Node" << ((void *)v) << "\n";
        lastVar = v;
        lastIsVar = true;
        found = true;
        debugMsgs.push_back(getStringMsg(v));
        if (getDGDebugLoc(v, DL))
          debugLocs.push_back(DL);
        break;
      }

      if (found)
        continue;

      for (Value const *v : visitedLLVMNodesByDist[i]) {
        if (count(getRange(llvmToLLVMParents, v), lastValue) == 0)
          continue;

        visitedLLVMNodes.insert(v);
        strStream << "Node" << ((void *)lastValue) << " -> "
                  << "Node" << ((void *)v) << "\n";
        lastValue = v;
        lastIsVar = false;
        found = true;
        debugMsgs.push_back(getStringMsg(v));
        if (getDGDebugLoc(v, DL))
          debugLocs.push_back(DL);
        break;
      }

      assert(found);
    }
  }

  // compute visited functions
  std::set<Function const *> visitedFunctions;
  for (auto I : funcToLLVMNodesMap) {
    for (Value const *v : I.second) {
      if (visitedLLVMNodes.find(v) != visitedLLVMNodes.end())
        visitedFunctions.insert(I.first);
    }
  }

  for (auto I : funcToSSANodesMap) {
    for (MSSAVar *v : I.second) {
      if (visitedSSANodes.find(v) != visitedSSANodes.end())
        visitedFunctions.insert(I.first);
    }
  }

  // Dot visited functions and nodes
  for (Function const *F : visitedFunctions) {
    stream << "\tsubgraph cluster_" << ((void *)F) << " {\n";
    stream << "style=filled;\ncolor=lightgrey;\n";
    stream << "label=< <B>" << F->getName() << "</B> >;\n";
    stream << "node [style=filled,color=white];\n";

    for (Value const *v : visitedLLVMNodes) {
      if (count(getRange(funcToLLVMNodesMap, F), v) == 0)
        continue;

      stream << "Node" << ((void *)v) << " [label=\"" << getValueLabel(v)
             << "\" " << getNodeStyle(v) << "];\n";
    }

    for (MSSAVar *v : visitedSSANodes) {
      if (count(getRange(funcToSSANodesMap, F), v) == 0)
        continue;

      stream << "Node" << ((void *)v) << " [label=\"" << v->getName()
             << "\" shape=diamond " << getNodeStyle(v) << "];\n";
    }

    stream << "}\n";
  }

  // Dot edges
  stream << strStream.str();

  stream << "}\n";

  for (unsigned i = 0; i < debugMsgs.size(); i++)
    stream << debugMsgs[i];

  // Write trace
  std::string trace;
  if (getDebugTrace(debugLocs, trace, collective)) {
    std::string tracefilename = (filename + ".trace").str();
    errs() << "Writing '" << tracefilename << "' ...\n";
    raw_fd_ostream tracestream(tracefilename, EC, sys::fs::OF_Text);
    tracestream << trace;
  }
}

std::string DepGraphDCF::getStringMsg(Value const *v) const {
  std::string msg;
  msg.append("# ");
  msg.append(getValueLabel(v));
  msg.append(":\n# ");

  DebugLoc loc = NULL;
  std::string funcName = "unknown";
  Instruction const *inst = dyn_cast<Instruction>(v);
  if (inst) {
    loc = inst->getDebugLoc();
    funcName = inst->getParent()->getParent()->getName().str();
  }

  msg.append("function: ");
  msg.append(funcName);
  if (loc) {
    msg.append(" file ");
    msg.append(loc->getFilename().str());
    msg.append(" line ");
    msg.append(std::to_string(loc.getLine()));
  } else {
    msg.append(" no debug loc");
  }
  msg.append("\n");

  return msg;
}

std::string DepGraphDCF::getStringMsg(MSSAVar *v) const {
  std::string msg;
  msg.append("# ");
  msg.append(v->getName());
  msg.append(":\n# ");
  std::string funcName = "unknown";
  if (v->bb)
    funcName = v->bb->getParent()->getName().str();

  MSSADef *def = v->def;
  assert(def);

  // Def can be PHI, call, store, chi, entry, extvararg, extarg, extret,
  // extcall, extretcall

  DebugLoc loc = NULL;

  if (isa<MSSACallChi>(def)) {
    MSSACallChi *callChi = cast<MSSACallChi>(def);
    funcName = callChi->inst->getParent()->getParent()->getName().str();
    loc = callChi->inst->getDebugLoc();
  } else if (isa<MSSAStoreChi>(def)) {
    MSSAStoreChi *storeChi = cast<MSSAStoreChi>(def);
    funcName = storeChi->inst->getParent()->getParent()->getName().str();
    loc = storeChi->inst->getDebugLoc();
  } else if (isa<MSSAExtCallChi>(def)) {
    MSSAExtCallChi *extCallChi = cast<MSSAExtCallChi>(def);
    funcName = extCallChi->inst->getParent()->getParent()->getName().str();
    loc = extCallChi->inst->getDebugLoc();
  } else if (isa<MSSAExtVarArgChi>(def)) {
    MSSAExtVarArgChi *varArgChi = cast<MSSAExtVarArgChi>(def);
    funcName = varArgChi->func->getName().str();
  } else if (isa<MSSAExtArgChi>(def)) {
    MSSAExtArgChi *extArgChi = cast<MSSAExtArgChi>(def);
    funcName = extArgChi->func->getName().str();
  } else if (isa<MSSAExtRetChi>(def)) {
    MSSAExtRetChi *extRetChi = cast<MSSAExtRetChi>(def);
    funcName = extRetChi->func->getName().str();
  }

  msg.append("function: ");
  msg.append(funcName);

  if (loc) {
    msg.append(" file ");
    msg.append(loc->getFilename().str());
    msg.append(" line ");
    msg.append(std::to_string(loc.getLine()));
  } else {
    msg.append(" no debug loc");
  }
  msg.append("\n");

  return msg;
}

bool DepGraphDCF::getDGDebugLoc(Value const *v, DGDebugLoc &DL) const {
  DL.F = NULL;
  DL.line = -1;
  DL.filename = "unknown";

  DebugLoc loc = NULL;

  Instruction const *inst = dyn_cast<Instruction>(v);
  if (inst) {
    loc = inst->getDebugLoc();
    DL.F = inst->getParent()->getParent();
  }

  if (loc) {
    DL.filename = loc->getFilename().str();
    DL.line = loc->getLine();
  } else {
    return false;
  }

  return DL.F != NULL;
}

bool DepGraphDCF::getDGDebugLoc(MSSAVar *v, DGDebugLoc &DL) const {
  DL.F = NULL;
  DL.line = -1;
  DL.filename = "unknown";

  if (v->bb)
    DL.F = v->bb->getParent();

  MSSADef *def = v->def;
  assert(def);

  // Def can be PHI, call, store, chi, entry, extvararg, extarg, extret,
  // extcall, extretcall

  DebugLoc loc = NULL;

  if (isa<MSSACallChi>(def)) {
    MSSACallChi *callChi = cast<MSSACallChi>(def);
    DL.F = callChi->inst->getParent()->getParent();
    loc = callChi->inst->getDebugLoc();
  } else if (isa<MSSAStoreChi>(def)) {
    MSSAStoreChi *storeChi = cast<MSSAStoreChi>(def);
    DL.F = storeChi->inst->getParent()->getParent();
    loc = storeChi->inst->getDebugLoc();
  } else if (isa<MSSAExtCallChi>(def)) {
    MSSAExtCallChi *extCallChi = cast<MSSAExtCallChi>(def);
    DL.F = extCallChi->inst->getParent()->getParent();
    loc = extCallChi->inst->getDebugLoc();
  } else if (isa<MSSAExtVarArgChi>(def)) {
    MSSAExtVarArgChi *varArgChi = cast<MSSAExtVarArgChi>(def);
    DL.F = varArgChi->func;
  } else if (isa<MSSAExtArgChi>(def)) {
    MSSAExtArgChi *extArgChi = cast<MSSAExtArgChi>(def);
    DL.F = extArgChi->func;
  } else if (isa<MSSAExtRetChi>(def)) {
    MSSAExtRetChi *extRetChi = cast<MSSAExtRetChi>(def);
    DL.F = extRetChi->func;
  }

  if (loc) {
    DL.filename = loc->getFilename().str();
    DL.line = loc->getLine();
  } else {
    return false;
  }

  return DL.F != NULL;
}

static bool getStrLine(std::ifstream &file, int line, std::string &str) {
  // go to line
  file.seekg(std::ios::beg);
  for (int i = 0; i < line - 1; ++i)
    file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  getline(file, str);

  return true;
}

void DepGraphDCF::reorderAndRemoveDup(std::vector<DGDebugLoc> &DLs) const {
  std::vector<DGDebugLoc> sameFuncDL;
  std::vector<DGDebugLoc> res;

  if (DLs.empty())
    return;

  Function const *prev = DLs[0].F;
  while (!DLs.empty()) {
    // pop front
    DGDebugLoc DL = DLs.front();
    DLs.erase(DLs.begin());

    // new function or end
    if (DL.F != prev || DLs.empty()) {
      if (!DLs.empty())
        DLs.insert(DLs.begin(), DL);
      else
        sameFuncDL.push_back(DL);

      prev = DL.F;

      // sort
      std::sort(sameFuncDL.begin(), sameFuncDL.end());

      // remove duplicates
      int line_prev = -1;
      for (unsigned i = 0; i < sameFuncDL.size(); ++i) {
        if (sameFuncDL[i].line == line_prev) {
          line_prev = sameFuncDL[i].line;
          sameFuncDL.erase(sameFuncDL.begin() + i);
          i--;
        } else {
          line_prev = sameFuncDL[i].line;
        }
      }

      // append to res
      res.insert(res.end(), sameFuncDL.begin(), sameFuncDL.end());
      sameFuncDL.clear();
    } else {
      sameFuncDL.push_back(DL);
    }
  }

  DLs.clear();
  DLs.insert(DLs.begin(), res.begin(), res.end());
}

bool DepGraphDCF::getDebugTrace(std::vector<DGDebugLoc> &DLs,
                                std::string &trace,
                                Instruction const *collective) const {
  DGDebugLoc collectiveLoc;
  if (getDGDebugLoc(collective, collectiveLoc))
    DLs.push_back(collectiveLoc);

  Function const *prevFunc = NULL;
  std::ifstream file;

  reorderAndRemoveDup(DLs);

  for (unsigned i = 0; i < DLs.size(); ++i) {
    std::string strline;
    Function const *F = DLs[i].F;
    if (!F)
      return false;

    // new function, print filename and protoype
    if (F != prevFunc) {
      file.close();
      prevFunc = F;
      DISubprogram *DI = F->getSubprogram();
      if (!DI)
        return false;

      std::string filename = DI->getFilename().str();
      std::string dir = DI->getDirectory().str();
      std::string path = dir + "/" + filename;
      int line = DI->getLine();

      file.open(path, std::ios::in);
      if (!file.good()) {
        errs() << "error opening file: " << path << "\n";
        return false;
      }

      getStrLine(file, line, strline);
      trace.append("\n" + filename + "\n");
      trace.append(strline);
      trace.append(" l." + std::to_string(line) + "\n");
    }

    getStrLine(file, DLs[i].line, strline);
    trace.append("...\n" + strline + " l." + std::to_string(DLs[i].line) +
                 "\n");
  }

  file.close();

  return true;
}

void DepGraphDCF::floodFunction(Function const *F) {
  std::queue<MSSAVar *> varToVisit;
  std::queue<Value const *> valueToVisit;

  // 1) taint LLVM and SSA sources
  for (MSSAVar const *s : ssaSources) {
    if (funcToSSANodesMap.find(F) == funcToSSANodesMap.end())
      continue;

    if (funcToSSANodesMap[F].find(const_cast<MSSAVar *>(s)) !=
        funcToSSANodesMap[F].end())
      taintedSSANodes.insert(s);
  }

  for (Value const *s : valueSources) {
    Instruction const *inst = dyn_cast<Instruction>(s);
    if (!inst || inst->getParent()->getParent() != F)
      continue;

    taintedLLVMNodes.insert(s);
  }

  // 2) Add tainted variables of the function to the queue.
  if (funcToSSANodesMap.find(F) != funcToSSANodesMap.end()) {
    for (MSSAVar *v : funcToSSANodesMap[F]) {
      if (taintedSSANodes.find(v) != taintedSSANodes.end()) {
        varToVisit.push(v);
      }
    }
  }
  if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
    for (Value const *v : funcToLLVMNodesMap[F]) {
      if (taintedLLVMNodes.find(v) != taintedLLVMNodes.end()) {
        valueToVisit.push(v);
      }
    }
  }

  // 3) flood function
  while (varToVisit.size() > 0 || valueToVisit.size() > 0) {
    if (varToVisit.size() > 0) {
      MSSAVar *s = varToVisit.front();
      varToVisit.pop();

      if (taintResetSSANodes.find(s) != taintResetSSANodes.end())
        continue;

      if (ssaToSSAChildren.find(s) != ssaToSSAChildren.end()) {
        for (MSSAVar *d : ssaToSSAChildren[s]) {
          if (funcToSSANodesMap.find(F) == funcToSSANodesMap.end())
            continue;

          if (funcToSSANodesMap[F].find(d) == funcToSSANodesMap[F].end())
            continue;
          if (taintedSSANodes.count(d) != 0)
            continue;

          taintedSSANodes.insert(d);
          varToVisit.push(d);
        }
      }

      if (ssaToLLVMChildren.find(s) != ssaToLLVMChildren.end()) {
        for (Value const *d : ssaToLLVMChildren[s]) {
          if (funcToLLVMNodesMap[F].find(d) == funcToLLVMNodesMap[F].end())
            continue;

          if (taintedLLVMNodes.count(d) != 0)
            continue;

          taintedLLVMNodes.insert(d);
          valueToVisit.push(d);
        }
      }
    }

    if (valueToVisit.size() > 0) {
      Value const *s = valueToVisit.front();
      valueToVisit.pop();

      if (llvmToLLVMChildren.find(s) != llvmToLLVMChildren.end()) {
        for (Value const *d : llvmToLLVMChildren[s]) {
          if (funcToLLVMNodesMap.find(F) == funcToLLVMNodesMap.end())
            continue;

          if (funcToLLVMNodesMap[F].find(d) == funcToLLVMNodesMap[F].end())
            continue;

          if (taintedLLVMNodes.count(d) != 0)
            continue;

          taintedLLVMNodes.insert(d);
          valueToVisit.push(d);
        }
      }

      if (llvmToSSAChildren.find(s) != llvmToSSAChildren.end()) {
        for (MSSAVar *d : llvmToSSAChildren[s]) {
          if (funcToSSANodesMap.find(F) == funcToSSANodesMap.end())
            continue;
          if (funcToSSANodesMap[F].find(d) == funcToSSANodesMap[F].end())
            continue;

          if (taintedSSANodes.count(d) != 0)
            continue;
          taintedSSANodes.insert(d);
          varToVisit.push(d);
        }
      }
    }
  }
}

void DepGraphDCF::floodFunctionFromFunction(Function const *to,
                                            Function const *from) {
  if (funcToSSANodesMap.find(from) != funcToSSANodesMap.end()) {
    for (MSSAVar *s : funcToSSANodesMap[from]) {
      if (taintedSSANodes.find(s) == taintedSSANodes.end())
        continue;
      if (taintResetSSANodes.find(s) != taintResetSSANodes.end()) {
        if (ssaToSSAChildren.find(s) != ssaToSSAChildren.end()) {
          for (MSSAVar *d : ssaToSSAChildren[s]) {
            if (funcToSSANodesMap.find(to) == funcToSSANodesMap.end())
              continue;
            if (funcToSSANodesMap[to].find(d) == funcToSSANodesMap[to].end())
              continue;
            taintedSSANodes.erase(d);
          }
        }

        if (ssaToLLVMChildren.find(s) != ssaToLLVMChildren.end()) {
          for (Value const *d : ssaToLLVMChildren[s]) {
            if (funcToLLVMNodesMap.find(to) == funcToLLVMNodesMap.end())
              continue;

            if (funcToLLVMNodesMap[to].find(d) == funcToLLVMNodesMap[to].end())
              continue;
            taintedLLVMNodes.erase(d);
          }
        }

        continue;
      }

      if (ssaToSSAChildren.find(s) != ssaToSSAChildren.end()) {
        for (MSSAVar *d : ssaToSSAChildren[s]) {
          if (funcToSSANodesMap.find(to) == funcToSSANodesMap.end())
            continue;
          if (funcToSSANodesMap[to].find(d) == funcToSSANodesMap[to].end())
            continue;
          taintedSSANodes.insert(d);
        }
      }

      if (ssaToLLVMChildren.find(s) != ssaToLLVMChildren.end()) {
        for (Value const *d : ssaToLLVMChildren[s]) {
          if (funcToLLVMNodesMap.find(to) == funcToLLVMNodesMap.end())
            continue;

          if (funcToLLVMNodesMap[to].find(d) == funcToLLVMNodesMap[to].end())
            continue;
          taintedLLVMNodes.insert(d);
        }
      }
    }
  }

  if (funcToLLVMNodesMap.find(from) != funcToLLVMNodesMap.end()) {
    for (Value const *s : funcToLLVMNodesMap[from]) {
      if (taintedLLVMNodes.find(s) == taintedLLVMNodes.end())
        continue;

      if (llvmToSSAChildren.find(s) != llvmToSSAChildren.end()) {
        for (MSSAVar *d : llvmToSSAChildren[s]) {
          if (funcToSSANodesMap.find(to) == funcToSSANodesMap.end())
            continue;
          if (funcToSSANodesMap[to].find(d) == funcToSSANodesMap[to].end())
            continue;
          taintedSSANodes.insert(d);
        }
      }

      if (llvmToLLVMChildren.find(s) != llvmToLLVMChildren.end()) {
        for (Value const *d : llvmToLLVMChildren[s]) {
          if (funcToLLVMNodesMap.find(to) == funcToLLVMNodesMap.end())
            continue;
          if (funcToLLVMNodesMap[to].find(d) == funcToLLVMNodesMap[to].end())
            continue;
          taintedLLVMNodes.insert(d);
        }
      }
    }
  }
}

void DepGraphDCF::resetFunctionTaint(Function const *F) {
  assert(F && CG.isReachableFromEntry(*F));
  if (funcToSSANodesMap.find(F) != funcToSSANodesMap.end()) {
    for (MSSAVar *v : funcToSSANodesMap[F]) {
      if (taintedSSANodes.find(v) != taintedSSANodes.end()) {
        taintedSSANodes.erase(v);
      }
    }
  }

  if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
    for (Value const *v : funcToLLVMNodesMap[F]) {
      if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
        taintedLLVMNodes.erase(v);
      }
    }
  }
}

void DepGraphDCF::computeFunctionCSTaintedConds(llvm::Function const *F) {
  for (BasicBlock const &BB : *F) {
    for (Instruction const &I : BB) {
      if (!isa<CallInst>(I))
        continue;

      if (callsiteToConds.find(cast<Value>(&I)) != callsiteToConds.end()) {
        for (Value const *v : callsiteToConds[cast<Value>(&I)]) {
          if (taintedLLVMNodes.find(v) != taintedLLVMNodes.end()) {
            // EMMA : if(v->getName() != "cmp1" && v->getName() != "cmp302"){
            taintedConditions.insert(v);
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
  unsigned funcToLLVMNodesMapSize = funcToLLVMNodesMap.size();
  unsigned funcToSSANodesMapSize = funcToSSANodesMap.size();
  unsigned varArgNodeSize = varArgNodes.size();
  unsigned llvmToLLVMChildrenSize = llvmToLLVMChildren.size();
  unsigned llvmToLLVMParentsSize = llvmToLLVMParents.size();
  unsigned llvmToSSAChildrenSize = llvmToSSAChildren.size();
  unsigned llvmToSSAParentsSize = llvmToSSAParents.size();
  unsigned ssaToLLVMChildrenSize = ssaToLLVMChildren.size();
  unsigned ssaToLLVMParentsSize = ssaToLLVMParents.size();
  unsigned ssaToSSAChildrenSize = ssaToSSAChildren.size();
  unsigned ssaToSSAParentsSize = ssaToSSAParents.size();
  unsigned funcToCallNodesSize = funcToCallNodes.size();
  unsigned callToFuncEdgesSize = callToFuncEdges.size();
  unsigned condToCallEdgesSize = condToCallEdges.size();
  unsigned funcToCallSitesSize = funcToCallSites.size();
  unsigned callsiteToCondsSize = callsiteToConds.size();
#endif

  PTACallGraphNode const *entry = CG.getEntry();
  if (entry->getFunction()) {
    computeTaintedValuesCSForEntry(entry);
  } else {
    for (auto I = entry->begin(), E = entry->end(); I != E; ++I) {
      PTACallGraphNode *calleeNode = I->second;
      computeTaintedValuesCSForEntry(calleeNode);
    }
  }

  assert(funcToLLVMNodesMapSize == funcToLLVMNodesMap.size());
  assert(funcToSSANodesMapSize == funcToSSANodesMap.size());
  assert(varArgNodeSize == varArgNodes.size());
  assert(llvmToLLVMChildrenSize == llvmToLLVMChildren.size());
  assert(llvmToLLVMParentsSize == llvmToLLVMParents.size());
  assert(llvmToSSAChildrenSize == llvmToSSAChildren.size());
  assert(llvmToSSAParentsSize == llvmToSSAParents.size());
  assert(ssaToLLVMChildrenSize == ssaToLLVMChildren.size());
  assert(ssaToLLVMParentsSize == ssaToLLVMParents.size());
  assert(ssaToSSAChildrenSize == ssaToSSAChildren.size());
  assert(ssaToSSAParentsSize == ssaToSSAParents.size());
  assert(funcToCallNodesSize == funcToCallNodes.size());
  assert(callToFuncEdgesSize == callToFuncEdges.size());
  assert(condToCallEdgesSize == condToCallEdges.size());
  assert(funcToCallSitesSize == funcToCallSites.size());
  assert(callsiteToCondsSize == callsiteToConds.size());
}

void DepGraphDCF::computeTaintedValuesCSForEntry(
    PTACallGraphNode const *entry) {
  std::vector<PTACallGraphNode const *> S;

  std::map<PTACallGraphNode const *, std::set<PTACallGraphNode *>>
      node2VisitedChildrenMap;
  S.push_back(entry);

  bool goingDown = true;
  Function const *prev = NULL;

  while (!S.empty()) {
    PTACallGraphNode const *N = S.back();
    bool foundChildren = false;

    //    if (N->getFunction())
    //      errs() << "current =" << N->getFunction()->getName() << "\n";

    /*    if (goingDown)
          errs() << "down\n";
        else
          errs() << "up\n";
    */
    if (prev) {
      if (goingDown) {
        // errs() << "tainting " << N->getFunction()->getName() << " from "
        // << prev->getName() << "\n";
        floodFunctionFromFunction(N->getFunction(), prev);

        // errs() << "tainting " << N->getFunction()->getName() << "\n";
        floodFunction(N->getFunction());

        // errs() << "for each call site get PDF+ and save tainted
        // conditions\n";
        computeFunctionCSTaintedConds(N->getFunction());
      } else {
        // errs() << "tainting " << N->getFunction()->getName() << " from "
        //   << prev->getName() << "\n";
        floodFunctionFromFunction(N->getFunction(), prev);

        // errs() << "tainting " << N->getFunction()->getName() << "\n";
        floodFunction(N->getFunction());

        // errs() << "for each call site get PDF+ and save tainted
        // conditions\n";
        computeFunctionCSTaintedConds(N->getFunction());

        // errs() << "untainting " << prev->getName() << "\n";
        resetFunctionTaint(prev);
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
      PTACallGraphNode *calleeNode = I->second;
      if (node2VisitedChildrenMap[N].find(calleeNode) ==
          node2VisitedChildrenMap[N].end()) {
        foundChildren = true;
        node2VisitedChildrenMap[N].insert(calleeNode);
        if (calleeNode->getFunction()) {
          S.push_back(calleeNode);
          break;
        }
      }
    }

    if (!foundChildren) {
      S.pop_back();
      goingDown = false;
    } else {
      goingDown = true;
    }

    prev = N->getFunction();
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
