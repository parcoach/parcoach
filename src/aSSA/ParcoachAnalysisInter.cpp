#include "Collectives.h"
#include "Options.h"
#include "ParcoachAnalysisInter.h"
#include "Utils.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"


using namespace llvm;
using namespace std;

int ParcoachAnalysisInter::id = 0;

void
ParcoachAnalysisInter::run(){
	// Parcoach analysis

	/* (1) BFS on each function of the Callgraph in reverse topological order
	 *  -> set a function summary with sequence of collectives
	 *  -> keep a set of collectives per BB and set the conditionals at NAVS if
	 *     it can lead to a deadlock
	 */
	errs() << " (1) BFS\n";
	scc_iterator<PTACallGraph *> cgSccIter = scc_begin(&PTACG);
	while(!cgSccIter.isAtEnd()) {
		const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;
		for (PTACallGraphNode *node : nodeVec) {
			Function *F = node->getFunction();
			if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(F))
				continue;
			//DBG: //errs() << "Function: " << F->getName() << "\n";
      if(optMpiTaint)
				MPI_BFS(F);
			else
				BFS(F);
		}
		++cgSccIter;
	}

	/* (2) Check collectives */
	errs() << " (2) CheckCollectives\n";
	cgSccIter = scc_begin(&PTACG);
	while(!cgSccIter.isAtEnd()) {
		const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;
		for (PTACallGraphNode *node : nodeVec) {
			Function *F = node->getFunction();
			if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(F))
				continue;
			//DBG: //errs() << "Function: " << F->getName() << "\n";
			checkCollectives(F);
			// Get the number of MPI communicators
			if(F->getName() == "main")
				errs() << " ... Found " << mpiCollperFuncMap[F].size() << " MPI communicators in " << F->getName() << "\n";
		}
		++cgSccIter;
	}

	// If you always want to instrument the code, uncomment the following line
	//if(nbWarnings !=0){
	if(nbWarnings !=0 && !disableInstru){
		errs() << "\033[0;35m=> Static instrumentation of the code ...\033[0;0m\n";
		for (Function &F : M) {
			instrumentFunction(&F);
		}
	}

	errs() << " ... Parcoach analysis done\n";
}


/*
 * FUNCTIONS USED TO CHECK COLLECTIVES
 */

// Get the sequence of collectives in a BB
void 
ParcoachAnalysisInter::setCollSet(BasicBlock *BB){

	for(auto i = BB->rbegin(), e = BB->rend(); i != e; ++i)
	{
    const Instruction *inst = &*i;
    if(const CallInst *CI = dyn_cast<CallInst>(inst)) 
		{
      Function *callee = CI->getCalledFunction();

      //// Indirect calls
      if(callee == NULL)
      {
        for (const Function *mayCallee : PTACG.indirectCallMap[inst]) {
          if (isIntrinsicDbgFunction(mayCallee))  continue;
          callee = const_cast<Function *>(mayCallee);
          // Is it a function containing collectives?
					if(!collperFuncMap[callee].empty()) // && collMap[BB]!="NAVS")
						collMap[BB] = collperFuncMap[callee] +  " " + collMap[BB];
          // Is it a collective operation?
          if(isCollective(callee)){
            string OP_name = callee->getName().str();
            collMap[BB] = OP_name + " " + collMap[BB]; 
          }
        }
      //// Direct calls
      }else{
        // Is it a function containing collectives?
				if(!collperFuncMap[callee].empty()) // && collMap[BB]!="NAVS")
          collMap[BB] = collperFuncMap[callee] + " " + collMap[BB];
        // Is it a collective operation?
        if(isCollective(callee)){
          string OP_name = callee->getName().str();
          collMap[BB] = OP_name + " " + collMap[BB];
        }
      }
    }
  }
}

// Get the sequence of collectives in a BB, per MPI communicator
void ParcoachAnalysisInter::setMPICollSet(BasicBlock *BB){

	for(auto i = BB->rbegin(), e = BB->rend(); i != e; ++i)
  {
    const Instruction *inst = &*i;
    if(const CallInst *CI = dyn_cast<CallInst>(inst))
    {
      Function *callee = CI->getCalledFunction();

      //// Indirect calls
      if(callee == NULL)
      {
        for (const Function *mayCallee : PTACG.indirectCallMap[inst]) {
          if (isIntrinsicDbgFunction(mayCallee))  continue;
          callee = const_cast<Function *>(mayCallee);
          // Is it a function containing collectives?
          if(!mpiCollperFuncMap[callee].empty()){ // && mpiCollMap[BB]!="NAVS"){
						auto &BBcollMap = mpiCollMap[BB];
						for(auto& pair : mpiCollperFuncMap[callee]){
        			BBcollMap[pair.first] = pair.second + " " + BBcollMap[pair.first];	
						}
          }
          // Is it a collective operation?
          if(isCollective(callee)){
            string OP_name = callee->getName().str();
						int OP_color = getCollectiveColor(callee);
						Value* OP_com = CI->getArgOperand(Com_arg_id(OP_color));
            mpiCollMap[BB][OP_com] = OP_name + " " + mpiCollMap[BB][OP_com];
            //errs() << "in BB CollSet i  = " << collMap[BB] << "\n";
          }
        }
      //// Direct calls
      }else{
        // Is it a function containing collectives?
        if(!mpiCollperFuncMap[callee].empty()){ // && collMap[BB]!="NAVS"){
						auto &BBcollMap = mpiCollMap[BB];
						for(auto& pair : mpiCollperFuncMap[callee]){
        			BBcollMap[pair.first] = pair.second + " " + BBcollMap[pair.first];	
						}
          //mpiCollMap[BB] = mpiCollperFuncMap[callee] + " " + mpiCollMap[BB];
          //errs() << "in BB (0) CollSet d = " << collMap[BB] << "\n";
        }
        // Is it a collective operation?
        if(isCollective(callee)){
          string OP_name = callee->getName().str();
					int OP_color = getCollectiveColor(callee);
					Value* OP_com = CI->getArgOperand(Com_arg_id(OP_color));
          //collMap[BB].append(" ").append(OP_name);
          //errs() << "in BB (1) CollSet d = " << collMap[BB] << "\n";
          mpiCollMap[BB][OP_com] = OP_name + " " + mpiCollMap[BB][OP_com];
          //errs() << "in BB (2) CollSet d = " << collMap[BB] << "\n";
        }
      }
    }
  }
}

void
ParcoachAnalysisInter::MPI_BFS(llvm::Function *F){
  std::vector<BasicBlock *> Unvisited;
  //map<BasicBlock *, bool seen> ;

  // GET ALL EXIT NODES 
  for(BasicBlock &I : *F){
    if(isa<ReturnInst>(I.getTerminator())){
      Unvisited.push_back(&I);
      setMPICollSet(&I);
			
		/*	for(const auto& itm : mpiCollMap){
				errs() << itm.first << "\n";
				for(const auto& itc : itm.second){
					errs() << itc.first << "{" << itc.second << "}\n";
				}
			}*/

    }
  }
  while(Unvisited.size()>0)
  {
    BasicBlock *header=*Unvisited.begin();
    Unvisited.erase(Unvisited.begin());
    bbVisitedMap[header]=true;
    pred_iterator PI=pred_begin(header), E=pred_end(header);
    for(; PI!=E; ++PI){
      BasicBlock *Pred = *PI;
      //errs() << F->getName() << " - BB: " << Pred->getName() << "\n";
      // BB NOT SEEN BEFORE
      if(bbVisitedMap[Pred] != true){
				for(auto& pair : mpiCollMap[header])
        	mpiCollMap[Pred][pair.first] = mpiCollMap[header][pair.first];

				/*for(auto& pair : mpiCollMap[Pred]){
      		errs() << pair.first << "{" << pair.second << "}\n";
  			}*/
        setMPICollSet(Pred);
				/*for(auto& pair : mpiCollMap[Pred]){
      		errs() << pair.first << "{" << pair.second << "}\n";
  			}*/
        bbVisitedMap[Pred]=true;
        Unvisited.push_back(Pred);
      // BB ALREADY SEEN
      }else{
        //errs() << F->getName() << " - BB: " << Pred->getName() << " already seen\n";
				//ComCollMap temp = mpiCollMap[Pred];
				ComCollMap temp;
				for(auto& pair : mpiCollMap[Pred]){
					temp[pair.first] = pair.second;
				}
				/*errs() << "* Temp = \n";
				for(auto& pair : temp){
      		errs() << pair.first << "{" << pair.second << "}\n";
  			}*/
			 	mpiCollMap[Pred] = mpiCollMap[header];	
				/*for(auto& pair : mpiCollMap[Pred]){
          errs() << pair.first << "{" << pair.second << "}\n";
        }	*/			
        setMPICollSet(Pred);
				/*for(auto& pair : mpiCollMap[Pred]){
          errs() << pair.first << "{" << pair.second << "}\n";
        }	*/			


				for(auto& pair : mpiCollMap[Pred]){
					if(temp[pair.first] != mpiCollMap[Pred][pair.first]){
						//errs() << temp[pair.first] << " = " << mpiCollMap[Pred][pair.first] << "?\n";
						mpiCollMap[Pred][pair.first]="NAVS";
					}
				}
				/*for(auto& pair : mpiCollMap[Pred]){
      		errs() << pair.first << "{" << pair.second << "}\n";
  			}*/

      }
    }
  }
  BasicBlock &entry = F->getEntryBlock();
  mpiCollperFuncMap[F]=mpiCollMap[&entry];
  /*errs() << F->getName() << " summary = \n";
  for(auto& pair : mpiCollperFuncMap[F]){
    	errs() << pair.first << "{" << pair.second << "}\n";
  }*/
}


void
ParcoachAnalysisInter::BFS(llvm::Function *F){
	std::vector<BasicBlock *> Unvisited;

	// GET ALL EXIT NODES 
  for(BasicBlock &I : *F){
    if(isa<ReturnInst>(I.getTerminator())){
			Unvisited.push_back(&I);
			setCollSet(&I);
		}
	}
	while(Unvisited.size()>0)
  {
		BasicBlock *header=*Unvisited.begin();
    Unvisited.erase(Unvisited.begin());
		bbVisitedMap[header]=true;
		pred_iterator PI=pred_begin(header), E=pred_end(header);
		for(; PI!=E; ++PI){
      BasicBlock *Pred = *PI;
			//errs() << F->getName() << " - BB: " << Pred->getName() << "\n";
			// BB NOT SEEN BEFORE
			if(bbVisitedMap[Pred] != true){
				collMap[Pred] = collMap[header];
				setCollSet(Pred);
				bbVisitedMap[Pred]=true;
				Unvisited.push_back(Pred);
			// BB ALREADY SEEN
			}else{
				//errs() << F->getName() << " - BB: " << Pred->getName() << " already seen\n";
				string temp = collMap[Pred];
				collMap[Pred] = collMap[header];
				setCollSet(Pred);
				//errs() << collMap[Pred] << " = " << temp << "?\n";
				if(temp != collMap[Pred])
					collMap[Pred]="NAVS";
			}
		}
	}
	BasicBlock &entry = F->getEntryBlock();
	collperFuncMap[F]=collMap[&entry];
	//errs() << F->getName() << " summary = " << collperFuncMap[F] << "\n";
}


void
ParcoachAnalysisInter::checkCollectives(llvm::Function *F){
	StringRef FuncSummary;
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
		Value* OP_com = CI->getArgOperand(Com_arg_id(OP_color)); // 0 for Barrier only
		//CI->getArgOperand(0)->dump();
		//errs() << "Found " << OP_name << " on " << OP_com << " line " << OP_line << "\n";


		nbCollectivesFound++;

		bool isColWarning = false;
		bool isColWarningParcoach = false;

		// Get conditionals from the callsite
		set<const BasicBlock *> callIPDF;
		DG->getCallInterIPDF(CI, callIPDF);
		// For the summary-based approach, use the following instead of the previous line 
		//DG->getCallIntraIPDF(CI, callIPDF);


		for (const BasicBlock *BB : callIPDF) {

			// Is this node detected as potentially dangerous by parcoach?
			if(!optMpiTaint && collMap[BB]!="NAVS") continue;
      if(optMpiTaint && mpiCollMap[BB][OP_com]!="NAVS") continue;		



			isColWarningParcoach = true;
			nbCondsParcoachOnly++;
			conditionSetParcoachOnly.insert(BB);

			// Is this condition tainted?
			const Value *cond = getBasicBlockCond(BB);

			if ( !cond || (!optNoDataFlow && !DG->isTaintedValue(cond)) ) {
				const Instruction *instE = BB->getTerminator();
				DebugLoc locE = instE->getDebugLoc();
				//errs() << " -> Condition not tainted for a conditional with NAVS line " << locE.getLine() << " in " << locE->getFilename() << "\n";
				continue;
			}

			isColWarning = true;
			nbConds++;
			conditionSet.insert(BB);

			DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
			const Instruction *inst = BB->getTerminator();
			DebugLoc loc = inst->getDebugLoc();
			COND_lines.append(" ").append(to_string(loc.getLine()));
			COND_lines.append(" (").append(loc->getFilename()).append(")");

			if (optDotTaintPaths) {
				string dotfilename("taintedpath-");
				string cfilename = loc->getFilename();
				size_t lastpos_slash = cfilename.find_last_of('/');
				if (lastpos_slash != cfilename.npos)
					cfilename = cfilename.substr(lastpos_slash+1, cfilename.size());
				dotfilename.append(cfilename).append("-");
				dotfilename.append(to_string(loc.getLine())).append(".dot");
				DG->dotTaintPath(cond, dotfilename, i);
			}
		}

		// Is there at least one node from the IPDF+ detected as potentially
		// dangerous by parcoach
		if (isColWarningParcoach) {
			nbWarningsParcoachOnly++;
			warningSetParcoachOnly.insert(CI);
		}

		// Is there at least one node from the IPDF+ tainted
		if (!isColWarning)
			continue;

    //CI->dump();
		nbWarnings++;
		warningSet.insert(CI);

		WarningMsg = to_string(nbWarnings) + " - " + OP_name + " line " + to_string(OP_line) + \
			" possibly not called by all processes because of conditional(s) " \
			"line(s) " + COND_lines;
		mdNode = MDNode::get(i->getContext(),
				MDString::get(i->getContext(), WarningMsg));
		i->setMetadata("inter.inst.warning"+to_string(id),mdNode);
		Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
		Diag.print(ProgName, errs(), 1,1);
	}
}



/*
 * INSTRUMENTATION
 */

void
ParcoachAnalysisInter::instrumentFunction(llvm::Function *F){
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
					nbCC++;
					continue;
				}
				// Before a collective
				if(OP_color>=0){
					errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n";
					insertCC(Inst,OP_color, OP_name, OP_line, Warning, File);
					nbCC++;
				}
			}
		}
	}
}



// Check Collective function before a collective
// + Check Collective function before return statements
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line,
// char* OP_warnings, char *FILE_name)
// --> void check_collective_UPC(int OP_color, const char* OP_name,
// int OP_line, char* warnings, char *FILE_name)
void
ParcoachAnalysisInter::insertCC(llvm::Instruction *I, int OP_color,
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
ParcoachAnalysisInter::getWarning(llvm::Instruction &inst){
	string warning = " ";
	if (MDNode *node = inst.getMetadata("inter.inst.warning"+to_string(id))) {
		if (Metadata *value = node->getOperand(0)) {
			MDString *mdstring = cast<MDString>(value);
			warning = mdstring->getString();
		}
	}else {
		errs() << "Did not find metadata\n";
	}
	return warning;
}
