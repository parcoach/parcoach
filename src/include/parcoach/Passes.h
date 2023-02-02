#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {
void RegisterModuleAnalyses(llvm::ModuleAnalysisManager &MAM);
void RegisterFunctionAnalyses(llvm::FunctionAnalysisManager &FAM);
void RegisterPasses(llvm::ModulePassManager &MPM);
void PrintVersion(llvm::raw_ostream &Out);
} // namespace parcoach
