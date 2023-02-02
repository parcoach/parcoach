#include "LocalConcurrencyDetection.h"

#include "parcoach/LocalConcurrencyDetectionPass.h"

#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"

using namespace llvm;
using namespace std;

FunctionType *handlerType = nullptr;

FunctionType *getHandlerType(LLVMContext &C) {
  if (!handlerType) {
    handlerType = FunctionType::get(Type::getVoidTy(C),
                                    {Type::getInt8Ty(C)->getPointerTo(),
                                     Type::getInt64Ty(C), Type::getInt64Ty(C),
                                     Type::getInt8Ty(C)->getPointerTo()},
                                    false);
  }
  return handlerType;
}

FunctionCallee handlerLoad;
FunctionCallee handlerStore;

FunctionCallee &getHandlerLoad(Module *M) {
  if (!handlerLoad) {
    handlerLoad =
        M->getOrInsertFunction("LOAD", getHandlerType(M->getContext()));
  }
  return handlerLoad;
}

FunctionCallee &getHandlerStore(Module *M) {
  if (!handlerStore) {
    handlerStore =
        M->getOrInsertFunction("STORE", getHandlerType(M->getContext()));
  }
  return handlerStore;
}

// To instrument LOAD/STORE
void CreateAndInsertFunction(Instruction &I, Value *Addr,
                             FunctionCallee &handler, size_t size, int line,
                             StringRef file) {

  IRBuilder<> builder(&I);
  builder.SetInsertPoint(I.getParent(), builder.GetInsertPoint());
  Value *filename = builder.CreateGlobalStringPtr(file.str());

  CastInst *CI = CastInst::CreatePointerCast(
      Addr, builder.getInt8Ty()->getPointerTo(), "", &I);
  CastInst *CIf = CastInst::CreatePointerCast(
      filename, builder.getInt8Ty()->getPointerTo(), "", &I);
  builder.CreateCall(handler,
                     {CI, ConstantInt::get(builder.getInt64Ty(), size),
                      ConstantInt::get(builder.getInt64Ty(), line), CIf});
  // DEBUG INFO: print function prototype//
  // handler.getFunctionType()->print(errs());
}

// To modify MPI-RMA functions - For C codes
void ReplaceCallInst(Instruction &I, CallInst &ci, int line, StringRef file,
                     StringRef newFuncName) {
  IRBuilder<> builder(&ci);
  Value *filename = builder.CreateGlobalStringPtr(file.str());

  Function *callee = ci.getCalledFunction();
  auto newArgsType = callee->getFunctionType()->params().vec();
  newArgsType.push_back(builder.getInt8PtrTy());
  newArgsType.push_back(builder.getInt64Ty());
  FunctionType *handlerType =
      FunctionType::get(callee->getReturnType(), newArgsType, false);
  // handlerType->print(errs());

  Module *M = ci.getFunction()->getParent();
  FunctionCallee newFunc = M->getOrInsertFunction(newFuncName, handlerType);

  SmallVector<Value *, 8> Args(ci.args());
  CastInst *CIf =
      CastInst::CreatePointerCast(filename, builder.getInt8PtrTy(), "", &I);
  Args.push_back(CIf);
  Args.push_back(ConstantInt::get(builder.getInt64Ty(), line));

  Value *newci = builder.CreateCall(newFunc, Args);
  ci.replaceAllUsesWith(newci);
}

/*	void ReplaceBitcast(CallInst &ci, int line, StringRef file, LLVMContext
&Ctx)
        {
        IRBuilder<> builder(&ci);
        Value *filename = builder.CreateGlobalStringPtr(file.str());
        errs() << "DEB 1\n";
        Function *callee = ci.getCalledFunction();
// PB ICIIIIII getFunctionType()
auto newArgsType = callee->getFunctionType()->params().vec();
errs() << "DEB 2\n";
//newArgsType.push_back(Type::getInt8PtrTy(Ctx));
newArgsType.push_back(Type::getInt64Ty(Ctx));
errs() << "DEB 3\n";
FunctionType *handlerType = FunctionType::get(callee->getReturnType(),
newArgsType, false); errs() << "DEB 4\n";
//handlerType->print(errs());

Module *M = ci.getFunction()->getParent();
FunctionCallee newFunc = M->getOrInsertFunction ("",handlerType);
errs() << "DEB 5\n";

SmallVector<Value *,8> Args(ci.args());
//CastInst* CIf = CastInst::CreatePointerCast(filename,
Type::getInt8PtrTy(Ctx),"", &I);
//Args.push_back(CIf);
Args.push_back(ConstantInt::get(Type::getInt64Ty(Ctx), line));
errs() << "DEB 6\n";

Value *newci = builder.CreateCall(newFunc, Args );
errs() << "DEB 7\n";
ci.replaceAllUsesWith(newci);
}
*/

vector<Instruction *> toInstrument;
vector<Instruction *>
    toDelete; // CallInst to delete (all MPI-RMA are replaced by new functions)

auto MagentaErr = []() {
  return WithColor(errs(), raw_ostream::Colors::MAGENTA);
};
auto GreenErr = []() { return WithColor(errs(), raw_ostream::Colors::GREEN); };
auto RedErr = []() { return WithColor(errs(), raw_ostream::Colors::RED); };

// Instrument MPI FORTRAN operations
bool LocalConcurrencyDetection::changeFuncNamesFORTRAN(Instruction &I) {

  if (I.getOperand(0)->getName() == ("mpi_put_")) {
    I.getOperand(0)->setName("new_put_");
  } else if (I.getOperand(0)->getName() == ("mpi_get_")) {
    I.getOperand(0)->setName("new_get_");
  } else if (I.getOperand(0)->getName() == ("mpi_accumulate_")) {
    I.getOperand(0)->setName("new_accumulate_");
  } else if (I.getOperand(0)->getName() == ("mpi_win_create_")) {
    I.getOperand(0)->setName("new_win_create_");
  } else if (I.getOperand(0)->getName() == ("mpi_win_free_")) {
    I.getOperand(0)->setName("new_win_free_");
  } else if (I.getOperand(0)->getName() == ("mpi_win_fence_")) {
    I.getOperand(0)->setName("new_fence_");
    return true;
  } else if (I.getOperand(0)->getName() == ("mpi_win_unlock_all_")) {
    I.getOperand(0)->setName("new_win_unlock_all_");
    return true;
  } else if (I.getOperand(0)->getName() == ("mpi_win_unlock_")) {
    I.getOperand(0)->setName("new_win_unlock_");
    return true;
  } else if (I.getOperand(0)->getName() == ("mpi_win_lock_")) {
    I.getOperand(0)->setName("new_win_lock_");
    return true;
  } else if (I.getOperand(0)->getName() == ("mpi_win_lock_all_")) {
    I.getOperand(0)->setName("new_win_lock_all_");
    return true;
  } else if (I.getOperand(0)->getName() == ("mpi_win_flush_")) {
    I.getOperand(0)->setName("new_win_flush_");
  } else if (I.getOperand(0)->getName() == ("mpi_barrier_")) {
    I.getOperand(0)->setName("new_barrier_");
  }
  return false;
}

// Instrument MPI C functions and update toDelete
// return true if the function is an epoch creation/destruction
// for now, just instrument CallInst, TODO: do the same thing for invoke
bool LocalConcurrencyDetection::changeFuncNamesC(Instruction &I, CallInst *ci,
                                                 Function *calledFunction,
                                                 int line, StringRef file) {

  if (calledFunction->getName() == "MPI_Get") {
    ReplaceCallInst(I, *ci, line, file, "new_Get");
    toDelete.push_back(ci);
  } else if (calledFunction->getName() == "MPI_Put") {
    ReplaceCallInst(I, *ci, line, file, "new_Put");
    toDelete.push_back(ci);
  } else if (calledFunction->getName() == "MPI_Win_create") {
    calledFunction->setName("new_Win_create");
  } else if (calledFunction->getName() == "MPI_Accumulate") {
    calledFunction->setName("new_Accumulate");
  } else if (calledFunction->getName() == "MPI_Win_fence") {
    return true;
  } else if (calledFunction->getName() == "MPI_Win_flush") {
  } else if (calledFunction->getName() == "MPI_Win_lock") {
    calledFunction->setName("new_Win_lock");
    return true;
  } else if (calledFunction->getName() == "MPI_Win_unlock") {
    calledFunction->setName("new_Win_unlock");
    return true;
  } else if (calledFunction->getName() == "MPI_Win_unlock_all") {
    calledFunction->setName("new_Win_unlock_all");
    return true;
  } else if (calledFunction->getName() == "MPI_Win_lock_all") {
    calledFunction->setName("new_Win_lock_all");
    return true;
  } else if (calledFunction->getName() == "MPI_Win_free") {
    calledFunction->setName("new_Win_free");
  } else if (calledFunction->getName() == "MPI_Barrier") {
    calledFunction->setName("new_Barrier");
  }
  return false;
}

bool LocalConcurrencyDetection::InstrumentBB(BasicBlock &bb, bool inEpoch) {

  Module *M = bb.getModule();
  DataLayout const &DL = M->getDataLayout();

  bool newEpoch = false;

  for (Instruction &I : bb) {

    DebugLoc dbg = I.getDebugLoc(); // get debug infos
    int line = 0;
    StringRef file = "";
    if (dbg) {
      line = dbg.getLine();
      file = dbg->getFilename();
    }

    // Change MPI function names for FORTRAN codes
    if (I.getOpcode() == Instruction::BitCast) {
      if (I.getOperand(0)->getName().startswith("mpi_")) {
        newEpoch = changeFuncNamesFORTRAN(I);
      }
      // If the instruction is a MPI call, change its name"
      //}else if (CallBase *cb = dyn_cast<CallBase>(&I)) {
    } else if (CallInst *ci = dyn_cast<CallInst>(&I)) {
      if (Function *calledFunction = ci->getCalledFunction()) {
        if (calledFunction->getName().startswith("MPI_")) {
          newEpoch = changeFuncNamesC(I, ci, calledFunction, line, file);
        }
      }
      // If the instruction is a LOAD or a STORE
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
      Value *Addr = SI->getPointerOperand();
      auto elTy = SI->getValueOperand()->getType();
      size_t size = DL.getTypeSizeInBits(elTy);
      // DEBUG INFO: //errs() << "PARCOACH DEBUG: Found a store of " << size <<
      // " bits of type "; SI->getPointerOperandType()->print(errs()); errs() <<
      // " (TypeID = " << elTy->getTypeID() <<")\n";
      if (inEpoch || newEpoch) {
        CreateAndInsertFunction(I, Addr, getHandlerStore(M), size, line, file);
        count_inst_STORE++;
      }
      count_STORE++;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
      Value *Addr = LI->getPointerOperand();
      auto elTy = LI->getType();
      size_t size = DL.getTypeSizeInBits(elTy);
      // DEBUG INFO: // errs() << "PARCOACH DEBUG: Found a load of " << size <<
      // " bits of type "; LI->getPointerOperandType()->print(errs()); errs() <<
      // " (TypeID = " << elTy->getTypeID() <<")\n";
      if (inEpoch || newEpoch) {
        CreateAndInsertFunction(I, Addr, getHandlerLoad(M), size, line, file);
        count_inst_LOAD++;
      }
      count_LOAD++;
    }
  }
  if (newEpoch)
    return (1 - inEpoch);
  return inEpoch;
}

// Instrument memory accesses (DFS is used to instrument only in epochs)
// TODO: interprocedural info: start with the main function

// recursive version
void LocalConcurrencyDetection::DFS_BB(BasicBlock *bb, bool inEpoch) {

  if (ColorMap[bb] == WHITE) {
    inEpoch = InstrumentBB(*bb, inEpoch);
    ColorMap[bb] = GREY;
  }
  succ_iterator SI = succ_begin(bb), E = succ_end(bb);
  for (; SI != E; ++SI) {
    BasicBlock *Succ = *SI;
    // printBB(Succ);
    if (Succ != bb) // TODO: fix  if is loop header and white ok, otherwise,
                    // don't do DFS_BB
      DFS_BB(Succ, inEpoch);
  }
  ColorMap[bb] = BLACK;
}

void LocalConcurrencyDetection::InstrumentMemAccessesRec(Function &F) {
  // All BB must be white at the beginning
  for (BasicBlock &BB : F) {
    ColorMap[&BB] = WHITE;
  }
  // start with entry BB
  BasicBlock &entry = F.getEntryBlock();
  DFS_BB(&entry, false);
  for (Instruction *i : toDelete) {
    i->eraseFromParent();
  }
}

bool LocalConcurrencyDetection::hasWhiteSucc(BasicBlock *BB) {
  succ_iterator SI = succ_begin(BB), E = succ_end(BB);
  for (; SI != E; ++SI) {
    BasicBlock *Succ = *SI;
    if (ColorMap[Succ] == WHITE)
      return true;
  }
  return false;
}

// Iterative version
void LocalConcurrencyDetection::InstrumentMemAccessesIt(Function &F) {
  // All BB must be white at the beginning
  for (BasicBlock &BB : F) {
    ColorMap[&BB] = WHITE;
  }
  toDelete.clear();
  // start with entry BB
  BasicBlock &entry = F.getEntryBlock();
  bool inEpoch = false;
  std::deque<BasicBlock *> Unvisited;
  Unvisited.push_back(&entry);
  inEpoch = InstrumentBB(entry, inEpoch);
  ColorMap[&entry] = GREY;

  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();

    if (hasWhiteSucc(header)) {
      // inEpoch = InstrumentBB(*header, Ctx, datalayout, inEpoch);
      succ_iterator SI = succ_begin(header), E = succ_end(header);
      for (; SI != E; ++SI) {
        BasicBlock *Succ = *SI;
        if (ColorMap[Succ] == WHITE) {
          Unvisited.push_front(Succ);
          inEpoch = InstrumentBB(*Succ, inEpoch);
          ColorMap[Succ] = GREY;
        }
      }
    } else {
      Unvisited.pop_front();
      ColorMap[header] = BLACK;
    }
  }
  // delete instructions in toDelete per function (because we change the IR)
  for (Instruction *i : toDelete) {
    i->eraseFromParent();
  }
}

void LocalConcurrencyDetection::printBB(BasicBlock *bb) {
  errs() << "DEBUG: BB ";
  bb->printAsOperand(errs(), false);
  errs() << "\n";
}

void LocalConcurrencyDetection::resetCounters() {
  count_LOAD = 0;
  count_STORE = 0;
  count_inst_LOAD = 0;
  count_inst_STORE = 0;
}

// Main function of the pass
bool LocalConcurrencyDetection::runOnModule(Module &M,
                                            ModuleAnalysisManager &AM) {
  auto CyanErr = []() { return WithColor(errs(), raw_ostream::Colors::CYAN); };

  MagentaErr() << "===========================\n";
  MagentaErr() << "===  PARCOACH ANALYSIS  ===\n";
  MagentaErr() << "===========================\n";

  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;

    resetCounters();

    CyanErr() << "===========================\n";
    CyanErr() << "ANALYZING function " << F.getName() << "...\n";

    // Get statistics
    CyanErr() << "(1) Get statistics ...";
    auto const &Stats = FAM.getResult<parcoach::RMAStatisticsAnalysis>(F);
    CyanErr() << "done \n";

    // Detection of local concurrency errors - BFS
    CyanErr() << "(2) Local concurrency errors detection ...";
    if (Stats.getTotalOneSided() != 0) {
      auto &Res = FAM.getResult<parcoach::LocalConcurrencyAnalysis>(F);
      for (auto [I1, I2] : Res) {
        RedErr() << "LocalConcurrency detected: conflit with the "
                    "following instructions: \n";
        I1->print(errs());
        DebugLoc dbg = I1->getDebugLoc();
        if (dbg)
          GreenErr() << " - LINE " << dbg.getLine() << " in "
                     << dbg->getFilename();
        RedErr() << "\nAND\n";
        I2->print(errs());
        dbg = I2->getDebugLoc();
        if (dbg)
          GreenErr() << " - LINE " << dbg.getLine() << " in "
                     << dbg->getFilename();
        errs() << "\n";
      }
    }
    CyanErr() << "done \n";

    // Instrumentation of memory accesses for dynamic analysis
    CyanErr() << "(3) Instrumentation for dynamic analysis ...";
    InstrumentMemAccessesIt(F);
    CyanErr() << "done \n";

    // Print statistics per function
    CyanErr() << "=== STATISTICS === \n";
    CyanErr() << Stats.Mpi << " MPI functions including " << Stats.getTotalRMA()
              << " RMA functions \n";
    CyanErr() << "= WINDOW CREATION/DESTRUCTION: " << Stats.Free
              << " MPI_Win_free, " << Stats.Win << " MPI_Win_create \n";
    CyanErr() << "= EPOCH CREATION/DESTRUCTION: " << Stats.Fence
              << " MPI_Win_fence, " << Stats.Lock << " MPI_Lock, "
              << Stats.Lockall << " MPI_Lockall " << Stats.Unlock
              << " MPI_Unlock, " << Stats.Unlockall << " MPI_Unlockall \n";
    CyanErr() << "= ONE-SIDED COMMUNICATIONS: " << Stats.Get << " MPI_Get, "
              << Stats.Put << " MPI_Put, " << Stats.Acc << " MPI_Accumulate \n";
    CyanErr() << "= SYNCHRONIZATION: " << Stats.Flush << " MPI_Win_Flush \n";

    CyanErr() << "LOAD/STORE STATISTICS: " << count_inst_LOAD << " (/"
              << count_LOAD << ") LOAD and " << count_inst_STORE << " (/"
              << count_STORE << ") STORE are instrumented\n";
    // DEBUG INFO: dump the module// F.getParent()->print(errs(),nullptr);
  }
  MagentaErr() << "===========================\n";
  return true;
}

int LocalConcurrencyDetection::count_LOAD = 0;
int LocalConcurrencyDetection::count_STORE = 0;
int LocalConcurrencyDetection::count_inst_LOAD = 0;
int LocalConcurrencyDetection::count_inst_STORE = 0;

namespace parcoach {

PreservedAnalyses
LocalConcurrencyDetectionPass::run(Module &M, ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("LocalConcurrencyDetectionPass");
  LocalConcurrencyDetection LCD;
  return LCD.runOnModule(M, AM) ? PreservedAnalyses::none()
                                : PreservedAnalyses::all();
}

} // namespace parcoach
