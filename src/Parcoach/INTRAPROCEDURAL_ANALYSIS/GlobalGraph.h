#ifndef GLOBALGRAPH_H
#define GLOBALGRAPH_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

#include <vector>

/* #include "FunctionSummary.h" */
/* #include "InterDependenceMap.h" */
class FunctionSummary;
class InterDependenceMap;

class GlobalGraph {
public:
  GlobalGraph(std::vector<FunctionSummary *> *summaries,
	      InterDependenceMap &map);
  ~GlobalGraph();

  void toDot(llvm::StringRef filename);

 private:
  std::vector<FunctionSummary *> *summaries;
  InterDependenceMap &map;
};

#endif /* GLOBALGRAPH */
