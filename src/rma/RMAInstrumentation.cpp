#include "parcoach/RMAPasses.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace parcoach::rma {

namespace {

constexpr StringRef PARCOACH_PREFIX = "parcoach_rma_";

std::pair<bool, bool> getInstrumentationInfo(CallBase const &CB) {
  if (!CB.getCalledFunction()) {
    return {};
  }
  return StringSwitch<std::pair<bool, bool>>(CB.getCalledFunction()->getName())
#define RMA_INSTRUMENTED(Name, FName, ChangesEpoch)                            \
  .Case(#Name, {true, ChangesEpoch}).Case(#FName, {true, ChangesEpoch})
#include "InstrumentedFunctions.def"
      .Default({});
}

Twine getInstrumentedName(Function const &F) {
  return PARCOACH_PREFIX + F.getName();
}

FunctionType *getInstrumentedFunctionType(Function const &F) {
  FunctionType *Original = F.getFunctionType();
  LLVMContext &Ctx = F.getContext();
  // FIXME: in LLVM 16 we can construct a SmallVector from an ArrayRef.
  SmallVector<Type *> InstrumentedParamsTypes(Original->params().begin(),
                                              Original->params().end());
  InstrumentedParamsTypes.emplace_back(Type::getInt32Ty(Ctx));
  InstrumentedParamsTypes.emplace_back(Type::getInt8PtrTy(Ctx));
  return FunctionType::get(F.getReturnType(), InstrumentedParamsTypes, false);
}

FunctionCallee getInstrumentedFunction(Function &F) {
  return F.getParent()->getOrInsertFunction(getInstrumentedName(F).str(),
                                            getInstrumentedFunctionType(F));
}

CallInst *createInstrumentedCall(CallBase &CB) {
  IRBuilder<> B(&CB);
  SmallVector<Value *> Args(CB.args());

  DebugLoc Dbg = CB.getDebugLoc();

  Args.push_back(B.getInt32(Dbg ? Dbg.getLine() : 0));
  Constant *Zero = ConstantInt::get(B.getInt8PtrTy(), 0);
  // FIXME: this creates one constant per call, even if the filename is the
  // same.
  // We should do a getOrInsertGlobal (where the name is the filename) to
  // reuse existing constant.
  Args.push_back(Dbg ? B.CreateGlobalStringPtr(Dbg->getFilename()) : Zero);

  // We assume shouldInstrumentFunction is true and that getCalledFunction is
  // not null.
  FunctionCallee InstrumentedF =
      getInstrumentedFunction(*CB.getCalledFunction());
  return B.CreateCall(InstrumentedF, Args);
}

FunctionCallee getInstrumentationMemAccessFunction(Module &M,
                                                   StringRef InstName) {
  LLVMContext &Ctx = M.getContext();
  std::array<Type *, 4> Args = {
      Type::getInt8PtrTy(Ctx),
      Type::getInt64Ty(Ctx),
      Type::getInt32Ty(Ctx),
      Type::getInt8PtrTy(Ctx),
  };
  auto *CalledFTy = FunctionType::get(Type::getVoidTy(Ctx), Args, false);
  return M.getOrInsertFunction((PARCOACH_PREFIX + InstName).str(), CalledFTy);
}

void insertInstrumentationCall(Instruction &I, Value *Addr, Type *Ty,
                               StringRef InstName) {
  FunctionCallee CalledF =
      getInstrumentationMemAccessFunction(*I.getModule(), InstName);
  IRBuilder<> B(&I);

  Constant *Size =
      B.getInt64(I.getModule()->getDataLayout().getTypeSizeInBits(Ty));
  DebugLoc Dbg = I.getDebugLoc();
  Constant *Line = B.getInt32(Dbg ? Dbg.getLine() : 0);
  StringRef Filename = Dbg ? Dbg->getFilename() : "?";
  Constant *ConstantFilename = B.CreateGlobalStringPtr(Filename);

  B.CreateCall(CalledF, {Addr, Size, Line, ConstantFilename});
}

void insertInstrumentationCall(StoreInst &SI) {
  insertInstrumentationCall(SI, SI.getPointerOperand(),
                            SI.getValueOperand()->getType(), "store");
}

void insertInstrumentationCall(LoadInst &LI) {
  insertInstrumentationCall(LI, LI.getPointerOperand(), LI.getType(), "load");
}

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
  // Debug
#ifndef NDEBUG
  void printBB(BasicBlock *BB);
#endif
  // Utils
  void resetCounters();
  enum Color { WHITE, GREY, BLACK };
  using BBColorMap = DenseMap<BasicBlock const *, Color>;
  using MemMap = ValueMap<Value *, Instruction *>;
  BBColorMap ColorMap;
};

bool LocalConcurrencyDetection::InstrumentBB(BasicBlock &bb, bool inEpoch) {
  bool newEpoch = false;

  // We use make_early_inc_range here because we may have to erase the
  // current instruction.
  for (Instruction &I : make_early_inc_range(bb)) {
    if (CallInst *CI = dyn_cast<CallInst>(&I)) {
      auto [shouldInstrument, changesEpoch] = getInstrumentationInfo(*CI);
      if (shouldInstrument) {
        CI->replaceAllUsesWith(createInstrumentedCall(*CI));
        CI->eraseFromParent();
        newEpoch = changesEpoch;
      }
    }
    if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
      if (inEpoch || newEpoch) {
        insertInstrumentationCall(*SI);
        count_inst_STORE++;
      }
      count_STORE++;
    }
    if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
      if (inEpoch || newEpoch) {
        insertInstrumentationCall(*LI);
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
