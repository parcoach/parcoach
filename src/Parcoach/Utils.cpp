#include "Utils.h"
#include "Collectives.h"


#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace std;


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
        vector<BasicBlock * > iPDF_temp;
        vector<BasicBlock * > PDF;
        vector<BasicBlock * > PDF_temp;

        iPDF=postdominance_frontier(PDT, BB);
        if(iPDF.size()==0)
                return iPDF;

        // iterate
        iPDF_temp=iPDF;
        while(iPDF_temp.size()!=0){
                vector<BasicBlock * >::const_iterator PDFI = iPDF_temp.begin(), PDFE = iPDF_temp.end();
                for(; PDFI != PDFE; ++PDFI){
                        PDF.clear();
                        PDF=postdominance_frontier(PDT, *PDFI);
                        PDF_temp.insert(std::end(PDF_temp), std::begin(PDF), std::end(PDF));
                        iPDF.insert(std::end(iPDF), std::begin(PDF), std::end(PDF));
                }
                iPDF_temp.clear();
                iPDF_temp=PDF_temp;
                PDF_temp.clear();
        }

        return iPDF;
}

// Get the sequence of collectives for a BB
string get_BB_collectives(BasicBlock *BB){
	string CollSequence="empty";
	for(BasicBlock::iterator i = BB->begin(), e = BB->end(); i != e; ++i){
		if(CallInst *CI = dyn_cast<CallInst>(i))
		{
			Function *f = CI->getCalledFunction();
			if(!f) continue;

			string OP_name = f->getName().str();
			StringRef funcName = f->getName();

                	// Is it a collective?
                	for (vector<const char *>::iterator vI = MPI_v_coll.begin(), E = MPI_v_coll.end(); vI != E; ++vI) {
                        	if (!funcName.equals(*vI))
                                	continue;

				if(CollSequence=="empty"){
					CollSequence=OP_name;
				}else{
					CollSequence.append(" ");
					CollSequence.append(OP_name);
				}
			}
		}
	}
	return CollSequence;
}


// Metadata
string
getBBcollSequence(Instruction &inst){
        string collSequence="white";
         if (MDNode *node = inst.getMetadata("inst.collSequence")) {
                if (Metadata *value = node->getOperand(0)) {
                        MDString *mdstring = cast<MDString>(value);
                        collSequence=mdstring->getString();
                        return collSequence;
                }
        }
        return collSequence;
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
			Unvisited.push_back(&I);
			// set the coll seq of this return bb
			StringRef return_coll = StringRef(get_BB_collectives(&I));
			mdNode = MDNode::get(I.getContext(),MDString::get(I.getContext(),return_coll));
			I.getTerminator()->setMetadata("inst.collSequence",mdNode);
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

			// Ignore backedge
			//     -> loops! if a node in a loop encounters its header, do not consider it - beware of boucles imbriquees
			//bool inLoop = LI->getLoopFor(Pred); // true if the basic block is in a loop
			//Loop* BBloop = LI->getLoopFor(Pred); // loop containing this basic block

			// BB NOT SEEN BEFORE
			if(getBBcollSequence(*TI)=="white"){
				string N="empty";
				if(CollSequence_Header.str()=="empty"){
					N=get_BB_collectives(Pred);
				}else{
					N=CollSequence_Header.str();
					if(get_BB_collectives(Pred)!="empty"){
						N.append(" ");
						N.append(get_BB_collectives(Pred)); // add the coll in Pred
					}
				}
				CollSequence=StringRef(N);
				mdNode = MDNode::get(TI->getContext(),MDString::get(TI->getContext(),CollSequence));
				TI->setMetadata("inst.collSequence",mdNode);
				Unvisited.push_back(Pred);
			// BB ALREADY SEEN
			//    -> check if already metadata set. if conditional and different sequences, return a warning
			}else{
				string seq_temp;
				if(CollSequence_Header.str()=="empty"){
					seq_temp=get_BB_collectives(Pred);
				}else{
					seq_temp=CollSequence_Header.str();
					if(get_BB_collectives(Pred)!="empty"){
						seq_temp.append(" ");
						seq_temp.append(get_BB_collectives(Pred));
					}
				}
				StringRef CollSequence_temp=StringRef(seq_temp);

				// if temp != coll seq -> warning + keep the bb in the PDF+
				//errs() << "  >>> " << CollSequence_temp.str() << " = " << getBBcollSequence(*TI) << " ?\n";
				if(CollSequence_temp.str() != getBBcollSequence(*TI) || CollSequence_temp.str()=="NAVS" || getBBcollSequence(*TI)=="NAVS"){
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

