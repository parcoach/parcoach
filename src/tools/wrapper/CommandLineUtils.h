#pragma once

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <optional>
#include <string>

namespace parcoach {

using ArgList = llvm::SmallVector<llvm::StringRef>;
using FoundProgramResult = std::pair<ArgList::const_iterator, std::string>;

int Execute(ArgList const &Args);
bool IsPreventLinkFlag(llvm::StringRef Arg);

ArgList BuildOriginalCommandLine(ArgList const &Argv,
                                 FoundProgramResult &FoundProgram);

ArgList BuildEmitIRCommandLine(ArgList const &OriginalCommandLine,
                               llvm::StringRef IRFileName);

ArgList BuildParcoachArgs(ArgList const &Argv, llvm::StringRef ParcoachBin,
                          llvm::StringRef IRFile);

std::optional<FoundProgramResult> FindProgram(ArgList const &Argv);

} // namespace parcoach
