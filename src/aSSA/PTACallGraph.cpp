#include "PTACallGraph.h"

#include "llvm/IR/Module.h"

using namespace llvm;

#include <queue>

PTACallGraph::PTACallGraph(llvm::Module &M, Andersen *AA)
  : M(M), AA(AA), Root(nullptr), ProgEntry(nullptr),
    ExternalCallingNode(getOrInsertFunction(nullptr)),
    CallsExternalNode(llvm::make_unique<PTACallGraphNode>(nullptr)) {

  for (Function &F : M)
    addToCallGraph(&F);

  if (!Root)
    Root = ExternalCallingNode;

  if (!ProgEntry) {
    errs() << "Warning: no main function in module\n";
  } else {
    // Compute reachable functions from main
    std::queue<PTACallGraphNode *> toVisit;
    std::set<PTACallGraphNode *> visited;

    toVisit.push(ProgEntry);
    visited.insert(ProgEntry);

    while (!toVisit.empty()) {
      PTACallGraphNode *N = toVisit.front();
      toVisit.pop();

      Function *F = N->getFunction();
      if (F)
	reachableFunctions.insert(F);

      for (auto I = N->begin(), E = N->end(); I != E; ++I) {
	PTACallGraphNode *calleeNode = I->second;
	assert(calleeNode);
	if (visited.find(calleeNode) == visited.end()) {
	  visited.insert(calleeNode);
	  toVisit.push(calleeNode);
	}
      }
    }
  }
}

PTACallGraph::~PTACallGraph() {
  // TODO
}

void
PTACallGraph::addToCallGraph(Function *F) {
  PTACallGraphNode *Node = getOrInsertFunction(F);

  // If this function has external linkage, anything could call it.
  if (!F->hasLocalLinkage()) {
    ExternalCallingNode->addCalledFunction(CallSite(), Node);

    // Found the entry point?
    if (F->getName() == "main") {
      if (Root) // Found multiple external mains?  Don't pick one.
        Root = ExternalCallingNode;
      else
        Root = Node; // Found a main, keep track of it!

      ProgEntry = Node;
    }
  }

  // If this function has its address taken, anything could call it.
  if (F->hasAddressTaken())
    ExternalCallingNode->addCalledFunction(CallSite(), Node);

  // If this function is not defined in this translation unit, it could call
  // anything.
  if (F->isDeclaration() && !F->isIntrinsic())
    Node->addCalledFunction(CallSite(), CallsExternalNode.get());

  // Look for calls by this function.
  for (BasicBlock &BB : *F)
    for (Instruction &I : BB) {
      if (auto CS = CallSite(&I)) {
        const Function *Callee = CS.getCalledFunction();

        if (!Callee || !Intrinsic::isLeaf(Callee->getIntrinsicID()))
          // Indirect calls of intrinsics are not allowed so no need to check.
          // We can be more precise here by using TargetArg returned by
          // Intrinsic::isLeaf.
          Node->addCalledFunction(CS, CallsExternalNode.get());
        else if (!Callee->isIntrinsic())
          Node->addCalledFunction(CS, getOrInsertFunction(Callee));

	// Indirect calls
	if (!Callee && isa<CallInst>(I)) {
	  CallInst &CI = cast<CallInst>(I);
	  const Value *calledValue = CI.getCalledValue();
	  assert(calledValue);

	  std::vector<const Value *> ptsSet;
	  if (!AA->getPointsToSet(calledValue, ptsSet)) {
	    errs() << "coult not compute points to set for call inst : "
		   << I << "\n";
	    continue;
	  }

	  bool found = false;
	  for (const Value *v : ptsSet) {
	    Callee = dyn_cast<Function>(v);
	    if (!Callee)
	      continue;

	    if (CS.arg_size() != Callee->arg_size())
	      continue;

	    found = true;

	    indirectCallMap[&CI].insert(Callee);

	    if (Intrinsic::isLeaf(Callee->getIntrinsicID()))
	      Node->addCalledFunction(CS, getOrInsertFunction(Callee));
	  }

	  if (!found)
	    errs() << "could not find called function for call inst : "
		   << I << "\n";
	}
      }
    }
}

bool
PTACallGraph::isReachableFromEntry(const Function *F) const {
  return !ProgEntry || reachableFunctions.find(F) != reachableFunctions.end();
}

PTACallGraphNode *
PTACallGraph::getOrInsertFunction(const llvm::Function *F) {
  auto &CGN = FunctionMap[F];
  if (CGN)
    return CGN.get();

  assert((!F || F->getParent() == &M) && "Function not in current module!");
  CGN = llvm::make_unique<PTACallGraphNode>(const_cast<Function *>(F));
  return CGN.get();
}
