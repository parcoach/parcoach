#include "InterDepGraph.h"

#include "llvm/Support/FileSystem.h"

#include <sstream>

using namespace llvm;
using namespace std;

InterDepGraph::InterDepGraph() {}
InterDepGraph::~InterDepGraph() {}

bool
InterDepGraph::addEdge(llvm::MemoryLocation src, llvm::MemoryLocation dst,
		       enum type depType) {
  if (src == dst)
    return false;

  if (src.Ptr == NULL || dst.Ptr == NULL)
    abort();

  EdgeMapTy *edgeMap = NULL;

  switch(depType) {
  case Argument:
    edgeMap = &argEdges;
    break;
  case SideEffect:
    edgeMap = &sideEffectEdges;
    break;
  case Return:
    edgeMap = &returnEdges;
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
InterDepGraph::debug() {
  // TODO
}

void
InterDepGraph::toDot(llvm::StringRef filename) {
  error_code EC;
  raw_fd_ostream stream(filename, EC, sys::fs::F_Text);

  stream << "digraph G {\n";

  toDot(stream);

  stream << "}\n";
}

void
InterDepGraph::toDot(llvm::raw_fd_ostream &stream) {
  // Return Edges
  for (auto I = returnEdges.begin(), E = returnEdges.end(); I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    NodeTy *node = I->second;
    for (auto J = node->begin(), F = node->end(); J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
  	     << "Node" << ((void *) dstPtr)
  	     << " [label=\"Return\" color=\"purple\"]\n";
    }
  }

  // SideEffect Edges
  for (auto I = sideEffectEdges.begin(), E = sideEffectEdges.end(); I != E; ++I)
    {
    const Value *srcPtr = I->first.Ptr;

    NodeTy *node = I->second;
    for (auto J = node->begin(), F = node->end(); J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
  	     << "Node" << ((void *) dstPtr)
  	     << " [label=\"SideEffect\" color=\"black\"]\n";
    }
  }

  // Arg Edges
  for (auto I = argEdges.begin(), E = argEdges.end(); I != E; ++I) {
    const Value *srcPtr = I->first.Ptr;

    NodeTy *node = I->second;
    for (auto J = node->begin(), F = node->end(); J != F; ++J) {
      const Value *dstPtr = J->Ptr;

      stream << "Node" << ((void *) srcPtr) << " -> "
  	     << "Node" << ((void *) dstPtr)
  	     << " [label=\"Arg\" color=\"black\"]\n";
    }
  }
}
