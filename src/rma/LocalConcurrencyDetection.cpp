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
                             StringRef file, LLVMContext &Ctx) {

  IRBuilder<> builder(&I);
  builder.SetInsertPoint(I.getParent(), builder.GetInsertPoint());
  Value *filename = builder.CreateGlobalStringPtr(file.str());

  CastInst *CI = CastInst::CreatePointerCast(
      Addr, Type::getInt8Ty(Ctx)->getPointerTo(), "", &I);
  CastInst *CIf = CastInst::CreatePointerCast(
      filename, Type::getInt8Ty(Ctx)->getPointerTo(), "", &I);
  builder.CreateCall(handler,
                     {CI, ConstantInt::get(Type::getInt64Ty(Ctx), size),
                      ConstantInt::get(Type::getInt64Ty(Ctx), line), CIf});
  // DEBUG INFO: print function prototype//
  // handler.getFunctionType()->print(errs());
}

// To modify MPI-RMA functions - For C codes
void ReplaceCallInst(Instruction &I, CallInst &ci, int line, StringRef file,
                     LLVMContext &Ctx, StringRef newFuncName) {
  IRBuilder<> builder(&ci);
  Value *filename = builder.CreateGlobalStringPtr(file.str());

  Function *callee = ci.getCalledFunction();
  auto newArgsType = callee->getFunctionType()->params().vec();
  newArgsType.push_back(Type::getInt8PtrTy(Ctx));
  newArgsType.push_back(Type::getInt64Ty(Ctx));
  FunctionType *handlerType =
      FunctionType::get(callee->getReturnType(), newArgsType, false);
  // handlerType->print(errs());

  Module *M = ci.getFunction()->getParent();
  FunctionCallee newFunc = M->getOrInsertFunction(newFuncName, handlerType);

  SmallVector<Value *, 8> Args(ci.args());
  CastInst *CIf =
      CastInst::CreatePointerCast(filename, Type::getInt8PtrTy(Ctx), "", &I);
  Args.push_back(CIf);
  Args.push_back(ConstantInt::get(Type::getInt64Ty(Ctx), line));

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

// Count MPI operations in Fortran
void LocalConcurrencyDetection::CountMPIfuncFORTRAN(Instruction &I) {
  if (I.getOperand(0)->getName() == ("mpi_put_")) {
    // GreenErr() << "PARCOACH DEBUG: Found Put\n ";
    // DEBUG INFO: I.print(errs(),nullptr);
    count_PUT++;
  } else if (I.getOperand(0)->getName() == ("mpi_get_")) {
    count_GET++;
  } else if (I.getOperand(0)->getName() == ("mpi_accumulate_")) {
    count_ACC++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_create_")) {
    count_Win++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_free_")) {
    count_Free++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_fence_")) {
    count_FENCE++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_unlock_all_")) {
    count_UNLOCKALL++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_unlock_")) {
    count_UNLOCK++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_lock_")) {
    count_LOCK++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_lock_all_")) {
    count_LOCKALL++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_flush_")) {
    count_FLUSH++;
  } else if (I.getOperand(0)->getName() == ("mpi_barrier_")) {
    count_BARRIER++;
  }
}

void LocalConcurrencyDetection::CountMPIfuncC(Instruction &I,
                                              Function *calledFunction) {

  if (calledFunction->getName() == "MPI_Get") {
    count_GET++;
    // DEBUG INFO: errs() << "PARCOACH DEBUG: Found " <<
    // calledFunction->getName() << " from " << ci->getArgOperand(7) << " to "
    // << ci->getArgOperand(0) << "\n"; calledFunction->setName("new_Get");
  } else if (calledFunction->getName() == "MPI_Put") {
    count_PUT++;
    // DEBUG INFO:
    // GreenErr() << "PARCOACH DEBUG: Found Put\n "; // << ci->getArgOperand(0)
    // << " to " << ci->getArgOperand(7) << "\n";
  } else if (calledFunction->getName() == "MPI_Win_create") {
    count_Win++;
    // DEBUG INFO: errs() << "PARCOACH DEBUG: Found Win_create on " <<
    // ci->getArgOperand(0) << "\n";
  } else if (calledFunction->getName() == "MPI_Accumulate") {
    count_ACC++;
  } else if (calledFunction->getName() == "MPI_Win_fence") {
    count_FENCE++;
  } else if (calledFunction->getName() == "MPI_Win_flush") {
    count_FLUSH++;
  } else if (calledFunction->getName() == "MPI_Win_lock") {
    count_LOCK++;
  } else if (calledFunction->getName() == "MPI_Win_unlock") {
    count_UNLOCK++;
  } else if (calledFunction->getName() == "MPI_Win_unlock_all") {
    count_UNLOCKALL++;
  } else if (calledFunction->getName() == "MPI_Win_lock_all") {
    count_LOCKALL++;
  } else if (calledFunction->getName() == "MPI_Win_free") {
    count_Free++;
  } else if (calledFunction->getName() == "MPI_Barrier") {
    count_BARRIER++;
  }
}

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
bool LocalConcurrencyDetection::changeFuncNamesC(LLVMContext &Ctx,
                                                 Instruction &I, CallInst *ci,
                                                 Function *calledFunction,
                                                 int line, StringRef file) {

  if (calledFunction->getName() == "MPI_Get") {
    ReplaceCallInst(I, *ci, line, file, Ctx, "new_Get");
    toDelete.push_back(ci);
  } else if (calledFunction->getName() == "MPI_Put") {
    ReplaceCallInst(I, *ci, line, file, Ctx, "new_Put");
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

void LocalConcurrencyDetection::GetRMAstatistics(Function *F) {

  for (Instruction &I : instructions(F)) {
    // I.print(errs());
    // errs() << "\n";
    DebugLoc dbg = I.getDebugLoc(); // get debug infos
    if (I.getOpcode() == Instruction::BitCast) {
      if (I.getOperand(0)->getName().startswith("mpi_")) {
        // GreenErr() << "PARCOACH DEBUG: Found a MPI function \n ";
        count_MPI++;
        CountMPIfuncFORTRAN(I);
      }
    } else if (CallBase *cb = dyn_cast<CallBase>(&I)) {
      //}else if (CallInst *ci = dyn_cast<CallInst>(&I)) {

      if (Function *calledFunction = cb->getCalledFunction()) {
        // errs() << calledFunction->getName() << "\n";
        if (calledFunction->getName().startswith("MPI_")) {
          // GreenErr() << "PARCOACH DEBUG: Found a MPI function \n ";
          // I.print(errs());
          count_MPI++;
          CountMPIfuncC(I, calledFunction);
        }
      }
    }
  }
}

// void LocalConcurrencyDetection::instrumentMemAccesses(Function &F,LLVMContext
// &Ctx, DataLayout* datalayout)
bool LocalConcurrencyDetection::InstrumentBB(BasicBlock &bb, LLVMContext &Ctx,
                                             DataLayout *datalayout,
                                             bool inEpoch) {

  Function *F = bb.getParent();
  Module *M = F->getParent();

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
          newEpoch = changeFuncNamesC(Ctx, I, ci, calledFunction, line, file);
        }
      }
      // If the instruction is a LOAD or a STORE
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
      Value *Addr = SI->getPointerOperand();
      auto elTy = SI->getValueOperand()->getType();
      size_t size = datalayout->getTypeSizeInBits(elTy);
      // DEBUG INFO: //errs() << "PARCOACH DEBUG: Found a store of " << size <<
      // " bits of type "; SI->getPointerOperandType()->print(errs()); errs() <<
      // " (TypeID = " << elTy->getTypeID() <<")\n";
      if (inEpoch || newEpoch) {
        CreateAndInsertFunction(I, Addr, getHandlerStore(M), size, line, file,
                                Ctx);
        count_inst_STORE++;
      }
      count_STORE++;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
      Value *Addr = LI->getPointerOperand();
      auto elTy = LI->getType();
      size_t size = datalayout->getTypeSizeInBits(elTy);
      // DEBUG INFO: // errs() << "PARCOACH DEBUG: Found a load of " << size <<
      // " bits of type "; LI->getPointerOperandType()->print(errs()); errs() <<
      // " (TypeID = " << elTy->getTypeID() <<")\n";
      if (inEpoch || newEpoch) {
        CreateAndInsertFunction(I, Addr, getHandlerLoad(M), size, line, file,
                                Ctx);
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
void LocalConcurrencyDetection::DFS_BB(BasicBlock *bb, LLVMContext &Ctx,
                                       DataLayout *datalayout, bool inEpoch) {

  if (ColorMap[bb] == WHITE) {
    inEpoch = InstrumentBB(*bb, Ctx, datalayout, inEpoch);
    ColorMap[bb] = GREY;
  }
  succ_iterator SI = succ_begin(bb), E = succ_end(bb);
  for (; SI != E; ++SI) {
    BasicBlock *Succ = *SI;
    // printBB(Succ);
    if (Succ != bb) // TODO: fix  if is loop header and white ok, otherwise,
                    // don't do DFS_BB
      DFS_BB(Succ, Ctx, datalayout, inEpoch);
  }
  ColorMap[bb] = BLACK;
}

void LocalConcurrencyDetection::InstrumentMemAccessesRec(
    Function &F, LLVMContext &Ctx, DataLayout *datalayout) {
  // All BB must be white at the beginning
  for (BasicBlock &BB : F) {
    ColorMap[&BB] = WHITE;
  }
  // start with entry BB
  BasicBlock &entry = F.getEntryBlock();
  DFS_BB(&entry, Ctx, datalayout, false);
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
void LocalConcurrencyDetection::InstrumentMemAccessesIt(
    Function &F, LLVMContext &Ctx, DataLayout *datalayout) {
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
  inEpoch = InstrumentBB(entry, Ctx, datalayout, inEpoch);
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
          inEpoch = InstrumentBB(*Succ, Ctx, datalayout, inEpoch);
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

// Get the memory access from the instruction
// We consider only local accesses here
enum LocalConcurrencyDetection::ACCESS
LocalConcurrencyDetection::getInstructionType(Instruction *I) {

  // For FORTRAN codes:
  if (I->getOpcode() == Instruction::BitCast) {
    if (I->getOperand(0)->getName() == ("mpi_get_")) {
      return WRITE;
    } else if (I->getOperand(0)->getName() == ("mpi_put_")) {
      return READ;
    }
    // For C codes:
  } else if (CallBase *ci = dyn_cast<CallBase>(I)) {
    if (Function *calledFunction = ci->getCalledFunction()) {
      if (calledFunction->getName() == "MPI_Get") {
        return WRITE;
      } else if (calledFunction->getName() == "MPI_Put") {
        return READ;
      }
    }
  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    return WRITE;
  } else if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    return READ;
  }
  // Default return
  return READ;
}

bool LocalConcurrencyDetection::hasBeenReported(Function *F, Instruction *I1,
                                                Instruction *I2) {
  for (auto &Instpair : ConcurrentAccesses[F]) {
    if ((Instpair.first == I1 && Instpair.second == I2) ||
        (Instpair.first == I2 && Instpair.second == I1)) // anyof
      return true;
  }
  return false;
}

// Store the memory accesses - we keep the memory which is a value and the last
// instruction accessing this memory address
// TODO: Check interprocedural information
// TODO: keep the accesses per window
void LocalConcurrencyDetection::analyzeBB(BasicBlock *bb, AAResults &AA) {
  Function *F = bb->getParent();

  for (auto i = bb->begin(), e = bb->end(); i != e; ++i) {
    Instruction *inst = &*i;
    DebugLoc dbg = inst->getDebugLoc(); // get debug infos
    int line = 0;
    StringRef file = "";
    if (dbg) {
      line = dbg.getLine();
      file = dbg->getFilename();
    }
    Value *mem = NULL;
    bool isLoadOrStore = false;

    // (1) Get memory access
    if (CallBase *ci = dyn_cast<CallBase>(inst)) {
      // ci->print(errs());
      // errs() << "\n";
      if (Function *calledFunction = ci->getCalledFunction()) {
        // errs() << "Calledfunction = " << calledFunction->getName() << "\n";
        if ((calledFunction->getName() == "MPI_Get") ||
            (calledFunction->getName() == "MPI_Put")) {
          mem = ci->getArgOperand(0);
          // errs() << "!!!!!!!! Found a put / get -> store mem \n";
        } else if ((calledFunction->getName() == "MPI_Win_flush") ||
                   (calledFunction->getName() == "MPI_Win_flush_all") ||
                   (calledFunction->getName() == "MPI_Win_fence") ||
                   (calledFunction->getName() == "MPI_Win_flush_local")) {
          // GreenErr() << "---> Found a synchro\n";
          BBToMemAccess[bb].clear();
        }
      }
    } else if (inst->getOpcode() == Instruction::BitCast) {
      // inst->print(errs());
      // errs() << "\n";
      if (inst->getOperand(0)->getName() == ("mpi_put_") ||
          inst->getOperand(0)->getName() == ("mpi_get_")) {
        // errs() << "!!!!!!!! Found a put / get !!!!!! line " << line << " in "
        // << file << "\n";

        for (auto user = inst->user_begin(); user != inst->user_end(); ++user) {
          // user->print(errs());
          // errs() << "\n";
          if (isa<CallBase>(cast<Instruction>(*user))) {
            // errs() << "CallInst!\n";
            CallBase *c = dyn_cast<CallBase>(*user);
            // c->print(errs());
            // errs() << "\n";
            // errs() << "\n";
            mem = c->getArgOperand(0);
            errs() << inst->getOpcodeName()
                   << " with operands: " << c->getArgOperand(0) << ", "
                   << c->getArgOperand(1) << "\n";
            // errs() << "!!!! store mem : " << mem << "!!!\n";
          } else if (isa<InvokeInst>(cast<Instruction>(*user))) {
            // errs() << "Invoke! Nothing is stored\n";
          }
        }
      } else {
        StringRef bitcastName = inst->getOperand(0)->getName();
        if (bitcastName == ("mpi_win_flush_") ||
            bitcastName == ("mpi_win_flush_all_") ||
            bitcastName == "MPI_Win_flush_all" ||
            bitcastName == "MPI_Win_flush" || bitcastName == "MPI_Win_fence" ||
            bitcastName == ("mpi_win_fence_") ||
            bitcastName == ("MPI_Win_flush_local")) {
          // GreenErr() << "---> Found a synchro\n";
          BBToMemAccess[bb].clear();
        }
      }
    } else if (StoreInst *SI = dyn_cast<StoreInst>(inst)) {
      mem = SI->getPointerOperand();
      isLoadOrStore = true;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(inst)) {
      mem = LI->getPointerOperand();
      isLoadOrStore = true;
    }

    if (mem) {
      auto PrevAccess = BBToMemAccess[bb].find(mem);

      // (2) mem is stored in ValueMap
      if (BBToMemAccess[bb].count(mem) != 0) {
        if (getInstructionType(PrevAccess->second) == WRITE ||
            getInstructionType(inst) == WRITE) {
          // Check if we already report this error
          // if(ConcurrentAccesses[F].count(inst) == 0 &&
          // ConcurrentAccesses[F].count(PrevAccess->second) == 0){
          if (!hasBeenReported(F, inst, PrevAccess->second)) {
            RedErr() << "LocalConcurrency detected: conflit with the following "
                        "instructions: \n";
            inst->print(errs());
            if (dbg)
              GreenErr() << " - LINE " << line << " in " << file;
            errs() << "\n";
            RedErr() << " AND ";
            PrevAccess->second->print(errs());
            DebugLoc Prevdbg =
                PrevAccess->second
                    ->getDebugLoc(); // get debug infos for previous access
            if (Prevdbg)
              GreenErr() << " - LINE " << Prevdbg.getLine() << " in "
                         << Prevdbg->getFilename();
            errs() << "\n";
            GreenErr() << "=> BEWARE: debug information (line+filename) may be "
                          "wrong on Fortran codes\n";
            ConcurrentAccesses[F].push_back({inst, PrevAccess->second});
          }
        } else {
          /*MagentaErr() << "INFO: Memory address already in map - last access
            from instruction: "; PrevAccess->second->print(errs()); errs() <<
            "\n";*/
          if (!isLoadOrStore)
            PrevAccess->second = inst;
        }
        // (2) mem is not stored in ValueMap - check if a memory stored in
        // ValueMap alias with mem
      } else {
        // errs() << "(2) No memory access found in ValueMap: " <<
        // PrevAccess->second << "\n";
        /*GreenErr() << "DEBUG INFO: no PrevAccess found for instruction: ";
          inst->print(errs());
          errs()  << "\n";*/
        ValueMap<Value *, Instruction *>::iterator it;
        // iterate over the ValueMap to get the first write that alias with mem
        for (it = BBToMemAccess[bb].begin(); it != BBToMemAccess[bb].end();
             it++) {
          if (AA.alias(it->first, mem) != AliasResult::NoAlias) {
            // if(AA.alias(it->first,mem) == AliasResult::MayAlias)
            // errs() << "(2) No memory access found in ValueMap: " << it->first
            //<< " but found a may alias!\n";
            // errs() << "(2) No memory access found in ValueMap: " << it->first
            //<< " but found an alias!\n";
            if (getInstructionType(it->second) == WRITE ||
                getInstructionType(inst) == WRITE) {
              // Check if we already reported this error
              if (!hasBeenReported(F, inst, it->second)) {
                errs() << "first: ";
                it->first->print(errs());
                errs() << "\nsecond: ";
                it->second->print(errs());
                errs() << "\nmem: ";
                mem->print(errs());
                errs() << "\n";
                RedErr() << "LocalConcurrency detected: conflit with the "
                            "following instructions: \n";
                inst->print(errs());
                if (dbg)
                  GreenErr() << " - LINE " << line << " in " << file;
                errs() << "\n";
                RedErr() << " AND ";
                it->second->print(errs());
                DebugLoc Prevdbg =
                    it->second
                        ->getDebugLoc(); // get debug infos for previous access
                if (Prevdbg)
                  GreenErr() << " -  LINE " << Prevdbg.getLine() << " in "
                             << Prevdbg->getFilename();
                errs() << "\n";
                GreenErr() << "=> BEWARE: debug information (line+filename) "
                              "may be wrong on Fortran codes\n";
                ConcurrentAccesses[F].push_back({inst, it->second});
              }
            }
          }
        }
        // store mem if the instruction is a MPI-RMA
        if (!isLoadOrStore) {
          BBToMemAccess[bb].insert({mem, inst});
          /*MagentaErr() << "DEBUG INFO: Add new memory access from instruction:
            "; inst->print(errs()); errs() << "\n";*/
        }
      }
    }
  }
}

void LocalConcurrencyDetection::printBB(BasicBlock *bb) {
  errs() << "DEBUG: BB ";
  bb->printAsOperand(errs(), false);
  errs() << "\n";
}

// If all predecessors have not been set to black, return true otherwise return
// false
bool LocalConcurrencyDetection::mustWait(BasicBlock *BB) {
  if (LheaderMap[BB]) {
    // errs() << "is lopp header\n";
    return false; // ignore loop headers
  }
  pred_iterator PI = pred_begin(BB), E = pred_end(BB);
  for (; PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    if (ColorMap[Pred] != BLACK)
      return true;
  }
  return false;
}

bool LocalConcurrencyDetection::mustWaitLoop(llvm::BasicBlock *BB, Loop *L) {
  pred_iterator PI = pred_begin(BB), E = pred_end(BB);
  for (; PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    // BB is in the only bb in loop
    if ((Pred == BB) || (LlatchMap[Pred])) {
      continue;
    }
    // TODO: plus complique que ca
    if (ColorMap[Pred] != BLACK && L->contains(Pred)) {
      // printBB(Pred);
      // errs() << " is white \n";
      return true;
    }
  }
  return false;
}

void LocalConcurrencyDetection::BFS_Loop(Loop *L, AAResults &AA) {
  std::deque<BasicBlock *> Unvisited;
  BasicBlock *Lheader = L->getHeader();
  BasicBlock *Llatch = L->getLoopLatch();

  for (Loop *ChildLoop : *L) {
    BFS_Loop(ChildLoop, AA);
  }
  /*errs() << ".. BFS on loop containing ..\n";

  for (BasicBlock *BB : L->blocks()) {
          printBB(BB);
  }
  errs() << "....\n";
  */
  Unvisited.push_back(Lheader);
  LheaderMap[L->getHeader()] = true;

  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();
    // printBB(header);
    Unvisited.pop_front();

    if (ColorMap[header] == BLACK)
      continue;
    if (mustWaitLoop(header, L) &&
        header != L->getHeader()) { // all predecessors have not been seen
      // errs() << "must wait..\n";
      Unvisited.push_back(header);
      continue;
    }

    analyzeBB(header, AA);
    ColorMap[header] = GREY;

    succ_iterator SI = succ_begin(header), E = succ_end(header);
    for (; SI != E; ++SI) {
      BasicBlock *Succ = *SI;
      // Ignore successor not in loop
      if (!(L->contains(Succ)))
        continue;
      // ignore back edge when the loop has already been checked
      if (LlatchMap[header] && LheaderMap[Succ]) {
        continue;
      }

      // Succ not seen before
      if (ColorMap[Succ] == WHITE) {
        BBToMemAccess[Succ].insert(BBToMemAccess[header].begin(),
                                   BBToMemAccess[header].end());
        ColorMap[Succ] = GREY;
        Unvisited.push_back(Succ);
        // Succ already seen
      } else {
        // merge the memory accesses from the previous paths - only local errors
        // detection
        // TODO: For latter: report concurrent one-sided
        BBToMemAccess[Succ].insert(BBToMemAccess[header].begin(),
                                   BBToMemAccess[header].end());
      }
    }
    ColorMap[header] = BLACK;
  }
  // reset BB colors in loop and ignore backedge for the rest of the BFS
  for (BasicBlock *BB : L->blocks()) {
    ColorMap[BB] = WHITE;
  }
  LlatchMap[Llatch] = true;
}

// Local concurrency errors detection (BFS)
void LocalConcurrencyDetection::FindLocalConcurrency(Function *F, AAResults &AA,
                                                     LoopInfo &LI) {

  // All BB must be white at the beginning
  for (BasicBlock &BB : *F) {
    ColorMap[&BB] = WHITE;
    LheaderMap[&BB] = false;
    LlatchMap[&BB] = false;
  }

  std::deque<BasicBlock *> Unvisited;
  BasicBlock &entry = F->getEntryBlock();
  Unvisited.push_back(&entry);

  // errs() << ".. BFS on loops ..\n";
  for (Loop *L : LI) {
    BFS_Loop(L, AA);
  }

  // errs() << ".. BFS ..\n";
  //  BFS
  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();
    // printBB(header);
    // errs() << "has color = " << ColorMap[header] << "\n";
    Unvisited.pop_front();

    if (ColorMap[header] == BLACK)
      continue;

    if (mustWait(header)) { // all predecessors have not been seen
      // errs() << " must wait \n";
      Unvisited.push_back(header);
      continue;
    }

    analyzeBB(header, AA);
    ColorMap[header] = GREY;

    succ_iterator SI = succ_begin(header), E = succ_end(header);
    for (; SI != E; ++SI) {
      BasicBlock *Succ = *SI;
      // ignore back edge
      if (LlatchMap[header] && LheaderMap[Succ])
        continue;
      // Succ not seen before
      if (ColorMap[Succ] == WHITE) {
        BBToMemAccess[Succ].insert(BBToMemAccess[header].begin(),
                                   BBToMemAccess[header].end());
        // analyzeBB(Succ, AA);
        ColorMap[Succ] = GREY;
        Unvisited.push_back(Succ);
        // Succ already seen
      } else {
        // merge the memory accesses from the previous paths - only local errors
        // detection
        // TODO: For latter: report concurrent one-sided
        BBToMemAccess[Succ].insert(BBToMemAccess[header].begin(),
                                   BBToMemAccess[header].end());
      }
    }
    ColorMap[header] = BLACK;
  }
  // Lheaders.clear();
}

void LocalConcurrencyDetection::resetCounters() {
  count_GET = 0;
  count_PUT = 0;
  count_ACC = 0;
  count_Win = 0;
  count_Free = 0;
  count_FENCE = 0;
  count_FLUSH = 0;
  count_LOCK = 0;
  count_LOCKALL = 0;
  count_UNLOCK = 0;
  count_UNLOCKALL = 0;
  count_BARRIER = 0;
  count_MPI = 0;
  count_LOAD = 0;
  count_STORE = 0;
  count_inst_LOAD = 0;
  count_inst_STORE = 0;
}

// Main function of the pass
bool LocalConcurrencyDetection::runOnModule(Module &M) {
  DataLayout *datalayout = new DataLayout(&M);
  auto CyanErr = []() { return WithColor(errs(), raw_ostream::Colors::CYAN); };

  MagentaErr() << "===========================\n";
  MagentaErr() << "===  PARCOACH ANALYSIS  ===\n";
  MagentaErr() << "===========================\n";

  auto &FAM = AM_.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  for (Function &F : M) {
    LLVMContext &Ctx = F.getContext();
    if (F.isDeclaration())
      continue;

    resetCounters();

    // Get Alias Analysis infos
    DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
    // PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);
    AAResults &AA = FAM.getResult<AAManager>(F);
    // AAResults &AA = getAnalysis<AAResultsWrapperPass>(F).getAAResults();
    LoopInfo LI(DT);

    CyanErr() << "===========================\n";
    CyanErr() << "ANALYZING function " << F.getName() << "...\n";

    // Get statistics
    CyanErr() << "(1) Get statistics ...";
    GetRMAstatistics(&F);
    int count_RMA = count_Win + count_PUT + count_GET + count_FENCE +
                    count_ACC + count_LOCK + count_LOCKALL + count_UNLOCK +
                    count_UNLOCKALL + count_Free + count_FLUSH;
    int count_oneSided = count_PUT + count_GET + count_ACC;
    CyanErr() << "done \n";

    // Detection of local concurrency errors - BFS
    CyanErr() << "(2) Local concurrency errors detection ...";
    if (count_oneSided != 0)
      FindLocalConcurrency(&F, AA, LI);
    CyanErr() << "done \n";

    // Instrumentation of memory accesses for dynamic analysis
    CyanErr() << "(3) Instrumentation for dynamic analysis ...";
    InstrumentMemAccessesIt(F, Ctx, datalayout);
    CyanErr() << "done \n";

    // Print statistics per function
    CyanErr() << "=== STATISTICS === \n";
    CyanErr() << count_MPI << " MPI functions including " << count_RMA
              << " RMA functions \n";
    CyanErr() << "= WINDOW CREATION/DESTRUCTION: " << count_Free
              << " MPI_Win_free, " << count_Win << " MPI_Win_create \n";
    CyanErr() << "= EPOCH CREATION/DESTRUCTION: " << count_FENCE
              << " MPI_Win_fence, " << count_LOCK << " MPI_Lock, "
              << count_LOCKALL << " MPI_Lockall " << count_UNLOCK
              << " MPI_Unlock, " << count_UNLOCKALL << " MPI_Unlockall \n";
    CyanErr() << "= ONE-SIDED COMMUNICATIONS: " << count_GET << " MPI_Get, "
              << count_PUT << " MPI_Put, " << count_ACC << " MPI_Accumulate \n";
    CyanErr() << "= SYNCHRONIZATION: " << count_FLUSH << " MPI_Win_Flush \n";

    CyanErr() << "LOAD/STORE STATISTICS: " << count_inst_LOAD << " (/"
              << count_LOAD << ") LOAD and " << count_inst_STORE << " (/"
              << count_STORE << ") STORE are instrumented\n";
    // DEBUG INFO: dump the module// F.getParent()->print(errs(),nullptr);
  }
  MagentaErr() << "===========================\n";
  return true;
}

int LocalConcurrencyDetection::count_GET = 0;
int LocalConcurrencyDetection::count_PUT = 0;
int LocalConcurrencyDetection::count_ACC = 0;
int LocalConcurrencyDetection::count_Win = 0;
int LocalConcurrencyDetection::count_Free = 0;
int LocalConcurrencyDetection::count_FENCE = 0;
int LocalConcurrencyDetection::count_FLUSH = 0;
int LocalConcurrencyDetection::count_LOCK = 0;
int LocalConcurrencyDetection::count_LOCKALL = 0;
int LocalConcurrencyDetection::count_UNLOCK = 0;
int LocalConcurrencyDetection::count_UNLOCKALL = 0;
int LocalConcurrencyDetection::count_BARRIER = 0;
int LocalConcurrencyDetection::count_MPI = 0;
int LocalConcurrencyDetection::count_LOAD = 0;
int LocalConcurrencyDetection::count_STORE = 0;
int LocalConcurrencyDetection::count_inst_LOAD = 0;
int LocalConcurrencyDetection::count_inst_STORE = 0;

namespace parcoach {

PreservedAnalyses
LocalConcurrencyDetectionPass::run(Module &M, ModuleAnalysisManager &AM) {
  LocalConcurrencyDetection LCD(AM);
  return LCD.runOnModule(M) ? PreservedAnalyses::none()
                            : PreservedAnalyses::all();
}

} // namespace parcoach
