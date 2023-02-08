#include "parcoach/RMAPasses.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;

namespace parcoach::rma {

namespace {

FunctionType *getHandlerType(LLVMContext &C) {
  static FunctionType *HandlerType_ = nullptr;
  if (!HandlerType_) {
    HandlerType_ = FunctionType::get(Type::getVoidTy(C),
                                     {Type::getInt8Ty(C)->getPointerTo(),
                                      Type::getInt64Ty(C), Type::getInt64Ty(C),
                                      Type::getInt8Ty(C)->getPointerTo()},
                                     false);
  }
  return HandlerType_;
}

FunctionCallee getHandlerLoad(Module *M) {
  return M->getOrInsertFunction("LOAD", getHandlerType(M->getContext()));
}

FunctionCallee getHandlerStore(Module *M) {
  return M->getOrInsertFunction("STORE", getHandlerType(M->getContext()));
}

// To instrument LOAD/STORE
void CreateAndInsertFunction(Instruction &I, Value *Addr,
                             FunctionCallee handler, size_t size, int line,
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

std::vector<Instruction *> toInstrument;
std::vector<Instruction *>
    toDelete; // CallInst to delete (all MPI-RMA are replaced by new functions)

auto MagentaErr = []() {
  return WithColor(errs(), raw_ostream::Colors::MAGENTA);
};
auto GreenErr = []() { return WithColor(errs(), raw_ostream::Colors::GREEN); };
auto RedErr = []() { return WithColor(errs(), raw_ostream::Colors::RED); };

class LocalConcurrencyDetection {

public:
  LocalConcurrencyDetection(){};
  bool runOnModule(Module &M, ModuleAnalysisManager &AM);

private:
  static int count_LOAD, count_STORE, count_inst_STORE, count_inst_LOAD;

  // Instrumentation for dynamic analysis
  bool hasWhiteSucc(BasicBlock *BB);
  void InstrumentMemAccessesIt(Function &F);
  void DFS_BB(BasicBlock *bb, bool inEpoch);
  bool InstrumentBB(BasicBlock &BB, bool inEpoch);
  bool changeFuncNamesFORTRAN(CallBase &CB);
  bool changeFuncNamesC(Instruction &I, CallInst *ci, Function *calledFunction,
                        int line, StringRef file);
  // Debug
#ifndef NDEBUG
  void printBB(BasicBlock *BB);
#endif
  // Utils
  void resetCounters();
  enum Color { WHITE, GREY, BLACK };
  using BBColorMap = DenseMap<const BasicBlock *, Color>;
  using MemMap = ValueMap<Value *, Instruction *>;
  BBColorMap ColorMap;
};

// Instrument MPI FORTRAN operations
bool LocalConcurrencyDetection::changeFuncNamesFORTRAN(CallBase &CB) {
  StringRef Name = CB.getCalledFunction()->getName();
  using PairTy = std::pair<StringRef, bool>;
  PairTy NewVals =
      StringSwitch<PairTy>(Name)
          .Case("mpi_put_", {"new_put_", false})
          .Case("mpi_get_", {"new_get_", false})
          .Case("mpi_accumulate_", {"new_accumulate_", false})
          .Case("mpi_win_create_", {"new_win_create_", false})
          .Case("mpi_win_free_", {"new_win_free_", false})
          .Case("mpi_win_fence_", {"new_fence_", true})
          .Case("mpi_win_unlock_all_", {"new_win_unlock_all_", true})
          .Case("mpi_win_unlock_", {"new_win_unlock_", true})
          .Case("mpi_win_lock_", {"new_win_lock_", true})
          .Case("mpi_win_lock_all_", {"new_win_lock_all_", true})
          .Case("mpi_win_flush_", {"new_win_flush_", false})
          .Case("mpi_barrier_", {"new_barrier_", false})
          .Default({"", false});
  auto [NewName, NewEpoch] = NewVals;
  if (!NewName.empty()) {
    FunctionCallee NewF = CB.getModule()->getOrInsertFunction(
        NewVals.first, CB.getCalledFunction()->getFunctionType());
    CB.setCalledFunction(cast<Function>(NewF.getCallee()));
  }
  return NewVals.second;
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

    if (CallInst *ci = dyn_cast<CallInst>(&I)) {
      if (Function *calledFunction = ci->getCalledFunction()) {
        if (calledFunction->getName().startswith("MPI_")) {
          newEpoch = changeFuncNamesC(I, ci, calledFunction, line, file);
        } else if (calledFunction->getName().startswith("mpi_")) {
          newEpoch = changeFuncNamesFORTRAN(*ci);
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

#ifndef NDEBUG
void LocalConcurrencyDetection::printBB(BasicBlock *bb) {
  errs() << "DEBUG: BB ";
  bb->printAsOperand(errs(), false);
  errs() << "\n";
}
#endif

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
    auto const &Stats = FAM.getResult<RMAStatisticsAnalysis>(F);
    CyanErr() << "done \n";

    // Detection of local concurrency errors - BFS
    CyanErr() << "(2) Local concurrency errors detection ...";
    if (Stats.getTotalOneSided() != 0) {
      auto &Res = FAM.getResult<LocalConcurrencyAnalysis>(F);
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

} // namespace

PreservedAnalyses RMAInstrumentationPass::run(Module &M,
                                              ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("LocalConcurrencyDetectionPass");
  LocalConcurrencyDetection LCD;
  return LCD.runOnModule(M, AM) ? PreservedAnalyses::none()
                                : PreservedAnalyses::all();
}

} // namespace parcoach::rma
