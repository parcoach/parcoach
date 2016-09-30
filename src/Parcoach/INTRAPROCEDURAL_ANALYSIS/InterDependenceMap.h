/*
 * This class represents a map of interdependencies between functions.
 * Each function has a DepMap <MemoryLocation src, MemoryLocation dst>.
 *
 * For a function F, <src> represents the rank dependent memory location
 * inside a function calling F, and <dst> represents the memory location of an
 * argument of F.
 *
 * For example let's assume we have to functions :
 * - foo()
 * - bar(%arg1, %arg2)
 *
 * If function foo calls function bar(%a, 0) and %a is rank dependent inside
 * foo.
 * The DepMap of bar will be :
 * %a -> %arg1
 */


#ifndef INTERDEPENDENCEMAP_H
#define INTERDEPENDENCEMAP_H

#include "GlobalGraph.h"

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Function.h"

typedef llvm::DenseMap<llvm::MemoryLocation, llvm::MemoryLocation> DepMap;

class InterDependenceMap {
  friend class GlobalGraph;

 private:
  std::map<const llvm::Function *,  DepMap *> map;

 public:
  bool addDependence(const llvm::Function *F, llvm::MemoryLocation src,
		     llvm::MemoryLocation dst) {
    if (src.Ptr == NULL || dst.Ptr == NULL)
      abort();

    DepMap *DM = NULL;
    std::map<const llvm::Function *, DepMap *>::iterator I = map.find(F);
    if (I == map.end()) {
      DM = new DepMap();
      map[F] = DM;
    } else {
      DM = map[F];
    }

    if (DM->find(src) != DM->end())
      return false;

    (*DM)[src] = dst;

    return true;
  }

  DepMap *getFunctionDepMap(const llvm::Function *F) {
    auto I = map.find(F);
    DepMap *DM;
    if (I == map.end()) {
      DM = new DepMap();
      map[F] = DM;
      return DM;
    }

    DM = (*I).second;
    return DM;
  }
};

#endif /* INTERDEPENDENCEMAP */
