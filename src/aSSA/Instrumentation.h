#pragma once

#include "ParcoachAnalysisInter.h"

namespace llvm {
class Function;
}

namespace parcoach {
struct CollectiveInstrumentation {
  CollectiveInstrumentation(IAResult const &);
  bool run(llvm::Function &F);

private:
  IAResult const &AR;
};
} // namespace parcoach
