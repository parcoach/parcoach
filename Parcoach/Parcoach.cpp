// Parcoach.cpp - implements LLVM Compile Pass which checks errors caused by MPI collective operations
//
// This pass inserts functions



#define DEBUG_TYPE "hello"

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


#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>


using std::vector;
using namespace llvm;
using namespace std;


std::vector<char*> MPI_v_coll = {(char*)"MPI_Barrier", (char*)"MPI_Bcast", (char*)"MPI_Scatter", (char*)"MPI_Scatterv", 
			     (char*)"MPI_Gather", (char*)"MPI_Gatherv", (char*)"MPI_Allgather", (char*)"MPI_Allgatherv", 
			     (char*)"MPI_Alltoall", (char*)"MPI_Alltoallv", (char*)"MPI_Alltoallw", (char*)"MPI_Reduce", 
			     (char*)"MPI_Allreduce", (char*)"MPI_Reduce_scatter", (char*)"MPI_Scan", (char*)"MPI_Comm_split",
			     (char*)"MPI_Comm_create", (char*)"MPI_Comm_dup", (char*)"MPI_Comm_dup_with_info", (char*)"MPI_Ibarrier", 
			     (char*)"MPI_Igather", (char*)"MPI_Igatherv", (char*)"MPI_Iscatter", (char*)"MPI_Iscatterv", 
			     (char*)"MPI_Iallgather", (char*)"MPI_Iallgatherv", (char*)"MPI_Ialltoall", (char*)"MPI_Ialltoallv", 
			     (char*)"MPI_Ialltoallw", (char*)"MPI_Ireduce", (char*)"MPI_Iallreduce", (char*)"MPI_Ireduce_scatter_block",
			     (char*)"MPI_Ireduce_scatter", (char*)"MPI_Iscan", (char*)"MPI_Iexscan",(char*)"MPI_Ibcast"};

std::vector<char*> UPC_v_coll = {(char*)"_upcr_notify"}; // upc_barrier= upc_notify+upc_wait, TODO: add all collectives
std::vector<char*> OMP_v_coll = {(char*)"todo"}; // TODO

std::vector<char*> v_coll(MPI_v_coll.size() + UPC_v_coll.size()); // TODO: add OMP_v_coll





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

// Check Collective function before a MPI collective
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line, char* OP_warnings, char *FILE_name)
// add a type to handle MPI, UPC and OMP
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
        Value * CCFunction = M->getOrInsertFunction("check_collective_MPI", FTy);
        // Create new function
        CallInst::Create(CCFunction, ArrayRef<Value*>(CallArgs), "", I);
        DEBUG(errs() << "=> Insertion of check_collective_MPI(" << OP_color << ", " << OP_name << ", " << OP_line << ", " << WarningMsg << ", " << File <<  ")\n");   
}


// Check Collective function before return statements
// --> void CC(int color, int line, char* filename)
// TODO






/*
 * Function Pass
 *   -> check MPI collective operations  OK
 *   -> if one set of collectives have a PDF+ non null, instrument all collectives + insert a call before return statements NOK
 *   -> get the conditional instruction in the PDF+ 
 *
 */

struct ParcoachInstr : public FunctionPass {
	static char ID;
	static int STAT_collectives;
	static StringRef File;
	vector<char *>::iterator vitr;
	vector<BasicBlock *>::iterator Bitr;

	ParcoachInstr() : FunctionPass(ID) {}

	virtual bool runOnFunction(Function &F) {

		STAT_collectives=0;
		Module* M = F.getParent();
		BasicBlock &entry = F.getEntryBlock();

		// Dominance/Postdominance info
		DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
		PostDominatorTree &PDT=getAnalysis<PostDominatorTree>();
		// Loop info
		LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
		// collective info
		int OP_color=0;
		// Warning info
		StringRef WarningMsg;
		const char *ProgName="PARCOACH";
		SMDiagnostic Diag;

		std::sort(MPI_v_coll.begin(), MPI_v_coll.end());
		std::sort(UPC_v_coll.begin(), UPC_v_coll.end());
		std::merge(MPI_v_coll.begin(),MPI_v_coll.end(), UPC_v_coll.begin(), UPC_v_coll.end(),v_coll.begin());

		for(Function::iterator bb = F.begin(), e = F.end(); bb!=e; ++bb)
		{
			BasicBlock *BB = bb;	
			// Loop info
			bool inLoop = LI->getLoopFor(BB);
			Loop* BBloop = LI->getLoopFor(BB);
			for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i) 
			{         
				// Debug info (line in the source code, file)
				File="";
				DebugLoc DLoc = i->getDebugLoc();
				unsigned OP_line = 0;
				if(DLoc){
					OP_line = DLoc.getLine();
					File=DLoc->getFilename();
				}

				/** FOUND A CALL INSTRUCTION **/
				if(CallInst *CI = dyn_cast<CallInst>(i))
				{
					Function *f = CI->getCalledFunction();
					if(!f)	return false;

					// Collective info
					std::string OP_name = f->getName().str();
					// Warning info
					string coll_line=to_string(OP_line);

					// Is it a collective call?
					for (vitr = v_coll.begin(); vitr != v_coll.end(); vitr++) 
					{
						if(f->getName().equals(*vitr)){
							OP_color=vitr-v_coll.begin();
							DEBUG(errs() << "-> Found " << OP_name << " line " << OP_line << ", OP_color=" << OP_color << "\n") ;
							STAT_collectives++; // update statistics

							// Check if the collective is in a loop
							if(inLoop){
								DEBUG(errs() << "* BB " << BB->getName().str() << " is in a loop. The header is " << (BBloop->getHeader()->getName()).str() << "\n");
								WarningMsg = OP_name + " line " + coll_line + " is in a loop";
								Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
								Diag.print(ProgName, errs(), 1,1);
							}
							// Check is the PDF is non null
							vector<BasicBlock * > iPDF = iterated_postdominance_frontier(PDT, BB);
							if(iPDF.size()!=0){
								errs() << "* iPDF( " << (BB)->getName().str() << ") = {";
								for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
									errs() << "- " << (*Bitr)->getName().str() << " ";
								}
								errs() << "}\n";

								WarningMsg = OP_name + " line " + coll_line + " possibly not called by all processes: iPDF non null!"; 
								Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
								Diag.print(ProgName, errs(), 1,1);
								// works only for MPI for now... TODO: add a test to know which collective has been found
								//instrumentCC(M,i,OP_color, OP_name, OP_line, WarningMsg, File);
							}
						}
					}
				} 
			}
		}

		if(STAT_collectives>0){
			errs() << "\033[0;36m=============================================\033[0;0m\n";
			errs() << "\033[0;36m============  PARCOACH STATISTICS ===========\033[0;0m\n";
			errs() << "\033[0;36m FUNCTION " << F.getName() << " FROM " << File << "\033[0;0m\n";
			errs() << "\033[0;36m=============================================\033[0;0m\n";
			errs() << "\033[0;36m Number of collectives: " << STAT_collectives << "\033[0;0m\n";
			errs() << "\033[0;36m=============================================\033[0;0m\n";
		}
		return true;
	}

	virtual bool doInitialization(Module &M) {
		errs() << "\033[0;36m=============================================\033[0;0m\n";
		errs() << "\033[0;36m=============  STARTING PARCOACH  ===========\033[0;0m\n";
		errs() << "\033[0;36m============================================\033[0;0m\n";
		return true;
	}

	virtual bool doInitialization(Function &F) {
		return true;
	}

	virtual bool doFinalization(Module &M) {
		return true;
	}

	virtual bool doFinalization(Function &F) {
		return true;
	}

	virtual void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.setPreservesAll();
		AU.addRequired<DominatorTreeWrapperPass>();
		AU.addRequired<PostDominatorTree>();
		AU.addRequired<LoopInfoWrapperPass>();
	};
};
}


char ParcoachInstr::ID = 0;
int ParcoachInstr::STAT_collectives = 0;
StringRef ParcoachInstr::File = "";
static RegisterPass<ParcoachInstr>
Z("parcoach", "Function pass");
