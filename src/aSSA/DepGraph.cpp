#include "DepGraph.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace std;
using namespace llvm;

DepGraph::DepGraph() {}

DepGraph::~DepGraph() {}

void
DepGraph::addFunction(const llvm::Function *F) {
  functions.insert(F);
}

void
DepGraph::addEdge(const Value *from, const Value *to) {
  if (from == to)
    return;

  if (graph.count(from) == 0)
    graph[from] = new set<const Value *>();
  if (graph.count(to) == 0)
    graph[to] = new set<const Value *>();

  graph[from]->insert(to);
}

void
DepGraph::addSource(const llvm::Value *src) {
  sources.insert(src);
}

void
DepGraph::taintRec(const llvm::Value *v) {
  std::set<const Value *> *children = graph[v];

  for (auto I = children->begin(), E = children->end(); I != E; ++I) {
    if (taintedValues.count(*I) > 0)
      continue;

    taintedValues.insert(*I);
    taintRec(*I);
  }
}

void
DepGraph::computeTaintedValues() {
  for (auto I = sources.begin(), E = sources.end(); I != E; ++I) {
    taintedValues.insert(*I);
    taintRec(*I);
  }
}

void
DepGraph::toDot(StringRef filename) {
  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph G {\n";

  for (auto I = functions.begin(), E = functions.end(); I != E; ++I) {
    const Function *func = *I;
    stream << "\tsubgraph cluster_" << func->getName() << " {\n";
    stream << "style=filled;\ncolor=lightgrey;\n";
    stream << "label=\"" << func->getName() << "\";\n";
    stream << "node [style=filled,color=white];\n";

    for (auto J = graph.begin(), F = graph.end(); J != F; ++J) {
      const Value *node = (*J).first;
      if (isa<Function>(node))
	continue;

      const Instruction *inst = dyn_cast<Instruction>(node);
      if (inst) {
	if (inst->getParent()->getParent() == func) {
	  stream << "Node" << ((void *) node) << " [label=\""
		 << *node << "\" ";
	  if (taintedValues.count(inst) != 0)
	    stream << "style=filled, color=red";
	  stream << "];\n";
	}

	continue;
      }

      const Argument *argument = dyn_cast<Argument>(node);
      if (argument) {
	if (argument->getParent() == func) {
	  stream << "Node" << ((void *) node) << " [label=\""
		 << node->getName() << "\" ";
	  if (taintedValues.count(argument) != 0)
	    stream << " style=filled, color=red";
	  stream << "];\n";
	}

	continue;
      }
    }

    stream << "}\n";
  }

  for (auto I = graph.begin(), E = graph.end(); I != E; ++I) {
    const Value *from = (*I).first;
    if (isa<Function>(from)) {
      stream << "Node" << ((void *) from) << " [label=\"" << from->getName()
	     << "\" ";
      if (taintedValues.count(from) != 0)
	stream << " style=filled, color=red";
      stream << "];\n";
    }
    if (isa<Constant>(from)) {
      if (isa<Function>(from))
	continue;

      stream << "Node" << ((void *) from) << " [label=\"" << *from << "\" ";
      if (taintedValues.count(from) != 0)
	stream << " style=filled, color=red";
      stream<< "];\n";
    }
  }

  toDot(stream);

  stream << "}\n";
}

void
DepGraph::toDot(raw_fd_ostream &stream) {
  for (auto I = graph.begin(), E = graph.end(); I != E; ++I) {
    const Value *from = (*I).first;

    set<const Value *> *children = (*I).second;
    for (auto J = children->begin(), F = children->end(); J != F; ++J) {
      const Value * to = *J;
      stream << "Node" << ((void *) from) << " -> "
	     << "Node" << ((void *) to) << "\n";
    }
  }
}
