// Parcoach.cpp - implements LLVM Compile Pass which checks errors caused by MPI collective operations
//
// This pass inserts functions

// Interprocedural Analysis: Each function detected as potentially leading to a deadlock keeps a metadata = 1
// Then, in the caller function, the callee is considered as a collective (iPDF is computed,...) 


#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/User.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/DominanceFrontierImpl.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include <llvm/Analysis/LoopInfo.h>
#include "llvm/TableGen/Error.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Signals.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"

#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"
#include <set>



#include <cstdio>
#include <string>
#include <fstream>
#include <sstream>


using std::vector;
using namespace llvm;
using namespace std;


// COLLECTIVES DECLARATION
#include "Collectives.h"

// POSTDOMINANCE + INSTRUMENTATION + METADATA + OTHER
#include "Utils.h"



namespace {

/*
 * Module Pass with runOnSCC 
 *   -> check MPI collective operations  OK
 *   -> if one set of collectives have a PDF+ non null, instrument all collectives + insert a call before return statements OK
 *   -> get the conditional instruction in the PDF+ OK
 *
 *  upcri_check_finalbarrier  already checks if all UPC threads will call the same number of barriers 
 */


  //struct ParcoachInstr : public CallGraphSCCPass {
  struct ParcoachInstr : public ModulePass {
	static char ID;
	// Keep statistics on the entire program
	static int STAT_Total_collectives;
	static int STAT_Total_warnings;

	//ParcoachInstr() : CallGraphSCCPass(ID){}
	ParcoachInstr() : ModulePass(ID){}


	// CallGraphSCCPass appears to be a special case that doesn't support every analysis very well.. 
	// use a runOnModule Pass to use Dominators infos
	virtual bool runOnModule(Module &M)
	{
		// Each module will do that...
		std::sort(MPI_v_coll.begin(), MPI_v_coll.end());
		//std::sort(UPC_v_coll.begin(), UPC_v_coll.end());
		std::sort(OMP_v_coll.begin(), OMP_v_coll.end());
		std::merge(MPI_v_coll.begin(),MPI_v_coll.end(), OMP_v_coll.begin(), OMP_v_coll.end(),v_coll.begin());

		CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
		scc_iterator<CallGraph*> cgSccIter = scc_begin(&CG);
		CallGraphSCC curSCC(&cgSccIter);
		while(!cgSccIter.isAtEnd()){
			const vector<CallGraphNode*> &nodeVec = *cgSccIter;
			curSCC.initialize(nodeVec.data(), nodeVec.data() + nodeVec.size());
			runOnSCC(curSCC);
			++cgSccIter;
		}
		return false;
	}

	void instrumentFunction(Function &F)
	{
		Module* M = F.getParent();	
		//errs() << "==> Function " << F->getName() << " is instrumented:\n";
		for(Function::iterator bb = F.begin(), e = F.end(); bb!=e; ++bb)  
		{
			for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i){
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
				if(CallInst *CI = dyn_cast<CallInst>(i)){
					Function *f = CI->getCalledFunction();
					string OP_name = f->getName().str();
					if(f->getName().equals("MPI_Finalize")){
						instrumentCC(M,Inst,v_coll.size()+1, "MPI_Finalize", OP_line, Warning, File);
						continue;
					}
					int OP_color = isCollectiveFunction(*f);
					if(OP_color>=0){
						 instrumentCC(M,Inst,OP_color, OP_name, OP_line, Warning, File);
					}
 
					// return instruction
					if(isa<ReturnInst>(i)){
						instrumentCC(M,Inst,v_coll.size()+1, "Return", OP_line, Warning, File); 
					}
				}
			}
		}
	}

	void checkCollectives(Function &F,StringRef filename, int &STAT_collectives, int &STAT_warnings){
		MDNode* mdNode;
		// Warning info
		StringRef WarningMsg;
		const char *ProgName="PARCOACH";
		SMDiagnostic Diag;
		std::string COND_lines;

		// Get analyses
		PostDominatorTree &PDT=getAnalysis<PostDominatorTree>(F);

		// Process the function by visiting all instructions
		for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
			Instruction *i=&*I;
			BasicBlock *BB = i->getParent();
			// Debug info (line in the source code, file)
			DebugLoc DLoc = i->getDebugLoc();
			StringRef File=""; unsigned OP_line=0;
			if(DLoc){ 
				OP_line = DLoc.getLine();
				File=DLoc->getFilename();
			}

			// FOUND A CALL INSTRUCTION
			CallInst *CI = dyn_cast<CallInst>(i);
			if(!CI) continue;

			Function *f = CI->getCalledFunction();
			if(!f) continue;

			string OP_name = f->getName().str();

			// Is it a call set has containing collectives or a possible deadlock (NAVS)?
			if(getFuncSummary(*f).find("NAVS")!=std::string::npos || getFuncSummary(*f)!=""){
				// Check is the PDF is non null
				vector<BasicBlock * > iPDF = iterated_postdominance_frontier(PDT, BB);
				vector<BasicBlock *>::iterator Bitr;
				if(iPDF.size()!=0){
					COND_lines="";
					for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
						TerminatorInst* TI = (*Bitr)->getTerminator();
						DebugLoc BDLoc = TI->getDebugLoc();
						COND_lines.append(" ").append(to_string(BDLoc.getLine()));
					}
					STAT_warnings++; // update statistics 
					STAT_Total_warnings++; // update statistics
					WarningMsg = OP_name + " (containing colectives:" + getFuncSummary(*f) + ") line " + to_string(OP_line) + " possibly not called by all processes because of conditional(s) line(s) " + COND_lines;
					mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
					i->setMetadata("inst.warning",mdNode);
					Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
					Diag.print(ProgName, errs(), 1,1); 
				}
			}
			// Is it a collective call?
			int OP_color = isCollectiveFunction(*f);
			if (OP_color < 0)
				continue;

			errs() << "--> Found " << OP_name << " line " << OP_line << ", OP_color=" << OP_color << "\n";
			STAT_collectives++; // update statistics 
			STAT_Total_collectives++; // update statistics 

			// Check if the PDF+ is non null - no need to check if the coll is in a loop (for is considered as a cond)
			vector<BasicBlock * > iPDF = iterated_postdominance_frontier(PDT, BB);
			vector<BasicBlock *>::iterator Bitr;
			int warning=0;
			if(iPDF.size()!=0){ 
				COND_lines="";
				for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
					TerminatorInst* TI = (*Bitr)->getTerminator();
					if(getBBcollSequence(*TI)=="NAVS"){
						DebugLoc BDLoc = TI->getDebugLoc();
						COND_lines.append(" ").append(to_string(BDLoc.getLine()));
						warning=1;
					}
				}
				errs() << "}\n";
				if(warning==1){
					STAT_warnings++; // update statistics 
					STAT_Total_warnings++; // update statistics
					WarningMsg = OP_name + " line " + to_string(OP_line) + " possibly not called by all processes because of conditional(s) line(s) " + COND_lines;
					mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
					i->setMetadata("inst.warning",mdNode);
					Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
					Diag.print(ProgName, errs(), 1,1);
				}	
			}
		}
	}

	string get_BB_collectives(BasicBlock *BB){
		string CollSequence="empty";
		for(BasicBlock::iterator i = BB->begin(), e = BB->end(); i != e; ++i){
			if(CallInst *CI = dyn_cast<CallInst>(i))
                        {
				Function *f = CI->getCalledFunction();
                                if(!f) continue;

				string OP_name = f->getName().str();
	
				if(getFuncSummary(*f)!=""){
					if(CollSequence=="empty"){
						CollSequence=getFuncSummary(*f);
					}else{	
						CollSequence.append(" ");
						CollSequence.append(getFuncSummary(*f));	
					}
				}

				if (isCollectiveFunction(*f) < 0)
					continue;

				if(CollSequence=="empty"){
					CollSequence=OP_name;
				}else{
					CollSequence.append(" ");
					CollSequence.append(OP_name);
				}	
			}
		}
		return CollSequence;	
	}


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
						N.append(" ");
						if(get_BB_collectives(Pred)!="empty")
							N.append(get_BB_collectives(Pred)); // add the coll in Pred
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
						if(get_BB_collectives(Pred)!="empty")
							seq_temp.append(get_BB_collectives(Pred));
					}
					StringRef CollSequence_temp=StringRef(seq_temp);
					// if temp != coll seq -> warning + keep the bb in the PDF+
					//errs() << "  >>> " << CollSequence_temp.str() << " = " << getBBcollSequence(*TI) << " ?\n";
					if(CollSequence_temp.str() != getBBcollSequence(*TI) || CollSequence_temp.str()=="NAVS" || getBBcollSequence(*TI)=="NAVS"){
						mdNode = MDNode::get(Pred->getContext(),MDString::get(Pred->getContext(),"NAVS"));
						TI->setMetadata("inst.collSequence",mdNode);
					}
				}
			}
		}
	}



	bool runOnSCC(CallGraphSCC &SCC)
	{
		StringRef File;
		vector<char *>::iterator vitr;
		int STAT_collectives=0;
		int STAT_warnings=0;
		StringRef FuncSummary;

		for(CallGraphSCC::iterator CGit=SCC.begin(); CGit != SCC.end(); CGit++)
		{
			// Reset statistics
			STAT_warnings=0; // number of warnings in the function
			STAT_collectives=0;// number of collectives in the function

			// Function this call graph node represents
			Function *F=(*CGit)->getFunction();
			// Is the body of this function unkown (basic block list empty if so)?
			if(!F || F->isDeclaration()){ continue; }
			string File = getFunctionFilename(*F);

			// Get analyses
			//DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
			//PostDominatorTree &PDT=getAnalysis<PostDominatorTree>(*F);
			//LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>(*F).getLoopInfo();

			// Metadata
			MDNode* mdNode;

			errs() << "\033[0;35m====== STARTING PARCOACH on function " << F->getName().str() << " ======\033[0;0m\n";
			STAT_collectives=0; STAT_warnings=0; 

			// (1) Set the sequence of collectives with a BFS from the exit node in the CFG
			BFS(F);
			// (2) Check calls to collectives with PDF+ of each collective node
			checkCollectives(*F, File, STAT_collectives, STAT_warnings);

			// Keep a metadata for the summary of the function
			// Set the summary of a function even if no potential errors detected
			// Then take into account the summary when setting the sequence of collectives of a BB
			BasicBlock &entry = F->getEntryBlock();
			FuncSummary=getBBcollSequence(*entry.getTerminator());	
			mdNode = MDNode::get(F->getContext(),MDString::get(F->getContext(),FuncSummary));
			F->setMetadata("func.summary",mdNode);  	
			errs() << "Summary of function " << F->getName() << " : " << getFuncSummary(*F) << "\n";
			// Instrument the code if a potential deadlock has been found in the function
			// SPMD programs
			if(STAT_warnings>0)
			{
				errs() << "\033[0;36m=========  Function " << F->getName() << " statistics  ==========\033[0;0m\n";
				errs() << "\033[0;36m # collectives=" << STAT_collectives << " - # warnings=" << STAT_warnings  << "\033[0;0m\n";
				instrumentFunction(*F);
				errs() << "\033[0;36m=============================================\033[0;0m\n"; 
			}

		}
		return true;
	}


	
	virtual void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.setPreservesAll();
		AU.addRequired<CallGraphWrapperPass>();
		AU.addRequired<DominatorTreeWrapperPass>();
		AU.addRequired<PostDominatorTree>();
		AU.addRequired<LoopInfoWrapperPass>();
	};

	virtual bool doFinalization(Module &M) {
		// Print the statisctics per module
		if(STAT_Total_collectives>0){
			errs() << "\n\033[0;36m=============================================\033[0;0m\n";
			errs() << "\033[0;36m============  PARCOACH STATISTICS ===========\033[0;0m\n";
			errs() << "\033[0;36m=============================================\033[0;0m\n";
			errs() << "\033[0;36m Number of collectives: " << STAT_Total_collectives << "\033[0;0m\n";
			errs() << "\033[0;36m Number of warnings: " << STAT_Total_warnings << "\033[0;0m\n";
			errs() << "\033[0;36m=============================================\033[0;0m\n"; 
		}
		return true;
	}


  };
}

char ParcoachInstr::ID = 0;
int ParcoachInstr::STAT_Total_collectives = 0;
int ParcoachInstr::STAT_Total_warnings = 0;
static RegisterPass<ParcoachInstr> tmp("parcoach", "Parcoach Analysis", true, true );
