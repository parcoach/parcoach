#include "Utils.h"

#include <sys/time.h>

#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace std;


bool isCallSite(const llvm::Instruction *inst) {
  return llvm::isa<llvm::CallInst>(inst) || llvm::isa<llvm::InvokeInst>(inst);
}



std::string getValueLabel(const llvm::Value *v) {
  std::string label;
  llvm::raw_string_ostream rso(label);
  v->print(rso);
  size_t pos = label.find_first_of('=');
  if (pos != std::string::npos)
    label = label.substr(0, pos);
  return label;
}

std::string getCallValueLabel(const llvm::Value *v) {
  std::string label;
  llvm::raw_string_ostream rso(label);
  v->print(rso);
  size_t pos = label.find_first_of('=');
  if (pos != std::string::npos)
    label = label.substr(pos+2);
  return label;
}

/*
 * POSTDOMINANCE
 */

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

void
print_iPDF(vector<BasicBlock * > iPDF, BasicBlock *BB){
  vector<BasicBlock * >::const_iterator Bitr;
  errs() << "iPDF(" << BB->getName().str() << ") = {";
  for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
    errs() << "- " << (*Bitr)->getName().str() << " ";
  }
  errs() << "}\n";
}



const Argument *
getFunctionArgument(const Function *F, unsigned idx) {
  unsigned i = 0;

  for (const Argument &arg : F->getArgumentList()) {
    if (i == idx) {
      return &arg;
    }

    i++;
  }

  errs() << "returning null, querying arg no " << idx << " on function "
	 << F->getName() << "\n";
  return NULL;
}

std::set<const llvm::Value *>
computeIPDFPredicates(llvm::PostDominatorTree &PDT,
		      llvm::BasicBlock *BB) {
  std::set<const llvm::Value *> preds;

  // Get IPDF
  vector<BasicBlock *> IPDF = iterated_postdominance_frontier(PDT, BB);

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
      preds.insert(cond);
    } else if(isa<SwitchInst>(ti)) {
      const SwitchInst *si = cast<SwitchInst>(ti);
      assert(si);
      const Value *cond = si->getCondition();
      preds.insert(cond);
    }
  }

  return preds;
}

const llvm::Value *getReturnValue(const llvm::Function *F) {
  const Value *ret = NULL;

  for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    if (const ReturnInst *RI = dyn_cast<ReturnInst>(inst)) {
      assert(ret == NULL && "There exists more than one return instruction");

      ret = RI->getReturnValue();
    }
  }

  assert(ret != NULL && "There is no return instruction");

  return ret;
}

double gettime()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec*1.0e-6;
}
