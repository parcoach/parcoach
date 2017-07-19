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
ParcoachAnalysisInter::run() {
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
    }
    ++cgSccIter;
  }

  if(nbWarnings !=0 && !disableInstru){
    errs() << "\033[0;35m=> Static instrumentation of the code ...\033[0;0m\n";
    for (Function &F : M) {
      instrumentFunction(&F);
    }
  }

  errs() << " ... Parcoach analysis done\n";
}

// BFS in reverse topological order
void
ParcoachAnalysisInter::BFS(llvm::Function *F) {
  MDNode* mdNode;
  string CollSequence_Header;
  string CollSequence=string();
  std::vector<BasicBlock *> Unvisited;

  // GET ALL EXIT NODES AND SET THE COLLECTIVE SEQUENCE
  for(BasicBlock &I : *F){
    if(isa<ReturnInst>(I.getTerminator())){
      string return_coll = string(getCollectivesInBB(&I));
      mdNode = MDNode::get(I.getContext(),MDString::get(I.getContext(),return_coll));
      I.getTerminator()->setMetadata("inter.inst.collSequence"+to_string(id),mdNode);
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
	    TI->setMetadata("inter.inst.collSequence"+to_string(id),mdNode);
	    Unvisited.push_back(Pred);

	    // BB ALREADY SEEN
	  }else{
	  string seq_temp = CollSequence_Header;
	  if(CollSequence_Header=="empty"){
	    seq_temp=getCollectivesInBB(Pred);
	  }else{
	    if(getCollectivesInBB(Pred)!="empty" && seq_temp!="NAVS")
	      seq_temp.append(" ").append(getCollectivesInBB(Pred));
	  }
	  string CollSequence_temp=string(seq_temp);

	  // Check if CollSequence_temp and sequence in BB are equals. If not set the metadata as NAVS
	  //DEBUG(errs() << " EMMA >>> " << CollSequence_temp << " = " << getBBcollSequence(*TI) << " ?\n");
	  if(CollSequence_temp != getBBcollSequence(*TI)){
	    mdNode = MDNode::get(Pred->getContext(),MDString::get(Pred->getContext(),"NAVS"));
	    TI->setMetadata("inter.inst.collSequence"+to_string(id),mdNode);
	    DebugLoc BDLoc = TI->getDebugLoc();
	    //DEBUG(errs() << "  EMMA ===>>> Line " << BDLoc.getLine() << " -> " << getBBcollSequence(*TI) << "\n");
	  }
	}
      }
    }
  // Keep a metadata for the summary of the function
  BasicBlock &entry = F->getEntryBlock();
  string FuncSummary="";

  if(getBBcollSequence(*entry.getTerminator()) != "white")
    FuncSummary=getBBcollSequence(*entry.getTerminator());
  else
    errs() << F->getName() << " has white summary!\n";
  if(FuncSummary.find("NAVS")!=std::string::npos)
    FuncSummary="NAVS";


  mdNode = MDNode::get(F->getContext(),MDString::get(F->getContext(),FuncSummary));
  F->setMetadata("inter.func.summary"+to_string(id),mdNode);
  //DEBUG(errs() << "Summary of function " << F->getName() << " : " << getFuncSummary(*F) << "\n");
}

void
ParcoachAnalysisInter::checkCollectives(llvm::Function *F) {
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

    nbCollectivesFound++;

    bool isColWarning = false;
    bool isColWarningParcoach = false;

    // Get conditionals from the callsite
    set<const BasicBlock *> callIPDF;
    DG->getCallInterIPDF(CI, callIPDF);

    for (const BasicBlock *BB : callIPDF) {

      // Is this node detected as potentially dangerous by parcoach?
      string Seq = getBBcollSequence(*(BB->getTerminator()));
      if(Seq!="NAVS") continue;

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
    nbWarnings++;
    warningSet.insert(CI);

    WarningMsg = OP_name + " line " + to_string(OP_line) +
      " possibly not called by all processes because of conditional(s) " \
      "line(s) " + COND_lines;
    mdNode = MDNode::get(i->getContext(),
			 MDString::get(i->getContext(), WarningMsg));
    i->setMetadata("inter.inst.warning"+to_string(id),mdNode);
    Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
    Diag.print(ProgName, errs(), 1,1);
  }
}

void
ParcoachAnalysisInter::instrumentFunction(llvm::Function *F) {
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

/*
 * FUNCTIONS USED TO CHECK COLLECTIVES
 */

// Get the sequence of collectives in a BB
string
ParcoachAnalysisInter::getCollectivesInBB(BasicBlock *BB) {
  string CollSequence="empty";

  for(BasicBlock::iterator i = BB->begin(), e = BB->end(); i != e; ++i){
    const Instruction *inst = &*i;

    if(CallInst *CI = dyn_cast<CallInst>(i)) {
      Function *callee = CI->getCalledFunction();

      //// Indirect calls
      if(callee == NULL)
	{
	  for (const Function *mayCallee : PTACG.indirectCallMap[inst]) {
	    if (isIntrinsicDbgFunction(mayCallee))  continue;
	    Function *callee = const_cast<Function *>(mayCallee);
	    // Is it a function containing a collective?
	    if(getFuncSummary(*callee)!="no summary" && getFuncSummary(*callee)!="empty"){
	      if(CollSequence=="empty"){
		CollSequence=getFuncSummary(*callee);
	      }else{
		// To avoid a summary like NAVS NAVS
		if(CollSequence=="NAVS" || getFuncSummary(*callee) == "NAVS"){
		  CollSequence="NAVS";
		}else{
		  CollSequence.append(" ").append(getFuncSummary(*callee));
		}
	      }
	    }
	    // Is it a collective operation?
	    if(isCollective(callee)){
	      if(CollSequence=="empty"){
		CollSequence=callee->getName().str();
	      }else{
		if(CollSequence!="NAVS")
		  CollSequence.append(" ").append(callee->getName().str());
	      }
	    }
	  }
	  //// Direct calls
	}else{
	// Is it a function containing a collective?
	if(getFuncSummary(*callee)!="no summary" && getFuncSummary(*callee)!="empty"){

	  if(CollSequence=="empty"){
	    CollSequence=getFuncSummary(*callee);
	  }else{
	    if(CollSequence=="NAVS" || getFuncSummary(*callee) == "NAVS"){
	      CollSequence="NAVS";
	    }else{
	      CollSequence.append(" ").append(getFuncSummary(*callee));
	    }
	  }
	}
	// Is it a collective operation?
	if(isCollective(callee)){
	  if(CollSequence=="empty"){
	    CollSequence=callee->getName().str();
	  }else{
	    if(CollSequence!="NAVS")
	      CollSequence.append(" ").append(callee->getName().str());
	  }
	}
      }
    }
  }
  return CollSequence;
}

// Metadata
std::string
ParcoachAnalysisInter::getBBcollSequence(const llvm::Instruction &inst) {
  if (MDNode *node = inst.getMetadata("inter.inst.collSequence"+to_string(id))) {
    if (Metadata *value = node->getOperand(0)) {
      MDString *mdstring = cast<MDString>(value);
      assert(mdstring->getString()!="white");
      return mdstring->getString();
    }
  }
  return "white";
}

std::string
ParcoachAnalysisInter::getFuncSummary(llvm::Function &F) {
  if (MDNode *node = F.getMetadata("inter.func.summary"+to_string(id))) {
    if (Metadata *value = node->getOperand(0)) {
      MDString *mdstring = cast<MDString>(value);
      return mdstring->getString();
    }
  }
  return "no summary";
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
ParcoachAnalysisInter::insertCC(llvm::Instruction *I, int OP_color,
				std::string OP_name, int OP_line,
				llvm::StringRef WarningMsg,
				llvm::StringRef File) {
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
ParcoachAnalysisInter::getWarning(llvm::Instruction &inst) {
  string warning = " ";
  if (MDNode *node = inst.getMetadata("inter.inst.warning"+to_string(id))) {
    if (Metadata *value = node->getOperand(0)) {
      MDString *mdstring = cast<MDString>(value);
      warning = mdstring->getString();
    }
  }else {
    //errs() << "Did not find metadata\n";
  }
  return warning;
}
