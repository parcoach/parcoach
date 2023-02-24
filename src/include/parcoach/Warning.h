#pragma once

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugLoc.h"

#include <string>

namespace llvm {
class Function;
}

namespace parcoach {
struct Warning {
  using ConditionalsContainerTy = llvm::SmallVector<llvm::DebugLoc>;
  Warning(llvm::Function const *, llvm::DebugLoc, ConditionalsContainerTy &&);
  Warning() = default;
  ~Warning() = default;
  llvm::Function const *Missed;
  llvm::DebugLoc Where;
  ConditionalsContainerTy Conditionals;
  std::string toString() const;
};
} // namespace parcoach
