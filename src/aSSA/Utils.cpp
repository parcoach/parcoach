#include "Utils.h"
#include "Collectives.h"

#include <sys/time.h>

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"

#define DEBUG_TYPE "hello"

using namespace llvm;
using namespace std;


bool isCallSite(const llvm::Instruction *inst) {
  return llvm::isa<llvm::CallInst>(inst) || llvm::isa<llvm::InvokeInst>(inst);
}



std::string getValueLabel(const llvm::Value *v) {
  const Function *F = dyn_cast<Function>(v);
  if (F)
    return F->getName();

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

static map<BasicBlock *, set<BasicBlock *> *> pdfCache;

// PDF computation
vector<BasicBlock * >
postdominance_frontier(PostDominatorTree &PDT, BasicBlock *BB){
  vector<BasicBlock * > PDF;

  set<BasicBlock *> *cache = pdfCache[BB];
  if (cache) {
    for (BasicBlock *b : *cache)
      PDF.push_back(b);
    return PDF;
  }

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

  pdfCache[BB] = new set<BasicBlock *>();
  pdfCache[BB]->insert(PDF.begin(), PDF.end());

  return PDF;
}

static map<BasicBlock *, set<BasicBlock *> *> ipdfCache;

// PDF+ computation
vector<BasicBlock * >
iterated_postdominance_frontier(PostDominatorTree &PDT, BasicBlock *BB){
  vector<BasicBlock * > iPDF;

  set<BasicBlock *> *cache = ipdfCache[BB];
  if (cache) {
    for (BasicBlock *b : *cache)
      iPDF.push_back(b);
    return iPDF;
  }

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

  ipdfCache[BB] = new set<BasicBlock *>();
  ipdfCache[BB]->insert(iPDF.begin(), iPDF.end());

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

  if (F->isVarArg())
    return &*F->arg_end();

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

const llvm::Value *
getBasicBlockCond(const BasicBlock *BB) {
  const TerminatorInst *ti = BB->getTerminator();
  assert(ti);

  if (isa<BranchInst>(ti)) {
    const BranchInst *bi = cast<BranchInst>(ti);
    assert(bi);

    if (bi->isUnconditional())
      return NULL;

    return bi->getCondition();
  } else if(isa<SwitchInst>(ti)) {
    const SwitchInst *si = cast<SwitchInst>(ti);
    assert(si);
    return si->getCondition();
  }

  return NULL;
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

bool isIntrinsicDbgFunction(const llvm::Function *F) {
  return F != NULL && F->getName().startswith("llvm.dbg");
}

bool isIntrinsicDbgInst(const llvm::Instruction *I) {
  return isa<DbgInfoIntrinsic>(I);
}

bool functionDoesNotRet(const llvm::Function *F) {
  std::vector<const BasicBlock *> toVisit;
  std::set<const BasicBlock *> visited;

  toVisit.push_back(&F->getEntryBlock());

  while (!toVisit.empty()) {
    const BasicBlock *BB = toVisit.back();
    toVisit.pop_back();

    for (const Instruction &inst : *BB) {
      if(isa<ReturnInst>(inst))
	return false;
    }

    for (auto I = succ_begin(BB), E = succ_end(BB); I != E; ++I) {
      const BasicBlock *succ = *I;
      if (visited.find(succ) == visited.end()) {
	toVisit.push_back(succ);
	visited.insert(succ);
      }
    }
  }

  return true;
}

unsigned getBBSetIntersectionSize(const std::set<const BasicBlock *> S1,
				  const std::set<const BasicBlock *> S2) {
  unsigned ret = 0;

  for (const BasicBlock *BB : S1) {
    if (S2.find(BB) != S2.end())
      ret++;
  }

  return ret;
}

unsigned getInstSetIntersectionSize(const std::set<const Instruction *> S1,
				    const std::set<const Instruction *> S2) {
  unsigned ret = 0;

  for (const Instruction *I : S1) {
    if (S2.find(I) != S2.end())
      ret++;
  }

  return ret;
}

