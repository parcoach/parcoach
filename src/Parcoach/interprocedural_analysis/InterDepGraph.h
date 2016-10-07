#ifndef INTERDEPGRAPH_H
#define INTERDEPGRAPH_H

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/iterator.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

/*
 * Class representing the dependencies between functions.
 *
 * There if 3 types of dependencies:
 * - Argument dependencies.
 * - Side effect dependencies.
 * - Return value dependencies.
 */


class InterDepGraph {
 public:

  enum type {
    Argument,
    SideEffect,
    Return
  };

  // Set of memory memory locations.
  typedef llvm::DenseSet<llvm::MemoryLocation> NodeTy;
  // Set of directed edges with same source.
  typedef llvm::DenseMap<llvm::MemoryLocation, NodeTy *> EdgeMapTy;

  InterDepGraph();
  ~InterDepGraph();

  bool addEdge(llvm::MemoryLocation src, llvm::MemoryLocation dst,
	       enum type depType);

  void debug();
  void toDot(llvm::StringRef filename);
  void toDot(llvm::raw_fd_ostream &stream);

 private:
  EdgeMapTy returnEdges;
  EdgeMapTy sideEffectEdges;
  EdgeMapTy argEdges; // Source if a memory location from a caller function,
  // dests are memory locations of arguments from the called function.
};

#endif /* INTERDEPGRAPH_H */
