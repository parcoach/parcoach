#include "Utils.h"
#include "Collectives.h"


#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace std;


// PDF computation
static map<BasicBlock *, set<BasicBlock *> *> pdfCache;

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

// PDF+ computation
static map<BasicBlock *, set<BasicBlock *> *> ipdfCache;

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

// Get the sequence of collectives for a BB
string getCollectivesInBB(BasicBlock *BB){
	string CollSequence="empty";

	for(BasicBlock::iterator i = BB->begin(), e = BB->end(); i != e; ++i){
  const Instruction *inst = &*i;

		if(CallInst *CI = dyn_cast<CallInst>(i)){
			Function *callee = CI->getCalledFunction();
			if(!callee) continue;

   // Is it a collective?
   for (vector<const char *>::iterator vI = MPI_v_coll.begin(), E = MPI_v_coll.end(); vI != E; ++vI) {
    if (!(callee->getName()).equals(*vI))
     continue;

				if(CollSequence=="empty"){
					CollSequence=callee->getName().str();
				}else{
     if(CollSequence!="NAVS") 
      CollSequence.append(" ").append(callee->getName().str());
				}
			}
		}
	}
	return CollSequence;
}


// Metadata
string
getBBcollSequence(Instruction &inst){
         if (MDNode *node = inst.getMetadata("inst.collSequence")) {
                if (Metadata *value = node->getOperand(0)) {
                        MDString *mdstring = cast<MDString>(value);
                        return mdstring->getString();
                }
        }
        return "white";
}

// BFS in reverse topological order
void BFS(Function *F){
	MDNode* mdNode;
	StringRef CollSequence_Header;
	StringRef CollSequence=StringRef();
	std::vector<BasicBlock *> Unvisited;

	// GET ALL EXIT NODES
	for(BasicBlock &I : *F){
		if(isa<ReturnInst>(I.getTerminator())){
			StringRef return_coll = StringRef(getCollectivesInBB(&I));
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
				string N=CollSequence_Header;
				if(CollSequence_Header=="empty"){
					N=getCollectivesInBB(Pred);
				}else{
      if(getCollectivesInBB(Pred)!="empty" && N!="NAVS")
       N.append(" ").append(getCollectivesInBB(Pred));
				}
				CollSequence=string(N);
				mdNode = MDNode::get(TI->getContext(),MDString::get(TI->getContext(),CollSequence));
				TI->setMetadata("inst.collSequence",mdNode);
				Unvisited.push_back(Pred);

			// BB ALREADY SEEN
			}else{
				string seq_temp=CollSequence_Header;
				if(CollSequence_Header=="empty"){
					seq_temp=getCollectivesInBB(Pred);
				}else{
     if(getCollectivesInBB(Pred)!="empty" && seq_temp!="NAVS")
      seq_temp.append(" ").append(getCollectivesInBB(Pred));
				}
				StringRef CollSequence_temp=string(seq_temp);

    // Check if CollSequence_temp and sequence in BB are equals. If not set the metadata as NAVS
				if(CollSequence_temp != getBBcollSequence(*TI)){
					mdNode = MDNode::get(Pred->getContext(),MDString::get(Pred->getContext(),"NAVS"));
					TI->setMetadata("inst.collSequence",mdNode);
					// DEBUG
					//DebugLoc BDLoc = TI->getDebugLoc();
                                        //errs() << "  ===>>> Line " << BDLoc.getLine() << " -> " << getBBcollSequence(*TI) << "\n";
				}
			}
		}
	}
}

