#include "DepGraph.h"
#include "Utils.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <queue>

using namespace llvm;
using namespace std;

DepGraph::DepGraph(MemorySSA *mssa, PTACallGraph *CG, Pass *pass)
  : mssa(mssa), CG(CG), pass(pass),
    buildGraphTime(0), phiElimTime(0),
    floodDepTime(0), floodCallTime(0),
    dotTime(0) {}

DepGraph::~DepGraph() {}

void
DepGraph::buildFunction(const llvm::Function *F) {
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
      MSSAChi *entryChi = mssa->extVarArgEntryChi[F];
      assert(entryChi && entryChi->var);
      funcToSSANodesMap[F].insert(entryChi->var);
      MSSAChi *exitChi =  mssa->extVarArgExitChi[F];
      assert(exitChi && exitChi->var);
      funcToSSANodesMap[F].insert(exitChi->var);
      addEdge(exitChi->opVar, exitChi->var);
    }

    // Add args entry and exit chi nodes for external functions.
    unsigned argNo = 0;
    for (const Argument &arg : F->getArgumentList()) {
      if (!arg.getType()->isPointerTy()) {
	argNo++;
	continue;
      }

      MSSAChi *entryChi = mssa->extArgEntryChi[F][argNo];
      assert(entryChi && entryChi->var);
      funcToSSANodesMap[F].insert(entryChi->var);
      MSSAChi *exitChi =  mssa->extArgExitChi[F][argNo];
      assert(exitChi && exitChi->var);
      funcToSSANodesMap[F].insert(exitChi->var);
      addEdge(exitChi->opVar, exitChi->var);

      argNo++;
    }

    // Add retval chi node for external functions
    if (F->getReturnType()->isPointerTy()) {
      MSSAChi *retChi = mssa->extRetChi[F];
      assert(retChi && retChi->var);
      funcToSSANodesMap[F].insert(retChi->var);
    }

    // If the function is MPI_Comm_rank set the address-taken ssa of the
    // second argument as a contamination source.
    if (F->getName().equals("MPI_Comm_rank")) {
      assert(mssa->extArgExitChi[F][1]);
      ssaSources.insert(mssa->extArgExitChi[F][1]->var);
    }

    // If the function is MPI_Group_rank set the address-taken ssa of the
    // second argument as a contamination source.
    else if (F->getName().equals("MPI_Group_rank")) {
      assert(mssa->extArgExitChi[F][1]);
      ssaSources.insert(mssa->extArgExitChi[F][1]->var);
    }

    // memcpy
    else if (F->getName().find("memcpy") != StringRef::npos) {
      MSSAChi *srcEntryChi = mssa->extArgEntryChi[F][1];
      MSSAChi *dstExitChi = mssa->extArgExitChi[F][0];

      addEdge(srcEntryChi->var, dstExitChi->var);

      // llvm.mempcy instrinsic returns void whereas memcpy returns dst
      if (F->getReturnType()->isPointerTy()) {
	MSSAChi *retChi = mssa->extRetChi[F];
	addEdge(dstExitChi->var, retChi->var);
      }
    }

    // memmove
    else if (F->getName().find("memmove") != StringRef::npos) {
      MSSAChi *srcEntryChi = mssa->extArgEntryChi[F][1];
      MSSAChi *dstExitChi = mssa->extArgExitChi[F][0];

      addEdge(srcEntryChi->var, dstExitChi->var);

      // llvm.memmove instrinsic returns void whereas memmove returns dst
      if (F->getReturnType()->isPointerTy()) {
	MSSAChi *retChi = mssa->extRetChi[F];
	addEdge(dstExitChi->var, retChi->var);
      }
    }

    // memset
    else if (F->getName().find("memset") != StringRef::npos) {
      MSSAChi *argExitChi = mssa->extArgExitChi[F][0];
      const Argument *cArg = getFunctionArgument(F, 1);
      assert(cArg);

      addEdge(cArg, argExitChi->var);

      // llvm.memset instrinsic returns void whereas memset returns dst
      if (F->getReturnType()->isPointerTy()) {
	MSSAChi *retChi = mssa->extRetChi[F];
	addEdge(argExitChi->var, retChi->var);
      }
    }

    // Unknown external function, we have to connect every input to every
    // output.
    else {
      std::set<MSSAVar *> ssaOutputs;
      std::set<MSSAVar *> ssaInputs;

      // Compute SSA outputs
      for (auto I : mssa->extArgExitChi[F]) {
	MSSAChi *argExitChi = I.second;
	ssaOutputs.insert(argExitChi->var);
      }
      if (F->isVarArg()) {
	MSSAChi *varArgExitChi = mssa->extVarArgExitChi[F];
	ssaOutputs.insert(varArgExitChi->var);
      }
      if (F->getReturnType()->isPointerTy()) {
	MSSAChi *retChi = mssa->extRetChi[F];
	ssaOutputs.insert(retChi->var);
      }

      // Compute SSA inputs
      for (auto I : mssa->extArgEntryChi[F]) {
	MSSAChi *argEntryChi = I.second;
	ssaInputs.insert(argEntryChi->var);
      }
      if (F->isVarArg()) {
	MSSAChi *varArgEntryChi = mssa->extVarArgEntryChi[F];
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

  double t2 = gettime();

  buildGraphTime += t2 - t1;
}

void
DepGraph::visitBasicBlock(llvm::BasicBlock &BB) {
  // Add MSSA Phi nodes and edges to the graph.
  for (MSSAPhi *phi : mssa->bbToPhiMap[&BB]) {
    assert(phi && phi->var);
    funcToSSANodesMap[curFunc].insert(phi->var);
    for (auto I : phi->opsVar) {
      assert(I.second);
      funcToSSANodesMap[curFunc].insert(I.second);
      addEdge(I.second, phi->var);
    }

    for (const Value *pred : phi->preds) {
      funcToLLVMNodesMap[curFunc].insert(pred);
      addEdge(pred, phi->var);
    }
  }
}

void
DepGraph::visitAllocaInst(llvm::AllocaInst &I) {
  // Do nothing
}

void
DepGraph::visitTerminatorInst(llvm::TerminatorInst &I) {
  // Do nothing
}

void
DepGraph::visitCmpInst(llvm::CmpInst &I) {
  // Cmp instruction is a value, connect the result to its operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraph::visitLoadInst(llvm::LoadInst &I) {
  // Load inst, connect MSSA mus and the pointer loaded.
  funcToLLVMNodesMap[curFunc].insert(&I);
  funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());

  for (MSSAMu *mu : mssa->loadToMuMap[&I]) {
    assert(mu && mu->var);
    funcToSSANodesMap[curFunc].insert(mu->var);
    addEdge(mu->var, &I);
  }

  addEdge(I.getPointerOperand(), &I);
}

void
DepGraph::visitStoreInst(llvm::StoreInst &I) {
  // Store inst
  // For each chi, connect the pointer, the value stored and the MSSA operand.
  for (MSSAChi *chi : mssa->storeToChiMap[&I]) {
    assert(chi && chi->var && chi->opVar);
    funcToSSANodesMap[curFunc].insert(chi->var);
    funcToSSANodesMap[curFunc].insert(chi->opVar);
    funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());
    funcToLLVMNodesMap[curFunc].insert(I.getValueOperand());

    addEdge(chi->opVar, chi->var);
    addEdge(I.getValueOperand(), chi->var);
    addEdge(I.getPointerOperand(), chi->var);
  }
}

void
DepGraph::visitGetElementPtrInst(llvm::GetElementPtrInst &I) {
  // GetElementPtr, connect operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraph::visitPHINode(llvm::PHINode &I) {
  // Connect LLVM Phi, connect operands and predicates.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }

  for (const Value *v : mssa->llvmPhiToPredMap[&I]) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraph::visitCastInst(llvm::CastInst &I) {
  // Cast inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraph::visitSelectInst(llvm::SelectInst &I) {
  // Select inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraph::visitBinaryOperator(llvm::BinaryOperator &I) {
  // Binary op, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraph::visitCallInst(llvm::CallInst &I) {
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
  }

  // indirect call
  else {
    for (const Function *mayCallee : CG->indirectCallMap[&I]) {
      callToFuncEdges[&I] = mayCallee;
      funcToCallSites[mayCallee].insert(&I);
    }
  }
}

void
DepGraph::connectCSMus(llvm::CallInst &I) {
  // Mu of the call site.
  for (MSSAMu *mu : mssa->callSiteToMuMap[CallSite(&I)]) {
    assert(mu && mu->var);
    funcToSSANodesMap[curFunc].insert(mu->var);
    MSSACallMu *callMu = cast<MSSACallMu>(mu);
    const Function *called = callMu->called;

    // External Function, we connect call mu to artifical chi of the external
    // function for each argument.
    if (called->isDeclaration()) {
      MSSAExtCallMu *extCallMu = cast<MSSAExtCallMu>(callMu);
      unsigned argNo = extCallMu->argNo;

      // Case where this is a var arg parameter
      if (argNo >= called->arg_size()) {
	assert(called->isVarArg());

	assert(mssa->extVarArgEntryChi[called]);
	MSSAVar *var = mssa->extVarArgEntryChi[called]->var;
	assert(var);
	funcToSSANodesMap[called].insert(var);
	addEdge(mu->var, var); // rule3
      }

      else {
	// rule3
	assert(mssa->extArgEntryChi[called][argNo]);
	addEdge(mu->var, mssa->extArgEntryChi[called][argNo]->var);
      }

      continue;
    }

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
DepGraph::connectCSChis(llvm::CallInst &I) {
  // Chi of the callsite.
  for (MSSAChi *chi : mssa->callSiteToChiMap[CallSite(&I)]) {
    assert(chi && chi->var && chi->opVar);
    funcToSSANodesMap[curFunc].insert(chi->opVar);
    funcToSSANodesMap[curFunc].insert(chi->var);
    addEdge(chi->opVar, chi->var); // rule4

    MSSACallChi *callChi = cast<MSSACallChi>(chi);
    const Function *called = callChi->called;

    // External Function, we connect call chi to artifical chi of the external
    // function for each argument.
    if (called->isDeclaration()) {
      MSSAExtCallChi *extCallChi = cast<MSSAExtCallChi>(callChi);
      unsigned argNo = extCallChi->argNo;

      // Case where this is a var arg parameter.
      if (argNo >= called->arg_size()) {
	assert(called->isVarArg());

	assert(mssa->extVarArgExitChi[called]);
	MSSAVar *var = mssa->extVarArgExitChi[called]->var;
	assert(var);
	funcToSSANodesMap[called].insert(var);
	addEdge(var, chi->var); // rule5
      }

      else {
	// rule5
	assert(mssa->extArgExitChi[called][argNo]);
	addEdge(mssa->extArgExitChi[called][argNo]->var, chi->var);
      }

      continue;
    }

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
DepGraph::connectCSEffectiveParameters(llvm::CallInst &I) {
  // Connect effective parameters to formal parameters.
  const Function *callee = I.getCalledFunction();

  // direct call
  if (callee) {
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
DepGraph::connectCSCalledReturnValue(llvm::CallInst &I) {
  // If the function called returns a value, connect the return value to the
  // call value.

  const Function *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    if (!callee->isDeclaration() && !I.getType()->isVoidTy()) {
      funcToLLVMNodesMap[curFunc].insert(&I);
      addEdge(getReturnValue(callee), &I); // rule2
    }
  }

  // indirect call
  else {
    for (const Function *mayCallee : CG->indirectCallMap[&I]) {
      if (!mayCallee->isDeclaration() && !I.getType()->isVoidTy()) {
	funcToLLVMNodesMap[curFunc].insert(&I);
	addEdge(getReturnValue(mayCallee), &I); // rule2
      }
    }
  }
}

void
DepGraph::connectCSRetChi(llvm::CallInst &I) {
  // External function, if the function called returns a pointer, connect the
  // artifical ret chi to the retcallchi
  // return chi of the caller.

  const Function *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    if (callee->isDeclaration() && callee->getReturnType()->isPointerTy()) {
      for (MSSAChi *chi : mssa->extCallSiteToRetChiMap[CallSite(&I)]) {
	assert(chi && chi->var && chi->opVar);
	funcToSSANodesMap[curFunc].insert(chi->var);
	funcToSSANodesMap[curFunc].insert(chi->opVar);

	addEdge(chi->opVar, chi->var);
	addEdge(mssa->extRetChi[callee]->var, chi->var);
      }
    }
  }

  // indirect call
  else {
    for (const Function *mayCallee : CG->indirectCallMap[&I]) {
      if (mayCallee->isDeclaration() &&
	  mayCallee->getReturnType()->isPointerTy()) {
	for (MSSAChi *chi : mssa->extCallSiteToRetChiMap[CallSite(&I)]) {
	  assert(chi && chi->var && chi->opVar);
	  funcToSSANodesMap[curFunc].insert(chi->var);
	  funcToSSANodesMap[curFunc].insert(chi->opVar);

	  addEdge(chi->opVar, chi->var);
	  addEdge(mssa->extRetChi[mayCallee]->var, chi->var);
	}
      }
    }
  }
}

void
DepGraph::visitExtractValueInst(llvm::ExtractValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraph::visitExtractElementInst(llvm::ExtractElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraph::visitInsertElementInst(llvm::InsertElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraph::visitShuffleVectorInst(llvm::ShuffleVectorInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraph::visitInstruction(llvm::Instruction &I) {
  errs() << "Error: Unhandled instruction " << I << "\n";
}

void
DepGraph::toDot(string filename) {
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
DepGraph::dotFunction(raw_fd_ostream &stream, const Function *F) {
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
DepGraph::dotExtFunction(raw_fd_ostream &stream, const Function *F) {
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
DepGraph::getNodeStyle(const llvm::Value *v) {
  if (taintedLLVMNodes.count(v) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}

std::string
DepGraph::getNodeStyle(const MSSAVar *v) {
  if (taintedSSANodes.count(v) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}

std::string
DepGraph::getNodeStyle(const Function *f) {
  if (taintedFunctions.count(f) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}

std::string
DepGraph::getCallNodeStyle(const llvm::Value *v) {
  if (taintedCallNodes.count(v) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}


void
DepGraph::computeTaintedValues() {
  double t1 = gettime();

  std::queue<MSSAVar *> varToVisit;
  std::queue<const Value *> valueToVisit;

  for(const MSSAVar *src : ssaSources) {
    taintedSSANodes.insert(src);
    varToVisit.push(const_cast<MSSAVar *>(src));
  }

  while (varToVisit.size() > 0 || valueToVisit.size() > 0) {
    if (varToVisit.size() > 0) {
      MSSAVar *s = varToVisit.front();
      varToVisit.pop();

      for (MSSAVar *d : ssaToSSAChildren[s]) {
	if (taintedSSANodes.count(d) != 0)
	  continue;

	taintedSSANodes.insert(d);
	varToVisit.push(d);
      }

      for (const Value *d : ssaToLLVMChildren[s]) {
	if (taintedLLVMNodes.count(d) != 0)
	  continue;

	taintedLLVMNodes.insert(d);
	valueToVisit.push(d);
      }
    }

    if (valueToVisit.size() > 0) {
      const Value *s = valueToVisit.front();
      valueToVisit.pop();

      for (const Value *d : llvmToLLVMChildren[s]) {
	if (taintedLLVMNodes.count(d) != 0)
	  continue;

	taintedLLVMNodes.insert(d);
	valueToVisit.push(d);
      }

      for (MSSAVar *d : llvmToSSAChildren[s]) {
	if (taintedSSANodes.count(d) != 0)
	  continue;
	taintedSSANodes.insert(d);
	varToVisit.push(d);
      }
    }
  }

  double t2 = gettime();

  floodDepTime += t2 - t1;
}

void
DepGraph::computeTaintedCalls() {
  double t1 = gettime();

  queue<const Function *> funcToVisit;

  for (auto I : condToCallEdges) {
    const Value *cond = I.first;
    if (taintedLLVMNodes.count(cond) == 0)
      continue;

    for (const Value *call : I.second) {
      taintedCallNodes.insert(call);
      funcToVisit.push(callToFuncEdges[call]);
    }
  }

 while (funcToVisit.size() > 0) {
   const Function *s = funcToVisit.front();
   funcToVisit.pop();

   for (const Value *d : funcToCallNodes[s]) {
     if (taintedCallNodes.count(d) != 0)
       continue;
     taintedCallNodes.insert(d);
     funcToVisit.push(callToFuncEdges[d]);
   }
 }

 double t2 = gettime();

 floodCallTime += t2 - t1;
}

void
DepGraph::printTimers() const {
  errs() << "Build graph time : " << buildGraphTime*1.0e3 << " ms\n";
  errs() << "Phi elimination time : " << phiElimTime*1.0e3 << " ms\n";
  errs() << "Flood dependencies time : " << floodDepTime*1.0e3 << " ms\n";
  errs() << "Flood calls PDF+ time : " << floodCallTime*1.0e3 << " ms\n";
  errs() << "Dot graph time : " << dotTime*1.0e3 << " ms\n";
}

bool
DepGraph::isTaintedCall(const CallInst *CI) {
  return taintedCallNodes.count(CI) != 0;
}

bool
DepGraph::isTaintedValue(const Value *v){
	if (taintedLLVMNodes.count(v) != 0)
	//	errs() << getCallValueLabel(v) << " IS tainted 1\n";
		return true;
	return false;
}

void
DepGraph::getTaintedCallConditions(const llvm::CallInst *call,
				   std::set<const llvm::Value *> &conditions) {
  std::set<const llvm::CallInst *> visitedCallSites;
  queue<const CallInst *> callsitesToVisit;
  callsitesToVisit.push(call);

  while (callsitesToVisit.size() > 0) {
    const CallInst *CS = callsitesToVisit.front();
    const Function *F = CS->getParent()->getParent();
    callsitesToVisit.pop();
    visitedCallSites.insert(CS);

    for (const Value *cond : callsiteToConds[CS])
      conditions.insert(cond);

    for (const Value *v : funcToCallSites[F]) {
      const CallInst *CS2 = cast<CallInst>(v);
      if (visitedCallSites.count(CS2) != 0)
	continue;
      callsitesToVisit.push(CS2);
    }
  }
}

void
DepGraph::getTaintedCallInterIPDF(const llvm::CallInst *call,
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

    for (const Value *v : funcToCallSites[F]) {
      const CallInst *CS2 = cast<CallInst>(v);
      if (visitedCallSites.count(CS2) != 0)
	continue;
      callsitesToVisit.push(CS2);
    }
  }
}

bool
DepGraph::areSSANodesEquivalent(MSSAVar *var1, MSSAVar *var2) {
  assert(var1);
  assert(var2);

  if (var1->def->type == MSSADef::PHI || var2->def->type == MSSADef::PHI)
    return false;

  VarSet incomingSSAsVar1;
  VarSet incomingSSAsVar2;

  ValueSet incomingValuesVar1;
  ValueSet incomingValuesVar2;

  // Check whether number of edges are the same for both nodes.
  if (ssaToSSAChildren[var1].size() != ssaToSSAChildren[var2].size())
    return false;

  if (ssaToLLVMChildren[var1].size() != ssaToLLVMChildren[var2].size())
    return false;

  if (ssaToSSAParents[var1].size() != ssaToSSAParents[var2].size())
    return false;

  if (ssaToLLVMParents[var1].size() != ssaToLLVMParents[var2].size())
    return false;


  // Check whether outgoing edges are the same for both nodes.
  for (MSSAVar *v : ssaToSSAChildren[var1]) {
    if (ssaToSSAChildren[var2].find(v) == ssaToSSAChildren[var2].end())
      return false;
  }
  for (const Value *v : ssaToLLVMChildren[var1]) {
    if (ssaToLLVMChildren[var2].find(v) == ssaToLLVMChildren[var2].end())
      return false;
  }

  // Check whether incoming edges are the same for both nodes.
  for (MSSAVar *v : ssaToSSAParents[var1]) {
    if (ssaToSSAParents[var2].find(v) == ssaToSSAParents[var2].end())
      return false;
  }
  for (const Value *v : ssaToLLVMParents[var1]) {
    if (ssaToLLVMParents[var2].find(v) == ssaToLLVMParents[var2].end())
      return false;
  }

  return true;
}

void
DepGraph::eliminatePhi(MSSAPhi *phi, MSSAVar *op1, MSSAVar *op2) {
  // Remove links from predicates to PHI
  for (const Value *v : phi->preds)
    removeEdge(v, phi->var);

  // Remove links from op1,op2 to PHI
  removeEdge(op1, phi->var);
  removeEdge(op2, phi->var);

  // For each outgoing edge from PHI to a SSA node N, connect
  // op1 to N and remove the link from PHI to N.
  for (MSSAVar *v : ssaToSSAChildren[phi->var]) {
    addEdge(op1, v);
    removeEdge(phi->var, v);

    // If N is a phi replace the phi operand of N with op1
    if (v->def->type == MSSADef::PHI) {
      MSSAPhi *outPHI = cast<MSSAPhi>(v->def);

      bool found = false;
      for (auto I = outPHI->opsVar.begin(), E = outPHI->opsVar.end(); I != E;
	   ++I) {
	if (I->second == phi->var) {
	  found = true;
	  I->second = op1;
	  break;
	}
      }
      assert(found);
    }
  }

  // For each outgoing edge from PHI to a LLVM node N, connect
  // connect op1 to N and remove the link from PHI to N.
  for (const Value *v : ssaToLLVMChildren[phi->var]) {
    addEdge(op1, v);
    removeEdge(phi->var, v);
  }

  // Remove PHI Node
  const Function *F = phi->var->bb->getParent();
  assert(F);
  auto it = funcToSSANodesMap[F].find(phi->var);
  assert(it != funcToSSANodesMap[F].end());
  funcToSSANodesMap[F].erase(it);

  // Remove edges connected to op2
  for (MSSAVar *v : ssaToSSAParents[op2])
    removeEdge(v, op2);
  for (const Value *v : ssaToLLVMParents[op2])
    removeEdge(v, op2);
  for (MSSAVar *v : ssaToSSAChildren[op2])
    removeEdge(op2, v);
  for (const Value *v : ssaToLLVMChildren[op2])
    removeEdge(op2, v);

  // Remove op2
  auto it2 = funcToSSANodesMap[F].find(op2);
  assert(it2 != funcToSSANodesMap[F].end());
  funcToSSANodesMap[F].erase(it2);
}


void
DepGraph::phiElimination() {
  double t1 = gettime();

  // For each function, iterate through its basic block and try to eliminate phi
  // function until reaching a fixed point.
  for (const Function &F : *mssa->m) {
    bool changed = true;

    while (changed) {
      changed = false;

      for (const BasicBlock &BB : F) {
	for (MSSAPhi *phi : mssa->bbToPhiMap[&BB]) {
	  if (funcToSSANodesMap[&F].count(phi->var) == 0)
	    continue;

	  // For each phi we test if its operands (chi) are not PHI and
	  // are equivalent
	  vector<MSSAVar *> phiOperands;
	  for (auto J : phi->opsVar)
	    phiOperands.push_back(J.second);

	  if (phiOperands.size() != 2)
	    continue;

	  assert(phiOperands[0] != phiOperands[1]);

	  if (!areSSANodesEquivalent(phiOperands[0], phiOperands[1]))
	    continue;

	  // PHI Node can be eliminated !
	  changed = true;
	  eliminatePhi(phi, phiOperands[0], phiOperands[1]);
	}
      }
    }
  }

  double t2 = gettime();
  phiElimTime += t2 - t1;
}

void
DepGraph::addEdge(const llvm::Value *s, const llvm::Value *d) {
  llvmToLLVMChildren[s].insert(d);
  llvmToLLVMParents[d].insert(s);
}

void
DepGraph::addEdge(const llvm::Value *s, MSSAVar *d) {
  llvmToSSAChildren[s].insert(d);
  ssaToLLVMParents[d].insert(s);
}

void
DepGraph::addEdge(MSSAVar *s, const llvm::Value *d) {
  ssaToLLVMChildren[s].insert(d);
  llvmToSSAParents[d].insert(s);
}

void
DepGraph::addEdge(MSSAVar *s, MSSAVar *d) {
  ssaToSSAChildren[s].insert(d);
  ssaToSSAParents[d].insert(s);
}

void
DepGraph::removeEdge(const llvm::Value *s, const llvm::Value *d) {
  int n;
  n = llvmToLLVMChildren[s].erase(d);
  assert(n == 1);
  n = llvmToLLVMParents[d].erase(s);
  assert(n == 1);
}

void
DepGraph::removeEdge(const llvm::Value *s, MSSAVar *d) {
  int n;
  n = llvmToSSAChildren[s].erase(d);
  assert(n == 1);
  n = ssaToLLVMParents[d].erase(s);
  assert(n == 1);
}

void
DepGraph::removeEdge(MSSAVar *s, const llvm::Value *d) {
  int n;
  n = ssaToLLVMChildren[s].erase(d);
  assert(n == 1);
  n = llvmToSSAParents[d].erase(s);
  assert(n == 1);
}

void
DepGraph::removeEdge(MSSAVar *s, MSSAVar *d) {
  int n;
  n = ssaToSSAChildren[s].erase(d);
  assert(n == 1);
  n = ssaToSSAParents[d].erase(s);
  assert(n == 1);
}
