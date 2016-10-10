#ifndef GLOBALDEPGRAPH_H
#define GLOBALDEPGRAPH_H

#include "InterDepGraph.h"
#include "IntraDepGraph.h"

class FunctionSummary;

class GlobalDepGraph {
 public:
  GlobalDepGraph(InterDepGraph *interDeps,
		 std::vector<FunctionSummary *> *summaries);
  ~GlobalDepGraph();

  void toDot(llvm::StringRef filename);

private:
  InterDepGraph *interDeps;
  std::vector<FunctionSummary *> *summaries;
};

#endif /* GLOBALDEPGRAPH_H */
