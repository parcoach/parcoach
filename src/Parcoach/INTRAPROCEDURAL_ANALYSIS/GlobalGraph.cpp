#include "GlobalGraph.h"

#include "FunctionSummary.h"
#include "InterDependenceMap.h"

using namespace llvm;
using namespace std;

GlobalGraph::GlobalGraph(std::vector<FunctionSummary *> *summaries,
			 InterDependenceMap &map)
  : summaries(summaries), map(map) {}

GlobalGraph::~GlobalGraph() {}

void
GlobalGraph::toDot(StringRef filename) {
  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph G {\n";

  // Function nodes
  for (unsigned i=0; i<summaries->size(); ++i) {
    stream << "Node" << ((void *) (*summaries)[i]->F);
    stream << " [label=< <B>" << (*summaries)[i]->F->getName() << " </B>>];\n";
  }

  // Function edges
  for (unsigned i=0; i<summaries->size(); ++i) {
    DependencyGraph *depGraph = &(*summaries)[i]->depGraph;
    for (auto I = depGraph->roots.begin(), E = depGraph->roots.end(); I != E;
	 ++I) {
      stream << "Node" << ((void *) (*summaries)[i]->F) << " -> "
	     << "Node" << ((void *) (*I).Ptr) << "\n";
    }
  }

  // Function graphs
  for (unsigned i=0; i<summaries->size(); ++i) {
    (*summaries)[i]->depGraph.toDot(stream);
  }

  // Inter dependencies
  for (auto I = map.map.begin(), E = map.map.end(); I != E; ++I) {
    DepMap *DM = (*I).second;

    for (auto J = DM->begin(), F = DM->end(); J != F; ++J) {
      MemoryLocation src = (*J).first;
      MemoryLocation dst = (*J).second;
      if (src.Ptr == NULL)
	abort();

      stream << "Node" << ((void *) src.Ptr) << " -> "
	     << "Node" << ((void *) dst.Ptr) << " [label=\"inter\"]\n";
    }
  }

  stream << "}\n";
}

