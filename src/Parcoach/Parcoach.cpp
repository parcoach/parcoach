#include "Parcoach.h"
#include "Utils.h"
#include "Collectives.h"


#include "llvm/IR/LLVMContext.h"
#include "llvm/PassSupport.h"
#include "llvm/IR/User.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
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

#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"


#include <vector>

using namespace llvm;
using namespace std;


static cl::OptionCategory ParcoachCategory("Parcoach options");

static cl::opt<bool> optNoInstrum("no-instrumentation",
        cl::desc("No static instrumentation"),
        cl::cat(ParcoachCategory)); 



// TODO: donner des noms plus user-friendly aux utilisateurs lors des warnings


ParcoachInstr::ParcoachInstr() : FunctionPass(ID) {}

void
ParcoachInstr::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  au.addRequired<DominanceFrontierWrapperPass>();
  au.addRequired<DominatorTreeWrapperPass>();
  au.addRequired<PostDominatorTreeWrapperPass>();
  au.addRequired<LoopInfoWrapperPass>();
}


void
ParcoachInstr::checkCollectives(Function &F, PostDominatorTree &PDT){
	MDNode* mdNode;
	// Warning info
	StringRef WarningMsg;
	const char *ProgName="PARCOACH";
	SMDiagnostic Diag;
	std::string COND_lines;
	int issue_warning=0;


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

		// Is it a collective?
		if(isCollective(f)<0)
			continue;

			ParcoachInstr::nbCollectivesFound++;
			vector<BasicBlock * > iPDF = iterated_postdominance_frontier(PDT, BB);

			if(iPDF.size()==0)
				continue;

	
			COND_lines="";
			vector<BasicBlock *>::iterator Bitr;
			for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
				TerminatorInst* TI = (*Bitr)->getTerminator();
				if(getBBcollSequence(*TI)=="NAVS"){
					DebugLoc BDLoc = TI->getDebugLoc();
					string cline = to_string(BDLoc.getLine());
					COND_lines.append(" ").append(cline);
					issue_warning=1;		
				}
			}
			if(issue_warning==1){
				ParcoachInstr::nbWarnings++;
				WarningMsg = OP_name + " line " + to_string(OP_line) + " possibly not called by all processes because of conditional(s) line(s) " + COND_lines;
				mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
				i->setMetadata("inst.warning",mdNode);
				Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
				Diag.print(ProgName, errs(), 1,1);
				issue_warning=0;
			}
		//}
	}
}


bool
ParcoachInstr::runOnFunction(Function &F) {

	ParcoachInstr::nbCollectivesFound = 0;
	ParcoachInstr::nbWarnings = 0;

	PostDominatorTree &PDT =
		getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();

	// (1) Set the sequence of collectives with a BFS from the exit node in the CFG
	BFS(&F);
	// (2) Check calls to collectives with PDF+ of each collective node
	checkCollectives(F,PDT);




	if(ParcoachInstr::nbCollectivesFound != 0) {
		errs() << "\n\033[0;36m==========================================\033[0;0m\n";
		errs() << "\033[0;36m==========  PARCOACH STATISTICS ==========\033[0;0m\n";
		errs() << "\033[0;36m==========================================\033[0;0m\n";
		errs() << "Function: " << F.getName().str() << "\n";
		errs() << ParcoachInstr::nbCollectivesFound << " collective(s) found\n"; 
		errs() << ParcoachInstr::nbWarnings << " warning(s) issued\n";

		if(ParcoachInstr::nbWarnings !=0 && !optNoInstrum){;
			// Static instrumentation of the code
			// TODO: no intrum for UPC yet
			errs() << "\033[0;35m=> Instrumentation of the function ...\033[0;0m\n";
			instrumentFunction(&F);
		}
		errs() << "\033[0;36m==========================================\033[0;0m\n";
	}
	return false;
}



char ParcoachInstr::ID = 0;
unsigned ParcoachInstr::nbCollectivesFound = 0;
unsigned ParcoachInstr::nbWarnings = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Function pass");
