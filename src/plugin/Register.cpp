#include "parcoach/Passes.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

using namespace llvm;

namespace {

bool addPassToMPM(StringRef Name, ModulePassManager &MPM,
                  ArrayRef<PassBuilder::PipelineElement>) {
  if (Name == "parcoach") {
    parcoach::RegisterPasses(MPM);
    return true;
  }
  return false;
}
} // namespace

PassPluginLibraryInfo getParcoachPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "PARCOACH", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerAnalysisRegistrationCallback(parcoach::RegisterAnalysis);
            PB.registerPipelineParsingCallback(addPassToMPM);
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getParcoachPluginInfo();
}
