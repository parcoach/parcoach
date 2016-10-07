#include "FunctionSummary.h"

#include "Collectives.h"
#include "Utils.h"

#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SourceMgr.h"

#include <vector>

using namespace std;
using namespace llvm;

#define PROGNAME "Parcoach"

FunctionSummary::FunctionSummary(llvm::Function *F,
				 FuncSummaryMapTy *funcMap,
				 InterDepGraph *interDeps,
				 llvm::Pass *pass)
  : interDeps(interDeps), isRetDependent(false), F(F), funcMap(funcMap),
    pass(pass) {

  MD = F->getParent();
  AA = &pass->getAnalysis<AliasAnalysis>();
  TLI = &pass->getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
}

FunctionSummary::~FunctionSummary() {}


void
FunctionSummary::firstPass() {
  // Get analyses
  DT = &pass->getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
  PDT = &pass->getAnalysis<PostDominatorTree>(*F);

  findMPICommRankRoots();
  computeListeners();
  bool changed = true;
  while (changed) {
    changed = false;
    changed = updateAliasDep(*F) || changed;
    changed = updateFlowDep(*F) || changed;
  }
}

bool
FunctionSummary::updateDeps() {
  // Get analyses
  DT = &pass->getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
  PDT = &pass->getAnalysis<PostDominatorTree>(*F);

  // Find all memory locations where value depends on rank until
  // reaching a fixed point.
  bool changed = true;
  while (changed) {
    changed = false;
    changed = updateAliasDep(*F) || changed;
    changed = updateFlowDep(*F) || changed;
  }

  changed = changed || updateRetDep();
  changed = changed || updateArgDep();

  return changed;
}

void
FunctionSummary::checkCollectives() {
  // Get analyses
  DT = &pass->getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
  PDT = &pass->getAnalysis<PostDominatorTree>(*F);

  string filename = getFunctionFilename(*F);

  for (inst_iterator I = inst_begin(*F), E = inst_end(*F); I != E; ++I) {
    Instruction *inst = &*I;

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

    // Collective found, now check if basic block is rank dependent.
    vector<BasicBlock * > IPDF = //getIPDF((BasicBlock *) inst->getParent());
      iterated_postdominance_frontier(*PDT, inst->getParent());

    MemoryLocation src;
    if(is_IPDF_rank_dependent(IPDF, &src)) {

      intraDeps.addBarrierEdge(src, ci);

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
      Diag.print(PROGNAME, errs(), 1, 1);
    } else {
      string WarningMsg = OP_name + " line " + to_string(OP_line) +
	" is not rank dependent";
      mdNode = MDNode::get(context, MDString::get(context,WarningMsg));
      ci->setMetadata("inst.warning",mdNode);
      SMDiagnostic Diag = SMDiagnostic(filename,
				       SourceMgr::DK_Note,WarningMsg);
      Diag.print(PROGNAME, errs(), 1, 1);
    }
  }
}

vector<BasicBlock *>
FunctionSummary::getIPDF(BasicBlock *bb) {
  DenseMap<const BasicBlock *,
	   vector<BasicBlock *>>::const_iterator I;

  I = iPDFMap.find(bb);
  if (I != iPDFMap.end())
    return I->getSecond();
  std::vector<BasicBlock *> iPDF = iterated_postdominance_frontier(*PDT, bb);
  iPDFMap[bb] = iPDF;

  return iPDF;
}

bool
FunctionSummary::is_IPDF_rank_dependent(vector<BasicBlock *> &PDF,
				      MemoryLocation *src) {
  for (unsigned i=0; i<PDF.size(); i++) {
    if (is_BB_rank_dependent(PDF[i], src))
      return true;
  }

  return false;
}

bool
FunctionSummary::is_BB_rank_dependent(const BasicBlock *BB, MemoryLocation *src) {
  const TerminatorInst *ti = BB->getTerminator();
  assert(ti);
  const BranchInst *bi = dyn_cast<BranchInst>(ti);
  assert(ti);

  if (bi->isUnconditional())
    return false;

  const Value *cond = bi->getCondition();

  return is_value_rank_dependent(cond, src);
}

bool
FunctionSummary::is_value_rank_dependent(const Value *value,
				       MemoryLocation *src) {
  if (isa<const Argument>(value)) {
    MemoryLocation ML = MemoryLocation(value, MemoryLocation::UnknownSize);
    for (auto I = intraDeps.nodeBegin(), E = intraDeps.nodeEnd();
	 I != E; ++I) {
      AliasResult res = AA->alias(ML, *I);
      if (res == MustAlias) {
	if (src)
	  *src = ML;
	return true;
      }
    }

    return false;
  }

  const User *user = dyn_cast<const User>(value);
  if (!user)
    return false;

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
    return is_value_rank_dependent(inst->getOperand(0), src) ||
      is_value_rank_dependent(inst->getOperand(1), src);

    // MemoryOperator
  case Instruction::Load:
    {
      const LoadInst *li = cast<LoadInst>(inst);
      const Value *ptr = li->getPointerOperand();
      assert(ptr);

      for (auto I = intraDeps.nodeBegin(), E = intraDeps.nodeEnd();
	   I != E; ++I) {
	MemoryLocation ML = MemoryLocation::get(li);
	AliasResult res = AA->alias(ML, *I);
	if (res == MustAlias) {
	  if (src)
	    *src = ML;
	  return true;
	}
      }

      return false;
    }

  case Instruction::AtomicCmpXchg:
    {
      const AtomicCmpXchgInst *ai = cast<AtomicCmpXchgInst>(inst);

      for (auto I = intraDeps.nodeBegin(), E = intraDeps.nodeEnd();
	   I != E; ++I) {
	MemoryLocation ML = MemoryLocation::get(ai);
	AliasResult res = AA->alias(ML, *I);
	if (res == MustAlias) {
	  if (src)
	    *src = ML;
	  return true;
	}
      }

      return is_value_rank_dependent(ai->getCompareOperand(), src) ||
	is_value_rank_dependent(ai->getNewValOperand(), src);
    }

  case Instruction::AtomicRMW:
    {
      const AtomicRMWInst *ai = cast<AtomicRMWInst>(inst);
      for (auto I = intraDeps.nodeBegin(), E = intraDeps.nodeEnd();
	   I != E; ++I) {
	MemoryLocation ML = MemoryLocation::get(ai);
	AliasResult res = AA->alias(ML, *I);
	if (res == MustAlias) {
	  if (src)
	    *src = ML;
	  return true;
	}
      }

      return is_value_rank_dependent(ai->getValOperand(), src);
    }
  case Instruction::GetElementPtr:
      return false;

  case Instruction::Alloca:
    for (auto I = intraDeps.nodeBegin(), E = intraDeps.nodeEnd();
	 I != E; ++I) {
      MemoryLocation ML = MemoryLocation(inst);
      AliasResult res = AA->alias(ML, *I);
      if (res == MustAlias) {
	if (src)
	  *src = ML;

	return true;
      }

      return false;
    }

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
    return is_value_rank_dependent(inst->getOperand(0), src);

    // Other operators...
  case Instruction::ICmp:
  case Instruction::FCmp:
    return is_value_rank_dependent(inst->getOperand(0), src) ||
      is_value_rank_dependent(inst->getOperand(1), src);

  case Instruction::PHI:
    {
      const PHINode *phi = cast<PHINode>(inst);
      for (unsigned i=0; i<phi->getNumIncomingValues(); ++i) {
	if (is_value_rank_dependent(phi->getIncomingValue(i), src)) {
	  return true;
	}
      }
      return false;
    }

  case Instruction::Call:
    {
      MemoryLocation ML = MemoryLocation(user, MemoryLocation::UnknownSize);
      for (auto I = intraDeps.nodeBegin(), E = intraDeps.nodeEnd();
	   I != E; ++I) {
	AliasResult res = AA->alias(ML, *I);
	if (res == MustAlias) {
	  if (src)
	    *src = ML;
	  return true;
	}
      }

    return false;
    }

  case Instruction::Select:
    return is_value_rank_dependent(inst->getOperand(0), src) ||
      is_value_rank_dependent(inst->getOperand(1), src) ||
      is_value_rank_dependent(inst->getOperand(2), src);

  case Instruction::VAArg:
    // We assume that argument are not rank dependent.
    return false;

  case Instruction::ExtractElement:
    return is_value_rank_dependent(inst->getOperand(0), src);

  case Instruction::InsertElement:
    return is_value_rank_dependent(inst->getOperand(0), src) ||
      is_value_rank_dependent(inst->getOperand(1), src);

  case Instruction::ShuffleVector:
    return is_value_rank_dependent(inst->getOperand(0), src) ||
      is_value_rank_dependent(inst->getOperand(1), src);

  case Instruction::ExtractValue:
    return is_value_rank_dependent(inst->getOperand(0), src);

  case Instruction::InsertValue:
    return is_value_rank_dependent(inst->getOperand(0), src) ||
      is_value_rank_dependent(inst->getOperand(1), src);

  default:
    assert(false);
  };
}

bool
FunctionSummary::updateAliasDep(const Function &F) {
  // Add aliasing memory locations.
  for (const_inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    if (isa<LoadInst>(inst) || isa<StoreInst>(inst) || isa<VAArgInst>(inst) ||
	isa<AtomicCmpXchgInst>(inst) || isa<AtomicRMWInst>(inst)) {
      MemoryLocation ML = MemoryLocation::get(inst);

      for (auto NI = intraDeps.nodeBegin(), NE = intraDeps.nodeEnd();
	   NI != NE; ++ NI) {
	AliasResult res = AA->alias(ML, *NI);

	if (res == NoAlias)
	  continue;

	bool changed = false;

	switch(res) {
	case MustAlias:
	  changed = intraDeps.addEdge(*NI, ML, IntraDepGraph::MustAlias);
	  break;
	case MayAlias:
	  changed = intraDeps.addEdge(*NI, ML, IntraDepGraph::MayAlias);
	  break;
	case PartialAlias:
	  changed = intraDeps.addEdge(*NI, ML, IntraDepGraph::PartialAlias);
	  break;
	case NoAlias:
	  break;
	};

	if (changed)
	  return true;
      }
    }
  }

  return false;
}

bool
FunctionSummary::updateFlowDep(const Function &F) {
  bool changed = false;

  // Add all memory location  whose value is computed using rank.
  for (const_inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    // Store
    if (isa<StoreInst>(inst)) {
      const StoreInst *si = cast<StoreInst>(inst);

      MemoryLocation src;
      if (is_value_rank_dependent(si->getValueOperand(), &src)) {
	changed = intraDeps.addEdge(src, MemoryLocation::get(si),
				   IntraDepGraph::Flow) ||
	  changed;
      }
    }

    // Memset
    if (isa<MemSetInst>(inst)) {
      const MemSetInst *ms = cast<MemSetInst>(inst);
      const Value *value = ms->getValue();
      MemoryLocation src;
      if (is_value_rank_dependent(value, &src)) {
	MemoryLocation dst = MemoryLocation::getForDest(ms);
	changed = intraDeps.addEdge(src, dst, IntraDepGraph::Flow) ||
	  changed;
      }
    }

    // MemTransfer
    if (isa<MemTransferInst>(inst)) {
      const MemTransferInst *mi = cast<MemTransferInst>(inst);
      MemoryLocation src;
      if (is_value_rank_dependent(mi->getSource(), &src)) {
	MemoryLocation dst = MemoryLocation::getForDest(mi);
	changed = intraDeps.addEdge(src, dst, IntraDepGraph::Flow) ||
	  changed;
      }
    }
  }

  // Add all memory location whose value depends on rank.
  // That is instructions writing in memory where IPDF is rank dependent.
  for (const_inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    // Store
    if (isa<StoreInst>(inst)) {
      const StoreInst *si = cast<StoreInst>(inst);
      const BasicBlock *bb = si->getParent();
      vector<BasicBlock * > IPDF = //getIPDF((BasicBlock *) bb);
	iterated_postdominance_frontier(*PDT, (BasicBlock *) bb);

      MemoryLocation src;
      if (is_IPDF_rank_dependent(IPDF, &src)) {
	MemoryLocation dst = MemoryLocation::get(si);
	changed = intraDeps.addEdge(src, dst, IntraDepGraph::Flow) ||
	  changed;
      }
    }

    // MemIntrinsic (memcpy, memset ..)
    if (isa<MemIntrinsic>(inst)) {
      const MemIntrinsic *mi = cast<MemIntrinsic>(inst);
      const BasicBlock *bb = mi->getParent();
      vector<BasicBlock * > IPDF = // getIPDF((BasicBlock *) bb);
	iterated_postdominance_frontier(*PDT, (BasicBlock *) bb);

      MemoryLocation src;
      if (is_IPDF_rank_dependent(IPDF, &src)) {
	MemoryLocation dst = MemoryLocation::getForDest(mi);
	changed = intraDeps.addEdge(src, dst, IntraDepGraph::Flow) ||
	  changed;
      }
    }
  }

  return changed;
}

bool
FunctionSummary::updateArgDep() {
  bool changed = false;

  for (auto I = inst_begin(*F), E = inst_end(*F); I != E; ++I) {
    const Instruction *inst = &*I;
    const CallInst *ci = dyn_cast<CallInst>(inst);
    if (!ci)
      continue;

    Function *called = ci->getCalledFunction();
    auto FIt = funcMap->find(called);
    if (FIt == funcMap->end())
      continue;
    FunctionSummary *calledSummary = (*FIt).second;

    for (unsigned i=0; i<ci->getNumArgOperands(); ++i) {
      if (isa<const MetadataAsValue>(ci->getArgOperand(i)))
	continue;

      MemoryLocation src;
      const Value *argValue = ci->getArgOperand(i);

      if (is_value_rank_dependent(argValue, &src)) {
	const Value *argument = getFunctionArgument(called, i);
	if (!argument) {
	  errs() << "argument no " << i << " is NULL for function "
		 << called->getName() << "\n";
	  continue;
	}

	MemoryLocation dst = MemoryLocation(getFunctionArgument(called, i),
					    MemoryLocation::UnknownSize);

	changed = interDeps->addEdge(src, dst, InterDepGraph::Argument) ||
	  changed;
	calledSummary->intraDeps.addRoot(dst);
      }
    }
  }

  // TODO: Add invoke instructions

  return changed;
}


bool
FunctionSummary::updateRetDep() {
  bool changed = false;

  if (!isRetDependent) {
    for (auto I = inst_begin(*F), E = inst_end(*F); I != E; ++I) {
      const Instruction *inst = &*I;
      const ReturnInst *ri = dyn_cast<ReturnInst>(inst);
      if (!ri)
	continue;

      MemoryLocation ML;
      Value *returnValue = ri->getReturnValue();
      if (!returnValue)
	continue;

      if (is_value_rank_dependent(returnValue, &ML)) {
	retDepSrc = ML;
	isRetDependent = true;
	changed = true;
	break;
      }
    }
  }

  if (isRetDependent) {
    for (auto I = retUserMap.begin(), E = retUserMap.end(); I != E; ++I) {
      MemoryLocation retUser = (*I).first;
      FunctionSummary *callerSummary = (*I).second;
      changed = interDeps->addEdge(retDepSrc, retUser, InterDepGraph::Return) ||
	changed;
      callerSummary->intraDeps.addRoot(retUser);
    }
  }

  return changed;
}

void
FunctionSummary::findMPICommRankRoots() {
  // Find memory locations where rank is stored by MPI_Comm_rank() function.
  for (auto I = inst_begin(*F), E = inst_end(*F); I != E; ++I) {
    const Instruction *inst = &*I;
    const CallInst *ci = dyn_cast<CallInst>(inst);
    if (!ci)
      continue;

    if (!ci->getCalledFunction()->getName().equals("MPI_Comm_rank"))
      continue;

    MemoryLocation ML = MemoryLocation::getForArgument(ImmutableCallSite(ci),
						       1,
						       *TLI);
    intraDeps.addRoot(ML);
  }
}

void
FunctionSummary::computeListeners() {
  for (auto I = inst_begin(*F), E = inst_end(*F); I != E; ++I) {
    const Instruction *inst = &*I;
    const CallInst *ci = dyn_cast<CallInst>(inst);
    if (!ci)
      continue;

    Function *called = ci->getCalledFunction();
    auto J = funcMap->find(called);
    if (J == funcMap->end())
      continue;

    FunctionSummary *calledSummary = (*J).second;

    MemoryLocation ML = MemoryLocation(ci, MemoryLocation::UnknownSize);
    calledSummary->retUserMap[ML] = this;
  }

  // TODO: comptue side effect listeners
}
