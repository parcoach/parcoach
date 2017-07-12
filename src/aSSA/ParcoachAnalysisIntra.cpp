#include "Collectives.h"
#include "Options.h"
#include "ParcoachAnalysisIntra.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "hello"

void
ParcoachAnalysisIntra::run() {
  for (Function &F : M) {
    unsigned oldNbWarnings = nbWarningsParcoachOnly;

    // (1) Set the sequence of collectives with a BFS from the exit node in the CFG
    BFS(&F);
    // (2) Check calls to collectives with PDF+ of each collective node
    checkCollectives(&F);

    if(nbWarningsParcoachOnly > oldNbWarnings && !disableInstru){;
      // Static instrumentation of the code
      // TODO: no intrum for UPC yet
      errs() << "\033[0;35m=> Instrumentation of the function ...\033[0;0m\n";
      instrumentFunction(&F);
    }
    errs() << "\033[0;36m==========================================\033[0;0m\n";
  }
}

// BFS in reverse topological order
void
ParcoachAnalysisIntra::BFS(llvm::Function *F) {
  MDNode* mdNode;
  string CollSequence_Header;
  string CollSequence=string();
  std::vector<BasicBlock *> Unvisited;


  // GET ALL EXIT NODES
  for(BasicBlock &I : *F){
    if(isa<ReturnInst>(I.getTerminator())){
      string return_coll = string(getCollectivesInBB(&I));
      mdNode = MDNode::get(I.getContext(),MDString::get(I.getContext(),return_coll));
      I.getTerminator()->setMetadata("intra.inst.collSequence",mdNode);
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
	      if(getCollectivesInBB(Pred)!="empty") // && N!="NAVS")
		N.append(" ").append(getCollectivesInBB(Pred));
	    }
	    CollSequence=string(N);
	    // Set the metadata with the collective sequence
	    mdNode = MDNode::get(TI->getContext(),MDString::get(TI->getContext(),CollSequence));
	    TI->setMetadata("intra.inst.collSequence",mdNode);
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
	    TI->setMetadata("intra.inst.collSequence",mdNode);
	    // DEBUG
	    //DebugLoc BDLoc = TI->getDebugLoc();
	    //errs() << "  ===>>> Line " << BDLoc.getLine() << " -> " << getBBcollSequence(*TI) << "\n";
	  }
	}
      }
    }
}

void
ParcoachAnalysisIntra::checkCollectives(llvm::Function *F) {
  MDNode* mdNode;
  // Warning info
  string WarningMsg;
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
    if(!isCollective(f))
      continue;

    nbCollectivesFound++;
    PostDominatorTree &PDT =
      pass->getAnalysis<PostDominatorTreeWrapperPass>(*F).getPostDomTree();

    vector<BasicBlock * > iPDF = iterated_postdominance_frontier(PDT, BB);

    if(iPDF.size()==0)
      continue;


    COND_lines="";
    vector<BasicBlock *>::iterator Bitr;
    for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
      TerminatorInst* TI = (*Bitr)->getTerminator();
      if(getBBcollSequence(*TI)=="NAVS"){
	nbCondsParcoachOnly++;
	conditionSetParcoachOnly.insert(*Bitr);
	DebugLoc BDLoc = TI->getDebugLoc();
	string cline = to_string(BDLoc.getLine());
	COND_lines.append(" ").append(cline);
	issue_warning=1;
      }
    }
    if(issue_warning==1){
      nbWarningsParcoachOnly++;
      WarningMsg = OP_name + " line " + to_string(OP_line) + " possibly not called by all processes because of conditional(s) line(s) " + COND_lines;
      mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
      i->setMetadata("intra.inst.warning",mdNode);
      Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
      Diag.print(ProgName, errs(), 1,1);
      issue_warning=0;
    }
    //}
  }
}

void
ParcoachAnalysisIntra::instrumentFunction(llvm::Function *F) {
  int nbInstrum=0;

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
	      int OP_color = getCollectiveColor(callee);

	      // Before finalize or abort !!
	      if(callee->getName().equals("MPI_Finalize") || callee->getName().equals("MPI_Abort")){
		DEBUG(errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n");
		instrumentCC(Inst,v_coll.size()+1, OP_name, OP_line, Warning, File);
		nbInstrum++;
		nbCC++;
		continue;
	      }
	      // Before a collective
	      if(OP_color>=0){
		DEBUG(errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n");
		instrumentCC(Inst,OP_color, OP_name, OP_line, Warning, File);
		nbInstrum++;
		nbCC++;
	      }
	    }
	  // Before a return instruction
	  if(isa<ReturnInst>(i)){
	    DEBUG(errs() << "-> insert check before return statement line " << OP_line << "\n");
	    instrumentCC(Inst,v_coll.size()+1, "Return", OP_line, Warning, File);
	    nbInstrum++;
	    nbCC++;
	  }
	}
    }
  errs() << "# CC = " << nbInstrum << "\n";
}

/*
 * FUNCTIONS USED TO CHECK COLLECTIVES
 */

// Get the sequence of collectives in a BB
string
ParcoachAnalysisIntra::getCollectivesInBB(BasicBlock *BB) {
  string CollSequence="empty";

  for(BasicBlock::iterator i = BB->begin(), e = BB->end(); i != e; ++i){

    if(CallInst *CI = dyn_cast<CallInst>(i)){
      Function *callee = CI->getCalledFunction();
      if(!callee) continue;

      // Is it a collective?
      if (!isCollective((callee)))
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
std::string
ParcoachAnalysisIntra::getBBcollSequence(const llvm::Instruction &inst) {
  if (MDNode *node = inst.getMetadata("intra.inst.collSequence")) {
    if (Metadata *value = node->getOperand(0)) {
      MDString *mdstring = cast<MDString>(value);
      return mdstring->getString();
    }
  }
  return "white";
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
ParcoachAnalysisIntra::instrumentCC(Instruction *I, int OP_color,
				    std::string OP_name,
				    int OP_line,
				    StringRef WarningMsg, StringRef File) {
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
ParcoachAnalysisIntra::getWarning(llvm::Instruction &inst) {
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
