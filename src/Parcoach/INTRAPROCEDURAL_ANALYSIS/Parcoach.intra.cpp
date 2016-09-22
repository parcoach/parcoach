// Parcoach.cpp - implements LLVM Compile Pass which checks errors caused by
// MPI collective operations
//
// This pass inserts functions

#include "Parcoach.intra.h"

#include "Collectives.h"
#include "Utils.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;
using namespace std;

ParcoachInstr::ParcoachInstr() : FunctionPass(ID), ProgName("PARCOACH") {}

void
ParcoachInstr::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  au.addRequired<DominatorTreeWrapperPass>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<LoopInfoWrapperPass>();
  au.addRequired<AliasAnalysis>();
}

bool
ParcoachInstr::doInitialization(Module &M) {
  errs() << "\033[0;36m=============================================\033[0;0m\n"
	 << "\033[0;36m=============  STARTING PARCOACH  ===========\033[0;0m\n"
	 << "\033[0;36m============================================\033[0;0m\n";
  return true;
}

static void printStats(StringRef funcName, StringRef fileName,
		       int STAT_collectives, int STAT_warnings)
{
  errs() << "\033[0;36m=============================================\033[0;0m\n"
	 << "\033[0;36m============  PARCOACH STATISTICS ===========\033[0;0m\n"
	 << "\033[0;36m FUNCTION " << funcName << " FROM " << fileName
	 << "\033[0;0m\n"
	 << "\033[0;36m=============================================\033[0;0m\n"
	 << "\033[0;36m Number of collectives: " << STAT_collectives
	 << "\033[0;0m\n"
	 << "\033[0;36m=============================================\n"
	 << "\033[0;36m Number of rank dependent collectives: " << STAT_warnings
	 << "\033[0;0m\n"
	 << "\033[0;36m============================================="
	 << "\033[0;0m\n";
}

static void instrumentFunction(Function &Func)
{
  errs() << "==> Function " << Func.getName() << " is instrumented:\n";
  Module *M = Func.getParent();
  for(Function::iterator I = Func.begin(), E = Func.end(); I!=E; ++I) {
    BasicBlock *bb = I;
    for (BasicBlock::iterator J = bb->begin(), F = bb->end(); J != F; ++J) {
      Instruction *Inst = J;
      string Warning = getWarning(*Inst);
      // Debug info (line in the source code, file)
      DebugLoc DLoc = Inst->getDebugLoc();
      string File="o";
      int OP_line = -1;
      if (DLoc) {
	File = DLoc->getFilename();
	OP_line = DLoc.getLine();
      }

      // call instruction
      if(CallInst *CI = dyn_cast<CallInst>(Inst)) {
	Function *f = CI->getCalledFunction();
	string OP_name = f->getName().str();
	if(f->getName().equals("MPI_Finalize")){
	  instrumentCC(M,Inst,v_coll.size()+1, "MPI_Finalize", OP_line,
		       Warning, File);
	  continue;
	}

	int OP_color = isCollectiveFunction(*f);
	if (OP_color >= 0) {
	  instrumentCC(M,Inst,OP_color, OP_name, OP_line, Warning, File);
	  continue;
	}
      }

      // return instruction
      if(isa<ReturnInst>(Inst)){
	instrumentCC(M,Inst,v_coll.size()+1, "Return", OP_line, Warning, File);
      }
    }
  }
}

void
ParcoachInstr::checkCollectives(Function &F, StringRef filename,
				int &STAT_collectives, int &STAT_warnings) {
  // Iterate over all instructions
  for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I) {
    BasicBlock *bb = I;

    // TODO: check if is in loop.

    for (BasicBlock::iterator J = bb->begin(), F = bb->end(); J != F; J++) {
      Instruction *inst = J;

      CallInst *ci = dyn_cast<CallInst>(inst);
      if (!ci)
	continue;

      Function *called = ci->getCalledFunction();
      assert(called);

      int OP_color = isCollectiveFunction(*called);
      if (OP_color < 0)
	continue;

      string OP_name = called->getName().str();
      int OP_line = getInstructionLine(*ci);
      LLVMContext &context = ci->getContext();
      MDNode *mdNode;

      DEBUG(errs() << "-> Found " << OP_name << " line " << to_string(OP_line)
	    << ", OP_color=" << OP_color << "\n");
      STAT_collectives++;

      // Collective found, now check if basic block is rank dependent.
      vector<BasicBlock * > IPDF = iterated_postdominance_frontier(*PDT, bb);

      if(is_IPDF_rank_dependent(IPDF)) {
	STAT_warnings++;

	// Get condition(s) line(s)
	// TODO: print only the rank dependent conditions.
	string COND_lines="";
	for (vector<BasicBlock *>::iterator BI = IPDF.begin(), BE = IPDF.end();
	     BI != BE; BI++) {
	  TerminatorInst* TI = (*BI)->getTerminator();
	  DebugLoc BDLoc = TI->getDebugLoc();
	  COND_lines.append(" ").append(to_string(BDLoc.getLine()));
	}

	string WarningMsg = OP_name + " line " + to_string(OP_line) +
	  " possibly not called by all processes because of conditional(s) " +
	  "line(s) " + COND_lines;
	mdNode = MDNode::get(context, MDString::get(context,WarningMsg));
	ci->setMetadata("inst.warning",mdNode);
	SMDiagnostic Diag = SMDiagnostic(filename,
					 SourceMgr::DK_Warning,WarningMsg);
	Diag.print(ProgName, errs(), 1, 1);
      } else {
	string WarningMsg = OP_name + " line " + to_string(OP_line) +
	  " is not rank dependent";
	mdNode = MDNode::get(context, MDString::get(context,WarningMsg));
	ci->setMetadata("inst.warning",mdNode);
	SMDiagnostic Diag = SMDiagnostic(filename,
					 SourceMgr::DK_Note,WarningMsg);
	Diag.print(ProgName, errs(), 1, 1);
      }
    }
  }
}

bool
ParcoachInstr::runOnFunction(Function &F) {
  MD = F.getParent();

  int STAT_warnings = 0;
  int STAT_collectives = 0;
  string File = getFunctionFilename(F);

  // Get analyses.
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  PDT = &getAnalysis<PostDominatorTree>();
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  AA = &getAnalysis<AliasAnalysis>();

  // Each function will do that...
  std::sort(MPI_v_coll.begin(), MPI_v_coll.end());
  std::sort(UPC_v_coll.begin(), UPC_v_coll.end());
  std::merge(MPI_v_coll.begin(),MPI_v_coll.end(),
	     UPC_v_coll.begin(), UPC_v_coll.end(),v_coll.begin());

  // Find memory locations where value depends on the rank.
  findRankPointers(F);
  DEBUG(printRankPointers());

  // Check calls to collectives.
  checkCollectives(F, File, STAT_collectives, STAT_warnings);

  // Instrument the code if needed.
  if (STAT_warnings > 0)
    instrumentFunction(F);

  // Print statistics.
  printStats(F.getName(), File, STAT_collectives, STAT_warnings);

  return (STAT_warnings > 0);
}

void
ParcoachInstr::printRankPointers() const {
  errs() << "Rank pointers (not including aliases): ";
  for (std::set<const Value *>::iterator I = rankPtrSet.begin(),
	 E = rankPtrSet.end(); I != E; ++I) {
    errs() << (*I)->getName() << " ";
  }
  errs() << "\n";
}

void
ParcoachInstr::findRankPointers(const Function &F) {
  // Find memory locations where rank is stored by MPI_Comm_rank() function.
  for (Function::const_iterator b = F.begin(), be = F.end(); b != be; ++b) {
    for (BasicBlock::const_iterator i = b->begin(), ie = b->end(); i != ie; ++i)
      {
      const CallInst *ci = dyn_cast<CallInst>(&*i);
      if (!ci)
	continue;

      if (!ci->getCalledFunction()->getName().equals("MPI_Comm_rank"))
	continue;

      const Value *rankPtr = ci->getOperand(1);
      rankPtrSet.insert(rankPtr);
    }
  }

  // Recursively find all memory locations storing rank
  SetVector<const User *> seenUsers;
  SetVector<const Value *> toAdd;

  for (std::set<const Value *>::iterator I = rankPtrSet.begin(),
	 E = rankPtrSet.end(); I != E; ++I)
    findRankPointersRec(*I, seenUsers, toAdd);

  rankPtrSet.insert(toAdd.begin(), toAdd.end());

  // Add all variable whose value depends on rank
  bool changed = true;
  while (changed) {
    changed = false;

    for (Function::const_iterator b = F.begin(), be = F.end(); b != be; ++b) {
      for (BasicBlock::const_iterator i = b->begin(), ie = b->end(); i != ie;
	   ++i) {
	const StoreInst *si = dyn_cast<StoreInst>(&*i);
	if (!si)
	  continue;

	// Compute IPDF
	vector<BasicBlock * > IPDF
	  = iterated_postdominance_frontier(*PDT, (BasicBlock *)&*b);

	if (is_IPDF_rank_dependent(IPDF)) {
	  if (rankPtrSet.insert(si->getPointerOperand()).second == true)
	    changed = true;
	}
      }
    }
  }
}

void
ParcoachInstr::findRankPointersRec(const Value *v,
				   SetVector<const User *> &seenUsers,
				   SetVector<const Value *> &toAdd) {
  for (const User *U : v->users()) {
    if (!seenUsers.insert(U))
      continue;

    const StoreInst *si = dyn_cast<StoreInst>(U);
    if (si) {
      toAdd.insert(si->getPointerOperand());
      continue;
    }

    findRankPointersRec(U, seenUsers, toAdd);
  }
}

bool
ParcoachInstr::is_IPDF_rank_dependent(vector<BasicBlock *> &PDF) {
  for (unsigned i=0; i<PDF.size(); i++) {
    if (is_BB_rank_dependent(PDF[i]))
      return true;
  }

  return false;
}

bool
ParcoachInstr::is_BB_rank_dependent(const BasicBlock *BB) {
  const TerminatorInst *ti = BB->getTerminator();
  assert(ti);
  const BranchInst *bi = dyn_cast<BranchInst>(ti);
  assert(ti);

  if (bi->isUnconditional())
    return false;

  const Value *cond = bi->getCondition();

  return is_value_rank_dependent(cond);
}

bool
ParcoachInstr::is_value_rank_dependent(const Value *value) {
  if (isa<const Argument>(value)) {
    // Here we assume that arguments are not rank dependent.
    return false;
  }

  const User *user = dyn_cast<const User>(value);
  assert(user);

  if (isa<const Constant>(user))
    return false;

  const Instruction *inst = dyn_cast<const Instruction>(user);
  assert(inst);

  switch(inst->getOpcode()) {
    // TermInst
  case Instruction::Ret:
  case Instruction::Br:
  case Instruction::Switch:
  case Instruction::IndirectBr:
  case Instruction::Invoke:
  case Instruction::Resume:
  case Instruction::Unreachable:
    return false;

    // BinaryInst
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::FDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return is_value_rank_dependent(inst->getOperand(0)) ||
      is_value_rank_dependent(inst->getOperand(1));

    // MemoryOperator
  case Instruction::Load:
    {
      const LoadInst *li = cast<LoadInst>(inst);
      const Value *ptr = li->getPointerOperand();
      assert(ptr);

      for (std::set<const Value *>::iterator I = rankPtrSet.begin(),
	     E = rankPtrSet.end(); I != E; ++I) {
	const Value *v = *I;
	AliasResult res = AA->alias(v, 4, ptr, 4);

	if (res != NoAlias)
	  return true;
      }

      return false;
    }

  case Instruction::AtomicCmpXchg:
    {
      const AtomicCmpXchgInst *ai = cast<AtomicCmpXchgInst>(inst);
      if (rankPtrSet.find(ai->getPointerOperand()) != rankPtrSet.end())
	return true;
      if (is_value_rank_dependent(ai->getCompareOperand()))
	return true;
      if (is_value_rank_dependent(ai->getNewValOperand()))
	return true;
      return false;
    }

  case Instruction::AtomicRMW:
    {
      const AtomicRMWInst *ai = cast<AtomicRMWInst>(inst);
      if (rankPtrSet.find(ai->getPointerOperand()) != rankPtrSet.end())
	return true;
      if (is_value_rank_dependent(ai->getValOperand()))
	return true;
      return false;
    }
  case Instruction::GetElementPtr:
      return false;

  case Instruction::Alloca:
  case Instruction::Store:
  case Instruction::Fence:
    return false;

    // Cast operators
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
    return is_value_rank_dependent(inst->getOperand(0));

    // Other operators...
  case Instruction::ICmp:
  case Instruction::FCmp:
    return is_value_rank_dependent(inst->getOperand(0)) ||
      is_value_rank_dependent(inst->getOperand(1));

  case Instruction::PHI:
    {
      const PHINode *phi = cast<PHINode>(inst);
      for (unsigned i=0; i<phi->getNumIncomingValues(); ++i) {
	if (is_value_rank_dependent(phi->getIncomingValue(i)))
	  return true;
      }
      return false;
    }

  case Instruction::Call:
    // Since there is no interprocedural analysis yet, return false.
    return false;

  case Instruction::Select:
    return is_value_rank_dependent(inst->getOperand(0)) ||
      is_value_rank_dependent(inst->getOperand(1)) ||
      is_value_rank_dependent(inst->getOperand(2));

  case Instruction::VAArg:
    // We assume that argument are not rank dependent.
    return false;

  case Instruction::ExtractElement:
    return is_value_rank_dependent(inst->getOperand(0));

  case Instruction::InsertElement:
    return is_value_rank_dependent(inst->getOperand(0)) ||
      is_value_rank_dependent(inst->getOperand(1));

  case Instruction::ShuffleVector:
    return is_value_rank_dependent(inst->getOperand(0)) ||
      is_value_rank_dependent(inst->getOperand(1));

  case Instruction::ExtractValue:
    return is_value_rank_dependent(inst->getOperand(0));

  case Instruction::InsertValue:
    return is_value_rank_dependent(inst->getOperand(0)) ||
      is_value_rank_dependent(inst->getOperand(1));

  default:
    assert(false);
  };
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Function pass");
