#pragma once

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugLoc.h"

#include <string>
#include <vector>

namespace llvm {
class Function;
class CallBase;
} // namespace llvm

namespace parcoach {

struct Location {
  Location(llvm::DebugLoc DL);
  llvm::StringRef Filename{};
  int Line{};
  inline bool operator<(Location const &Other) const {
    return std::tie(Filename, Line) < std::tie(Other.Filename, Other.Line);
  }
};

struct Warning {
  using ConditionalsContainerTy = std::vector<Location>;
  Warning(llvm::Function const &, llvm::DebugLoc, ConditionalsContainerTy &&);
  ~Warning() = default;
  llvm::Function const &MissedFunction;
  Location Where;
  ConditionalsContainerTy Conditionals{};
  std::string toString() const;
};

using WarningCollection = llvm::DenseMap<llvm::CallBase *, Warning>;

} // namespace parcoach
