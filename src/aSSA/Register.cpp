#include "Parcoach.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

using namespace llvm;

namespace {

bool addPassToMPM(StringRef Name, ModulePassManager &MPM,
                  ArrayRef<PassBuilder::PipelineElement>) {
  if (Name == "parcoach") {
    MPM.addPass(
        createModuleToFunctionPassAdaptor(UnifyFunctionExitNodesPass()));
    MPM.addPass(parcoach::ParcoachPass());
    return true;
  }
  return false;
}
} // namespace

PassPluginLibraryInfo getParcoachPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "PARCOACH", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(addPassToMPM);
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getParcoachPluginInfo();
}
