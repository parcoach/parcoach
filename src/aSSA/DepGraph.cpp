#include "DepGraph.h"
#include "Utils.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
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
DepGraph::addIPDFFuncNode(const llvm::Function *F, const llvm::Value *v) {
  if (graph.count(v) != 0)
    return;

  graph[v] = new set<const Value *>();

  IPDFFuncNodes[F] = v;
}

const llvm::Value *
DepGraph::getIPDFFuncNode(const llvm::Function *F) {
  if (IPDFFuncNodes.count(F) == 0)
    return NULL;

  return IPDFFuncNodes[F];
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
DepGraph::addSink(const llvm::Value *src) {
  sinks.insert(src);
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
DepGraph::computeTaintedValues(Pass *pass) {
  for (auto I = sources.begin(), E = sources.end(); I != E; ++I) {
    taintedValues.insert(*I);
    taintRec(*I);
  }

  // Compute tainted sinks
  for (auto I = sinks.begin(), E = sinks.end(); I != E; ++I) {
    Value *v = (Value *) *I;
    Instruction *inst = cast<Instruction>(v);
    Function *F = (Function *) inst->getParent()->getParent();
    PostDominatorTree &PDT = pass->getAnalysis<PostDominatorTree>(*F);

    // Get PDF+
    vector<BasicBlock *> IPDF =
      iterated_postdominance_frontier(PDT, inst->getParent());

    for (unsigned n = 0; n < IPDF.size(); ++n) {
      const TerminatorInst *ti = IPDF[n]->getTerminator();
      assert(ti);

      if (isa<BranchInst>(ti)) {
	const BranchInst *bi = cast<BranchInst>(ti);

	if (bi->isUnconditional())
	  continue;

	const Value *cond = bi->getCondition();
	if (taintedValues.count(cond) != 0) {
	  taintedSinks.insert(v);
	  break;
	}
      } else if (isa<SwitchInst>(ti)) {
	const SwitchInst *si = cast<SwitchInst>(ti);
	const Value *cond = si->getCondition();
	if (taintedValues.count(cond) != 0) {
	  taintedSinks.insert(v);
	  break;
	}
      } else {
	assert(false);
      }
    }
  }
}

std::string
DepGraph::getNodeStyle(const llvm::Value *v) {
  if (sinks.count(v) != 0) {
    if (taintedSinks.count(v) != 0)
      return "style=filled, color=blue";
    return "style=filled, color=green";
  }

  if (taintedValues.count(v) != 0)
    return "style=filled, color=red";

  return "";
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
    stream << "label=< <B>" << func->getName() << "</B> >;\n";
    stream << "node [style=filled,color=white];\n";

    for (auto J = graph.begin(), F = graph.end(); J != F; ++J) {
      const Value *node = (*J).first;

      if (isa<Function>(node))
	continue;

      string label = getValueLabel(node);

      const Instruction *inst = dyn_cast<Instruction>(node);
      if (inst) {
	if (inst->getParent()->getParent() == func) {
	  stream << "Node" << ((void *) node) << " [label=\""
		 << label << "\" ";
	  stream << getNodeStyle(inst);
	  stream << "];\n";
	}

	continue;
      }

      const Argument *argument = dyn_cast<Argument>(node);
      if (argument) {
	if (argument->getParent() == func) {
	  stream << "Node" << ((void *) node) << " [label=\""
		 << node->getName() << "\" ";
	  stream << getNodeStyle(argument);
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
      stream << getNodeStyle(from);
      stream << "];\n";
    }
    if (isa<Constant>(from)) {
      if (isa<Function>(from))
	continue;

      stream << "Node" << ((void *) from) << " [label=\"" << *from << "\" ";
      stream << getNodeStyle(from);
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
