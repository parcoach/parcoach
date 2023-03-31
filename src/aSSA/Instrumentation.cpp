#include "Instrumentation.h"

#include "parcoach/CollListFunctionAnalysis.h"
#include "parcoach/Collectives.h"
#include "parcoach/Passes.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"

#define DEBUG_TYPE "instrumentation"

namespace parcoach {

using namespace llvm;

namespace {

std::string getCheckFunctionName(Collective const *C) {
  if (C == nullptr) {
    return "check_collective_return";
  }
  if (isa<MPICollective>(C)) {
    return "check_collective_MPI";
#ifdef PARCOACH_ENABLE_OPENMP
  }
  if (isa<OMPCollective>(C)) {
    return "check_collective_OMP";
#endif
#ifdef PARCOACH_ENABLE_UPC
  } else if (isa<UPCCollective>(C)) {
    return "check_collective_UPC";
#endif
  }
  llvm_unreachable("unhandled collective type");
}

// Check Collective function before a collective
// + Check Collective function before return statements
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line,
// char* OP_warnings, char *FILE_name)
// --> void check_collective_UPC(int OP_color, const char* OP_name,
// int OP_line, char* warnings, char *FILE_name)
void insertCC(Instruction *I, Collective const *C, Function const &F,
              int OpLine, llvm::StringRef WarningMsg, llvm::StringRef File) {
  Module &M = *I->getModule();
  IRBuilder<> Builder(I);
  IntegerType *I32Ty = Builder.getInt32Ty();
  PointerType *I8PtrTy = Builder.getInt8PtrTy();
  // Arguments of the new function
  unsigned constexpr NParams = 5;
  std::array<Type *, NParams> Params{
      I32Ty,   // OP_color
      I8PtrTy, // OP_name
      I32Ty,   // OP_line
      I8PtrTy, // OP_warnings
      I8PtrTy, // FILE_name
  };
  Value *StrPtrName = Builder.CreateGlobalStringPtr(F.getName());
  Value *StrPtrWarnings = Builder.CreateGlobalStringPtr(WarningMsg);
  Value *StrPtrFilename = Builder.CreateGlobalStringPtr(File);
  // Set new function name, type and arguments
  FunctionType *FTy = FunctionType::get(Builder.getVoidTy(), Params, false);
  int OpColor = C != nullptr ? (int)C->K : -1;
  std::array<Value *, NParams> CallArgs = {
      ConstantInt::get(I32Ty, OpColor), StrPtrName,
      ConstantInt::get(I32Ty, OpLine), StrPtrWarnings, StrPtrFilename};
  std::string FunctionName = getCheckFunctionName(C);

  FunctionCallee CCFunction = M.getOrInsertFunction(FunctionName, FTy);

  // Create new function
  CallInst::Create(CCFunction, CallArgs, "", I);
  LLVM_DEBUG(dbgs() << "=> Insertion of " << FunctionName << " (" << OpColor
                    << ", " << F.getName() << ", " << OpLine << ", "
                    << WarningMsg << ", " << File << ")\n");
}
} // namespace

CollectiveInstrumentation::CollectiveInstrumentation(
    WarningCollection const &Warnings)
    : Warnings(Warnings) {}

bool CollectiveInstrumentation::run(Function &F) {
  TimeTraceScope TTS("CollectiveInstrumentation", F.getName());
  bool Changed = false;
  auto IsaDirectCall = [](Instruction const &I) {
    if (CallInst const *CI = dyn_cast<CallInst>(&I)) {
      Function const *F = CI->getCalledFunction();
      return (bool)F;
    }
    return false;
  };
  auto Candidates = make_filter_range(instructions(F), IsaDirectCall);
  for (Instruction &I : Candidates) {
    CallInst &CI = cast<CallInst>(I);
    std::string WarningStr = " ";
    auto It = Warnings.find(&CI);
    if (It != Warnings.end()) {
      WarningStr = It->second.toString();
    }

    // Debug info (line in the source code, file)
    DebugLoc DLoc = CI.getDebugLoc();
    std::string File = "o";
    int OpLine = -1;
    if (DLoc) {
      OpLine = DLoc.getLine();
      File = DLoc->getFilename().str();
    }
    // call instruction
    Function *Callee = CI.getCalledFunction();
    assert(Callee && "Callee expected to be not null");
    auto const *Coll = Collective::find(*Callee);
    StringRef CalleeName = Callee->getName();

    // Before finalize or exit/abort
    if (CalleeName == "MPI_Finalize" || CalleeName == "MPI_Abort" ||
        CalleeName == "abort") {
      LLVM_DEBUG(dbgs() << "-> insert check before " << CalleeName << " line "
                        << OpLine << "\n");
      insertCC(&CI, nullptr, *Callee, OpLine, WarningStr, File);
      Changed = true;
      continue;
    }
    if (Coll != nullptr) {
      // Before a collective
      LLVM_DEBUG(dbgs() << "-> insert check before " << CalleeName << " line "
                        << OpLine << "\n");
      insertCC(&CI, Coll, *Callee, OpLine, WarningStr, File);
      Changed = true;
    }
  } // END FOR
  return Changed;
}

PreservedAnalyses
ParcoachInstrumentationPass::run(llvm::Module &M,
                                 llvm::ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("CollectiveInstrumentation");
  auto const &Warnings = AM.getResult<CollectiveAnalysis>(M);
  if (Warnings->empty()) {
    LLVM_DEBUG(dbgs() << "Skipping instrumentation: no warnings detected.");
    return PreservedAnalyses::all();
  }
  parcoach::CollectiveInstrumentation Instrum(*Warnings);
  LLVM_DEBUG(
      dbgs()
      << "\033[0;35m=> Static instrumentation of the code ...\033[0;0m\n");
  for (Function &F : M) {
    Instrum.run(F);
  }
  return PreservedAnalyses::all();
}

} // namespace parcoach
