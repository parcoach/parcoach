#ifndef DEPGRAPH_H
#define DEPGRAPH_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"

#include <set>

class DepGraph {
public:
  virtual void buildFunction(const llvm::Function *F) = 0;
  virtual void toDot(std::string filename) = 0;
  virtual bool isTaintedValue(const llvm::Value *v) = 0;
  virtual void computeTaintedValuesContextInsensitive() = 0;
  virtual void computeTaintedValuesContextSensitive() = 0;
  virtual void getCallInterIPDF(const llvm::CallInst *call,
				std::set<const llvm::BasicBlock *> &ipdf) = 0;
  virtual void dotTaintPath(const llvm::Value *v, std::string filename,
			    const llvm::Instruction *collective) = 0;
};

#endif /* DEPGRAPH_H */
