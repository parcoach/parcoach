#include "DependencyGraph.h"

#include "llvm/Support/FileSystem.h"

#include <sstream>

using namespace llvm;
using namespace std;

DependencyGraph::DependencyGraph() {}

DependencyGraph::~DependencyGraph() {}

bool
DependencyGraph::addRoot(MemoryLocation ML) {
   nodes.insert(ML);

   RootIterator I = roots.find(ML);
  if (I == roots.end()) {
    roots.insert(ML);
    return true;
  }

  return false;
}

bool
DependencyGraph::addEdge(MemoryLocation src,  MemoryLocation dst,
			 enum type depType) {
  if (src == dst)
    return false;

  if (src.Ptr == NULL || dst.Ptr == NULL)
    abort();

  nodes.insert(src);
  nodes.insert(dst);

  DenseMap<MemoryLocation, Node *> *edgeMap = NULL;

  switch(depType) {
  case MustAlias:
    edgeMap = &mustAliasEdges;
    break;
  case MayAlias:
    edgeMap = &mayAliasEdges;
    break;
  case PartialAlias:
    edgeMap = &partialAliasEdges;
    break;
  case ValueDep:
    edgeMap = &valueDepEdges;
    break;
  };

  EdgeIterator I = edgeMap->find(src);
  if (I == edgeMap->end()) {
    Node *node = new Node();
    (*edgeMap)[src] = node;
    node->children.insert(dst);
    return true;
  }

  Node *node = I->second;
  ChildrenIterator J = node->children.find(dst);
  if (J == node->children.end()) {
    node->children.insert(dst);
    return true;
  }

  return false;
}

void
DependencyGraph::debug() {
  // Nodes
  errs() << "Nodes:\n";
  for (NodeIterator I = nodes.begin(), E = nodes.end(); I != E; ++I) {
    const Value *ptr = I->Ptr;
    errs() << *ptr << "\n";
  }

  // MustAlias Edges
  errs() << "MustAlias Edges:\n";
  for (EdgeIterator I = mustAliasEdges.begin(), E = mustAliasEdges.end();
       I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    Node *node = I->second;
    for (ChildrenIterator J = node->children.begin(), F = node->children.end();
  	 J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      errs() << *srcPtr << " =====>>>> " << *dstPtr << "\n";
    }
  }

  // MayAlias Edges
  errs() << "MayAlias Edges:\n";
  for (EdgeIterator I = mayAliasEdges.begin(), E = mayAliasEdges.end();
       I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    Node *node = I->second;
    for (ChildrenIterator J = node->children.begin(), F = node->children.end();
  	 J != F; ++J) {
      const Value *dstPtr = J->Ptr;
      errs() << *srcPtr << " =====>>>> " << *dstPtr << "\n";
    }
  }

  // PartialAlias Edges
  errs() << "PartialAlias Edges:\n";
  for (EdgeIterator I = partialAliasEdges.begin(), E = partialAliasEdges.end();
       I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    Node *node = I->second;
    for (ChildrenIterator J = node->children.begin(), F = node->children.end();
  	 J != F; ++J) {
      const Value *dstPtr = J->Ptr;
      errs() << *srcPtr << " =====>>>> " << *dstPtr << "\n";
    }
  }

  // ValueDep Edges
  errs() << "PartialAlias Edges:\n";
  for (EdgeIterator I = valueDepEdges.begin(), E = valueDepEdges.end();
       I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    Node *node = I->second;
    for (ChildrenIterator J = node->children.begin(), F = node->children.end();
  	 J != F; ++J) {
      const Value *dstPtr = J->Ptr;
      errs() << *srcPtr << " =====>>>> " << *dstPtr << "\n";
    }
  }

  errs() << "============================\n";
}

void
DependencyGraph::toDot(StringRef filename) {
  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph G {\n";

  toDot(stream);

  stream << "}\n";
}

void
DependencyGraph::toDot(raw_fd_ostream &stream) {
  // Nodes
  for (NodeIterator I = nodes.begin(), E = nodes.end(); I != E; ++I) {
    const Value *ptr = I->Ptr;
    if (roots.find(*I) != roots.end())
      stream << "Node" << ((void *) ptr) << " [shape=\"doublecircle\" "
	     << "label=\"" << *ptr << "\"];\n";
    else
      stream << "Node" << ((void *) ptr) << " [label=\"" << *ptr << "\"];\n";
  }

  // Barrier Nodes
  for (unsigned i=0; i<barrierNodes.size(); ++i) {
    stream << "Node" << barrierNodes[i] << " [shape=\"rectangle\" label=\""
	   << *barrierNodes[i] << "\"];\n";
  }

  // MustAlias Edges
  for (EdgeIterator I = mustAliasEdges.begin(), E = mustAliasEdges.end();
       I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    Node *node = I->second;
    for (ChildrenIterator J = node->children.begin(), F = node->children.end();
	 J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
	     << "Node" << ((void *) dstPtr)
	     << " [label=\"MustAlias\" color=\"red\"]\n";
    }
  }

  // MayAlias Edges
  for (EdgeIterator I = mayAliasEdges.begin(), E = mayAliasEdges.end();
       I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    Node *node = I->second;
    for (ChildrenIterator J = node->children.begin(), F = node->children.end();
	 J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
	     << "Node" << ((void *) dstPtr)
	     << " [label=\"MayAlias\" color=\"green\"]\n";
    }
  }

  // PartialAlias Edges
  for (EdgeIterator I = partialAliasEdges.begin(), E = partialAliasEdges.end();
       I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    Node *node = I->second;
    for (ChildrenIterator J = node->children.begin(), F = node->children.end();
	 J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
	     << "Node" << ((void *) dstPtr)
	     << " [label=\"PartialAlias\" color=\"yellow\"]\n";
    }
  }

  // ValueDep Edges
  for (EdgeIterator I = valueDepEdges.begin(), E = valueDepEdges.end();
       I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    Node *node = I->second;
    for (ChildrenIterator J = node->children.begin(), F = node->children.end();
	 J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
	     << "Node" << ((void *) dstPtr)
	     << " [label=\"value\" color=\"blue\"]\n";
    }
  }

  // Barrier Edges
  for (auto I = barrierEdges.begin(), E = barrierEdges.end(); I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    vector<const Instruction *> &children = I->second;

    for (unsigned i=0; i<children.size(); ++i) {
      stream << "Node" << ((void *) srcPtr) << " -> "
	     << "Node" << children[i] << "\n";
    }
  }
}

DependencyGraph::NodeIterator
DependencyGraph::nodeBegin() {
  return nodes.begin();
}

DependencyGraph::NodeIterator
DependencyGraph::nodeEnd() {
  return nodes.end();
}

void
DependencyGraph::addBarrierEdge(llvm::MemoryLocation ML,
				const llvm::Instruction *barrier) {
  barrierNodes.push_back(barrier);

  DenseMap<MemoryLocation, vector<const Instruction *>>::iterator I;
  I = barrierEdges.find(ML);
  if (I == barrierEdges.end()) {
    barrierEdges[ML] = vector<const Instruction *>();
    barrierEdges[ML].push_back(barrier);
    return;
  }

  barrierEdges[ML].push_back(barrier);
}
