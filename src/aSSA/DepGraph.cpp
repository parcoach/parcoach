#include "DepGraph.h"
#include "Utils.h"

using namespace llvm;
using namespace std;

void
DepGraph::build() {
  unsigned counter = 0;
  unsigned nbFunctions = PTACG->getModule().getFunctionList().size();

  for (Function &F : PTACG->getModule()) {
    if (!PTACG->isReachableFromEntry(&F))
      continue;

    if (counter % 100 == 0)
      errs() << "DepGraph: visited " << counter << " functions over " << nbFunctions
	     << " (" << (((float) counter)/nbFunctions*100) << "%)\n";

    counter++;

    if (isIntrinsicDbgFunction(&F))
      continue;

    buildFunction(&F);
  }
}
