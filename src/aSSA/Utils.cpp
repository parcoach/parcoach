#include "Utils.h"
#include "Collectives.h"

#include <sys/time.h>

#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_ostream.h"

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


/*
 * FUNCTIONS USED TO CHECK COLLECTIVES
 */

// Get the sequence of collectives in a BB
string getCollectivesInBB(BasicBlock *BB,PTACallGraph *PTACG){
 string CollSequence="empty";

 for(BasicBlock::iterator i = BB->begin(), e = BB->end(); i != e; ++i){
  const Instruction *inst = &*i;					

  if(CallInst *CI = dyn_cast<CallInst>(i)) {
   Function *callee = CI->getCalledFunction();

   //// Indirect calls
   if(callee == NULL){
    for (const Function *mayCallee : PTACG->indirectCallMap[inst]) {
     if (isIntrinsicDbgFunction(mayCallee))  continue;
     Function *callee = const_cast<Function *>(mayCallee);
     // Is it a function containing a collective?
     if(getFuncSummary(*callee)!="no summary" && getFuncSummary(*callee)!="empty"){
      if(CollSequence=="empty"){
       CollSequence=getFuncSummary(*callee);
      }else{
       // To avoid a summary like NAVS NAVS
       if(CollSequence=="NAVS" || getFuncSummary(*callee) == "NAVS"){
        CollSequence="NAVS";
       }else{
        CollSequence.append(" ").append(getFuncSummary(*callee));
       }
      }
     }
     // Is it a collective operation?
     if(isCollective(callee)){
      if(CollSequence=="empty"){
       CollSequence=callee->getName().str();
      }else{
        if(CollSequence!="NAVS")
         CollSequence.append(" ").append(callee->getName().str());
      }
     }
    }
   //// Direct calls
   }else{
    // Is it a function containing a collective?
    if(getFuncSummary(*callee)!="no summary" && getFuncSummary(*callee)!="empty"){        
 
     if(CollSequence=="empty"){
      CollSequence=getFuncSummary(*callee);
     }else{
      if(CollSequence=="NAVS" || getFuncSummary(*callee) == "NAVS"){
       CollSequence="NAVS";
      }else{
        CollSequence.append(" ").append(getFuncSummary(*callee));
      }
     }
    }
    // Is it a collective operation?
    if(isCollective(callee)){
     if(CollSequence=="empty"){
      CollSequence=callee->getName().str();
     }else{
      if(CollSequence!="NAVS")
       CollSequence.append(" ").append(callee->getName().str());
     }
    }
   }
  }
 }
 return CollSequence;
}




// Metadata
string
getBBcollSequence(const llvm::Instruction &inst){
				if (MDNode *node = inst.getMetadata("inst.collSequence")) {
								if (Metadata *value = node->getOperand(0)) {
												MDString *mdstring = cast<MDString>(value);
												assert(mdstring->getString()!="white");
												return mdstring->getString();
								}
				}
				return "white";
}


string
getFuncSummary(llvm::Function &F){
        if (MDNode *node = F.getMetadata("func.summary")) {
                if (Metadata *value = node->getOperand(0)) {
                        MDString *mdstring = cast<MDString>(value);
                        return mdstring->getString();
                }
        }
        return "no summary";
}


// BFS in reverse topological order
void BFS(llvm::Function *F, PTACallGraph *PTACG){
 MDNode* mdNode;
 StringRef CollSequence_Header;
 StringRef CollSequence=StringRef();
 std::vector<BasicBlock *> Unvisited;

 // GET ALL EXIT NODES AND SET THE COLLECTIVE SEQUENCE
 for(BasicBlock &I : *F){
  if(isa<ReturnInst>(I.getTerminator())){
   StringRef return_coll = StringRef(getCollectivesInBB(&I, PTACG));
   mdNode = MDNode::get(I.getContext(),MDString::get(I.getContext(),return_coll));
   I.getTerminator()->setMetadata("inst.collSequence",mdNode);
   Unvisited.push_back(&I);
   continue;
  }
 }
 while(Unvisited.size()>0)
 {
  BasicBlock *header=*Unvisited.begin();
  Unvisited.erase(Unvisited.begin());
  CollSequence_Header = getBBcollSequence(*header->getTerminator());
  pred_iterator PI=pred_begin(header), E=pred_end(header);

  for(; PI!=E; ++PI){
   BasicBlock *Pred = *PI;
   TerminatorInst* TI = Pred->getTerminator();

   // BB NOT SEEN BEFORE
   if(getBBcollSequence(*TI)=="white"){
    string N=CollSequence_Header.str();
    if(CollSequence_Header.str()=="empty"){
     N=getCollectivesInBB(Pred, PTACG);
    }else{
     if(getCollectivesInBB(Pred, PTACG)!="empty" && N!="NAVS")
      N.append(" ").append(getCollectivesInBB(Pred,PTACG)); 
    }
    CollSequence=StringRef(N);
   // Set the metadata with the collective sequence
    mdNode = MDNode::get(TI->getContext(),MDString::get(TI->getContext(),CollSequence));
    TI->setMetadata("inst.collSequence",mdNode);
    Unvisited.push_back(Pred);
    
    // BB ALREADY SEEN
   }else{
    string seq_temp = CollSequence_Header.str();
    if(CollSequence_Header.str()=="empty"){
     seq_temp=getCollectivesInBB(Pred, PTACG);
    }else{
     if(getCollectivesInBB(Pred, PTACG)!="empty" && seq_temp!="NAVS")
      seq_temp.append(" ").append(getCollectivesInBB(Pred, PTACG));
    }
    StringRef CollSequence_temp=StringRef(seq_temp);

    // Check if CollSequence_temp and sequence in BB are equals. If not set the metadata as NAVS
    DEBUG(errs() << " >>> " << CollSequence_temp.str() << " = " << getBBcollSequence(*TI) << " ?\n");
    if(CollSequence_temp.str() != getBBcollSequence(*TI)){
     mdNode = MDNode::get(Pred->getContext(),MDString::get(Pred->getContext(),"NAVS"));
     TI->setMetadata("inst.collSequence",mdNode);
     DebugLoc BDLoc = TI->getDebugLoc();
     DEBUG(errs() << "  ===>>> Line " << BDLoc.getLine() << " -> " << getBBcollSequence(*TI) << "\n");
    }
   }
  }
 }
 // Keep a metadata for the summary of the function
 BasicBlock &entry = F->getEntryBlock();
 StringRef FuncSummary="";

 if(getBBcollSequence(*entry.getTerminator()) != "white")
  FuncSummary=getBBcollSequence(*entry.getTerminator());
 else
  errs() << F->getName() << " has white summary!\n";
 if(FuncSummary.find("NAVS")!=std::string::npos)
  FuncSummary="NAVS";


 mdNode = MDNode::get(F->getContext(),MDString::get(F->getContext(),FuncSummary));
 F->setMetadata("func.summary",mdNode);
 DEBUG(errs() << "Summary of function " << F->getName() << " : " << getFuncSummary(*F) << "\n");
}
