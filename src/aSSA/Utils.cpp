#include "Utils.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Debug.h"

using namespace llvm;
using namespace std;

const Argument *
getFunctionArgument(const Function *F, unsigned idx) {
  unsigned i = 0;

  for (auto ai = F->arg_begin(), ae = F->arg_end(); ai != ae; ++ai, ++i) {
    if (i == idx) {
      const Argument *arg = ai;
      return arg;
    }
  }

  errs() << "returning null, querying arg no " << idx << " on function "
	 << F->getName() << "\n";
  return NULL;
}

unsigned getNumArgs(const llvm::Function *F) {
  int i = 0;

  for (auto ai = F->arg_begin(), ae = F->arg_end(); ai != ae; ++ai)
    i++;

  return i;
}

bool
blockDominatesEntry(BasicBlock *BB, PostDominatorTree &PDT, DominatorTree *DT,
		    BasicBlock *EntryBlock) {
  if (PDT.dominates(BB, EntryBlock))
    return true;

  return false;
}

// PDF computation
vector<BasicBlock * >
postdominance_frontier(PostDominatorTree &PDT, BasicBlock *BB){
  vector<BasicBlock * > PDF;
  PDF.clear();
  DomTreeNode *DomNode = PDT.getNode(BB);

  for (auto it = pred_begin(BB), et = pred_end(BB); it != et; ++it){
    // does BB immediately dominate this predecessor?
    DomTreeNode *ID = PDT[*it]; //->getIDom();
    if(ID && ID->getIDom()!=DomNode && *it!=BB){
      PDF.push_back(*it);
    }
  }
  for (DomTreeNode::const_iterator NI = DomNode->begin(), NE = DomNode->end();
       NI != NE; ++NI) {
    DomTreeNode *IDominee = *NI;
    vector<BasicBlock * > ChildDF =
      postdominance_frontier(PDT, IDominee->getBlock());
    vector<BasicBlock * >::const_iterator CDFI
      = ChildDF.begin(), CDFE = ChildDF.end();
    for (; CDFI != CDFE; ++CDFI) {
      if (PDT[*CDFI]->getIDom() != DomNode && *CDFI!=BB){
	PDF.push_back(*CDFI);
      }
    }
  }
  return PDF;
}

void
print_iPDF(vector<BasicBlock * > iPDF, BasicBlock *BB){
  vector<BasicBlock * >::const_iterator Bitr;
  errs() << "iPDF(" << BB->getName().str() << ") = {";
  for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
    errs() << "- " << (*Bitr)->getName().str() << " ";
  }
  errs() << "}\n";
}

// PDF+ computation
vector<BasicBlock * >
iterated_postdominance_frontier(PostDominatorTree &PDT, BasicBlock *BB){
  vector<BasicBlock * > iPDF;

  iPDF=postdominance_frontier(PDT, BB);
  if(iPDF.size()==0)
    return iPDF;

  std::set<BasicBlock *> S;
  S.insert(iPDF.begin(), iPDF.end());

  std::set<BasicBlock *> toCompute;
  std::set<BasicBlock *> toComputeTmp;
  toCompute.insert(iPDF.begin(), iPDF.end());

  bool changed = true;
  while (changed) {
    changed = false;

    for (auto I = toCompute.begin(), E = toCompute.end(); I != E; ++I) {
      vector<BasicBlock *> tmp = postdominance_frontier(PDT, *I);
      for (auto J = tmp.begin(), F = tmp.end(); J != F; ++J) {
	if ((S.insert(*J)).second) {
	  toComputeTmp.insert(*J);
	  changed = true;
	}
      }
    }

    toCompute = toComputeTmp;
    toComputeTmp.clear();
  }

  iPDF.insert(iPDF.end(), S.begin(), S.end());

  return iPDF;
}
