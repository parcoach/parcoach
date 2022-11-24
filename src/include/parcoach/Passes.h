#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {
void RegisterAnalysis(llvm::ModuleAnalysisManager &MAM);
void RegisterPasses(llvm::ModulePassManager &MPM);
void PrintVersion(llvm::raw_ostream &Out);
} // namespace parcoach
