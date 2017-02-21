#include "FunctionSummary.h"
#include "GlobalDepGraph.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

using namespace std;
using namespace llvm;

GlobalDepGraph::GlobalDepGraph(InterDepGraph *interDeps,
			       std::vector<FunctionSummary *> *summaries)
  : interDeps(interDeps), summaries(summaries) {
}

GlobalDepGraph::~GlobalDepGraph() {}

void
GlobalDepGraph::toDot(llvm::StringRef filename) {
  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph G {\n";

  // Function nodes
  stream << "{ rank=same; ";
  for (unsigned i=0; i<summaries->size(); ++i) {
    stream << "Node" << ((void *) (*summaries)[i]->F);
    stream << " [label=< <B>" << (*summaries)[i]->F->getName() << " </B>>] ";
  }
  stream << " }\n";

  // Function edges
  for (unsigned i=0; i<summaries->size(); ++i) {
    IntraDepGraph *intraDeps = &(*summaries)[i]->intraDeps;
    for (auto I = intraDeps->roots.begin(), E = intraDeps->roots.end(); I != E;
	 ++I) {
      stream << "Node" << ((void *) (*summaries)[i]->F) << " -> "
	     << "Node" << ((void *) (*I).Ptr) << "\n";
    }
  }

  for (unsigned i=0; i<summaries->size(); ++i)
    (*summaries)[i]->intraDeps.toDot(stream);
  interDeps->toDot(stream);

  stream << "}\n";
}
