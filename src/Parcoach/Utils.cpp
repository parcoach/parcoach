#include "Utils.h"
#include "Collectives.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Debug.h"

using namespace llvm;
using namespace std;



/*
 * POSTDOMINANCE
 *   X postominates Y if X appears on every path from Y to exit
 *   n postdominates m if there is a path from n to m in the postdominator tree
 */


// Return true if the specified block postdominates the entry block
bool
blockDominatesEntry(BasicBlock *BB, PostDominatorTree &PDT, DominatorTree *DT,
		BasicBlock *EntryBlock) {
	if (PDT.dominates(BB, EntryBlock))
		return true;

	return false;
}

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

void
print_iPDF(vector<BasicBlock * > iPDF, BasicBlock *BB){
  vector<BasicBlock * >::const_iterator Bitr;
  errs() << "iPDF(" << BB->getName().str() << ") = {";
  for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
    errs() << "- " << (*Bitr)->getName().str() << " ";
  }
  errs() << "}\n";
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
		if(OP_color< (int) MPI_v_coll.size()){
			FunctionName="check_collective_MPI";
		}else{
			FunctionName="check_collective_UPC";
		}
	}
	Value * CCFunction = M->getOrInsertFunction(FunctionName, FTy);
	// Create new function
	CallInst::Create(CCFunction, ArrayRef<Value*>(CallArgs), "", I);
	DEBUG(errs() << "=> Insertion of " << FunctionName << " (" << OP_color << ", " << OP_name << ", " << OP_line << ", " << WarningMsg << ", " << File <<  ")\n");
}



/*
 * METADATA INFO
 */

// Get metadata info
// Metadata represents optional information about an instruction (or module)

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

string
getFuncSummary(Function &F){
	string summary=""; // no warning
	if (MDNode *node = F.getMetadata("func.summary")) {
		if (Metadata *value = node->getOperand(0)) {
			MDString *mdstring = cast<MDString>(value);
			summary=mdstring->getString();
		}
	}
	return summary;
}


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


/*
 * OTHER
 */ 


int
isCollectiveFunction(const Function &F) {
	StringRef funcName = F.getName();
	for (vector<const char *>::iterator I = v_coll.begin(),
			E = v_coll.end(); I != E; ++I) {
		if (funcName.equals(*I)){
			return I - v_coll.begin();
		}
	}
	return -1;
}

int
getInstructionLine(const llvm::Instruction &I) {
  const DebugLoc *DLoc = &I.getDebugLoc();
  if (DLoc)
    return DLoc->getLine();
  return -1;
}

std::string
getFunctionFilename(const llvm::Function &F) {
  for (Function::const_iterator I = F.begin(), E = F.end(); I != E; ++I) {
    const BasicBlock *bb = I;
    for (BasicBlock::const_iterator J = bb->begin(), F = bb->end(); J != F;
	 J++) {
      const Instruction *inst = J;

      const DebugLoc *DLoc = &inst->getDebugLoc();
      DILocation *DILoc = DLoc->get();
      if (DILoc)
	return DILoc->getFilename();
    }
  }

  return "<unknown file>";
}
