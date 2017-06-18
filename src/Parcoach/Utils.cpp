#include "Utils.h"
#include "Collectives.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"

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

		if(CallInst *CI = dyn_cast<CallInst>(i)){
			Function *callee = CI->getCalledFunction();
			if(!callee) continue;

   		// Is it a collective?
    	if (isCollective((callee))<0)
     		continue;
	
			if(CollSequence=="empty"){
					CollSequence=callee->getName().str();
			}else{
    			if(CollSequence!="NAVS"){ 
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
	string CollSequence_Header;
	string CollSequence=string();
	std::vector<BasicBlock *> Unvisited;


	// GET ALL EXIT NODES
	for(BasicBlock &I : *F){
		if(isa<ReturnInst>(I.getTerminator())){
			string return_coll = string(getCollectivesInBB(&I));
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
			if(getBBcollSequence(*TI)=="white")
			{
				string N=string();
				N=CollSequence_Header;
				if(CollSequence_Header=="empty"){
					N=getCollectivesInBB(Pred);
				}else{
        	if(getCollectivesInBB(Pred)!="empty" && N!="NAVS")
        		N.append(" ").append(getCollectivesInBB(Pred));
				}
				CollSequence=string(N);
				// Set the metadata with the collective sequence
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
				string CollSequence_temp=string(seq_temp);

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


/*
 * INSTRUMENTATION
 */

// Check Collective function before a collective
// + Check Collective function before return statements
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line,
// char* OP_warnings, char *FILE_name)
// --> void check_collective_UPC(int OP_color, const char* OP_name,
// int OP_line, char* warnings, char *FILE_name)
void
instrumentCC(Module *M, Instruction *I, int OP_color,std::string OP_name,
    int OP_line, StringRef WarningMsg, StringRef File){
  IRBuilder<> builder(I);
  // Arguments of the new function
  vector<const Type *> params = vector<const Type *>();
  params.push_back(Type::getInt32Ty(M->getContext())); // OP_color
  params.push_back(PointerType::getInt8PtrTy(M->getContext())); // OP_name
  Value *strPtr_NAME = builder.CreateGlobalStringPtr(OP_name);
  params.push_back(Type::getInt32Ty(M->getContext())); // OP_line
  params.push_back(PointerType::getInt8PtrTy(M->getContext())); // OP_warnings
  const std::string Warnings = WarningMsg.str();
  Value *strPtr_WARNINGS = builder.CreateGlobalStringPtr(Warnings);
  params.push_back(PointerType::getInt8PtrTy(M->getContext())); // FILE_name
  const std::string Filename = File.str();
  Value *strPtr_FILENAME = builder.CreateGlobalStringPtr(Filename);
  // Set new function name, type and arguments
  FunctionType *FTy =FunctionType::get(Type::getVoidTy(M->getContext()),
      ArrayRef<Type *>((Type**)params.data(),
        params.size()),false);
  Value * CallArgs[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), OP_color), strPtr_NAME, ConstantInt::get(Type::getInt32Ty(M->getContext()), OP_line), strPtr_WARNINGS, strPtr_FILENAME};
  std::string FunctionName;

  if(OP_color == (int) v_coll.size()+1){
    FunctionName="check_collective_return";
  }else{
    if(OP_color < (int) MPI_v_coll.size()){
      FunctionName="check_collective_MPI";
    }else{
			if(	OP_color < ((int) MPI_v_coll.size() + OMP_v_coll.size())){
				FunctionName="check_collective_OMP";
			}else{
      	FunctionName="check_collective_UPC";
    	}
		}
  }


  Value * CCFunction = M->getOrInsertFunction(FunctionName, FTy);
  // Create new function
  CallInst::Create(CCFunction, ArrayRef<Value*>(CallArgs), "", I);
  errs() << "=> Insertion of " << FunctionName << " (" << OP_color << ", " << OP_name << ", " << OP_line << ", " << WarningMsg << ", " << File <<  ")\n";
}


string
getWarning(Instruction &inst) {
  string warning = " ";
  if (MDNode *node = inst.getMetadata("inst.warning")) {
    if (Metadata *value = node->getOperand(0)) {
      MDString *mdstring = cast<MDString>(value);
      warning = mdstring->getString();
    }
  }else {
    //errs() << "Did not find metadata\n";
  }
  return warning;
}


void instrumentFunction(Function *F)
{
				Module* M = F->getParent();
				//errs() << "==> Function " << F->getName() << " is instrumented:\n";
				for(Function::iterator bb = F->begin(), e = F->end(); bb!=e; ++bb)
				{
								for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i)
								{
												Instruction *Inst=&*i;
												string Warning = getWarning(*Inst);
												//string Warning = " ";
												// Debug info (line in the source code, file)
												DebugLoc DLoc = i->getDebugLoc();
												string File="o"; int OP_line = -1;
												if(DLoc){
																OP_line = DLoc.getLine();
																File=DLoc->getFilename();
												}
												// call instruction
												if(CallInst *CI = dyn_cast<CallInst>(i))
												{
																Function *callee = CI->getCalledFunction();
																if(callee==NULL) continue;	
																string OP_name = callee->getName().str();
																int OP_color = isCollective(callee);

																// Before finalize
																if(callee->getName().equals("MPI_Finalize")){
																				DEBUG(errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n"); 
																				instrumentCC(M,Inst,v_coll.size()+1, "MPI_Finalize", OP_line, Warning, File);
																				continue;
																}
																// Before a collective
																if(OP_color>=0){
																				DEBUG(errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n"); 
																				instrumentCC(M,Inst,OP_color, OP_name, OP_line, Warning, File);
																}
																// Before a return instruction
																if(isa<ReturnInst>(i)){
																				DEBUG(errs() << "-> insert check before return statement line " << OP_line << "\n"); 
																				instrumentCC(M,Inst,v_coll.size()+1, "Return", OP_line, Warning, File);
																}
												}
								}
				}
}



