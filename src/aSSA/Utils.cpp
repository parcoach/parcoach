#include "Utils.h"
#include "parcoach/Collectives.h"

#include <sys/time.h>

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_ostream.h"

#include <map>

#define DEBUG_TYPE "hello"

using namespace llvm;

bool isCallSite(llvm::Instruction const *Inst) {
  return llvm::isa<llvm::CallBase>(Inst);
}

std::string getValueLabel(llvm::Value const *V) {
  assert(!isa<Function>(V) && "Use F->getName for functions");

  std::string Label;
  llvm::raw_string_ostream Rso(Label);
  V->print(Rso);
  size_t Pos = Label.find_first_of('=');
  if (Pos != std::string::npos) {
    Label = Label.substr(0, Pos);
  }
  return Label;
}

std::string getCallValueLabel(llvm::Value const *V) {
  std::string Label;
  llvm::raw_string_ostream Rso(Label);
  V->print(Rso);
  size_t Pos = Label.find_first_of('=');
  if (Pos != std::string::npos) {
    Label = Label.substr(Pos + 2);
  }
  return Label;
}

/*
 * POSTDOMINANCE
 */

static std::map<BasicBlock *, std::unique_ptr<std::set<BasicBlock *>>> PdfCache;

// PDF computation
std::vector<BasicBlock *> postdominanceFrontier(PostDominatorTree &PDT,
                                                BasicBlock *BB) {
  std::vector<BasicBlock *> PDF;

  auto &Cache = PdfCache[BB];
  if (Cache) {
    for (BasicBlock *B : *Cache) {
      PDF.push_back(B);
    }
    return PDF;
  }

  PDF.clear();
  DomTreeNode *DomNode = PDT.getNode(BB);

  for (auto It = pred_begin(BB), Et = pred_end(BB); It != Et; ++It) {
    // does BB immediately dominate this predecessor?
    DomTreeNode *ID = PDT[*It]; //->getIDom();
    if ((ID != nullptr) && ID->getIDom() != DomNode && *It != BB) {
      PDF.push_back(*It);
    }
  }
  for (DomTreeNode::const_iterator NI = DomNode->begin(), NE = DomNode->end();
       NI != NE; ++NI) {
    DomTreeNode *IDominee = *NI;
    std::vector<BasicBlock *> ChildDF =
        postdominanceFrontier(PDT, IDominee->getBlock());
    std::vector<BasicBlock *>::const_iterator CDFI = ChildDF.begin();
    std::vector<BasicBlock *>::const_iterator CDFE = ChildDF.end();
    for (; CDFI != CDFE; ++CDFI) {
      if (PDT[*CDFI]->getIDom() != DomNode && *CDFI != BB) {
        PDF.push_back(*CDFI);
      }
    }
  }

  PdfCache[BB] = std::make_unique<std::set<BasicBlock *>>();
  PdfCache[BB]->insert(PDF.begin(), PDF.end());

  return PDF;
}

static std::map<BasicBlock *, std::unique_ptr<std::set<BasicBlock *>>>
    IpdfCache;

// PDF+ computation
std::vector<BasicBlock *>
iterated_postdominance_frontier(PostDominatorTree &PDT, BasicBlock *BB) {
  std::vector<BasicBlock *> IPDF;

  auto &Cache = IpdfCache[BB];
  if (Cache) {
    for (BasicBlock *B : *Cache) {
      IPDF.push_back(B);
    }
    return IPDF;
  }

  IPDF = postdominanceFrontier(PDT, BB);
  if (IPDF.empty()) {
    return IPDF;
  }

  std::set<BasicBlock *> S;
  S.insert(IPDF.begin(), IPDF.end());

  std::set<BasicBlock *> ToCompute;
  std::set<BasicBlock *> ToComputeTmp;
  ToCompute.insert(IPDF.begin(), IPDF.end());

  bool Changed = true;
  while (Changed) {
    Changed = false;

    for (auto I = ToCompute.begin(), E = ToCompute.end(); I != E; ++I) {
      std::vector<BasicBlock *> Tmp = postdominanceFrontier(PDT, *I);
      for (auto J = Tmp.begin(), F = Tmp.end(); J != F; ++J) {
        if ((S.insert(*J)).second) {
          ToComputeTmp.insert(*J);
          Changed = true;
        }
      }
    }

    ToCompute = ToComputeTmp;
    ToComputeTmp.clear();
  }

  IPDF.insert(IPDF.end(), S.begin(), S.end());

  IpdfCache[BB] = std::make_unique<std::set<BasicBlock *>>();
  IpdfCache[BB]->insert(IPDF.begin(), IPDF.end());

  return IPDF;
}

std::set<llvm::Value const *>
computeIPDFPredicates(llvm::PostDominatorTree &PDT, llvm::BasicBlock *BB) {
  std::set<llvm::Value const *> Preds;

  // Get IPDF
  std::vector<BasicBlock *> IPDF = iterated_postdominance_frontier(PDT, BB);

  for (unsigned N = 0; N < IPDF.size(); ++N) {
    // Push conditions of each BB in the IPDF
    Instruction const *Term = IPDF[N]->getTerminator();
    assert(Term);

    if (isa<BranchInst>(Term)) {
      BranchInst const *Bi = cast<BranchInst>(Term);
      assert(Bi);

      if (Bi->isUnconditional()) {
        continue;
      }

      Value const *Cond = Bi->getCondition();
      Preds.insert(Cond);
    } else if (isa<SwitchInst>(Term)) {
      SwitchInst const *SI = cast<SwitchInst>(Term);
      assert(SI);
      Value const *Cond = SI->getCondition();
      Preds.insert(Cond);
    }
  }

  return Preds;
}

llvm::Value const *getBasicBlockCond(BasicBlock const *BB) {
  Instruction const *Term = BB->getTerminator();
  assert(Term);

  if (isa<BranchInst>(Term)) {
    BranchInst const *Bi = cast<BranchInst>(Term);
    assert(Bi);

    if (Bi->isUnconditional()) {
      return nullptr;
    }

    return Bi->getCondition();
  }
  if (isa<SwitchInst>(Term)) {
    SwitchInst const *SI = cast<SwitchInst>(Term);
    assert(SI);
    return SI->getCondition();
  }

  return nullptr;
}

llvm::Value const *getReturnValue(llvm::Function const *F) {
  Value const *Ret = NULL;

  for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction const *Inst = &*I;

    if (ReturnInst const *RI = dyn_cast<ReturnInst>(Inst)) {
      assert(Ret == NULL && "There exists more than one return instruction");

      Ret = RI->getReturnValue();
    }
  }

  assert(Ret != NULL && "There is no return instruction");

  return Ret;
}

bool isIntrinsicDbgFunction(llvm::Function const *F) {
  return F != NULL && F->getName().startswith("llvm.dbg");
}

bool isIntrinsicDbgInst(llvm::Instruction const *I) {
  return isa<DbgInfoIntrinsic>(I);
}

bool functionDoesNotRet(llvm::Function const *F) {
  std::vector<BasicBlock const *> ToVisit;
  std::set<BasicBlock const *> Visited;

  ToVisit.push_back(&F->getEntryBlock());

  while (!ToVisit.empty()) {
    BasicBlock const *BB = ToVisit.back();
    ToVisit.pop_back();

    for (Instruction const &Inst : *BB) {
      if (isa<ReturnInst>(Inst)) {
        return false;
      }
    }

    for (auto I = succ_begin(BB), E = succ_end(BB); I != E; ++I) {
      BasicBlock const *Succ = *I;
      if (Visited.find(Succ) == Visited.end()) {
        ToVisit.push_back(Succ);
        Visited.insert(Succ);
      }
    }
  }

  return true;
}

unsigned getBBSetIntersectionSize(const std::set<BasicBlock const *> S1,
                                  const std::set<BasicBlock const *> S2) {
  unsigned Ret = 0;

  for (BasicBlock const *BB : S1) {
    if (S2.find(BB) != S2.end()) {
      Ret++;
    }
  }

  return Ret;
}

unsigned getInstSetIntersectionSize(const std::set<Instruction const *> S1,
                                    const std::set<Instruction const *> S2) {
  unsigned Ret = 0;

  for (Instruction const *I : S1) {
    if (S2.find(I) != S2.end()) {
      Ret++;
    }
  }

  return Ret;
}
