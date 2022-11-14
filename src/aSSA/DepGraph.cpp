#include "DepGraph.h"
#include "Utils.h"

using namespace llvm;
using namespace std;

void DepGraph::build() {
  unsigned Counter = 0;
  unsigned NbFunctions = PTACG.getModule().getFunctionList().size();
  static constexpr unsigned Steps = 100;

  for (Function const &F : PTACG.getModule()) {
    if (!PTACG.isReachableFromEntry(F)) {
      continue;
    }

    if (Counter % Steps == 0) {
      errs() << "DepGraph: visited " << Counter << " functions over "
             << NbFunctions << " (" << (((float)Counter) / NbFunctions * Steps)
             << "%)\n";
    }

    Counter++;

    if (isIntrinsicDbgFunction(&F)) {
      continue;
    }

    buildFunction(&F);
  }
}
