#include "parcoach/Warning.h"

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace parcoach {

Warning::Warning(Function const *F, DebugLoc DL, ConditionalsContainerTy &&C)
    : Missed(F), Where(std::move(DL)), Conditionals(C) {
  // Make sure lines are displayed in order.
  llvm::sort(Conditionals, [](DebugLoc const &A, DebugLoc const &B) {
    return (A ? A.getLine() : 0) < (B ? B.getLine() : 0);
  });
}

std::string Warning::toString() const {
  assert(Missed != nullptr && "toString called on an invalid Warning");

  std::string Res;
  raw_string_ostream OS(Res);
  auto Line = Where ? Where.getLine() : 0;
  OS << Missed->getName() << " line " << Line;
  OS << " possibly not called by all processes because of conditional(s) "
        "line(s) ";

  for (auto &Loc : Conditionals) {
    OS << " " << (Loc ? std::to_string(Loc.getLine()) : "?");
    OS << " (" << (Loc ? Loc->getFilename() : "?") << ")";
  }
  OS << " (full-inter)";
  return Res;
}

} // namespace parcoach
