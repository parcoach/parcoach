#include "FunctionSummary.h"

#include "Collectives.h"
#include "Utils.h"

#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;
using namespace std;

FunctionSummary::FunctionSummary(Function *F,
				 const char *ProgName,
				 InterDependenceMap &interDepMap,
				 Pass *pass)
  : ProgName(ProgName), pass(pass), interDepMap(interDepMap), F(F),
    STAT_warnings(0), STAT_collectives(0) {

  MD = F->getParent();
  AA = &pass->getAnalysis<AliasAnalysis>();
  TLI = &pass->getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  firstPass();
}

FunctionSummary::~FunctionSummary() {}

void
FunctionSummary::firstPass() {
  // Get analyses
  DT = &pass->getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
  PDT = &pass->getAnalysis<PostDominatorTree>(*F);

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
    depGraph.addRoot(ML);
  }

  // Add rank dependent arguments as roots of the dep graph.
  const DepMap *DM = interDepMap.getFunctionDepMap(F);
  for (auto I = DM->begin(), E = DM->end(); I != E; ++I) {
    MemoryLocation ML = (*I).second;
    depGraph.addRoot(ML);
  }

  // Find all memory locations where value depends on rank until
  // reaching a fixed point.
  bool changed = true;
  while (changed) {
    changed = false;
    changed = findAliasDep(*F) || changed;
    changed = findValueDep(*F) || changed;
  }

  // Update rank dependent arguments map.
  updateArgMap();
}

bool
FunctionSummary::updateInterDep() {
  // Get analyses
  DT = &pass->getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
  PDT = &pass->getAnalysis<PostDominatorTree>(*F);

  bool changed = false;

  // Add rank dependent arguments as roots.
  const DepMap *DM = interDepMap.getFunctionDepMap(F);

  for (auto I = DM->begin(), E = DM->end(); I != E; ++I) {
    MemoryLocation ML = (*I).second;
    changed = depGraph.addRoot(ML) || changed;
  }

  if (!changed)
    return false;

  while (changed) {
    changed = false;
    changed = findAliasDep(*F) || changed;
    changed = findValueDep(*F) || changed;
  }

  // Update rank dependent arguments map.
  updateArgMap();

  return true;
}

void
FunctionSummary::toDot(StringRef filename) {
    depGraph.toDot(filename);
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
    STAT_collectives++;

    // Collective found, now check if basic block is rank dependent.
    vector<BasicBlock * > IPDF = //getIPDF((BasicBlock *) inst->getParent());
      iterated_postdominance_frontier(*PDT, inst->getParent());

    MemoryLocation src;
    if(is_IPDF_rank_dependent(IPDF, &src)) {
      STAT_warnings++;

      depGraph.addBarrierEdge(src, ci);

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

bool
FunctionSummary::updateArgMap() {
  bool changed = false;

  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;
    const CallInst *ci = dyn_cast<CallInst>(inst);
    if (ci) {
      const Function *called = ci->getCalledFunction();

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

	  changed = interDepMap.addDependence(called, src, dst) || changed;

	}
      }
    }

    // TODO: Add invoke instructions.
  }

  return changed;
}

bool
FunctionSummary::findAliasDep(const Function &F) {
  // Add aliasing memory locations.
  for (const_inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    if (isa<LoadInst>(inst) || isa<StoreInst>(inst) || isa<VAArgInst>(inst) ||
	isa<AtomicCmpXchgInst>(inst) || isa<AtomicRMWInst>(inst)) {
      MemoryLocation ML = MemoryLocation::get(inst);

      for (auto NI = depGraph.nodeBegin(), NE = depGraph.nodeEnd();
	   NI != NE; ++ NI) {
	AliasResult res = AA->alias(ML, *NI);

	if (res == NoAlias)
	  continue;

	bool changed = false;

	switch(res) {
	case MustAlias:
	  changed = depGraph.addEdge(*NI, ML, DependencyGraph::MustAlias);
	  break;
	case MayAlias:
	  changed = depGraph.addEdge(*NI, ML, DependencyGraph::MayAlias);
	  break;
	case PartialAlias:
	  changed = depGraph.addEdge(*NI, ML, DependencyGraph::PartialAlias);
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
FunctionSummary::findValueDep(const Function &F) {
  bool changed = false;

  // Add all memory location  whose value is computed using rank.
  for (const_inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    // Store
    if (isa<StoreInst>(inst)) {
      const StoreInst *si = cast<StoreInst>(inst);

      MemoryLocation src;
      if (is_value_rank_dependent(si->getValueOperand(), &src)) {
	changed = depGraph.addEdge(src, MemoryLocation::get(si),
				   DependencyGraph::ValueDep) ||
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
	changed = depGraph.addEdge(src, dst, DependencyGraph::ValueDep) ||
	  changed;
      }
    }

    // MemTransfer
    if (isa<MemTransferInst>(inst)) {
      const MemTransferInst *mi = cast<MemTransferInst>(inst);
      MemoryLocation src;
      if (is_value_rank_dependent(mi->getSource(), &src)) {
	MemoryLocation dst = MemoryLocation::getForDest(mi);
	changed = depGraph.addEdge(src, dst, DependencyGraph::ValueDep) ||
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
	changed = depGraph.addEdge(src, dst, DependencyGraph::ValueDep) ||
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
	changed = depGraph.addEdge(src, dst, DependencyGraph::ValueDep) ||
	  changed;
      }
    }
  }

  return changed;
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
    for (auto I = depGraph.nodeBegin(), E = depGraph.nodeEnd();
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

      for (auto I = depGraph.nodeBegin(), E = depGraph.nodeEnd();
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

      for (auto I = depGraph.nodeBegin(), E = depGraph.nodeEnd();
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
      for (auto I = depGraph.nodeBegin(), E = depGraph.nodeEnd();
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
    for (auto I = depGraph.nodeBegin(), E = depGraph.nodeEnd();
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
    // Since there is no interprocedural analysis yet, return false.
    return false;

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

