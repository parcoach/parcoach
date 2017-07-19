#include "DepGraphDCF.h"
#include "Options.h"
#include "Utils.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <queue>
#include <algorithm>

using namespace llvm;
using namespace std;


struct functionArg {
  functionArg(string name, int arg) : name(name), arg(arg) {}
  string name;
  int arg;
};

static vector<functionArg> ssaSourceFunctions;
static vector<functionArg> valueSourceFunctions;
static vector<const char *> loadValueSources;
static vector<functionArg> resetFunctions;


DepGraphDCF::DepGraphDCF(MemorySSA *mssa, PTACallGraph *CG, Pass *pass,
			 bool noPtrDep, bool noPred, bool disablePhiElim)
  : DepGraph(CG),
    mssa(mssa), CG(CG), pass(pass),
    buildGraphTime(0), phiElimTime(0),
    floodDepTime(0), floodCallTime(0),
    dotTime(0), noPtrDep(noPtrDep), noPred(noPred),
    disablePhiElim(disablePhiElim) {

  if (optMpiTaint)
    enableMPI();
  if (optOmpTaint)
    enableOMP();
  if (optUpcTaint)
    enableUPC();
  if (optCudaTaint)
    enableCUDA();
}

DepGraphDCF::~DepGraphDCF() {}

void
DepGraphDCF::build() {
  unsigned counter = 0;
  unsigned nbFunctions = PTACG->getModule().getFunctionList().size();

  for (Function &F : PTACG->getModule()) {
    if (!PTACG->isReachableFromEntry(&F))
      continue;

    if (counter % 100 == 0)
      errs() << "DepGraph: visited " << counter << " functions over " << nbFunctions
	     << " (" << (((float) counter)/nbFunctions*100) << "%)\n";

    counter++;

    if (isIntrinsicDbgFunction(&F))
      continue;

    buildFunction(&F);
  }

  if (!disablePhiElim)
    phiElimination();
}


void
DepGraphDCF::enableMPI() {
  resetFunctions.push_back(functionArg("MPI_Bcast", 0));
  resetFunctions.push_back(functionArg("MPI_Allgather", 3));
  resetFunctions.push_back(functionArg("MPI_Allgatherv", 3));
  resetFunctions.push_back(functionArg("MPI_Alltoall", 3));
  resetFunctions.push_back(functionArg("MPI_Alltoallv", 4));
  resetFunctions.push_back(functionArg("MPI_Alltoallw", 4));
  resetFunctions.push_back(functionArg("MPI_Allreduce", 1));
  ssaSourceFunctions.push_back(functionArg("MPI_Comm_rank", 1));
  ssaSourceFunctions.push_back(functionArg("MPI_Group_rank", 1));
}

void
DepGraphDCF::enableOMP() {
  valueSourceFunctions.push_back(functionArg("__kmpc_global_thread_num", -1));
  valueSourceFunctions.push_back(functionArg("_omp_get_thread_num", -1));
  valueSourceFunctions.push_back(functionArg("omp_get_thread_num", -1));
}

void
DepGraphDCF::enableUPC() {
  loadValueSources.push_back("gasneti_mynode");
}

void
DepGraphDCF::enableCUDA() {
  valueSourceFunctions.
    push_back(functionArg("llvm.nvvm.read.ptx.sreg.tid.x", -1)); // threadIdx.x
  valueSourceFunctions.
    push_back(functionArg("llvm.nvvm.read.ptx.sreg.tid.y", -1)); // threadIdx.y
  valueSourceFunctions.
    push_back(functionArg("llvm.nvvm.read.ptx.sreg.tid.z", -1)); // threadIdx.z
}

void
DepGraphDCF::buildFunction(const llvm::Function *F) {
  double t1 = gettime();

  curFunc = F;

  if (F->isDeclaration())
    curPDT = NULL;
  else
    curPDT =
      &pass->getAnalysis<PostDominatorTreeWrapperPass>
      (*const_cast<Function *>(F)).getPostDomTree();

  visit(*const_cast<Function *>(F));

  // Add entry chi nodes to the graph.
  for (MSSAChi *chi : mssa->funToEntryChiMap[F]) {
    assert(chi && chi->var);
    funcToSSANodesMap[F].insert(chi->var);
    if (chi->opVar) {
      funcToSSANodesMap[F].insert(chi->opVar);
      addEdge(chi->opVar, chi->var);
    }
  }

  // External functions
  if (F->isDeclaration()) {

    // Add var arg entry and exit chi nodes.
    if (F->isVarArg()) {
      for (auto I : mssa->extCallSiteToVarArgEntryChi[F]) {
	MSSAChi *entryChi = I.second;
	assert(entryChi && entryChi->var);
	funcToSSANodesMap[F].insert(entryChi->var);
      }
      for (auto I : mssa->extCallSiteToVarArgExitChi[F]) {
	MSSAChi *exitChi = I.second;
	assert(exitChi && exitChi->var);
	funcToSSANodesMap[F].insert(exitChi->var);
	addEdge(exitChi->opVar, exitChi->var);
      }
    }

    // Add args entry and exit chi nodes for external functions.
    unsigned argNo = 0;
    for (const Argument &arg : F->getArgumentList()) {
      if (!arg.getType()->isPointerTy()) {
	argNo++;
	continue;
      }

      for (auto I : mssa->extCallSiteToArgEntryChi[F]) {
	MSSAChi *entryChi = I.second[argNo];
	assert(entryChi && entryChi->var);
	funcToSSANodesMap[F].insert(entryChi->var);
      }
      for (auto I : mssa->extCallSiteToArgExitChi[F]) {
	MSSAChi *exitChi =  I.second[argNo];
	assert(exitChi && exitChi->var);
	funcToSSANodesMap[F].insert(exitChi->var);
	addEdge(exitChi->opVar, exitChi->var);
      }

      argNo++;
    }

    // Add retval chi node for external functions
    if (F->getReturnType()->isPointerTy()) {
      for (auto I : mssa->extCallSiteToCalleeRetChi[F]) {
	MSSAChi *retChi = I.second;
	assert(retChi && retChi->var);
	funcToSSANodesMap[F].insert(retChi->var);
      }
    }

    // memcpy
    if (F->getName().find("memcpy") != StringRef::npos) {
      for (auto I : mssa->extCallSiteToArgEntryChi[F]) {
	CallSite CS = I.first;
	MSSAChi *srcEntryChi = mssa->extCallSiteToArgEntryChi[F][CS][1];
	MSSAChi *dstExitChi = mssa->extCallSiteToArgExitChi[F][CS][0];

	addEdge(srcEntryChi->var, dstExitChi->var);

	// llvm.mempcy instrinsic returns void whereas memcpy returns dst
	if (F->getReturnType()->isPointerTy()) {
	  MSSAChi *retChi = mssa->extCallSiteToCalleeRetChi[F][CS];
	  addEdge(dstExitChi->var, retChi->var);
	}
      }
    }

    // memmove
    else if (F->getName().find("memmove") != StringRef::npos) {
      for (auto I : mssa->extCallSiteToArgEntryChi[F]) {
	CallSite CS = I.first;

	MSSAChi *srcEntryChi = mssa->extCallSiteToArgEntryChi[F][CS][1];
	MSSAChi *dstExitChi = mssa->extCallSiteToArgExitChi[F][CS][0];

	addEdge(srcEntryChi->var, dstExitChi->var);

	// llvm.memmove instrinsic returns void whereas memmove returns dst
	if (F->getReturnType()->isPointerTy()) {
	  MSSAChi *retChi = mssa->extCallSiteToCalleeRetChi[F][CS];
	  addEdge(dstExitChi->var, retChi->var);
	}
      }
    }

    // memset
    else if (F->getName().find("memset") != StringRef::npos) {
      for (auto I : mssa->extCallSiteToArgExitChi[F]) {
	CallSite CS = I.first;

	MSSAChi *argExitChi = mssa->extCallSiteToArgExitChi[F][CS][0];
	addEdge(getFunctionArgument(F, 1), argExitChi->var);

	// llvm.memset instrinsic returns void whereas memset returns dst
	if (F->getReturnType()->isPointerTy()) {
	  MSSAChi *retChi = mssa->extCallSiteToCalleeRetChi[F][CS];
	  addEdge(argExitChi->var, retChi->var);
	}
      }
    }

    // Unknown external function, we have to connect every input to every
    // output.
    else {
      for (CallSite cs : mssa->extFuncToCSMap[F]) {
	std::set<MSSAVar *> ssaOutputs;
	std::set<MSSAVar *> ssaInputs;

	// Compute SSA outputs
	for (auto I : mssa->extCallSiteToArgExitChi[F][cs]) {
	  MSSAChi *argExitChi = I.second;
	  ssaOutputs.insert(argExitChi->var);
	}
	if (F->isVarArg()) {
	  MSSAChi *varArgExitChi = mssa->extCallSiteToVarArgExitChi[F][cs];
	  ssaOutputs.insert(varArgExitChi->var);
	}
	if (F->getReturnType()->isPointerTy()) {
	  MSSAChi *retChi = mssa->extCallSiteToCalleeRetChi[F][cs];
	  ssaOutputs.insert(retChi->var);
	}

	// Compute SSA inputs
	for (auto I : mssa->extCallSiteToArgEntryChi[F][cs]) {
	  MSSAChi *argEntryChi = I.second;
	  ssaInputs.insert(argEntryChi->var);
	}
	if (F->isVarArg()) {
	  MSSAChi *varArgEntryChi = mssa->extCallSiteToVarArgEntryChi[F][cs];
	  ssaInputs.insert(varArgEntryChi->var);
	}

	// Connect SSA inputs to SSA outputs
	for (MSSAVar *in : ssaInputs) {
	  for (MSSAVar *out : ssaOutputs) {
	    addEdge(in, out);
	  }
	}

	// Connect LLVM arguments to SSA outputs
	for (const Argument &arg : F->getArgumentList()) {
	  for (MSSAVar *out : ssaOutputs) {
	    addEdge(&arg, out);
	  }
	}
      }
    }

    // SSA Source functions
    for (unsigned i=0; i<ssaSourceFunctions.size(); ++i) {
      if (!F->getName().equals(ssaSourceFunctions[i].name))
	continue;
      unsigned argNo = ssaSourceFunctions[i].arg;
      for (auto I : mssa->extCallSiteToArgExitChi[F]) {
	assert(I.second[argNo]);
	ssaSources.insert(I.second[argNo]->var);
      }
    }
  }

  double t2 = gettime();

  buildGraphTime += t2 - t1;
}

void
DepGraphDCF::visitBasicBlock(llvm::BasicBlock &BB) {
  // Add MSSA Phi nodes and edges to the graph.
  for (MSSAPhi *phi : mssa->bbToPhiMap[&BB]) {
    assert(phi && phi->var);
    funcToSSANodesMap[curFunc].insert(phi->var);
    for (auto I : phi->opsVar) {
      assert(I.second);
      funcToSSANodesMap[curFunc].insert(I.second);
      addEdge(I.second, phi->var);
    }

    if (!noPred) {
      for (const Value *pred : phi->preds) {
	funcToLLVMNodesMap[curFunc].insert(pred);
	addEdge(pred, phi->var);
      }
    }
  }
}

void
DepGraphDCF::visitAllocaInst(llvm::AllocaInst &I) {
  // Do nothing
}

void
DepGraphDCF::visitTerminatorInst(llvm::TerminatorInst &I) {
  // Do nothing
}

void
DepGraphDCF::visitCmpInst(llvm::CmpInst &I) {
  // Cmp instruction is a value, connect the result to its operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraphDCF::visitLoadInst(llvm::LoadInst &I) {
  // Load inst, connect MSSA mus and the pointer loaded.
  funcToLLVMNodesMap[curFunc].insert(&I);
  funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());

  for (MSSAMu *mu : mssa->loadToMuMap[&I]) {
    assert(mu && mu->var);
    funcToSSANodesMap[curFunc].insert(mu->var);
    addEdge(mu->var, &I);
  }

  // Load value rank source
  for (unsigned i=0; i<loadValueSources.size(); i++) {
    if (I.getPointerOperand()->getName().equals(loadValueSources[i])) {
      for (MSSAMu *mu : mssa->loadToMuMap[&I]) {
	assert(mu && mu->var);
	ssaSources.insert(mu->var);
      }

      break;
    }
  }

  if (!noPtrDep)
    addEdge(I.getPointerOperand(), &I);
}

void
DepGraphDCF::visitStoreInst(llvm::StoreInst &I) {
  // Store inst
  // For each chi, connect the pointer, the value stored and the MSSA operand.
  for (MSSAChi *chi : mssa->storeToChiMap[&I]) {
    assert(chi && chi->var && chi->opVar);
    funcToSSANodesMap[curFunc].insert(chi->var);
    funcToSSANodesMap[curFunc].insert(chi->opVar);
    funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());
    funcToLLVMNodesMap[curFunc].insert(I.getValueOperand());


    addEdge(I.getValueOperand(), chi->var);

    if (optWeakUpdate)
      addEdge(chi->opVar, chi->var);

    if (!noPtrDep)
      addEdge(I.getPointerOperand(), chi->var);
  }
}

void
DepGraphDCF::visitGetElementPtrInst(llvm::GetElementPtrInst &I) {
  // GetElementPtr, connect operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraphDCF::visitPHINode(llvm::PHINode &I) {
  // Connect LLVM Phi, connect operands and predicates.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }

  if (!noPred) {
    for (const Value *v : mssa->llvmPhiToPredMap[&I]) {
      addEdge(v, &I);
      funcToLLVMNodesMap[curFunc].insert(v);
    }
  }
}
void
DepGraphDCF::visitCastInst(llvm::CastInst &I) {
  // Cast inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraphDCF::visitSelectInst(llvm::SelectInst &I) {
  // Select inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraphDCF::visitBinaryOperator(llvm::BinaryOperator &I) {
  // Binary op, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraphDCF::visitCallInst(llvm::CallInst &I) {
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
  set<const Value *> preds = computeIPDFPredicates(*curPDT, I.getParent());
  for (const Value *pred : preds) {
    condToCallEdges[pred].insert(&I);
    callsiteToConds[&I].insert(pred);
  }

  // Add call to func edge
  const Function *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    callToFuncEdges[&I] = callee;
    funcToCallSites[callee].insert(&I);

    // Return value source
    for (unsigned i=0; i<valueSourceFunctions.size(); ++i) {
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
    for (const Function *mayCallee : CG->indirectCallMap[&I]) {
      callToFuncEdges[&I] = mayCallee;
      funcToCallSites[mayCallee].insert(&I);

      // Return value source
      for (unsigned i=0; i<valueSourceFunctions.size(); ++i) {
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
  for (MSSAChi *chi : mssa->callSiteToSyncChiMap[CallSite(&I)]) {
    assert(chi && chi->var && chi->opVar);
    funcToSSANodesMap[curFunc].insert(chi->var);
    funcToSSANodesMap[curFunc].insert(chi->opVar);

    addEdge(chi->opVar, chi->var);
    taintResetSSANodes.insert(chi->var);
  }
}

void
DepGraphDCF::connectCSMus(llvm::CallInst &I) {
  // Mu of the call site.
  for (MSSAMu *mu : mssa->callSiteToMuMap[CallSite(&I)]) {
    assert(mu && mu->var);
    funcToSSANodesMap[curFunc].insert(mu->var);
    const Function *called = NULL;

    // External Function, we connect call mu to artifical chi of the external
    // function for each argument.
    if (isa<MSSAExtCallMu>(mu)) {
      CallSite CS(&I);

      MSSAExtCallMu *extCallMu = cast<MSSAExtCallMu>(mu);
      called = extCallMu->called;
      unsigned argNo = extCallMu->argNo;

      // Case where this is a var arg parameter
      if (argNo >= called->arg_size()) {
	assert(called->isVarArg());

	assert(mssa->extCallSiteToVarArgEntryChi[called][CS]);
	MSSAVar *var = mssa->extCallSiteToVarArgEntryChi[called][CS]->var;
	assert(var);
	funcToSSANodesMap[called].insert(var);
	addEdge(mu->var, var); // rule3
      }

      else {
	// rule3
	assert(mssa->extCallSiteToArgEntryChi[called][CS][argNo]);
	addEdge(mu->var,
		mssa->extCallSiteToArgEntryChi[called][CS][argNo]->var);
      }

      continue;
    }

    MSSACallMu *callMu = cast<MSSACallMu>(mu);
    called = callMu->called;

    auto it = mssa->funRegToEntryChiMap.find(called);
    if (it != mssa->funRegToEntryChiMap.end()) {
      MSSAChi *entryChi = it->second[mu->region];
      assert(entryChi && entryChi->var);
      funcToSSANodesMap[called].insert(entryChi->var);
      addEdge(callMu->var, entryChi->var); // rule3
    }
  }
}

void
DepGraphDCF::connectCSChis(llvm::CallInst &I) {
  // Chi of the callsite.
  for (MSSAChi *chi : mssa->callSiteToChiMap[CallSite(&I)]) {
    assert(chi && chi->var && chi->opVar);
    funcToSSANodesMap[curFunc].insert(chi->opVar);
    funcToSSANodesMap[curFunc].insert(chi->var);

    if (optWeakUpdate)
      addEdge(chi->opVar, chi->var); // rule4

    const Function *called = NULL;

    // External Function, we connect call chi to artifical chi of the external
    // function for each argument.
    if (isa<MSSAExtCallChi>(chi)) {
      CallSite CS(&I);
      MSSAExtCallChi *extCallChi = cast<MSSAExtCallChi>(chi);
      called = extCallChi->called;
      unsigned argNo = extCallChi->argNo;

      // Case where this is a var arg parameter.
      if (argNo >= called->arg_size()) {
	assert(called->isVarArg());

	assert(mssa->extCallSiteToVarArgExitChi[called][CS]);
	MSSAVar *var = mssa->extCallSiteToVarArgExitChi[called][CS]->var;
	assert(var);
	funcToSSANodesMap[called].insert(var);
	addEdge(var, chi->var); // rule5
      }

      else {
	// rule5
	assert(mssa->extCallSiteToArgExitChi[called][CS][argNo]);
	addEdge(mssa->extCallSiteToArgExitChi[called][CS][argNo]->var, chi->var);

	// Reset functions
	for (unsigned i=0; i<resetFunctions.size(); ++i) {
	  if (!called->getName().equals(resetFunctions[i].name))
	    continue;

	  if ((int) argNo != resetFunctions[i].arg)
	    continue;

	  taintResetSSANodes.insert(chi->var);
	}
      }

      continue;
    }

    MSSACallChi *callChi = cast<MSSACallChi>(chi);
    called = callChi->called;

    auto it = mssa->funRegToReturnMuMap.find(called);
    if (it != mssa->funRegToReturnMuMap.end()) {
      MSSAMu *returnMu = it->second[chi->region];
      assert(returnMu && returnMu->var);
      funcToSSANodesMap[called].insert(returnMu->var);
      addEdge(returnMu->var, chi->var); // rule5
    }
  }
}

void
DepGraphDCF::connectCSEffectiveParameters(llvm::CallInst &I) {
  // Connect effective parameters to formal parameters.
  const Function *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    if (callee->isDeclaration()) {
      connectCSEffectiveParametersExt(I, callee);
      return;
    }

    unsigned argIdx = 0;
    for (const Argument &arg : callee->getArgumentList()) {
      funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(argIdx));
      funcToLLVMNodesMap[callee].insert(&arg);

      addEdge(I.getArgOperand(argIdx), &arg); // rule1

      argIdx++;
    }
  }

  // indirect call
  else {
    for (const Function *mayCallee : CG->indirectCallMap[&I]) {
      if (mayCallee->isDeclaration()) {
	connectCSEffectiveParametersExt(I, mayCallee);
	return;
      }

      unsigned argIdx = 0;
      for (const Argument &arg : mayCallee->getArgumentList()) {
	funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(argIdx));
	funcToLLVMNodesMap[callee].insert(&arg);

	addEdge(I.getArgOperand(argIdx), &arg); // rule1

	argIdx++;
      }
    }
  }
}

void
DepGraphDCF::connectCSEffectiveParametersExt(CallInst &I, const Function *callee) {
  CallSite CS(&I);

  if (callee->getName().find("memset") != StringRef::npos) {
    MSSAChi *argExitChi = mssa->extCallSiteToArgExitChi[callee][CS][0];
    const Value *cArg = I.getArgOperand(1);
    assert(cArg);
    funcToLLVMNodesMap[I.getParent()->getParent()].insert(cArg);
    addEdge(cArg, argExitChi->var);

  }
}

void
DepGraphDCF::connectCSCalledReturnValue(llvm::CallInst &I) {
  // If the function called returns a value, connect the return value to the
  // call value.

  const Function *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    if (!callee->isDeclaration() && !callee->getReturnType()->isVoidTy()) {
      funcToLLVMNodesMap[curFunc].insert(&I);
      addEdge(getReturnValue(callee), &I); // rule2
    }
  }

  // indirect call
  else {
    for (const Function *mayCallee : CG->indirectCallMap[&I]) {
      if (!mayCallee->isDeclaration() &&
	        !mayCallee->getReturnType()->isVoidTy()) {
				funcToLLVMNodesMap[curFunc].insert(&I);
				addEdge(getReturnValue(mayCallee), &I); // rule2
      }
    }
  }
}

void
DepGraphDCF::connectCSRetChi(llvm::CallInst &I) {
  // External function, if the function called returns a pointer, connect the
  // artifical ret chi to the retcallchi
  // return chi of the caller.

  const Function *callee = I.getCalledFunction();
  CallSite CS(&I);

  // direct call
  if (callee) {
    if (callee->isDeclaration() && callee->getReturnType()->isPointerTy()) {
      for (MSSAChi *chi : mssa->extCallSiteToCallerRetChi[CallSite(&I)]) {
	assert(chi && chi->var && chi->opVar);
	funcToSSANodesMap[curFunc].insert(chi->var);
	funcToSSANodesMap[curFunc].insert(chi->opVar);

	addEdge(chi->opVar, chi->var);
	addEdge(mssa->extCallSiteToCalleeRetChi[callee][CS]->var, chi->var);
      }
    }
  }

  // indirect call
  else {
    for (const Function *mayCallee : CG->indirectCallMap[&I]) {
      if (mayCallee->isDeclaration() &&
	  mayCallee->getReturnType()->isPointerTy()) {
	for (MSSAChi *chi : mssa->extCallSiteToCallerRetChi[CallSite(&I)]) {
	  assert(chi && chi->var && chi->opVar);
	  funcToSSANodesMap[curFunc].insert(chi->var);
	  funcToSSANodesMap[curFunc].insert(chi->opVar);

	  addEdge(chi->opVar, chi->var);
	  addEdge(mssa->extCallSiteToCalleeRetChi[mayCallee][CS]->var,
		  chi->var);
	}
      }
    }
  }
}

void
DepGraphDCF::visitExtractValueInst(llvm::ExtractValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraphDCF::visitExtractElementInst(llvm::ExtractElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraphDCF::visitInsertElementInst(llvm::InsertElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraphDCF::visitInsertValueInst(llvm::InsertValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraphDCF::visitShuffleVectorInst(llvm::ShuffleVectorInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraphDCF::visitInstruction(llvm::Instruction &I) {
  errs() << "Error: Unhandled instruction " << I << "\n";
}

void
DepGraphDCF::toDot(string filename) {
  errs() << "Writing '" << filename << "' ...\n";

  double t1 = gettime();

  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph F {\n";
  stream << "compound=true;\n";
  stream << "rankdir=LR;\n";


  // dot global LLVM values in a separate cluster
  stream << "\tsubgraph cluster_globalsvar {\n";
  stream << "style=filled;\ncolor=lightgrey;\n";
  stream << "label=< <B>  Global Values </B> >;\n";
  stream << "node [style=filled,color=white];\n";
  for (const Value &g : mssa->m->globals()) {
    stream << "Node" << ((void *) &g) << " [label=\""
	   << getValueLabel(&g) << "\" "
	   << getNodeStyle(&g) << "];\n";
  }
  stream << "}\n;";

  for (auto I = mssa->m->begin(), E = mssa->m->end(); I != E; ++I) {
    const Function *F = &*I;
    if (isIntrinsicDbgFunction(F))
      continue;

    if (F->isDeclaration())
      dotExtFunction(stream, F);
    else
      dotFunction(stream, F);
  }

  // Edges
  for (auto I : llvmToLLVMChildren) {
    const Value *s = I.first;
    for (const Value *d : I.second) {
      stream << "Node" << ((void *) s) << " -> "
      	     << "Node" << ((void *) d) << "\n";
    }
  }

  for (auto I : llvmToSSAChildren) {
    const Value *s = I.first;
    for (MSSAVar *d : I.second) {
      stream << "Node" << ((void *) s) << " -> "
  	     << "Node" << ((void *) d) << "\n";
    }
  }

  for (auto I : ssaToSSAChildren) {
    MSSAVar *s = I.first;
    for (MSSAVar *d : I.second) {
      stream << "Node" << ((void *) s) << " -> "
  	     << "Node" << ((void *) d) << "\n";
    }
  }

  for (auto I : ssaToLLVMChildren) {
    MSSAVar *s = I.first;
    for (const Value *d : I.second) {
      stream << "Node" << ((void *) s) << " -> "
  	     << "Node" << ((void *) d) << "\n";
    }
  }

  for (auto I : callToFuncEdges) {
    const Value *call = I.first;
    const Function *f = I.second;
    stream << "NodeCall" << ((void *) call) << " -> "
	   << "Node" << ((void *) f)
      	   << " [lhead=cluster_" << ((void *) f)
	   <<"]\n";
  }

  for (auto I : condToCallEdges) {
    const Value *s = I.first;
    for (const Value *call : I.second) {
      stream << "Node" << ((void *) s) << " -> "
  	     << "NodeCall" << ((void *) call) << "\n";
    }
	/*if (taintedLLVMNodes.count(s) != 0){
		errs() << "DBG: " << s->getName() << " is a tainted condition \n";
      		s->dump();
	}*/
  }

  stream << "}\n";

  double t2 = gettime();

  dotTime += t2 - t1;
}


void
DepGraphDCF::dotFunction(raw_fd_ostream &stream, const Function *F) {
  stream << "\tsubgraph cluster_" << ((void *) F) << " {\n";
  stream << "style=filled;\ncolor=lightgrey;\n";
  stream << "label=< <B>" << F->getName() << "</B> >;\n";
  stream << "node [style=filled,color=white];\n";


  // Nodes with label
  for (const Value *v : funcToLLVMNodesMap[F]) {
    if (isa<GlobalValue>(v))
      continue;
    stream << "Node" << ((void *) v) << " [label=\""
	   << getValueLabel(v) << "\" "
	   << getNodeStyle(v) << "];\n";

  }

  for (const MSSAVar *v : funcToSSANodesMap[F]) {
    stream << "Node" << ((void *) v) << " [label=\""
	   << v->getName()
	   <<  "\" shape=diamond "
	   << getNodeStyle(v) << "];\n";
  }

  for (const Value *v : funcToCallNodes[F]) {
    stream << "NodeCall" << ((void *) v) << " [label=\""
	   << getCallValueLabel(v)
	   <<  "\" shape=rectangle "
	   << getCallNodeStyle(v) << "];\n";
  }

  stream << "Node" << ((void *) F) << " [style=invisible];\n";

  stream << "}\n";
}

void
DepGraphDCF::dotExtFunction(raw_fd_ostream &stream, const Function *F) {
  stream << "\tsubgraph cluster_" << ((void *)F) << " {\n";
  stream << "style=filled;\ncolor=lightgrey;\n";
  stream << "label=< <B>" << F->getName() << "</B> >;\n";
  stream << "node [style=filled,color=white];\n";


  // Nodes with label
  for (const Value *v : funcToLLVMNodesMap[F]) {
    stream << "Node" << ((void *) v) << " [label=\""
	   << getValueLabel(v) << "\" "
	   << getNodeStyle(v) << "];\n";
  }

  for (const MSSAVar *v : funcToSSANodesMap[F]) {
    stream << "Node" << ((void *) v) << " [label=\""
	   << v->getName()
	   <<  "\" shape=diamond "
	   << getNodeStyle(v) << "];\n";
  }

  stream << "Node" << ((void *) F) << " [style=invisible];\n";

  stream << "}\n";
}

std::string
DepGraphDCF::getNodeStyle(const llvm::Value *v) {
  if (taintedLLVMNodes.count(v) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}

std::string
DepGraphDCF::getNodeStyle(const MSSAVar *v) {
  if (taintedSSANodes.count(v) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}

std::string
DepGraphDCF::getNodeStyle(const Function *f) {
  return "style=filled, color=white";
}

std::string
DepGraphDCF::getCallNodeStyle(const llvm::Value *v) {
  return "style=filled, color=white";
}


void
DepGraphDCF::computeTaintedValuesContextInsensitive() {
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

  double t1 = gettime();

  std::queue<MSSAVar *> varToVisit;
  std::queue<const Value *> valueToVisit;

  // SSA sources
  for(const MSSAVar *src : ssaSources) {
    taintedSSANodes.insert(src);
    varToVisit.push(const_cast<MSSAVar *>(src));
  }

  // Value sources
  for(const Value *src : valueSources) {
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
	for (const Value *d : ssaToLLVMChildren[s]) {
	  if (taintedLLVMNodes.count(d) != 0)
	    continue;

	  taintedLLVMNodes.insert(d);
	  valueToVisit.push(d);
	}
      }
    }

    if (valueToVisit.size() > 0) {
      const Value *s = valueToVisit.front();
      valueToVisit.pop();

      if (llvmToLLVMChildren.find(s) != llvmToLLVMChildren.end()) {
	for (const Value *d : llvmToLLVMChildren[s]) {
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

  double t2 = gettime();

  for (const Value *v : taintedLLVMNodes) {
    taintedConditions.insert(v);
  }

  floodDepTime += t2 - t1;
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


void
DepGraphDCF::printTimers() const {
  errs() << "Build graph time : " << buildGraphTime*1.0e3 << " ms\n";
  errs() << "Phi elimination time : " << phiElimTime*1.0e3 << " ms\n";
  errs() << "Flood dependencies time : " << floodDepTime*1.0e3 << " ms\n";
  errs() << "Flood calls PDF+ time : " << floodCallTime*1.0e3 << " ms\n";
  errs() << "Dot graph time : " << dotTime*1.0e3 << " ms\n";
}

bool
DepGraphDCF::isTaintedValue(const Value *v){
  return taintedConditions.find(v) != taintedConditions.end();
}

void
DepGraphDCF::getCallInterIPDF(const llvm::CallInst *call,
				  std::set<const llvm::BasicBlock *> &ipdf) {
  std::set<const llvm::CallInst *> visitedCallSites;
  queue<const CallInst *> callsitesToVisit;
  callsitesToVisit.push(call);

  while (callsitesToVisit.size() > 0) {
    const CallInst *CS = callsitesToVisit.front();
    Function *F = const_cast<Function *>(CS->getParent()->getParent());
    callsitesToVisit.pop();
    visitedCallSites.insert(CS);

    BasicBlock *BB = const_cast<BasicBlock *>(CS->getParent());
    PostDominatorTree *PDT =
      &pass->getAnalysis<PostDominatorTreeWrapperPass>(*F).getPostDomTree();
    vector<BasicBlock *> funcIPDF = iterated_postdominance_frontier(*PDT, BB);
    ipdf.insert(funcIPDF.begin(), funcIPDF.end());

    if (funcToCallSites.find(F) != funcToCallSites.end()) {
      for (const Value *v : funcToCallSites[F]) {
	const CallInst *CS2 = cast<CallInst>(v);
	if (visitedCallSites.count(CS2) != 0)
	  continue;
	callsitesToVisit.push(CS2);
      }
    }
  }
}

bool
DepGraphDCF::areSSANodesEquivalent(MSSAVar *var1, MSSAVar *var2) {
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
    for (const Value *v : ssaToLLVMChildren[var1]) {
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
    for (const Value *v : ssaToLLVMParents[var1]) {
      if (ssaToLLVMParents[var2].find(v) == ssaToLLVMParents[var2].end())
	return false;
    }
  }

  return true;
}

void
DepGraphDCF::eliminatePhi(MSSAPhi *phi, vector<MSSAVar *>ops) {
  struct ssa2SSAEdge {
    ssa2SSAEdge(MSSAVar *s, MSSAVar *d) : s(s), d(d) {}
    MSSAVar *s;
    MSSAVar *d;
  };
  struct ssa2LLVMEdge {
    ssa2LLVMEdge(MSSAVar *s, const Value *d) : s(s), d(d) {}
    MSSAVar *s;
    const Value *d;
  };
  struct llvm2SSAEdge {
    llvm2SSAEdge(const Value *s, MSSAVar *d) : s(s), d(d) {}
    const Value *s;
    MSSAVar *d;
  };
  struct llvm2LLVMEdge {
    llvm2LLVMEdge(const Value *s, const Value *d) : s(s), d(d) {}
    const Value *s;
    const Value *d;
  };


  // Singlify operands
  std::set<MSSAVar *> opsSet;
  for (MSSAVar *v : ops)
    opsSet.insert(v);
  ops.clear();
  for (MSSAVar *v : opsSet)
    ops.push_back(v);

  // Remove links from predicates to PHI
  for (const Value *v : phi->preds)
    removeEdge(v, phi->var);

  // Remove links from ops to PHI
  for (MSSAVar *op : ops)
    removeEdge(op, phi->var);

  // For each outgoing edge from PHI to a SSA node N, connect
  // op1 to N and remove the link from PHI to N.
  {
    vector<ssa2SSAEdge> edgesToAdd;
    vector<ssa2SSAEdge> edgesToRemove;
    if (ssaToSSAChildren.find(phi->var) != ssaToSSAChildren.end()) {
      for (MSSAVar *v : ssaToSSAChildren[phi->var]) {
	edgesToAdd.push_back(ssa2SSAEdge(ops[0], v));
	edgesToRemove.push_back(ssa2SSAEdge(phi->var, v));

	// If N is a phi replace the phi operand of N with op1
	if (v->def->type == MSSADef::PHI) {
	  MSSAPhi *outPHI = cast<MSSAPhi>(v->def);

	  bool found = false;
	  for (auto I = outPHI->opsVar.begin(), E = outPHI->opsVar.end(); I != E;
	       ++I) {
	    if (I->second == phi->var) {
	      found = true;
	      I->second = ops[0];
	      break;
	    }
	  }
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
    vector<ssa2LLVMEdge> edgesToAdd;
    vector<ssa2LLVMEdge> edgesToRemove;

    // For each outgoing edge from PHI to a LLVM node N, connect
    // connect op1 to N and remove the link from PHI to N.
    if (ssaToLLVMChildren.find(phi->var) != ssaToLLVMChildren.end()) {
      for (const Value *v : ssaToLLVMChildren[phi->var]) {
	// addEdge(ops[0], v);
	// removeEdge(phi->var, v);
	edgesToAdd.push_back(ssa2LLVMEdge(ops[0], v));
	edgesToRemove.push_back(ssa2LLVMEdge(phi->var, v));
      }
    }
    for (ssa2LLVMEdge e : edgesToAdd)
      addEdge(e.s, e.d);
    for (ssa2LLVMEdge e : edgesToRemove)
      removeEdge(e.s, e.d);
  }

  // Remove PHI Node
  const Function *F = phi->var->bb->getParent();
  assert(F);
  auto it = funcToSSANodesMap[F].find(phi->var);
  assert(it != funcToSSANodesMap[F].end());
  funcToSSANodesMap[F].erase(it);

  // Remove edges connected to other operands than op0
  {
    vector<ssa2SSAEdge> toRemove1;
    vector<ssa2LLVMEdge> toRemove2;
    vector<llvm2SSAEdge> toRemove3;
    for (unsigned i=1; i<ops.size(); ++i) {
      if (ssaToSSAParents.find(ops[i]) != ssaToSSAParents.end()) {
	for (MSSAVar *v : ssaToSSAParents[ops[i]])
	  toRemove1.push_back(ssa2SSAEdge(v, ops[i]));
      }
      if (ssaToLLVMParents.find(ops[i]) != ssaToLLVMParents.end()) {
	for (const Value *v : ssaToLLVMParents[ops[i]])
	  toRemove3.push_back(llvm2SSAEdge(v, ops[i]));
      }
      if (ssaToSSAChildren.find(ops[i]) != ssaToSSAChildren.end()) {
	for (MSSAVar *v : ssaToSSAChildren[ops[i]])
	  toRemove1.push_back(ssa2SSAEdge(ops[i], v));
      }
      if (ssaToLLVMChildren.find(ops[i]) != ssaToLLVMChildren.end()) {
	for (const Value *v : ssaToLLVMChildren[ops[i]])
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
  for (unsigned i=1; i<ops.size(); ++i) {
    auto it2 = funcToSSANodesMap[F].find(ops[i]);
    assert(it2 != funcToSSANodesMap[F].end());
    funcToSSANodesMap[F].erase(it2);
  }
}

void
DepGraphDCF::phiElimination() {
  double t1 = gettime();

  // For each function, iterate through its basic block and try to eliminate phi
  // function until reaching a fixed point.
  for (const Function &F : *mssa->m) {
    bool changed = true;

    while (changed) {
      changed = false;

      for (const BasicBlock &BB : F) {
	if (mssa->bbToPhiMap.find(&BB) == mssa->bbToPhiMap.end())
	  continue;

	for (MSSAPhi *phi : mssa->bbToPhiMap[&BB]) {

	  assert(funcToSSANodesMap.find(&F) != funcToSSANodesMap.end());

	  // Has the phi node been removed already ?
	  if (funcToSSANodesMap[&F].count(phi->var) == 0)
	    continue;

	  // For each phi we test if its operands (chi) are not PHI and
	  // are equivalent
	  vector<MSSAVar *> phiOperands;
	  for (auto J : phi->opsVar)
	    phiOperands.push_back(J.second);

	  bool canElim = true;
	  for (unsigned i=0; i<phiOperands.size() - 1; i++) {
	    if (!areSSANodesEquivalent(phiOperands[i], phiOperands[i+1])) {
	      canElim = false;
	      break;
	    }
	  }
	  if (!canElim)
	    continue;

	  // PHI Node can be eliminated !
	  changed = true;
	  eliminatePhi(phi, phiOperands);
	}
      }
    }
  }

  double t2 = gettime();
  phiElimTime += t2 - t1;
}

void
DepGraphDCF::addEdge(const llvm::Value *s, const llvm::Value *d) {
  llvmToLLVMChildren[s].insert(d);
  llvmToLLVMParents[d].insert(s);
}

void
DepGraphDCF::addEdge(const llvm::Value *s, MSSAVar *d) {
  llvmToSSAChildren[s].insert(d);
  ssaToLLVMParents[d].insert(s);
}

void
DepGraphDCF::addEdge(MSSAVar *s, const llvm::Value *d) {
  ssaToLLVMChildren[s].insert(d);
  llvmToSSAParents[d].insert(s);
}

void
DepGraphDCF::addEdge(MSSAVar *s, MSSAVar *d) {
  ssaToSSAChildren[s].insert(d);
  ssaToSSAParents[d].insert(s);
}

void
DepGraphDCF::removeEdge(const llvm::Value *s, const llvm::Value *d) {
  int n;
  n = llvmToLLVMChildren[s].erase(d);
  assert(n == 1);
  n = llvmToLLVMParents[d].erase(s);
  assert(n == 1);
}

void
DepGraphDCF::removeEdge(const llvm::Value *s, MSSAVar *d) {
  int n;
  n = llvmToSSAChildren[s].erase(d);
  assert(n == 1);
  n = ssaToLLVMParents[d].erase(s);
  assert(n == 1);
}

void
DepGraphDCF::removeEdge(MSSAVar *s, const llvm::Value *d) {
  int n;
  n = ssaToLLVMChildren[s].erase(d);
  assert(n == 1);
  n = llvmToSSAParents[d].erase(s);
  assert(n == 1);
}

void
DepGraphDCF::removeEdge(MSSAVar *s, MSSAVar *d) {
  int n;
  n = ssaToSSAChildren[s].erase(d);
  assert(n == 1);
  n = ssaToSSAParents[d].erase(s);
  assert(n == 1);
}

void
DepGraphDCF::dotTaintPath(const Value *v, string filename,
		       const Instruction *collective) {
  errs() << "Writing '" << filename << "' ...\n";

  // Parcours en largeur
  unsigned curDist = 0;
  unsigned curSize = 128;
  std::vector<std::set<const Value *> > visitedLLVMNodesByDist;
  std::set<const Value *> visitedLLVMNodes;
  std::vector<std::set<MSSAVar *> > visitedSSANodesByDist;
  std::set<MSSAVar *> visitedSSANodes;

  visitedSSANodesByDist.resize(curSize);
  visitedLLVMNodesByDist.resize(curSize);

  visitedLLVMNodes.insert(v);

  for (const Value *p : llvmToLLVMParents[v]) {
    if (visitedLLVMNodes.find(p) != visitedLLVMNodes.end())
      continue;

    if (taintedLLVMNodes.find(p) == taintedLLVMNodes.end())
      continue;

    visitedLLVMNodesByDist[curDist].insert(p);
  }
  for (MSSAVar *p : llvmToSSAParents[v]) {
    if (visitedSSANodes.find(p) != visitedSSANodes.end())
      continue;

    if (taintedSSANodes.find(p) == taintedSSANodes.end())
      continue;

    visitedSSANodesByDist[curDist].insert(p);
  }

  bool stop = false;
  MSSAVar *ssaRoot = NULL;
  const Value *llvmRoot = NULL;

  while (true) {
    if (curDist >= curSize) {
      curSize *=2;
      visitedLLVMNodesByDist.resize(curSize);
      visitedSSANodesByDist.resize(curSize);
    }

    // Visit parents of llvm values
    for (const Value *v : visitedLLVMNodesByDist[curDist]) {
      if (valueSources.find(v) != valueSources.end()) {
	llvmRoot = v;
	visitedLLVMNodes.insert(v);
	errs() << "found a path of size " << curDist << "\n";
	stop = true;
	break;
      }

      visitedLLVMNodes.insert(v);

      for (const Value *p : llvmToLLVMParents[v]) {
	if (visitedLLVMNodes.find(p) != visitedLLVMNodes.end())
	  continue;

	if (taintedLLVMNodes.find(p) == taintedLLVMNodes.end())
	  continue;

	visitedLLVMNodesByDist[curDist+1].insert(p);
      }
      for (MSSAVar *p : llvmToSSAParents[v]) {
	if (visitedSSANodes.find(p) != visitedSSANodes.end())
	  continue;

	if (taintedSSANodes.find(p) == taintedSSANodes.end())
	  continue;

	visitedSSANodesByDist[curDist+1].insert(p);
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

      for (const Value *p : ssaToLLVMParents[v]) {
	if (visitedLLVMNodes.find(p) != visitedLLVMNodes.end())
	  continue;

	if (taintedLLVMNodes.find(p) == taintedLLVMNodes.end())
	  continue;

	visitedLLVMNodesByDist[curDist+1].insert(p);
      }
      for (MSSAVar *p : ssaToSSAParents[v]) {
	if (visitedSSANodes.find(p) != visitedSSANodes.end())
	  continue;

	if (taintedSSANodes.find(p) == taintedSSANodes.end())
	  continue;

	visitedSSANodesByDist[curDist+1].insert(p);
      }

      if (stop)
	break;
    }

    if (stop)
      break;

    curDist++;
  }

  assert(stop);

  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph F {\n";
  stream << "compound=true;\n";
  stream << "rankdir=LR;\n";

  vector<string> debugMsgs;
  vector<DGDebugLoc> debugLocs;

  visitedSSANodes.clear();
  visitedLLVMNodes.clear();

  assert(llvmRoot || ssaRoot);

  if (ssaRoot)
    visitedSSANodes.insert(ssaRoot);
  else
    visitedLLVMNodes.insert(llvmRoot);

  string tmpStr;
  raw_string_ostream strStream(tmpStr);

  MSSAVar *lastVar = ssaRoot;
  const Value *lastValue = llvmRoot;
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
  for (unsigned i=curDist-1; i>0; i--) {
    bool found = false;
    if (lastIsVar) {
      for (MSSAVar *v : visitedSSANodesByDist[i]) {
	if (ssaToSSAParents[v].find(lastVar) == ssaToSSAParents[v].end())
	  continue;

	visitedSSANodes.insert(v);
	strStream << "Node" << ((void *) lastVar) << " -> "
		  << "Node" << ((void *) v) << "\n";
	lastVar = v;
	found = true;
	debugMsgs.push_back(getStringMsg(v));
	if (getDGDebugLoc(v, DL))
	  debugLocs.push_back(DL);
	break;
      }

      if (found)
	continue;

      for (const Value *v : visitedLLVMNodesByDist[i]) {
	if (llvmToSSAParents[v].find(lastVar) == llvmToSSAParents[v].end())
	  continue;

	visitedLLVMNodes.insert(v);
	strStream << "Node" << ((void *) lastVar) << " -> "
	       << "Node" << ((void *) v) << "\n";
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
	if (ssaToLLVMParents[v].find(lastValue) == ssaToLLVMParents[v].end())
	  continue;

	visitedSSANodes.insert(v);
	strStream << "Node" << ((void *) lastValue) << " -> "
	       << "Node" << ((void *) v) << "\n";
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

      for (const Value *v : visitedLLVMNodesByDist[i]) {
	if (llvmToLLVMParents[v].find(lastValue) == llvmToLLVMParents[v].end())
	  continue;

	visitedLLVMNodes.insert(v);
	strStream << "Node" << ((void *) lastValue) << " -> "
	       << "Node" << ((void *) v) << "\n";
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
  std::set<const Function *> visitedFunctions;
  for (auto I : funcToLLVMNodesMap) {
    for (const Value *v : I.second) {
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
  for (const Function *F : visitedFunctions) {
    stream << "\tsubgraph cluster_" << ((void *) F) << " {\n";
    stream << "style=filled;\ncolor=lightgrey;\n";
    stream << "label=< <B>" << F->getName() << "</B> >;\n";
    stream << "node [style=filled,color=white];\n";

    for (const Value *v : visitedLLVMNodes) {
      if (funcToLLVMNodesMap[F].find(v) == funcToLLVMNodesMap[F].end())
	continue;

      stream << "Node" << ((void *) v) << " [label=\""
	     << getValueLabel(v) << "\" "
	     << getNodeStyle(v) << "];\n";
    }

    for (MSSAVar *v : visitedSSANodes) {
      if (funcToSSANodesMap[F].find(v) == funcToSSANodesMap[F].end())
	continue;

      stream << "Node" << ((void *) v) << " [label=\""
	     << v->getName()
	     <<  "\" shape=diamond "
	     << getNodeStyle(v) << "];\n";
    }

    stream << "}\n";
  }

  // Dot edges
  stream << strStream.str();

  stream << "}\n";

  for (unsigned i=0; i<debugMsgs.size(); i++)
    stream << debugMsgs[i];


  // Write trace
  string trace;
  if (getDebugTrace(debugLocs, trace, collective)) {
    string tracefilename = filename + ".trace";
    errs() << "Writing '" << tracefilename << "' ...\n";
    raw_fd_ostream tracestream(tracefilename, EC, sys::fs::F_Text);
    tracestream << trace;
  }
}


string
DepGraphDCF::getStringMsg(const Value *v) {
  string msg;
  msg.append("# ");
  msg.append(getValueLabel(v));
  msg.append(":\n# ");

  DebugLoc loc = NULL;
  string funcName = "unknown";
  const Instruction *inst = dyn_cast<Instruction>(v);
  if (inst) {
    loc = inst->getDebugLoc();
    funcName = inst->getParent()->getParent()->getName();
  }

  msg.append("function: ");
  msg.append(funcName);
  if (loc) {
    msg.append(" file ");
    msg.append(loc->getFilename());
    msg.append(" line ");
    msg.append(to_string(loc.getLine()));
  } else {
    msg.append(" no debug loc");
  }
  msg.append("\n");

  return msg;
}

string
DepGraphDCF::getStringMsg(MSSAVar *v) {
  string msg;
  msg.append("# ");
  msg.append(v->getName());
  msg.append(":\n# ");
  string funcName = "unknown";
  if (v->bb)
    funcName = v->bb->getParent()->getName();

  MSSADef *def = v->def;
  assert(def);

  // Def can be PHI, call, store, chi, entry, extvararg, extarg, extret,
  // extcall, extretcall

  DebugLoc loc = NULL;

  if (isa<MSSACallChi>(def)) {
    MSSACallChi *callChi = cast<MSSACallChi>(def);
    funcName = callChi->inst->getParent()->getParent()->getName();
    loc = callChi->inst->getDebugLoc();
  } else if (isa<MSSAStoreChi>(def)) {
    MSSAStoreChi *storeChi = cast<MSSAStoreChi>(def);
    funcName = storeChi->inst->getParent()->getParent()->getName();
    loc = storeChi->inst->getDebugLoc();
  } else if (isa<MSSAExtCallChi>(def)) {
    MSSAExtCallChi *extCallChi = cast<MSSAExtCallChi>(def);
    funcName = extCallChi->inst->getParent()->getParent()->getName();
    loc = extCallChi->inst->getDebugLoc();
  } else if (isa<MSSAExtVarArgChi>(def)) {
    MSSAExtVarArgChi *varArgChi = cast<MSSAExtVarArgChi>(def);
    funcName = varArgChi->func->getName();
  } else if (isa<MSSAExtArgChi>(def)) {
    MSSAExtArgChi *extArgChi = cast<MSSAExtArgChi>(def);
    funcName = extArgChi->func->getName();
  } else if (isa<MSSAExtRetChi>(def)) {
    MSSAExtRetChi *extRetChi = cast<MSSAExtRetChi>(def);
    funcName = extRetChi->func->getName();
  }

  msg.append("function: ");
  msg.append(funcName);

  if (loc) {
    msg.append(" file ");
    msg.append(loc->getFilename());
    msg.append(" line ");
    msg.append(to_string(loc.getLine()));
  } else {
    msg.append(" no debug loc");
  }
  msg.append("\n");

  return msg;
}

bool
DepGraphDCF::getDGDebugLoc(const Value *v, DGDebugLoc &DL) {
  DL.F = NULL;
  DL.line = -1;
  DL.filename = "unknown";

  DebugLoc loc = NULL;

  const Instruction *inst = dyn_cast<Instruction>(v);
  if (inst) {
    loc = inst->getDebugLoc();
    DL.F = inst->getParent()->getParent();
  }

  if (loc) {
    DL.filename = loc->getFilename();
    DL.line = loc->getLine();
  } else {
    return false;
  }

  return DL.F != NULL;
}

bool
DepGraphDCF::getDGDebugLoc(MSSAVar *v, DGDebugLoc &DL) {
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
    DL.filename = loc->getFilename();
    DL.line = loc->getLine();
  } else {
    return false;
  }

  return DL.F != NULL;
}

static bool getStrLine(ifstream &file, int line, string &str)  {
    // go to line
    file.seekg(std::ios::beg);
    for (int i=0; i < line-1; ++i)
      file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');

    getline(file, str);

    return true;
}

void
DepGraphDCF::reorderAndRemoveDup(vector<DGDebugLoc> &DLs) {
  vector<DGDebugLoc> sameFuncDL;
  vector<DGDebugLoc> res;

  if (DLs.empty())
    return;

  const Function *prev = DLs[0].F;
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
      sort(sameFuncDL.begin(), sameFuncDL.end());

      // remove duplicates
      int line_prev = -1;
      for (unsigned i=0; i<sameFuncDL.size(); ++i) {
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

bool
DepGraphDCF::getDebugTrace(vector<DGDebugLoc> &DLs, string &trace,
			const Instruction *collective) {
  DGDebugLoc collectiveLoc;
  if (getDGDebugLoc(collective, collectiveLoc))
    DLs.push_back(collectiveLoc);

  const Function *prevFunc = NULL;
  ifstream file;

  reorderAndRemoveDup(DLs);

  for (unsigned i=0; i<DLs.size(); ++i) {
    string strline;
    const Function *F = DLs[i].F;
    if (!F)
      return false;

    // new function, print filename and protoype
    if (F != prevFunc) {
      file.close();
      prevFunc = F;
      DISubprogram *DI = F->getSubprogram();
      if (!DI)
	return false;

      string filename = DI->getFilename();
      string dir = DI->getDirectory();
      string path = dir + "/" + filename;
      int line = DI->getLine();


      file.open(path, ios::in);
      if (!file.good()) {
	errs() << "error opening file: " << path << "\n";
	return false;
      }


      getStrLine(file, line, strline);
      trace.append("\n" + filename + "\n");
      trace.append(strline);
      trace.append(" l." + to_string(line) + "\n");
    }

    getStrLine(file, DLs[i].line, strline);
    trace.append("...\n" + strline + " l." + to_string(DLs[i].line) + "\n");
  }

  file.close();

  return true;
}

void
DepGraphDCF::floodFunction(const Function *F) {
  std::queue<MSSAVar *> varToVisit;
  std::queue<const Value *> valueToVisit;

  // 1) taint LLVM and SSA sources
  for (const MSSAVar *s : ssaSources) {
    if (funcToSSANodesMap.find(F) == funcToSSANodesMap.end())
      continue;

    if (funcToSSANodesMap[F].find(const_cast<MSSAVar *>(s)) !=
  	funcToSSANodesMap[F].end())
      taintedSSANodes.insert(s);
  }

  for (const Value *s : valueSources) {
    const Instruction *inst = dyn_cast<Instruction>(s);
    if (!inst || inst->getParent()->getParent() != F)
      continue;

    taintedLLVMNodes.insert(s);
  }

  // 2) Add tainted variables of the function to the queue.
  if (funcToSSANodesMap.find(F) != funcToSSANodesMap.end()) {
    for (MSSAVar *v : funcToSSANodesMap[F]) {
      if (taintedSSANodes.find(v) != taintedSSANodes.end())
	varToVisit.push(v);
    }
  }
  if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
    for (const Value *v : funcToLLVMNodesMap[F]) {
      if (taintedLLVMNodes.find(v) != taintedLLVMNodes.end())
	valueToVisit.push(v);
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
	for (const Value *d : ssaToLLVMChildren[s]) {
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
      const Value *s = valueToVisit.front();
      valueToVisit.pop();

      if (llvmToLLVMChildren.find(s) != llvmToLLVMChildren.end()) {
	for (const Value *d : llvmToLLVMChildren[s]) {
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

void
DepGraphDCF::floodFunctionFromFunction(const Function *to, const Function *from) {
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
	  for (const Value *d : ssaToLLVMChildren[s]) {
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
	for (const Value *d : ssaToLLVMChildren[s]) {
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
    for (const Value *s : funcToLLVMNodesMap[from]) {
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
	for (const Value *d : llvmToLLVMChildren[s]) {
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

void
DepGraphDCF::resetFunctionTaint(const Function *F) {
  assert(CG->isReachableFromEntry(F));
  // assert(funcToSSANodesMap.find(F) != funcToSSANodesMap.end());
  if (funcToSSANodesMap.find(F) != funcToSSANodesMap.end()) {
    for (MSSAVar *v: funcToSSANodesMap[F]) {
      if (taintedSSANodes.find(v) != taintedSSANodes.end()) {
	taintedSSANodes.erase(v);
      }
    }
  }

  if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
    for (const Value *v : funcToLLVMNodesMap[F]) {
      if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
	taintedLLVMNodes.erase(v);
      }
    }
  }
}

void
DepGraphDCF::computeFunctionCSTaintedConds(const llvm::Function *F) {
  for (const BasicBlock &BB : *F) {
    for (const Instruction &I : BB) {
      if (!isa<CallInst>(I))
  	continue;

      if (callsiteToConds.find(cast<Value>(&I)) != callsiteToConds.end()) {
	for (const Value *v : callsiteToConds[cast<Value>(&I)]) {
	  if (taintedLLVMNodes.find(v) != taintedLLVMNodes.end()) {
	    taintedConditions.insert(v);
	  }
	}
      }
    }
  }
}

void
DepGraphDCF::computeTaintedValuesContextSensitive() {
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

  PTACallGraphNode *entry = CG->getEntry();
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

void
DepGraphDCF::computeTaintedValuesCSForEntry(PTACallGraphNode *entry) {
 vector<PTACallGraphNode *> S;

  map<PTACallGraphNode *, set<PTACallGraphNode *> > node2VisitedChildrenMap;
  S.push_back(entry);

  bool goingDown = true;
  const Function *prev = NULL;

  while (!S.empty()) {
    PTACallGraphNode *N = S.back();
    bool foundChildren = false;

    if (N->getFunction())
      errs() << "current =" << N->getFunction()->getName() << "\n";

    if (goingDown)
      errs() << "down\n";
    else
      errs() << "up\n";

    if (prev) {
      if (goingDown) {
	errs() << "tainting " << N->getFunction()->getName() << " from "
	       << prev->getName() << "\n";
	floodFunctionFromFunction(N->getFunction(), prev);

	errs() << "tainting " << N->getFunction()->getName() << "\n";
	floodFunction(N->getFunction());

	errs() << "for each call site get PDF+ and save tainted conditions\n";
	computeFunctionCSTaintedConds(N->getFunction());
      } else {
	errs() << "tainting " << N->getFunction()->getName() << " from "
	       << prev->getName() << "\n";
	floodFunctionFromFunction(N->getFunction(), prev);

	errs() << "tainting " << N->getFunction()->getName() << "\n";
	floodFunction(N->getFunction());

	errs() << "for each call site get PDF+ and save tainted conditions\n";
	computeFunctionCSTaintedConds(N->getFunction());

	errs() << "untainting " << prev->getName() << "\n";
	resetFunctionTaint(prev);
      }
    } else {
      errs() << "tainting " << N->getFunction()->getName() << "\n";
      floodFunction(N->getFunction());

      errs() << "for each call site get PDF+ and save tainted conditions\n";
      	computeFunctionCSTaintedConds(N->getFunction());
    }

    errs() << "stack : ";
    for (PTACallGraphNode *node : S)
      errs() << node->getFunction()->getName() << " ";
    errs() << "\n";

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
