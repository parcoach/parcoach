#include "IntraDepGraph.h"

#include "llvm/Support/FileSystem.h"

#include <sstream>

using namespace llvm;
using namespace std;

IntraDepGraph::IntraDepGraph() {}
IntraDepGraph::~IntraDepGraph() {}

bool
IntraDepGraph::addRoot(llvm::MemoryLocation ML) {
  nodes.insert(ML);

  auto I = roots.find(ML);
  if (I == roots.end()) {
    roots.insert(ML);
    return true;
  }

  return false;
}

bool
IntraDepGraph::addEdge(llvm::MemoryLocation src, llvm::MemoryLocation dst,
		       enum type depType) {
  if (src == dst)
    return false;

  if (src.Ptr == NULL || dst.Ptr == NULL)
    abort();

  nodes.insert(src);
  nodes.insert(dst);

  EdgeMapTy *edgeMap = NULL;

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
  case Flow:
    edgeMap = &flowEdges;
    break;
  };

  auto I = edgeMap->find(src);
  if (I == edgeMap->end()) {
    NodeTy *node = new NodeTy();
    (*edgeMap)[src] = node;
    node->insert(dst);
    return true;
  }

  NodeTy *node = I->second;
  auto J = node->find(dst);
  if (J == node->end()) {
    node->insert(dst);
    return true;
  }

  return false;
}

void
IntraDepGraph::addBarrierEdge(llvm::MemoryLocation ML,
			      const llvm::Instruction *barrier) {
  barriers.insert(barrier);

  auto I = barrierEdges.find(ML);
  if (I == barrierEdges.end()) {
    LeafTy *node = new LeafTy();
    barrierEdges[ML] = node;
    node->insert(barrier);
  }
}

IntraDepGraph::NodeTy::iterator
IntraDepGraph::nodeBegin() {
  return nodes.begin();
}

IntraDepGraph::NodeTy::iterator
IntraDepGraph::nodeEnd() {
  return nodes.end();
}


void
IntraDepGraph::debug() {
  // TODO
}

void
IntraDepGraph::toDot(llvm::StringRef filename) {
  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph G {\n";

  toDot(stream);

  stream << "}\n";
}

void
IntraDepGraph::toDot(llvm::raw_fd_ostream &stream) {
  // Nodes
  for (auto I = nodes.begin(), E = nodes.end(); I != E; ++I) {
    const Value *ptr = I->Ptr;
    if (roots.find(*I) != roots.end())
      stream << "Node" << ((void *) ptr) << " [shape=\"doublecircle\" "
	     << "label=\"" << *ptr << "\"];\n";
    else
      stream << "Node" << ((void *) ptr) << " [label=\"" << *ptr << "\"];\n";
  }

  // Barrier Nodes
  for (auto I = barriers.begin(), E = barriers.end(); I != E; ++I) {
    stream << "Node" << *I << " [shape=\"rectangle\" label=\""
	   << **I << "\"];\n";
  }

  // MustAlias Edges
  for (auto I = mustAliasEdges.begin(), E = mustAliasEdges.end(); I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    NodeTy *node = I->second;
    for (auto J = node->begin(), F = node->end(); J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
  	     << "Node" << ((void *) dstPtr)
  	     << " [label=\"MustAlias\" color=\"red\"]\n";
    }
  }

  // MayAlias Edges
  for (auto I = mayAliasEdges.begin(), E = mayAliasEdges.end(); I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    NodeTy *node = I->second;
    for (auto J = node->begin(), F = node->end(); J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
  	     << "Node" << ((void *) dstPtr)
  	     << " [label=\"MayAlias\" color=\"green\"]\n";
    }
  }

  // PartialAlias Edges
  for (auto I = partialAliasEdges.begin(), E = partialAliasEdges.end(); I != E;
       ++I) {
    const Value *srcPtr = I->first.Ptr;

    NodeTy *node = I->second;
    for (auto J = node->begin(), F = node->end(); J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
  	     << "Node" << ((void *) dstPtr)
  	     << " [label=\"PartialAlias\" color=\"yellow\"]\n";
    }
  }

  // Flow Edges
  for (auto I = flowEdges.begin(), E = flowEdges.end(); I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    NodeTy *node = I->second;
    for (auto J = node->begin(), F = node->end(); J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
  	     << "Node" << ((void *) dstPtr)
  	     << " [label=\"Flow\" color=\"blue\"]\n";
    }
  }

  // Barrier Edges
  for (auto I = barrierEdges.begin(), E = barrierEdges.end(); I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    LeafTy *leaf = I->second;
    for (auto J = leaf->begin(), F = leaf->end(); J != F; ++J) {
      const Instruction *barrier = *J;

      stream << "Node" << ((void *) srcPtr) << " -> "
  	     << "Node" << ((void *) barrier) << "\n";
    }
  }
}
