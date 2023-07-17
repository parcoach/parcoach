#include "parcoach/Warning.h"

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace parcoach {

Location::Location(DebugLoc DL)
    : Filename(DL ? DL->getFilename() : "?"), Line(DL ? DL->getLine() : 0) {}

Warning::Warning(Function const &F, DebugLoc DL, ConditionalsContainerTy &&C)
    : MissedFunction(F), Where(DL), Conditionals(C) {
  // Make sure lines are displayed in order.
  llvm::sort(Conditionals);
}

std::string Warning::toString() const {

  std::string Res;
  raw_string_ostream OS(Res);
  OS << MissedFunction.getName() << " line " << Where.Line;
  OS << " possibly not called by all processes because of conditional(s) "
        "line(s) ";

  for (auto const &Loc : Conditionals) {
    OS << " " << Loc.Line;
    OS << " (" << Loc.Filename << ")";
  }
  OS << " (Call Ordering Error)";
  return Res;
}

} // namespace parcoach
