#include "PTACallGraph.h"
#include "parcoach/andersen/Andersen.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include <memory>
#include <queue>

using namespace llvm;

AnalysisKey PTACallGraphAnalysis::Key;
PTACallGraphAnalysis::Result
PTACallGraphAnalysis::run(Module &M, ModuleAnalysisManager &AM) {
  Andersen const &AA = AM.getResult<AndersenAA>(M);
  return std::make_unique<PTACallGraph>(M, AA);
}

PTACallGraph::PTACallGraph(llvm::Module const &M, Andersen const &AA)
    : AA(AA), Root(nullptr), ProgEntry(nullptr),
      ExternalCallingNode(getOrInsertFunction(nullptr)),
      CallsExternalNode(std::make_unique<PTACallGraphNode>(nullptr)) {

  for (Function const &F : M)
    addToCallGraph(F);

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

void PTACallGraph::addToCallGraph(Function const &F) {
  PTACallGraphNode *Node = getOrInsertFunction(&F);

  // If this function has external linkage, anything could call it.
  if (!F.hasLocalLinkage()) {
    ExternalCallingNode->addCalledFunction(nullptr, Node);

    // Found the entry point?
    if (F.getName() == "main") {
      assert(!Root && "there should be at most one main");
      Root = Node; // Found a main, keep track of it!

      ProgEntry = Node;
    }
  }

  // If this function has its address taken, anything could call it.
  if (F.hasAddressTaken())
    ExternalCallingNode->addCalledFunction(nullptr, Node);

  // If this function is not defined in this translation unit, it could call
  // anything.
  if (F.isDeclaration() && !F.isIntrinsic())
    Node->addCalledFunction(nullptr, CallsExternalNode.get());

  // Look for calls by this function.
  for (BasicBlock const &BB : F)
    for (Instruction const &I : BB) {
      if (auto *CI = dyn_cast<CallInst>(&I)) {
        const Function *Callee = CI->getCalledFunction();

        if (!Callee || !Intrinsic::isLeaf(Callee->getIntrinsicID()))
          // Indirect calls of intrinsics are not allowed so no need to check.
          // We can be more precise here by using TargetArg returned by
          // Intrinsic::isLeaf.
          Node->addCalledFunction(CI, CallsExternalNode.get());
        else if (!Callee->isIntrinsic())
          Node->addCalledFunction(CI, getOrInsertFunction(Callee));

        // Indirect calls
        if (!Callee) {
          const Value *calledValue =
              CI->getCalledOperand(); // CI.getCalledValue();
          assert(calledValue);

          std::vector<const Value *> ptsSet;

          bool Found = AA.getPointsToSet(calledValue, ptsSet);
          assert(Found && "coult not compute points to set for call inst");

          Found = false;
          auto IsCandidateF = [&](Value const *V) {
            auto *CandidateF = dyn_cast<Function>(V);
            if (!CandidateF) {
              return false;
            }
            return CI->arg_size() == CandidateF->arg_size() ||
                   (CandidateF->isVarArg() &&
                    CI->arg_size() > CandidateF->arg_size());
          };
          for (const Value *v : make_filter_range(ptsSet, IsCandidateF)) {
            auto *LocalCallee = cast<Function>(v);
            Found = true;

            indirectCallMap[CI].insert(LocalCallee);

            if (Intrinsic::isLeaf(LocalCallee->getIntrinsicID()))
              Node->addCalledFunction(CI, getOrInsertFunction(LocalCallee));
          }
          assert(Found && "could not find called function for call inst");
          (void)Found;
        }
      }
    }
}

bool PTACallGraph::isReachableFromEntry(const Function &F) const {
  return !ProgEntry || reachableFunctions.find(&F) != reachableFunctions.end();
}

PTACallGraphNode *PTACallGraph::getOrInsertFunction(const llvm::Function *F) {
  auto &CGN = FunctionMap[F];
  if (CGN)
    return CGN.get();

  CGN = std::make_unique<PTACallGraphNode>(const_cast<Function *>(F));
  // LLVM10: CGN = std::make_unique<PTACallGraphNode>(const_cast<Function
  // *>(F));
  return CGN.get();
}

void PTACallGraphNode::addCalledFunction(CallBase const *CB,
                                         PTACallGraphNode *M) {
  CalledFunctions.emplace_back(CB, M);
}
