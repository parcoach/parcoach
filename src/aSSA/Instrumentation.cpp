#include "Instrumentation.h"

#include "Collectives.h"
#include "Options.h"
#include "parcoach/Analysis.h"
#include "parcoach/Passes.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"

#define DEBUG_TYPE "instrumentation"

namespace parcoach {

using namespace llvm;

namespace {

// Check Collective function before a collective
// + Check Collective function before return statements
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line,
// char* OP_warnings, char *FILE_name)
// --> void check_collective_UPC(int OP_color, const char* OP_name,
// int OP_line, char* warnings, char *FILE_name)
void insertCC(llvm::Instruction *I, int OP_color, llvm::StringRef OP_name,
              int OP_line, llvm::StringRef WarningMsg, llvm::StringRef File) {
  Module &M = *I->getModule();
  IRBuilder<> builder(I);
  IntegerType *I32Ty = builder.getInt32Ty();
  PointerType *I8PtrTy = builder.getInt8PtrTy();
  // Arguments of the new function
  std::array<Type *, 5> params{
      I32Ty,   // OP_color
      I8PtrTy, // OP_name
      I32Ty,   // OP_line
      I8PtrTy, // OP_warnings
      I8PtrTy, // FILE_name
  };
  Value *strPtr_NAME = builder.CreateGlobalStringPtr(OP_name);
  Value *strPtr_WARNINGS = builder.CreateGlobalStringPtr(WarningMsg);
  Value *strPtr_FILENAME = builder.CreateGlobalStringPtr(File);
  // Set new function name, type and arguments
  FunctionType *FTy = FunctionType::get(builder.getVoidTy(), params, false);
  std::array<Value *, 5> CallArgs = {
      ConstantInt::get(I32Ty, OP_color), strPtr_NAME,
      ConstantInt::get(I32Ty, OP_line), strPtr_WARNINGS, strPtr_FILENAME};
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

  FunctionCallee CCFunction = M.getOrInsertFunction(FunctionName, FTy);

  // Create new function
  CallInst::Create(CCFunction, CallArgs, "", I);
  LLVM_DEBUG(dbgs() << "=> Insertion of " << FunctionName << " (" << OP_color
                    << ", " << OP_name << ", " << OP_line << ", " << WarningMsg
                    << ", " << File << ")\n");
}
} // namespace

CollectiveInstrumentation::CollectiveInstrumentation(
    CallToWarningMapTy const &Warnings)
    : Warnings(Warnings) {}

bool CollectiveInstrumentation::run(Function &F) {
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
    int OP_line = -1;
    if (DLoc) {
      OP_line = DLoc.getLine();
      File = DLoc->getFilename().str();
    }
    // call instruction
    Function *callee = CI.getCalledFunction();
    assert(callee && "Callee expected to be not null");
    std::string OP_name = callee->getName().str();
    int OP_color = getCollectiveColor(*callee);

    // Before finalize or exit/abort
    if (callee->getName().equals("MPI_Finalize") ||
        callee->getName().equals("MPI_Abort") ||
        callee->getName().equals("abort")) {
      LLVM_DEBUG(dbgs() << "-> insert check before " << OP_name << " line "
                        << OP_line << "\n");
      insertCC(&CI, v_coll.size() + 1, OP_name, OP_line, WarningStr, File);
      Changed = true;
      continue;
    }
    // Before a collective
    if (OP_color >= 0) {
      LLVM_DEBUG(dbgs() << "-> insert check before " << OP_name << " line "
                        << OP_line << "\n");
      insertCC(&CI, OP_color, OP_name, OP_line, WarningStr, File);
      Changed = true;
      // nbCC++;
    }
  } // END FOR
  return Changed;
}

PreservedAnalyses
ParcoachInstrumentationPass::run(llvm::Module &M,
                                 llvm::ModuleAnalysisManager &AM) {
  auto const &PAInter = AM.getResult<InterproceduralAnalysis>(M);
  if (PAInter->getWarnings().empty() || !optInstrumInter) {
    LLVM_DEBUG(dbgs() << "Skipping instrumentation: no warnings detected"
                      << " or instrumentation disabled.");
    return PreservedAnalyses::all();
  }
  parcoach::CollectiveInstrumentation Instrum(PAInter->getWarnings());
  LLVM_DEBUG(
      dbgs()
      << "\033[0;35m=> Static instrumentation of the code ...\033[0;0m\n");
  for (Function &F : M) {
    Instrum.run(F);
  }
  return PreservedAnalyses::all();
}

} // namespace parcoach
