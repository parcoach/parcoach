#include "DepGraph.h"
#include "Utils.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace std;

DepGraph::DepGraph(MemorySSA *mssa) : mssa(mssa) {}
DepGraph::~DepGraph() {}

void
DepGraph::buildFunction(const llvm::Function *F, PostDominatorTree *PDT) {
  curFunc = F;
  curPDT = PDT;

  funcNodes.insert(F);

  visit(*const_cast<Function *>(F));

  // Add entry chi to the graph.
  for (MSSAChi *chi : mssa->funToEntryChiMap[F]) {
    funcToSSANodesMap[F].insert(chi->var);

    if (chi->opVar) {
      funcToSSANodesMap[F].insert(chi->opVar);
      ssaToSSAEdges[chi->opVar].insert(chi->var);
    }
  }

  // If the function is MPI_Comm_rank or MPI_Group_rank set the address-taken ssa of the
  // second argument as a contamination source.
  if (F->getName().equals("MPI_Comm_rank") || F->getName().equals("MPI_Group_rank")) {
    const Argument *taintedArg = getFunctionArgument(F, 1);
    MemReg *reg = mssa->extArgToRegMap[taintedArg];
    MSSAMu *mu = mssa->funRegToReturnMuMap[F][reg];
    ssaSources.insert(mu->var);
  }
}

void
DepGraph::visitBasicBlock(llvm::BasicBlock &BB) {
  // Add MSSA Phi nodes and edges to the graph.
  for (MSSAPhi *phi : mssa->bbToPhiMap[&BB]) {
    funcToSSANodesMap[curFunc].insert(phi->var);

    for (auto I : phi->opsVar) {
      funcToSSANodesMap[curFunc].insert(I.second);
      ssaToSSAEdges[I.second].insert(phi->var);
    }

    for (const Value *pred : phi->preds) {
      funcToLLVMNodesMap[curFunc].insert(pred);
      llvmToSSAEdges[pred].insert(phi->var);
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
    llvmToLLVMEdges[v].insert(&I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraph::visitLoadInst(llvm::LoadInst &I) {
  // Load inst, connect MSSA mus and the pointer loaded.
  funcToLLVMNodesMap[curFunc].insert(&I);
  funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());

  for (MSSAMu *mu : mssa->loadToMuMap[&I]) {
    funcToSSANodesMap[curFunc].insert(mu->var);
    ssaToLLVMEdges[mu->var].insert(&I);
  }

  llvmToLLVMEdges[I.getPointerOperand()].insert(&I);
}

void
DepGraph::visitStoreInst(llvm::StoreInst &I) {
  // Store inst
  // For each chi, connect the pointer, the value stored and the MSSA operand.
  for (MSSAChi *chi : mssa->storeToChiMap[&I]) {
    funcToSSANodesMap[curFunc].insert(chi->var);
    funcToSSANodesMap[curFunc].insert(chi->opVar);
    funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());
    funcToLLVMNodesMap[curFunc].insert(I.getValueOperand());

    ssaToSSAEdges[chi->opVar].insert(chi->var);
    llvmToSSAEdges[I.getValueOperand()].insert(chi->var);
    llvmToSSAEdges[I.getPointerOperand()].insert(chi->var);
  }
}

void
DepGraph::visitGetElementPtrInst(llvm::GetElementPtrInst &I) {
  // GetElementPtr, connect operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    llvmToLLVMEdges[v].insert(&I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraph::visitPHINode(llvm::PHINode &I) {
  // Connect LLVM Phi, connect operands and predicates.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    llvmToLLVMEdges[v].insert(&I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }

  for (const Value *v : mssa->llvmPhiToPredMap[&I]) {
    llvmToLLVMEdges[v].insert(&I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraph::visitCastInst(llvm::CastInst &I) {
  // Cast inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    llvmToLLVMEdges[v].insert(&I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraph::visitSelectInst(llvm::SelectInst &I) {
  // Select inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    llvmToLLVMEdges[v].insert(&I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}
void
DepGraph::visitBinaryOperator(llvm::BinaryOperator &I) {
  // Binary op, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    llvmToLLVMEdges[v].insert(&I);
    funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraph::visitCallInst(llvm::CallInst &I) {
  /* Building rules for call sites :
   *
   * %c = call f (..., %a, ...)
   * [ mu(oa1) oa2 = chi(oa1) ]
   * [ oc2 = chi(oc1) ]
   *
   * define f (..., %b, ...) {
   *  [ ob0 = X(ob) ]
   *
   *  ...
   *
   *  [ mu(obn) ]
   *  [ mu(orn) ]
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
   * rule3: oa1 ------> ob0
   * rule4: oa1 ------> oa2
   * rule5: oa2 <------ obn
   * rule6: oc2 <------ oc1
   * rule7: oc2 <------ orn
   */

  // Chi of pointer parameters of the callsite.
  for (MSSAChi *chi : mssa->callSiteToChiMap[CallSite(&I)]) {
    funcToSSANodesMap[curFunc].insert(chi->opVar);
    funcToSSANodesMap[curFunc].insert(chi->var);
    ssaToSSAEdges[chi->opVar].insert(chi->var); // rule4

    MSSACallChi *callChi = cast<MSSACallChi>(chi);
    const Function *called = callChi->called;

    if (called->isDeclaration()) {
      const Argument *arg = getFunctionArgument(called, callChi->argNo);
      MemReg *reg = mssa->extArgToRegMap[arg];

      MSSAMu *returnMu = mssa->funRegToReturnMuMap[called][reg];
      funcToSSANodesMap[called].insert(returnMu->var);
      ssaToSSAEdges[returnMu->var].insert(chi->var); // rule5
      continue;
    }

    auto it = mssa->funRegToReturnMuMap.find(called);
    if (it != mssa->funRegToReturnMuMap.end()) {
      MSSAMu *returnMu = it->second[chi->region];

      funcToSSANodesMap[called].insert(returnMu->var);
      ssaToSSAEdges[returnMu->var].insert(chi->var); // rule5
    }
  }

  // Mu of pointer parameters of the call site.
  for (MSSAMu *mu : mssa->callSiteToMuMap[CallSite(&I)]) {
    funcToSSANodesMap[curFunc].insert(mu->var);

    MSSACallMu *callMu = cast<MSSACallMu>(mu);
    const Function *called = callMu->called;
    if (called->isDeclaration()) {
      const Argument *arg = getFunctionArgument(called, callMu->argNo);
      MemReg *reg = mssa->extArgToRegMap[arg];

      MSSAChi *entryChi = mssa->funRegToEntryChiMap[called][reg];
      funcToSSANodesMap[called].insert(entryChi->var);
      ssaToSSAEdges[mu->var].insert(entryChi->var); // rule3
      continue;
    }

    auto it = mssa->funRegToEntryChiMap.find(called);
    if (it != mssa->funRegToEntryChiMap.end()) {
      MSSAChi *entryChi = it->second[mu->region];

      funcToSSANodesMap[called].insert(entryChi->var);
      ssaToSSAEdges[callMu->var].insert(entryChi->var); // rule3
    }
  }

  // Connect effective parameters to formal parameters.
  const Function *called = I.getCalledFunction();
  unsigned argIdx = 0;
  for (const Argument &arg : called->getArgumentList()) {
    funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(argIdx));
    funcToLLVMNodesMap[called].insert(&arg);

    llvmToLLVMEdges[I.getArgOperand(argIdx)].insert(&arg); // rule1

    argIdx++;
  }

  // If the function called returns a value, connect the return value to the
  // call value.
  if (!called->isDeclaration() && !I.getType()->isVoidTy()) {
    funcToLLVMNodesMap[curFunc].insert(&I);
    llvmToLLVMEdges[getReturnValue(called)].insert(&I); // rule2
  }

  // If the function returns a pointer, connect the return mu the callee to the
  // return chi of the caller.
  for (MSSAChi *chi : mssa->callSiteToRetChiMap[CallSite(&I)]) {
    // Case where the region is not used by a store/load inside the called
    // function.
    if (mssa->funRegToReturnMuMap[called].count(chi->region) == 0)
      continue;

    ssaToSSAEdges[mssa->funRegToReturnMuMap[called][chi->region]->var]
      .insert(chi->var); // rule7
    funcToSSANodesMap[curFunc].insert(chi->var);
    funcToSSANodesMap[curFunc].insert(chi->opVar);
    ssaToSSAEdges[chi->opVar].insert(chi->var); // rule6
  }

  // Add call node
  funcToCallNodes[curFunc].insert(&I);

  // Add pred to call edges
  set<const Value *> preds = computeIPDFPredicates(*curPDT, I.getParent());
  for (const Value *pred : preds)
    condToCallEdges[pred].insert(&I);

  // Add call to func edge
  callToFuncEdges[&I] = I.getCalledFunction();
}

void
DepGraph::visitInstruction(llvm::Instruction &I) {
  errs() << "Error: Unhandled instruction " << I << "\n";
}

void
DepGraph::toDot(string filename) {
  computeTaintedValues();
  computeTaintedCalls();

  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph F {\n";
  stream << "compound=true;\n";
  stream << "rankdir=LR;\n";

  for (auto I = mssa->m->begin(), E = mssa->m->end(); I != E; ++I) {
    const Function *F = &*I;
    if (F->isDeclaration())
      dotExtFunction(stream, F);
    else
      dotFunction(stream, F);
  }

  // Edges
  for (auto I : llvmToLLVMEdges) {
    const Value *s = I.first;
    for (const Value *d : I.second) {
      stream << "Node" << ((void *) s) << " -> "
      	     << "Node" << ((void *) d) << "\n";
    }
  }

  for (auto I : llvmToSSAEdges) {
    const Value *s = I.first;
    for (MSSAVar *d : I.second) {
      stream << "Node" << ((void *) s) << " -> "
  	     << "Node" << ((void *) d) << "\n";
    }
  }

  for (auto I : ssaToSSAEdges) {
    MSSAVar *s = I.first;
    for (MSSAVar *d : I.second) {
      stream << "Node" << ((void *) s) << " -> "
  	     << "Node" << ((void *) d) << "\n";
    }
  }

  for (auto I : ssaToLLVMEdges) {
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
      	   << " [lhead=cluster_" << f->getName()
	   <<"]\n";
  }

  for (auto I : condToCallEdges) {
    const Value *s = I.first;
    for (const Value *call : I.second) {
      stream << "Node" << ((void *) s) << " -> "
  	     << "NodeCall" << ((void *) call) << "\n";
    }
  }

  stream << "}\n";
}

void
DepGraph::dotFunction(raw_fd_ostream &stream, const Function *F) {
  stream << "\tsubgraph cluster_" << F->getName() << " {\n";
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
	   << v->def->region->getName() << v->version
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
  stream << "\tsubgraph cluster_" << F->getName() << " {\n";
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
	   << v->def->region->getName() << v->version
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
  for(const MSSAVar *src : ssaSources) {
    taintedSSANodes.insert(src);
    computeTaintedValuesRec(const_cast<MSSAVar *>(src));
  }
}

void
DepGraph::computeTaintedValuesRec(MSSAVar *v) {
  for (MSSAVar *c : ssaToSSAEdges[v]) {
    if (taintedSSANodes.count(c) != 0)
      continue;
    taintedSSANodes.insert(c);
    computeTaintedValuesRec(c);
  }

  for (const Value *c : ssaToLLVMEdges[v]) {
    if (taintedLLVMNodes.count(c) != 0)
      continue;
    taintedLLVMNodes.insert(c);
    computeTaintedValuesRec(c);
  }
}

void
DepGraph::computeTaintedValuesRec(const Value *v) {
  for (MSSAVar *c : llvmToSSAEdges[v]) {
    if (taintedSSANodes.count(c) != 0)
      continue;
    taintedSSANodes.insert(c);
    computeTaintedValuesRec(c);
  }

  for (const Value *c : llvmToLLVMEdges[v]) {
    if (taintedLLVMNodes.count(c) != 0)
      continue;
    assert(taintedLLVMNodes.count(v) != 0);
    taintedLLVMNodes.insert(c);
    computeTaintedValuesRec(c);
  }
}

void
DepGraph::computeTaintedCalls() {
  for (auto I : condToCallEdges) {
    const Value *cond = I.first;
    if (taintedLLVMNodes.count(cond) == 0)
      continue;

    for (const Value *call : I.second) {
      taintedCallNodes.insert(call);
      computeTaintedCalls(callToFuncEdges[call]);
    }
  }
}

void
DepGraph::computeTaintedCalls(const Function *f) {
  for (const Value *v : funcToCallNodes[f]) {
    if (taintedCallNodes.count(v) != 0)
      continue;
    taintedCallNodes.insert(v);
    computeTaintedCalls(callToFuncEdges[v]);
  }
}

void
DepGraph::isTaintedCalls(const Function *F) {
  for(const Value *v : funcToCallNodes[F]){
	if (taintedCallNodes.count(v) != 0)
		errs() << getCallValueLabel(v) << " called in " << F->getName() << " IS tainted\n";
  }
}

bool
DepGraph::isTainted(const Value *v){
	if (taintedCallNodes.count(v) != 0)
		return true;
	return false;
}

