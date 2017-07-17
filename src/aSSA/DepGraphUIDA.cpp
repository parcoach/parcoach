#include "DepGraphUIDA.h"
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

static vector<functionArg> valueSourceFunctions;
static vector<const char *> loadValueSources;
static vector<functionArg> resetFunctions;


DepGraphUIDA::DepGraphUIDA(PTACallGraph *CG, Pass *pass)
  : CG(CG), pass(pass),
    buildGraphTime(0), phiElimTime(0),
    floodDepTime(0), floodCallTime(0),
    dotTime(0) {

  if (optMpiTaint)
    enableMPI();
  if (optOmpTaint)
    enableOMP();
  if (optUpcTaint)
    enableUPC();
  if (optCudaTaint)
    enableCUDA();

  // Compute func to call sites map
  for (Function &F : CG->getModule()) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
	CallInst *CI = dyn_cast<CallInst>(&I);
	if (!CI)
	  continue;

	const Function *callee = CI->getCalledFunction();
	if (callee) {
	  funcToCallSites[callee].insert(CI);
	} else {
	  for (const Function *mayCallee : CG->indirectCallMap[&I]) {
	    funcToCallSites[mayCallee].insert(CI);
	  }
	}
      }
    }
  }
}

DepGraphUIDA::~DepGraphUIDA() {}

void
DepGraphUIDA::enableMPI() {
  valueSourceFunctions.push_back(functionArg("MPI_Comm_rank", 1));
  valueSourceFunctions.push_back(functionArg("MPI_Group_rank", 1));
}

void
DepGraphUIDA::enableOMP() {
  valueSourceFunctions.push_back(functionArg("__kmpc_global_thread_num", -1));
  valueSourceFunctions.push_back(functionArg("_omp_get_thread_num", -1));
  valueSourceFunctions.push_back(functionArg("omp_get_thread_num", -1));
}

void
DepGraphUIDA::enableUPC() {
  loadValueSources.push_back("gasneti_mynode");
}

void
DepGraphUIDA::enableCUDA() {
  valueSourceFunctions.
    push_back(functionArg("llvm.nvvm.read.ptx.sreg.tid.x", -1)); // threadIdx.x
  valueSourceFunctions.
    push_back(functionArg("llvm.nvvm.read.ptx.sreg.tid.y", -1)); // threadIdx.y
  valueSourceFunctions.
    push_back(functionArg("llvm.nvvm.read.ptx.sreg.tid.z", -1)); // threadIdx.z
}

void
DepGraphUIDA::buildFunction(const llvm::Function *F) {
  double t1 = gettime();

  curFunc = F;

  if (F->isDeclaration())
    curPDT = NULL;
  else
    curPDT =
      &pass->getAnalysis<PostDominatorTreeWrapperPass>
      (*const_cast<Function *>(F)).getPostDomTree();


  visit(*const_cast<Function *>(F));

  for (const Argument &arg : F->args()) {
    funcToLLVMNodesMap[F].insert(&arg);
    assert(!isa<GlobalValue>(&arg));
  }

  // External functions
  if (F->isDeclaration()) {
    // memcpy
    if (F->getName().find("memcpy") != StringRef::npos) {
      const Argument *src = getFunctionArgument(F, 1);
      const Argument *dst = getFunctionArgument(F, 0);
      addEdge(src, dst);
      // llvm.mempcy instrinsic returns void whereas memcpy returns dst
      if (F->getReturnType()->isPointerTy()) {
	for (const Value *cs : funcToCallSites[F])
	  addEdge(dst, cs);
      }
    }

    // memmove
    else if (F->getName().find("memmove") != StringRef::npos) {
      const Argument *src = getFunctionArgument(F, 1);
      const Argument *dst = getFunctionArgument(F, 0);
      addEdge(src, dst);
      // llvm.memmove instrinsic returns void whereas memmove returns dst
      if (F->getReturnType()->isPointerTy()) {
	for (const Value *cs : funcToCallSites[F])
	  addEdge(dst, cs);
      }
    }

    // memset
    else if (F->getName().find("memset") != StringRef::npos) {
      addEdge(getFunctionArgument(F, 1),
	      getFunctionArgument(F, 0));
      // llvm.memset instrinsic returns void whereas memset returns dst
      if (F->getReturnType()->isPointerTy()) {
	for (const Value *cs : funcToCallSites[F])
	  addEdge(getFunctionArgument(F, 0), cs);
      }
    }

    else if (F->getName().equals("MPI_Comm_rank")) {
      // Do not connect all arguments
    }

    else if (F->getName().equals("MPI_Group_rank")) {
      // Do not connect all arguments
    }

    // Unknown external function, we have to connect every input to every
    // output.
    else {
      // Connect LLVM arguments to LLVM arguments
      for (const Argument &arg1 : F->getArgumentList()) {
	funcToLLVMNodesMap[F].insert(&arg1);
	assert(!isa<GlobalValue>(&arg1));
	for (const Argument &arg2 : F->getArgumentList()) {
	  addEdge(&arg1, &arg2);
	}
      }

      if (F->getReturnType()->isPointerTy()) {
	for (const Argument &arg : F->getArgumentList()) {
	  for (const Value *cs : funcToCallSites[F]) {
	    addEdge(&arg, cs);
	  }
	}
      }
    }
  }

  // Value Source functions
  for (unsigned i=0; i<valueSourceFunctions.size(); ++i) {
    if (!F->getName().equals(valueSourceFunctions[i].name))
      continue;
    int argNo = valueSourceFunctions[i].arg;
    if (argNo == -1) {
      for (const Value *cs : funcToCallSites[F]) {
	valueSources.insert(cs);
	const Instruction *csInst = dyn_cast<Instruction>(cs);
	if (!csInst)
	  continue;
	funcToLLVMNodesMap[csInst->getParent()->getParent()].insert(cs);
      }
    } else {
      valueSources.insert(getFunctionArgument(F, argNo));
      funcToLLVMNodesMap[F].insert(getFunctionArgument(F, argNo));
      assert(!isa<GlobalValue>(getFunctionArgument(F, argNo)));
    }
  }

  double t2 = gettime();

  buildGraphTime += t2 - t1;
}

void
DepGraphUIDA::visitBasicBlock(llvm::BasicBlock &BB) {
  // Do nothing
}

void
DepGraphUIDA::visitAllocaInst(llvm::AllocaInst &I) {
  // Do nothing
}

void
DepGraphUIDA::visitTerminatorInst(llvm::TerminatorInst &I) {
  // Do nothing
}

void
DepGraphUIDA::visitCmpInst(llvm::CmpInst &I) {
  // Cmp instruction is a value, connect the result to its operands.
  funcToLLVMNodesMap[curFunc].insert(&I);
  assert(!isa<GlobalValue>(&I));

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);
  }
}

void
DepGraphUIDA::visitLoadInst(llvm::LoadInst &I) {
  funcToLLVMNodesMap[curFunc].insert(&I);
  if (!isa<GlobalValue>(I.getPointerOperand()))
    funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());

  addEdge(I.getPointerOperand(), &I);

  if (I.getType()->isPointerTy())
      addEdge(&I, I.getPointerOperand());

  // Load value rank source
  for (unsigned i=0; i<loadValueSources.size(); i++) {
    if (I.getPointerOperand()->getName().equals(loadValueSources[i])) {
      valueSources.insert(&I);
      break;
    }
  }
}

void
DepGraphUIDA::visitStoreInst(llvm::StoreInst &I) {
  if (!isa<GlobalValue>(I.getValueOperand()))
    funcToLLVMNodesMap[curFunc].insert(I.getValueOperand());
  if (!isa<GlobalValue>(I.getPointerOperand()))
    funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());
  addEdge(I.getValueOperand(), I.getPointerOperand());

  if (I.getValueOperand()->getType()->isPointerTy())
    addEdge(I.getPointerOperand(), I.getValueOperand());
}

void
DepGraphUIDA::visitGetElementPtrInst(llvm::GetElementPtrInst &I) {
  // GetElementPtr, connect operands.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);
  }

  if (I.getType()->isPointerTy()) {
    if (!isa<GlobalValue>(I.getPointerOperand()))
      funcToLLVMNodesMap[curFunc].insert(I.getPointerOperand());
    addEdge(&I, I.getPointerOperand());
  }
}
void
DepGraphUIDA::visitPHINode(llvm::PHINode &I) {
  // Compute PHI predicates
  for (unsigned i=0; i<I.getNumIncomingValues(); ++i) {
    // Get IPDF
    vector<BasicBlock *> IPDF =
      iterated_postdominance_frontier(*curPDT,
				      I.getIncomingBlock(i));

    for (unsigned n = 0; n < IPDF.size(); ++n) {
      // Push conditions of each BB in the IPDF
      const TerminatorInst *ti = IPDF[n]->getTerminator();
      assert(ti);

      if (isa<BranchInst>(ti)) {
	const BranchInst *bi = cast<BranchInst>(ti);
	assert(bi);

	if (bi->isUnconditional())
	  continue;

	const Value *cond = bi->getCondition();

	llvmPhiToPredMap[&I].insert(cond);
      } else if(isa<SwitchInst>(ti)) {
	const SwitchInst *si = cast<SwitchInst>(ti);
	assert(si);
	const Value *cond = si->getCondition();
	llvmPhiToPredMap[&I].insert(cond);
      }
    }
  }

  // Connect LLVM Phi, connect operands and predicates.
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);
    addEdge(v, &I);

    if (I.getType()->isPointerTy())
      addEdge(&I, v);
  }

  // Connect predicates
  for (const Value *v : llvmPhiToPredMap[&I]) {
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);
    addEdge(v, &I);
  }
}

void
DepGraphUIDA::visitCastInst(llvm::CastInst &I) {
  // Cast inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);

    if (I.getType()->isPointerTy())
      addEdge(&I, v);
  }
}
void
DepGraphUIDA::visitSelectInst(llvm::SelectInst &I) {
  // Select inst, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);

    if (I.getType()->isPointerTy())
      addEdge(&I, v);
  }
}
void
DepGraphUIDA::visitBinaryOperator(llvm::BinaryOperator &I) {
  // Binary op, connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);

    if (I.getType()->isPointerTy())
      addEdge(&I, v);
  }
}

void
DepGraphUIDA::visitCallInst(llvm::CallInst &I) {
  if (isIntrinsicDbgInst(&I))
    return;

  connectCSEffectiveParameters(I);
  connectCSCalledReturnValue(I);

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
}

void
DepGraphUIDA::connectCSEffectiveParameters(llvm::CallInst &I) {
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
      if (!isa<GlobalValue>(I.getArgOperand(argIdx)))
	funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(argIdx));
      funcToLLVMNodesMap[callee].insert(&arg);

      addEdge(I.getArgOperand(argIdx), &arg); // rule1

      if (arg.getType()->isPointerTy())
	addEdge(&arg, I.getArgOperand(argIdx));

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
	if (!isa<GlobalValue>(I.getArgOperand(argIdx)))
	  funcToLLVMNodesMap[curFunc].insert(I.getArgOperand(argIdx));
	funcToLLVMNodesMap[callee].insert(&arg);

	addEdge(I.getArgOperand(argIdx), &arg); // rule1

	if (arg.getType()->isPointerTy())
	  addEdge(&arg, I.getArgOperand(argIdx));

	argIdx++;
      }
    }
  }
}

void
DepGraphUIDA::connectCSEffectiveParametersExt(CallInst &I, const Function *callee) {
  for (unsigned i=0; i<I.getNumArgOperands(); i++) {
    Value *op = I.getArgOperand(i);
    addEdge(op, getFunctionArgument(callee, i));
    if (op->getType()->isPointerTy())
      addEdge(getFunctionArgument(callee, i), op);
  }
}

void
DepGraphUIDA::connectCSCalledReturnValue(llvm::CallInst &I) {
  // If the function called returns a value, connect the return value to the
  // call value.

  const Function *callee = I.getCalledFunction();

  // direct call
  if (callee) {
    if (!callee->isDeclaration() && !callee->getReturnType()->isVoidTy()) {
      funcToLLVMNodesMap[curFunc].insert(&I);

      const Value *retVal = getReturnValue(callee);
      addEdge(retVal, &I); // rule2
      if (retVal->getType()->isPointerTy())
	addEdge(&I, retVal);
    }
  }

  // indirect call
  else {
    for (const Function *mayCallee : CG->indirectCallMap[&I]) {
      if (!mayCallee->isDeclaration() &&
	  !mayCallee->getReturnType()->isVoidTy()) {
	funcToLLVMNodesMap[curFunc].insert(&I);
	const Value *retVal = getReturnValue(mayCallee);
	addEdge(retVal, &I); // rule2
	if (retVal->getType()->isPointerTy())
	  addEdge(&I, retVal);
      }
    }
  }
}


void
DepGraphUIDA::visitExtractValueInst(llvm::ExtractValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);
  }

  if (I.getType()->isPointerTy()) {
    addEdge(&I, I.getAggregateOperand());
    if (!isa<GlobalValue>(I.getAggregateOperand()))
      funcToLLVMNodesMap[curFunc].insert(I.getAggregateOperand());
  }
}

void
DepGraphUIDA::visitExtractElementInst(llvm::ExtractElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);
  }

  if (I.getType()->isPointerTy()) {
    addEdge(&I, I.getVectorOperand());
    if (!isa<GlobalValue>(I.getVectorOperand()))
      funcToLLVMNodesMap[curFunc].insert(I.getVectorOperand());
  }
}

void
DepGraphUIDA::visitInsertElementInst(llvm::InsertElementInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);

    if (v->getType()->isPointerTy())
      addEdge(&I, v);
  }
}

void
DepGraphUIDA::visitInsertValueInst(llvm::InsertValueInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);

    if (v->getType()->isPointerTy())
      addEdge(&I, v);
  }
}

void
DepGraphUIDA::visitShuffleVectorInst(llvm::ShuffleVectorInst &I) {
  // Connect operands
  funcToLLVMNodesMap[curFunc].insert(&I);

  for (const Value *v : I.operands()) {
    addEdge(v, &I);
    if (!isa<GlobalValue>(v))
      funcToLLVMNodesMap[curFunc].insert(v);

    if (v->getType()->isPointerTy())
      addEdge(&I, v);
  }
}

void
DepGraphUIDA::visitInstruction(llvm::Instruction &I) {
  errs() << "Error: Unhandled instruction " << I << "\n";
}

void
DepGraphUIDA::toDot(string filename) {
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
  for (const Value &g : CG->getModule().globals()) {
    stream << "Node" << ((void *) &g) << " [label=\""
	   << getValueLabel(&g) << "\" "
	   << getNodeStyle(&g) << "];\n";
  }
  stream << "}\n;";

  for (auto I = CG->getModule().begin(), E = CG->getModule().end();
       I != E; ++I) {
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
DepGraphUIDA::dotFunction(raw_fd_ostream &stream, const Function *F) {
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
DepGraphUIDA::dotExtFunction(raw_fd_ostream &stream, const Function *F) {
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

  stream << "Node" << ((void *) F) << " [style=invisible];\n";

  stream << "}\n";
}

std::string
DepGraphUIDA::getNodeStyle(const llvm::Value *v) {
  if (taintedLLVMNodes.count(v) != 0)
    return "style=filled, color=red";
  return "style=filled, color=white";
}

std::string
DepGraphUIDA::getNodeStyle(const Function *f) {
  return "style=filled, color=white";
}

std::string
DepGraphUIDA::getCallNodeStyle(const llvm::Value *v) {
  return "style=filled, color=white";
}


void
DepGraphUIDA::computeTaintedValuesContextInsensitive() {
  unsigned funcToLLVMNodesMapSize = funcToLLVMNodesMap.size();
  unsigned varArgNodeSize = varArgNodes.size();
  unsigned llvmToLLVMChildrenSize = llvmToLLVMChildren.size();
  unsigned llvmToLLVMParentsSize = llvmToLLVMParents.size();
  unsigned funcToCallNodesSize = funcToCallNodes.size();
  unsigned callToFuncEdgesSize = callToFuncEdges.size();
  unsigned condToCallEdgesSize = condToCallEdges.size();
  unsigned funcToCallSitesSize = funcToCallSites.size();
  unsigned callsiteToCondsSize = callsiteToConds.size();

  double t1 = gettime();

  std::queue<const Value *> valueToVisit;

  // Value sources
  for(const Value *src : valueSources) {
    taintedLLVMNodes.insert(src);
    valueToVisit.push(src);
  }

  while (valueToVisit.size() > 0) {
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
  }

  double t2 = gettime();

  for (const Value *v : taintedLLVMNodes) {
    taintedConditions.insert(v);
  }

  floodDepTime += t2 - t1;
  assert(funcToLLVMNodesMapSize == funcToLLVMNodesMap.size());
  assert(varArgNodeSize == varArgNodes.size());
  assert(llvmToLLVMChildrenSize == llvmToLLVMChildren.size());
  assert(llvmToLLVMParentsSize == llvmToLLVMParents.size());
  assert(funcToCallNodesSize == funcToCallNodes.size());
  assert(callToFuncEdgesSize == callToFuncEdges.size());
  assert(condToCallEdgesSize == condToCallEdges.size());
  assert(funcToCallSitesSize == funcToCallSites.size());
  assert(callsiteToCondsSize == callsiteToConds.size());
}


void
DepGraphUIDA::printTimers() const {
  errs() << "Build graph time : " << buildGraphTime*1.0e3 << " ms\n";
  errs() << "Flood dependencies time : " << floodDepTime*1.0e3 << " ms\n";
  errs() << "Flood calls PDF+ time : " << floodCallTime*1.0e3 << " ms\n";
  errs() << "Dot graph time : " << dotTime*1.0e3 << " ms\n";
}

bool
DepGraphUIDA::isTaintedValue(const Value *v){
  return taintedConditions.find(v) != taintedConditions.end();
}

void
DepGraphUIDA::getCallInterIPDF(const llvm::CallInst *call,
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

void
DepGraphUIDA::addEdge(const llvm::Value *s, const llvm::Value *d) {
  assert(s);
  assert(d);

  if (llvmToLLVMChildren.find(s) != llvmToLLVMChildren.end() &&
      llvmToLLVMChildren[s].find(d) != llvmToLLVMChildren[s].end())
    return;

  if (s) {
    const Instruction *inst_s = dyn_cast<Instruction>(s);
    if (inst_s)
      funcToLLVMNodesMap[inst_s->getParent()->getParent()].insert(s);
  }

  if (d) {
    const Instruction *inst_d = dyn_cast<Instruction>(d);
    if (inst_d)
      funcToLLVMNodesMap[inst_d->getParent()->getParent()].insert(d);
  }

  llvmToLLVMChildren[s].insert(d);
  llvmToLLVMParents[d].insert(s);

  if (isa<ConstantExpr>(s)) {
    const ConstantExpr *CE = cast<ConstantExpr>(s);
    for (const Value *op : CE->operands()) {
      addEdge(op, CE);
      if (op->getType()->isPointerTy())
	addEdge(CE, op);
    }
  }

  if (isa<ConstantExpr>(d)) {
    const ConstantExpr *CE = cast<ConstantExpr>(d);
    for (const Value *op : CE->operands()) {
      addEdge(op, CE);
      if (op->getType()->isPointerTy())
	addEdge(CE, op);
    }
  }
}

void
DepGraphUIDA::removeEdge(const llvm::Value *s, const llvm::Value *d) {
  int n;
  n = llvmToLLVMChildren[s].erase(d);
  assert(n == 1);
  n = llvmToLLVMParents[d].erase(s);
  assert(n == 1);
}

void
DepGraphUIDA::dotTaintPath(const Value *v, string filename,
		       const Instruction *collective) {
  errs() << "Writing '" << filename << "' ...\n";

  // Parcours en largeur
  unsigned curDist = 0;
  unsigned curSize = 128;
  std::vector<std::set<const Value *> > visitedLLVMNodesByDist;
  std::set<const Value *> visitedLLVMNodes;

  visitedLLVMNodesByDist.resize(curSize);

  visitedLLVMNodes.insert(v);

  for (const Value *p : llvmToLLVMParents[v]) {
    if (visitedLLVMNodes.find(p) != visitedLLVMNodes.end())
      continue;

    if (taintedLLVMNodes.find(p) == taintedLLVMNodes.end())
      continue;

    visitedLLVMNodesByDist[curDist].insert(p);
  }

  bool stop = false;
  const Value *llvmRoot = NULL;

  while (true) {
    if (curDist >= curSize) {
      curSize *=2;
      visitedLLVMNodesByDist.resize(curSize);
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

  visitedLLVMNodes.clear();

  assert(llvmRoot);

  visitedLLVMNodes.insert(llvmRoot);

  string tmpStr;
  raw_string_ostream strStream(tmpStr);

  const Value *lastValue = llvmRoot;
  DGDebugLoc DL;

  debugMsgs.push_back(getStringMsg(lastValue));
  if (getDGDebugLoc(lastValue, DL))
    debugLocs.push_back(DL);

  // Compute edges of the shortest path to strStream
  for (unsigned i=curDist-1; i>0; i--) {
    bool found = false;

    for (const Value *v : visitedLLVMNodesByDist[i]) {
      if (llvmToLLVMParents[v].find(lastValue) == llvmToLLVMParents[v].end())
	continue;

      visitedLLVMNodes.insert(v);
      strStream << "Node" << ((void *) lastValue) << " -> "
		<< "Node" << ((void *) v) << "\n";
      lastValue = v;
      found = true;
      debugMsgs.push_back(getStringMsg(v));
      if (getDGDebugLoc(v, DL))
	debugLocs.push_back(DL);
      break;
    }

    assert(found);
  }

  // compute visited functions
  std::set<const Function *> visitedFunctions;
  for (auto I : funcToLLVMNodesMap) {
    for (const Value *v : I.second) {
      if (visitedLLVMNodes.find(v) != visitedLLVMNodes.end())
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
DepGraphUIDA::getStringMsg(const Value *v) {
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

bool
DepGraphUIDA::getDGDebugLoc(const Value *v, DGDebugLoc &DL) {
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

static bool getStrLine(ifstream &file, int line, string &str)  {
    // go to line
    file.seekg(std::ios::beg);
    for (int i=0; i < line-1; ++i)
      file.ignore(std::numeric_limits<std::streamsize>::max(),'\n');

    getline(file, str);

    return true;
}

void
DepGraphUIDA::reorderAndRemoveDup(vector<DGDebugLoc> &DLs) {
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
DepGraphUIDA::getDebugTrace(vector<DGDebugLoc> &DLs, string &trace,
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
DepGraphUIDA::floodFunction(const Function *F) {
  std::queue<const Value *> valueToVisit;

  // 1) taint LLVM sources
  for (const Value *s : valueSources) {
    const Argument *arg = dyn_cast<Argument>(s);
    if (arg && arg->getParent() == F) {
      taintedLLVMNodes.insert(s);
      //      errs() << "TTTAINTING " << *arg << "\n";
      continue;
    }

    const Instruction *inst = dyn_cast<Instruction>(s);

    if (!inst || inst->getParent()->getParent() != F)
      continue;

    taintedLLVMNodes.insert(s);
  }

  // 2) Add tainted variables of the function to the queue.
  if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
    for (const Value *v : funcToLLVMNodesMap[F]) {
      if (taintedLLVMNodes.find(v) != taintedLLVMNodes.end()) {
	valueToVisit.push(v);
      }
    }
  }

  // 3) Add tainted variables from globals
  for (Value &v : CG->getModule().globals()) {
    if (taintedLLVMNodes.find(&v) == taintedLLVMNodes.end())
      continue;

    if (llvmToLLVMChildren.find(&v) != llvmToLLVMChildren.end()) {
      for (const Value *d : llvmToLLVMChildren[&v]) {
	if (funcToLLVMNodesMap.find(F) == funcToLLVMNodesMap.end())
	  continue;
	if (funcToLLVMNodesMap[F].find(d) == funcToLLVMNodesMap[F].end())
	  continue;
	//      assert(false);
	taintedLLVMNodes.insert(d);
	valueToVisit.push(d);
      }
    }
  }

  // 3) flood function
  while (valueToVisit.size() > 0) {
    if (valueToVisit.size() > 0) {
      const Value *s = valueToVisit.front();
      valueToVisit.pop();

      //      errs() << "visiting " << *s << "\n";

      if (llvmToLLVMChildren.find(s) != llvmToLLVMChildren.end()) {

	for (const Value *d : llvmToLLVMChildren[s]) {
	  if (isa<GlobalValue>(d)) {
	    taintedLLVMNodes.insert(d);
	    valueToVisit.push(d);
	    continue;
	  }

	  //	  errs() << "d = " << *d << "\n";
	  if (funcToLLVMNodesMap.find(F) == funcToLLVMNodesMap.end())
	    continue;

	  if (funcToLLVMNodesMap[F].find(d) == funcToLLVMNodesMap[F].end())
	    continue;

	  if (taintedLLVMNodes.count(d) != 0)
	    continue;

	  taintedLLVMNodes.insert(d);
	  //	  errs() << "TTAINTING " << *d << "\n";
	  valueToVisit.push(d);
	}
      }
    }
  }
}

void
DepGraphUIDA::floodFunctionFromFunction(const Function *to, const Function *from) {
  if (funcToLLVMNodesMap.find(from) != funcToLLVMNodesMap.end()) {
    for (const Value *s : funcToLLVMNodesMap[from]) {
      if (taintedLLVMNodes.find(s) == taintedLLVMNodes.end())
	continue;

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
DepGraphUIDA::resetFunctionTaint(const Function *F) {
  assert(CG->isReachableFromEntry(F));
  if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
    for (const Value *v : funcToLLVMNodesMap[F]) {

      // Do not reset taint for constant
      if (isa<Constant>(v))
	continue;

      if (funcToLLVMNodesMap.find(F) != funcToLLVMNodesMap.end()) {
	taintedLLVMNodes.erase(v);
      }
    }
  }
}

void
DepGraphUIDA::computeFunctionCSTaintedConds(const llvm::Function *F) {
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
DepGraphUIDA::computeTaintedValuesContextSensitive() {
  unsigned funcToLLVMNodesMapSize = funcToLLVMNodesMap.size();
  unsigned varArgNodeSize = varArgNodes.size();
  unsigned llvmToLLVMChildrenSize = llvmToLLVMChildren.size();
  unsigned llvmToLLVMParentsSize = llvmToLLVMParents.size();
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
  assert(varArgNodeSize == varArgNodes.size());
  assert(llvmToLLVMChildrenSize == llvmToLLVMChildren.size());
  assert(llvmToLLVMParentsSize == llvmToLLVMParents.size());
  assert(funcToCallNodesSize == funcToCallNodes.size());
  assert(callToFuncEdgesSize == callToFuncEdges.size());
  assert(condToCallEdgesSize == condToCallEdges.size());
  assert(funcToCallSitesSize == funcToCallSites.size());
  assert(callsiteToCondsSize == callsiteToConds.size());
}

void
DepGraphUIDA::computeTaintedValuesCSForEntry(PTACallGraphNode *entry) {
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
