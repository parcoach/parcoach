#include "Collectives.h"
#include "Options.h"
#include "ParcoachAnalysisIntra.h"
#include "Utils.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_os_ostream.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "hello"


void
ParcoachAnalysisIntra::run(){
	// Parcoach analysis
	for (Function &F : M) {
		unsigned oldNbWarnings = nbWarningsParcoachOnly;
		if (F.isDeclaration())
			continue;

		// (1) Set the sequence of collectives with a BFS from the exit node in the CFG
		errs() << "BFS\n"; 
		if(optMpiTaint)
			MPI_BFS(&F);
		else
			BFS(&F);

		// (2) Check calls to collectives with PDF+ of each collective node
		errs() << "checkCollectives\n"; 
		checkCollectives(&F);

		// EMMA: always instrument the code
		errs() << "Instru\n"; 
		if(nbWarningsParcoachOnly > oldNbWarnings){
		//if(nbWarningsParcoachOnly > oldNbWarnings && !disableInstru){
			// Static instrumentation of the code 
			errs() << "\033[0;35m=> Instrumentation of the function ...\033[0;0m\n";
			instrumentFunction(&F);
		}
	}
}



/*
 * FUNCTIONS USED TO CHECK COLLECTIVES
 */

// Get the sequence of collectives in a BB
void 
ParcoachAnalysisIntra::setCollSet(BasicBlock *BB){
	for(auto i = BB->rbegin(), e = BB->rend(); i != e; ++i){
		const Instruction *inst = &*i;
		if(const CallInst *CI = dyn_cast<CallInst>(inst)){ 

			Function *callee = CI->getCalledFunction();
			if(!callee) continue;

			// Is it a collective operation?
			if(isCollective(callee)){
				string OP_name = callee->getName().str();
				if(collMap[BB].empty())
					collMap[BB] = OP_name;
				else
					collMap[BB] = OP_name + " " + collMap[BB];
			}
		}
	}
}

// Get the sequence of collectives in a BB, per MPI communicator
void 
ParcoachAnalysisIntra::setMPICollSet(BasicBlock *BB){

	for(auto i = BB->rbegin(), e = BB->rend(); i != e; ++i){
		const Instruction *inst = &*i;
		if(const CallInst *CI = dyn_cast<CallInst>(inst)){
			Function *callee = CI->getCalledFunction();
			if(!callee) continue;
			
			// Is it a collective operation?
			if(isCollective(callee)){
				string OP_name = callee->getName().str();
				int OP_color = getCollectiveColor(callee);
				Value* OP_com = CI->getArgOperand(Com_arg_id(OP_color));
				if(!mpiCollMap[BB][OP_com].empty()){
					mpiCollMap[BB][OP_com] = OP_name + " " + mpiCollMap[BB][OP_com];
				}else{
					mpiCollMap[BB][OP_com] = OP_name;
				}
			}
		}
	}
}


// Tag loop preheader
void
ParcoachAnalysisIntra::Tag_LoopPreheader(llvm::Loop *L){
		BasicBlock *Lheader = L->getHeader();
		if(L->getNumBlocks()==1)
			return;
		pred_iterator PI=pred_begin(Lheader), E=pred_end(Lheader);
		for(; PI!=E; ++PI){
			BasicBlock *Pred = *PI;
			if(L->contains(Pred))
				bbPreheaderMap[Pred]=true;
		}
}

// FOR MPI APPLIS: BFS on loops
void
ParcoachAnalysisIntra::MPI_BFS_Loop(llvm::Function *F){

	std::vector<BasicBlock *> Unvisited;
	curLoop = &pass->getAnalysis<LoopInfoWrapperPass>
		(*const_cast<Function *>(F)).getLoopInfo();

	for(Loop *L: *curLoop){
		Tag_LoopPreheader(L);
		BasicBlock *Lheader = L->getHeader();
		Unvisited.push_back(Lheader);

		while(Unvisited.size()>0){
			BasicBlock *header=*Unvisited.begin();
			//DEBUG//errs() << "Header " << header->getName() << "\n";
			Unvisited.erase(Unvisited.begin());
			if(mustWait(header)){ // all successors have not been seen
				Unvisited.push_back(header);
				header=*Unvisited.begin(); // take the next BB in Unvisited
				//DEBUG//errs() << "New header " << header->getName() << "\n";
				Unvisited.erase(Unvisited.begin());
			}
			pred_iterator PI=pred_begin(header), E=pred_end(header);
			for(; PI!=E; ++PI){
				BasicBlock *Pred = *PI;
				if(!L->contains(Pred)) // ignore BB not in the loop
					continue;
				// BB NOT SEEN BEFORE
				if(bbVisitedMap[Pred] == white){
					//DEBUG//errs() << F->getName() << " Pred: " << Pred->getName() << "\n";
					for(auto& pair : mpiCollMap[header])
						mpiCollMap[Pred][pair.first] = mpiCollMap[header][pair.first];	
					setMPICollSet(Pred);
					bbVisitedMap[Pred]=grey;
					if(Pred !=Lheader)
						Unvisited.push_back(Pred);
					// BB ALREADY SEEN
				}else{
					//DEBUG//errs() << F->getName() << " Pred: " << Pred->getName() << " already seen\n";
					ComCollMap temp(mpiCollMap[Pred]);
					/* DEBUG//
						 errs() << "* Temp = \n";
						 for(auto& pair : temp){
						 errs() << pair.first << "{" << pair.second << "}\n";
						 }
						 errs() << "****\n";*/
					mpiCollMap[Pred].clear(); // reset
					for(auto& pair : mpiCollMap[header])
						mpiCollMap[Pred][pair.first] = mpiCollMap[header][pair.first];
					setMPICollSet(Pred);
					/* DEBUG//
						 errs() << "* New = \n";
						 for(auto& pair : mpiCollMap[Pred]){
						 errs() << pair.first << "{" << pair.second << "}\n";
						 }
						 errs() << "****\n";*/

					// Compare and update   
					for(auto& pair : mpiCollMap[Pred]){
						if(mpiCollMap[Pred][pair.first]!=temp[pair.first]){
							mpiCollMap[Pred][pair.first]="NAVS";
						}
						temp.erase(pair.first);
					}
					for(auto& pair : temp){ // for remaining communicators
						mpiCollMap[Pred][pair.first]="NAVS";
					}	
					/* DEBUG//
						 errs() << "* Then = \n";
						 for(auto& pair : mpiCollMap[Pred]){
						 errs() << pair.first << "{" << pair.second << "}\n";
						 }       
						 errs() << "****\n";*/

				} // END ELSE
				bbVisitedMap[header]=black;
			} // END FOR
		} // END WHILE
	}	// END FOR
}



// FOR MPI APPLIS: BFS
void
ParcoachAnalysisIntra::MPI_BFS(llvm::Function *F){
std::vector<BasicBlock *> Unvisited;

	//DEBUG//errs() << "** Analyzing function " << F->getName() << "\n";

	// BFS ON EACH LOOP IN F
	//DEBUG//errs() << "-- BFS IN EACH LOOP --\n";
	MPI_BFS_Loop(F);

	//DEBUG//errs() << "-- BFS --\n";

	// GET ALL EXIT NODES 
	for(BasicBlock &I : *F){
		// Set all nodes to white
		bbVisitedMap[&I]=white;
		// Return inst 
		if(isa<ReturnInst>(I.getTerminator())){
			Unvisited.push_back(&I);
			setMPICollSet(&I);
			bbVisitedMap[&I]=grey;
		}
	}
	while(Unvisited.size()>0){
		BasicBlock *header=*Unvisited.begin();
		Unvisited.erase(Unvisited.begin());
		//DEBUG//errs() << "Header " << header->getName() << "\n";
		if(mustWait(header)==true){ // all successors have not been seen
			Unvisited.push_back(header);
			header=*Unvisited.begin(); // take the next BB in Unvisited
			//DEBUG//errs() << "New header " << header->getName() << "\n";
			Unvisited.erase(Unvisited.begin());
		}
		pred_iterator PI=pred_begin(header), E=pred_end(header);
		for(; PI!=E; ++PI){
			BasicBlock *Pred = *PI;
			if( bbPreheaderMap[Pred]==true){ // ignore loops preheader
				//DEBUG//errs() << F->getName() << " - BB " << Pred->getName() << " is preheader\n";
				continue;
			}
			// BB NOT SEEN BEFORE
			if(bbVisitedMap[Pred] == white){
				//DEBUG//errs() << F->getName() << " Pred: " << Pred->getName() << "\n";

				// Loop header may have a mpiCollMap
				if(mpiCollMap[Pred].empty()){
					for(auto& pair : mpiCollMap[header])
						mpiCollMap[Pred][pair.first] = mpiCollMap[header][pair.first];
				}else{
					for(auto& pair : mpiCollMap[header]){
						if(mpiCollMap[Pred][pair.first].empty()) // copy only empty sequences, others should have NAVS
							mpiCollMap[Pred][pair.first] = mpiCollMap[header][pair.first];	
					}
				}
				/* DEBUG//
					 for(auto& pair : mpiCollMap[Pred]){
					 errs() << pair.first << "{" << pair.second << "}\n";
					 }*/
				setMPICollSet(Pred);
				/* DEBUG// 
					 errs() << Pred->getName() << ":\n";
					 for(auto& pair : mpiCollMap[Pred]){
					 errs() << pair.first << "{" << pair.second << "}\n";
					 }*/

				bbVisitedMap[Pred]=grey;
				if(header != Pred) // to handle loops of size 1
					Unvisited.push_back(Pred);
				// BB ALREADY SEEN
			}else{
				//DEBUG//errs() << F->getName() << " Pred: " << Pred->getName() << " already seen\n";
				ComCollMap temp(mpiCollMap[Pred]);
				/* DEBUG// 
					 errs() << "* Temp = \n";
					 for(auto& pair : temp){
					 errs() << pair.first << "{" << pair.second << "}\n";
					 }
					 errs() << "****\n";*/
				mpiCollMap[Pred].clear(); // reset
				for(auto& pair : mpiCollMap[header])
					mpiCollMap[Pred][pair.first] = mpiCollMap[header][pair.first];
				setMPICollSet(Pred);
				/* DEBUG//
					 errs() << "* New = \n";
					 for(auto& pair : mpiCollMap[Pred]){
					 errs() << pair.first << "{" << pair.second << "}\n";
					 }
					 errs() << "****\n";*/

				// Compare and update   
				for(auto& pair : mpiCollMap[Pred]){
					if(mpiCollMap[Pred][pair.first]!=temp[pair.first]){
						mpiCollMap[Pred][pair.first]="NAVS";
					}
					temp.erase(pair.first);
				}
				for(auto& pair : temp){ // for remaining communicators
					mpiCollMap[Pred][pair.first]="NAVS";
				}
				/* DEBUG//
					 errs() << "* Then = \n";
					 for(auto& pair : mpiCollMap[Pred]){
					 errs() << pair.first << "{" << pair.second << "}\n";
					 }
					 errs() << "****\n";*/

			} // END ELSE
			bbVisitedMap[header]=black;
		} // END FOR
	} // END WHILE
}


// FOR BFS: Returns true if all successors are not black
bool 
ParcoachAnalysisIntra::mustWait(llvm::BasicBlock *bb){
		succ_iterator SI=succ_begin(bb), E=succ_end(bb);
		for(; SI!=E; ++SI){
			BasicBlock *Succ = *SI;
			if(bbVisitedMap[Succ] != black)
				return true;
		}
		return false;
}

// BFS ON EACH LOOP
void 
ParcoachAnalysisIntra::BFS_Loop(llvm::Function *F){
	std::vector<BasicBlock *> Unvisited;

	curLoop = &pass->getAnalysis<LoopInfoWrapperPass>
		(*const_cast<Function *>(F)).getLoopInfo();

	for(Loop *L: *curLoop){
		Tag_LoopPreheader(L);
		BasicBlock *Lheader = L->getHeader();
		Unvisited.push_back(Lheader);

		while(Unvisited.size()>0){
			BasicBlock *header=*Unvisited.begin();
			//DEBUG//errs() << "Header " << header->getName() << "\n";
			Unvisited.erase(Unvisited.begin());
			if(mustWait(header)){ // all successors have not been seen
				Unvisited.push_back(header);
				header=*Unvisited.begin(); // take the next BB in Unvisited
				//DEBUG//errs() << "New header " << header->getName() << "\n";
				Unvisited.erase(Unvisited.begin());
			}

			pred_iterator PI=pred_begin(header), E=pred_end(header);
			for(; PI!=E; ++PI){
				BasicBlock *Pred = *PI;
				if(!L->contains(Pred)) //ignore BB not in the loop
					continue;

				// BB NOT SEEN BEFORE
				if(bbVisitedMap[Pred] == white){
					//DEBUG//errs() << F->getName() << " Pred " << Pred->getName() << "\n";
					collMap[Pred] = collMap[header];
					setCollSet(Pred);
					//DEBUG//errs() << "  -> has " << collMap[Pred] << "\n";
					bbVisitedMap[Pred]=grey;
					if(Pred !=Lheader)
						Unvisited.push_back(Pred);
					// BB ALREADY SEEN
				}else{
					//DEBUG//errs() << F->getName() << " - BB " << Pred->getName() << " already seen\n";
					string temp = collMap[Pred];
					collMap[Pred] = collMap[header];
					setCollSet(Pred);
					//DEBUG//errs() << collMap[Pred] << " = " << temp << "?\n";
					if(temp != collMap[Pred]){
						collMap[Pred]="NAVS";
						//DEBUG//errs() << "  -> BB has " << collMap[Pred] << "\n";
					}
				}
				bbVisitedMap[header]=black;
			} // END FOR
		} // END WHILE
	} // END FOR
}

static std::string getSimpleNodeLabel(const BasicBlock *Node) {
	if (!Node->getName().empty())
		return Node->getName().str();

	std::string Str;
	raw_string_ostream OS(Str);

	Node->printAsOperand(OS, false);
	return OS.str();
}

// BFS
void
ParcoachAnalysisIntra::BFS(llvm::Function *F){
		std::vector<BasicBlock *> Unvisited;

		// BFS ON EACH LOOP IN F
		//DEBUG//errs() << "-- BFS IN EACH LOOP --\n";
		BFS_Loop(F);

		//DEBUG//errs() << "-- BFS --\n";
		// GET ALL EXIT NODES 
		for(BasicBlock &I : *F){
			bbVisitedMap[&I]=white;
			if(isa<ReturnInst>(I.getTerminator())){
				Unvisited.push_back(&I);
				setCollSet(&I);
				bbVisitedMap[&I]=grey;
			}
		}
		while(Unvisited.size()>0){
			BasicBlock *header=*Unvisited.begin();
			Unvisited.erase(Unvisited.begin());
			if(mustWait(header)){ // all successors have not been seen
				Unvisited.push_back(header);
				header=*Unvisited.begin(); // take the next BB in Unvisited
				Unvisited.erase(Unvisited.begin());
			}

			pred_iterator PI=pred_begin(header), E=pred_end(header);
			for(; PI!=E; ++PI){
				BasicBlock *Pred = *PI;
				if( bbPreheaderMap[Pred]==true){ // ignore loops preheader
					//DEBUG//errs() << F->getName() << " - BB " << Pred->getName() << " is preheader\n";
					continue;
				}
				// BB NOT SEEN BEFORE
				if(bbVisitedMap[Pred] == white){
					//DEBUG//errs() << F->getName() << " Pred " << Pred->getName() << "\n";

					// Loop header may have a collMap 
					if(collMap[Pred].empty())
						collMap[Pred] = collMap[header];
					else{ // if not empty, it should be a loop header with NAVS
						collMap[Pred] = "NAVS";
					}
					setCollSet(Pred);
					bbVisitedMap[Pred]=grey;
					Unvisited.push_back(Pred);
					// BB ALREADY SEEN
				}else{
					//DEBUG//errs() << F->getName() << " - BB " << Pred->getName() << " already seen\n";
					string temp = collMap[Pred];
					collMap[Pred] = collMap[header];
					setCollSet(Pred);
					//DEBUG//errs() << collMap[Pred] << " = " << temp << "?\n";
					if(temp != collMap[Pred])
						collMap[Pred]="NAVS";
					//DEBUG//errs() << "  -> BB has " << collMap[Pred] << "\n";
				}
			}
			bbVisitedMap[header]=black;
		} // END WHILE
}




// CHECK COLLECTIVES FUNCTION
void
ParcoachAnalysisIntra::checkCollectives(llvm::Function *F){
	MDNode* mdNode;

	for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
		Instruction *i=&*I;
		// Debug info (line in the source code, file)
		DebugLoc DLoc = i->getDebugLoc();
		StringRef File=""; unsigned OP_line=0;
		if(DLoc){
			OP_line = DLoc.getLine();
			File=DLoc->getFilename();
		}
		// Warning info
		string WarningMsg;
		const char *ProgName="PARCOACH";
		SMDiagnostic Diag;
		std::string COND_lines;

		CallInst *CI = dyn_cast<CallInst>(i);
		if(!CI) continue;

		Function *f = CI->getCalledFunction();
		if(!f) continue;

		string OP_name = f->getName().str();

		// Is it a collective call?
		if (!isCollective(f)){
			continue;
		}
		int OP_color = getCollectiveColor(f);
		Value *OP_com = nullptr;
		if(optMpiTaint)
			OP_com = CI->getArgOperand(Com_arg_id(OP_color)); // 0 for Barrier only
			
		//CI->getArgOperand(0)->dump();
		//errs() << "Found " << OP_name << " on " << OP_com << " line " << OP_line << "\n";

		nbCollectivesFound++;
		bool isColWarningParcoach = false;

		// Get conditionals from the callsite
		set<const BasicBlock *> callIPDF;
		DG->getCallIntraIPDF(CI, callIPDF);

		for (const BasicBlock *BB : callIPDF) {
			// Is this node detected as potentially dangerous by parcoach?
			if(!optMpiTaint && collMap[BB]!="NAVS") continue;
			if(optMpiTaint && mpiCollMap[BB][OP_com]!="NAVS") continue;	


			isColWarningParcoach = true;
			nbCondsParcoachOnly++;
			conditionSetParcoachOnly.insert(BB);

			DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
			const Instruction *inst = BB->getTerminator();
			DebugLoc loc = inst->getDebugLoc();
			if (loc) {
			  COND_lines.append(" ").append(to_string(loc.getLine()));
			  COND_lines.append(" (").append(loc->getFilename()).append(")");
			} else {
			  COND_lines.append(" ").append("?");
			  COND_lines.append(" (").append("?").append(")");
			}

		} // END FOR


		// Is there at least one node from the IPDF+ detected as potentially
		// dangerous by parcoach
		if (isColWarningParcoach) {
			nbWarningsParcoachOnly++;
			warningSetParcoachOnly.insert(CI);

			WarningMsg = to_string(nbWarnings) + " - " + OP_name + " line " + to_string(OP_line) + \
								 " possibly not called by all processes because of conditional(s) " \
								 "line(s) " + COND_lines;
			mdNode = MDNode::get(i->getContext(),
				MDString::get(i->getContext(), WarningMsg));
			i->setMetadata("intra.inst.warning",mdNode);
			Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
			Diag.print(ProgName, errs(), 1,1);
		}
	}
}


/*
 * INSTRUMENTATION
 */

void
ParcoachAnalysisIntra::instrumentFunction(llvm::Function *F){
		for(Function::iterator bb = F->begin(), e = F->end(); bb!=e; ++bb) {
			for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
				Instruction *Inst=&*i;
				string Warning = getWarning(*Inst);
				// Debug info (line in the source code, file)
				DebugLoc DLoc = i->getDebugLoc();
				string File="o"; int OP_line = -1;
				if(DLoc){
					OP_line = DLoc.getLine();
					File=DLoc->getFilename();
				}
				// call instruction
				if(CallInst *CI = dyn_cast<CallInst>(i)) {
					Function *callee = CI->getCalledFunction();
					if(callee==NULL) continue;
					string OP_name = callee->getName().str();
					int OP_color = getCollectiveColor(callee);

					// Before finalize or exit/abort
					if(callee->getName().equals("MPI_Finalize") || callee->getName().equals("MPI_Abort")){
						errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n";
						insertCC(Inst,v_coll.size()+1, OP_name, OP_line, Warning, File);
						//nbCC++;
						continue;
					}
					// Before a collective
					if(OP_color>=0){
						errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n";
						insertCC(Inst,OP_color, OP_name, OP_line, Warning, File);
						//nbCC++;
					}
					// Before a return instruction
					if(isa<ReturnInst>(i)){
						errs() << "-> insert check before return statement line " << OP_line << "\n";
						insertCC(Inst,v_coll.size()+1, "Return", OP_line, Warning, File);
						//nbCC++;
				} // END IF
			} // END FOR
		} // END FOR
	}
}


// Check Collective function before a collective
// + Check Collective function before return statements
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line,
// char* OP_warnings, char *FILE_name)
// --> void check_collective_UPC(int OP_color, const char* OP_name,
// int OP_line, char* warnings, char *FILE_name)
void
ParcoachAnalysisIntra::insertCC(llvm::Instruction *I, int OP_color,
		std::string OP_name, int OP_line,
		llvm::StringRef WarningMsg,
		llvm::StringRef File){

		IRBuilder<> builder(I);
		// Arguments of the new function
		vector<const Type *> params = vector<const Type *>();
		params.push_back(Type::getInt32Ty(M.getContext())); // OP_color
		params.push_back(PointerType::getInt8PtrTy(M.getContext())); // OP_name
		Value *strPtr_NAME = builder.CreateGlobalStringPtr(OP_name);
		params.push_back(Type::getInt32Ty(M.getContext())); // OP_line
		params.push_back(PointerType::getInt8PtrTy(M.getContext())); // OP_warnings
		const std::string Warnings = WarningMsg.str();
		Value *strPtr_WARNINGS = builder.CreateGlobalStringPtr(Warnings);
		params.push_back(PointerType::getInt8PtrTy(M.getContext())); // FILE_name
		const std::string Filename = File.str();
		Value *strPtr_FILENAME = builder.CreateGlobalStringPtr(Filename);
		// Set new function name, type and arguments
		FunctionType *FTy =FunctionType::get(Type::getVoidTy(M.getContext()),
				ArrayRef<Type *>((Type**)params.data(),
					params.size()),false);
		Value * CallArgs[] = {ConstantInt::get(Type::getInt32Ty(M.getContext()), OP_color), strPtr_NAME, ConstantInt::get(Type::getInt32Ty(M.getContext()), OP_line), strPtr_WARNINGS, strPtr_FILENAME};
		std::string FunctionName;

		if (isMpiCollective(OP_color)) {
			FunctionName = "check_collective_MPI";
		} else if (isOmpCollective(OP_color)) {
			FunctionName = "check_collective_OMP";
		} else if (isUpcCollective(OP_color)) {
			FunctionName = "check_collective_UPC";
		} else {
			FunctionName = "check_collective_return";
		}

		Value * CCFunction = M.getOrInsertFunction(FunctionName, FTy);
		// Create new function
		CallInst::Create(CCFunction, ArrayRef<Value*>(CallArgs), "", I);
		errs() << "=> Insertion of " << FunctionName << " (" << OP_color << ", " << OP_name << ", " << OP_line << ", " << WarningMsg << ", " << File <<  ")\n";
}


std::string
ParcoachAnalysisIntra::getWarning(llvm::Instruction &inst){
		string warning = " ";
		if (MDNode *node = inst.getMetadata("intra.inst.warning")) {
			if (Metadata *value = node->getOperand(0)) {
				MDString *mdstring = cast<MDString>(value);
				warning = mdstring->getString();
			}
		}else {
			//errs() << "Did not find metadata\n";
		}
		return warning;
}
