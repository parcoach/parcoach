#ifndef DEPGRAPH_H
#define DEPGRAPH_H

#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>

class DepGraph {
 public:
  DepGraph();
  ~DepGraph();

  void addFunction(const llvm::Function *F);
  void addEdge(const llvm::Value *, const llvm::Value *);
  void addSource(const llvm::Value *src);
  void computeTaintedValues();
  void toDot(llvm::StringRef filename);
  void toDot(llvm::raw_fd_ostream &stream);

 private:
  std::map<const llvm::Value *, std::set<const llvm::Value *> *> graph;
  std::set<const llvm::Function *> functions;
  std::set<const llvm::Value *> sources;
  std::set<const llvm::Value *> taintedValues;

  void taintRec(const llvm::Value *v);
};

#endif /* DEPGRAPH_H */
