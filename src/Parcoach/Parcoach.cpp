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
#include <vector>
#include <fstream>
#include <sstream>


#define DEBUG_TYPE "parcoach"

using std::vector;
using namespace llvm;
using namespace std;


// COLLECTIVES DECLARATION

std::vector<char*> MPI_v_coll = {(char*)"MPI_Barrier", (char*)"MPI_Bcast", (char*)"MPI_Scatter", (char*)"MPI_Scatterv", 
			     (char*)"MPI_Gather", (char*)"MPI_Gatherv", (char*)"MPI_Allgather", (char*)"MPI_Allgatherv", 
			     (char*)"MPI_Alltoall", (char*)"MPI_Alltoallv", (char*)"MPI_Alltoallw", (char*)"MPI_Reduce", 
			     (char*)"MPI_Allreduce", (char*)"MPI_Reduce_scatter", (char*)"MPI_Scan", (char*)"MPI_Comm_split",
			     (char*)"MPI_Comm_create", (char*)"MPI_Comm_dup", (char*)"MPI_Comm_dup_with_info", (char*)"MPI_Ibarrier", 
			     (char*)"MPI_Igather", (char*)"MPI_Igatherv", (char*)"MPI_Iscatter", (char*)"MPI_Iscatterv", 
			     (char*)"MPI_Iallgather", (char*)"MPI_Iallgatherv", (char*)"MPI_Ialltoall", (char*)"MPI_Ialltoallv", 
			     (char*)"MPI_Ialltoallw", (char*)"MPI_Ireduce", (char*)"MPI_Iallreduce", (char*)"MPI_Ireduce_scatter_block",
			     (char*)"MPI_Ireduce_scatter", (char*)"MPI_Iscan", (char*)"MPI_Iexscan",(char*)"MPI_Ibcast"};

std::vector<char*> UPC_v_coll = {(char*)"_upcr_notify", (char*)"_upcr_all_broadcast", (char*)"_upcr_all_scatter", (char*)"_upcr_all_gather", 
				(char*)"_upcr_all_gather_all", (char*)"_upcr_all_exchange", (char*)"_upcr_all_permute",
				(char*)"_upcr_all_reduce", (char*)"_upcr_prefix_reduce", (char*)"_upcr_all_sort"}; // upc_barrier= upc_notify+upc_wait, TODO: add all collectives
std::vector<char*> OMP_v_coll = {(char*)"__kmpc_cancel_barrier"}; // TODO: may be more complicated..

std::vector<char*> v_coll(MPI_v_coll.size() + OMP_v_coll.size()); // TODO: add OMP_v_coll





namespace {


/*
 * POSTDOMINANCE
 *   X postominates Y if X appears on every path from Y to exit
 *   n postdominates m if there is a path from n to m in the postdominator tree
 */

// Return true if the specified block postdominates the entry block
static bool
blockDominatesEntry(BasicBlock *BB, PostDominatorTree &PDT, DominatorTree *DT, BasicBlock *EntryBlock) {
        if (PDT.dominates(BB, EntryBlock))
                return true;

        return false;
}

// PDF computation
vector<BasicBlock * > postdominance_frontier(PostDominatorTree &PDT, BasicBlock *BB){

        vector<BasicBlock * > PDF;
        PDF.clear();
        DomTreeNode *DomNode = PDT.getNode(BB);

        for (auto it = pred_begin(BB), et = pred_end(BB); it != et; ++it){
                BasicBlock* predecessor = *it;
                // does BB immediately dominate this predecessor?
                DomTreeNode *ID = PDT[*it]; //->getIDom();
                if(ID && ID->getIDom()!=DomNode && *it!=BB){
                        PDF.push_back(*it);
                }
        }
        for (DomTreeNode::const_iterator NI = DomNode->begin(), NE = DomNode->end(); NI != NE; ++NI) {
                DomTreeNode *IDominee = *NI;
                vector<BasicBlock * > ChildDF = postdominance_frontier(PDT, IDominee->getBlock());
                vector<BasicBlock * >::const_iterator CDFI = ChildDF.begin(), CDFE = ChildDF.end();
                for (; CDFI != CDFE; ++CDFI) {
                        if (PDT[*CDFI]->getIDom() != DomNode && *CDFI!=BB){
                                PDF.push_back(*CDFI);
                        }
                }
        }
        return PDF;
}



void print_iPDF(vector<BasicBlock * > iPDF, BasicBlock *BB){
	vector<BasicBlock * >::const_iterator Bitr;
	errs() << "iPDF(" << BB->getName().str() << ") = {";
	for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
		errs() << "- " << (*Bitr)->getName().str() << " ";
	}
	errs() << "}\n";
}

// PDF+ computation
vector<BasicBlock * > iterated_postdominance_frontier(PostDominatorTree &PDT, BasicBlock *BB){
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



/*
 * INSTRUMENTATION
 */

// Check Collective function before a collective
// + Check Collective function before return statements
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line, char* OP_warnings, char *FILE_name)
// --> void check_collective_UPC(int OP_color, const char* OP_name, int OP_line, char* warnings, char *FILE_name)
// --> void check_collective_OMP(int OP_color, const char* OP_name, int OP_line, char* warnings, char *FILE_name)
void instrumentCC(Module *M, Instruction *I, int OP_color,std::string OP_name, int OP_line, StringRef WarningMsg, StringRef File){
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
        FunctionType *FTy =FunctionType::get(Type::getVoidTy(M->getContext()),ArrayRef<Type *>((Type**)params.data(),params.size()),false);
        Value * CallArgs[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), OP_color), strPtr_NAME, ConstantInt::get(Type::getInt32Ty(M->getContext()), OP_line), strPtr_WARNINGS, strPtr_FILENAME};
	std::string FunctionName;


	if(OP_color == v_coll.size()+1){
		FunctionName="check_collective_return";
	}else{
		if(OP_color<MPI_v_coll.size()){
			FunctionName="check_collective_MPI";
		}else{
			FunctionName="check_collective_OMP";
		}
	}
        Value * CCFunction = M->getOrInsertFunction(FunctionName, FTy);
        // Create new function
        CallInst::Create(CCFunction, ArrayRef<Value*>(CallArgs), "", I);
        DEBUG(errs() << "=> Insertion of " << FunctionName << " (" << OP_color << ", " << OP_name << ", " << OP_line << ", " << WarningMsg << ", " << File <<  ")\n");   
}


// Get metadata info
// Metadata represents optional information about an instruction (or module)

static string getWarning(Instruction &inst) {
        string warning = " ";
        if (MDNode *node = inst.getMetadata("inst.warning")) {
                if (Metadata *value = node->getOperand(0)) {
                        MDString *mdstring = cast<MDString>(value);
                        warning = mdstring->getString();
                }
        }else {  //errs() << "Did not find metadata\n";
	}
        return warning;
}


static string getFuncSummary(Function &F){
	string summary=" "; // no warning
	if (MDNode *node = F.getMetadata("func.summary")) {
		if (Metadata *value = node->getOperand(0)) { 
			MDString *mdstring = cast<MDString>(value);
			summary=mdstring->getString();
		}
	} 
	return summary;
}


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


	bool runOnSCC(CallGraphSCC &SCC)
	{
		StringRef File;
		vector<char *>::iterator vitr;
		int STAT_collectives=0;
		int STAT_warnings=0;

		// collective info 
		std::string OP_name;
		int OP_color=0;
		unsigned OP_line = 0; 
		// Warning info
		StringRef WarningMsg;
		const char *ProgName="PARCOACH";
		SMDiagnostic Diag;
		std::string COND_lines;

		StringRef FuncSummary;
		//LLVMContext& context = new LLVMContext();

		for(CallGraphSCC::iterator CGit=SCC.begin(); CGit != SCC.end(); CGit++)
		{
			CallGraphNode *node=*CGit;
			// DEBUG: Print out this call graph node or use -debug 
			// node->dump();

			// Reset statistics
			STAT_warnings=0; // number of warnings in the function
			STAT_collectives=0;// number of collectives in the function

			// Function this call graph node represents
			Function *F=(*CGit)->getFunction();
			// Is the body of this function unkown (basic block list empty if so)?
			if(!F || F->isDeclaration()){ continue; }

			// Dominance/Postdominance info
			DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
			PostDominatorTree &PDT=getAnalysis<PostDominatorTree>(*F);
			// Loop info
			LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>(*F).getLoopInfo();

			// Metadata
			MDNode* mdNode;

			errs() << "\033[0;35m====== STARTING PARCOACH on function " << F->getName().str() << " ======\033[0;0m\n";
			STAT_collectives=0; STAT_warnings=0; 

			// Process the function by visiting all basic blocks
			for(Function::iterator bb=F->begin(),E=F->end(); bb!=E; ++bb)
			{
				BasicBlock *BB = bb;
				bool inLoop = LI->getLoopFor(BB); // true if the basic block is in a loop
				Loop* BBloop = LI->getLoopFor(BB); // loop containing this basic block 
				for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i)
				{
					//context = i->getContext();
					// Debug info (line in the source code, file)
					DebugLoc DLoc = i->getDebugLoc();
					File=""; OP_line=0;
					if(DLoc){ 
						OP_line = DLoc.getLine();
						File=DLoc->getFilename();
					}

					// FOUND A CALL INSTRUCTION
					if(CallInst *CI = dyn_cast<CallInst>(i))
					{
						Function *f = CI->getCalledFunction();
						if(!f) continue;
						OP_name = f->getName().str();
						
						//errs() << "Function " << OP_name << " found line " << OP_line << "\n";

						// Is it a call set has containing a possible deadlock?
						if(getFuncSummary(*f) == "1"){
							errs() << "Function " << OP_name << " has been detected as containing a possible deadlock\n";
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
								errs() << "}\n";
								STAT_warnings++; // update statistics 
								STAT_Total_warnings++; // update statistics
								WarningMsg = OP_name + " line " + to_string(OP_line) + " possibly not called by all processes because of conditional(s) line(s) " + COND_lines;
								mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
								i->setMetadata("inst.warning",mdNode);
								Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
								Diag.print(ProgName, errs(), 1,1); 
							}
						}

						// Is it a collective call? 
						for (vitr = v_coll.begin(); vitr != v_coll.end(); vitr++){
							if(f->getName().equals(*vitr)){
								OP_color=vitr-v_coll.begin(); 
								errs() << "-> Found " << OP_name << " line " << OP_line << ", OP_color=" << OP_color << "\n";
								STAT_collectives++; // update statistics 
								STAT_Total_collectives++; // update statistics 

								// Check if the collective is in a loop 
								if(inLoop){ 
									DEBUG(errs() << "* BB " << BB->getName().str() << " is in a loop. The header is " << (BBloop->getHeader()->getName()).str() << "\n");
									STAT_warnings++; // update statistics
									STAT_Total_warnings++; // update statistics
									WarningMsg = OP_name + " line " + to_string(OP_line) + " is in a loop";
									Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
									Diag.print(ProgName, errs(), 1,1);
								}
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
									errs() << "}\n";
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
				}
			}
			// Instrument the code if a potential deadlock has been found in the function
			// SPMD programs
			if(STAT_warnings>0)
			{
				errs() << "\033[0;36m=========  Function " << F->getName() << " statistics  ==========\033[0;0m\n";
				errs() << "\033[0;36m # collectives=" << STAT_collectives << " - # warnings=" << STAT_warnings  << "\033[0;0m\n";
				Module* M = F->getParent();	
				//errs() << "==> Function " << F->getName() << " is instrumented:\n";
				for(Function::iterator bb = F->begin(), e = F->end(); bb!=e; ++bb)  
				{
					BasicBlock *BB = bb;
					for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i){
						Instruction *Inst=i;  
						string Warning = getWarning(*Inst); 
						// Debug info (line in the source code, file)
						DebugLoc DLoc = i->getDebugLoc();
						File="o"; OP_line = 0;
						if(DLoc){ 
							OP_line = DLoc.getLine();
							File=DLoc->getFilename();
						}
						// call instruction
						if(CallInst *CI = dyn_cast<CallInst>(i)){
							Function *f = CI->getCalledFunction();
							OP_name = f->getName().str();
							if(f->getName().equals("MPI_Finalize")){
								instrumentCC(M,i,v_coll.size()+1, "MPI_Finalize", OP_line, Warning, File);
							}
							for (vitr = v_coll.begin(); vitr != v_coll.end(); vitr++){
								if(f->getName().equals(*vitr)){ 
									OP_color=vitr-v_coll.begin(); 
									instrumentCC(M,i,OP_color, OP_name, OP_line, Warning, File);
								}
							}
						}
						// return instruction
						if(ReturnInst *RI = dyn_cast<ReturnInst>(i)){
							instrumentCC(M,i,v_coll.size()+1, "Return", OP_line, Warning, File); 
						}
					}
				}
				// Keep a metadata for the summary of the function
				FuncSummary=to_string(1);	
				mdNode = MDNode::get(F->getContext(),MDString::get(F->getContext(),FuncSummary));
				F->setMetadata("func.summary",mdNode);  	
				errs() << "Function " << F->getName() << " set as containing a potential deadlock: " << getFuncSummary(*F) << "\n";
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
