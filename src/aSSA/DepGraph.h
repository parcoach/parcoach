#ifndef DEPGRAPH_H
#define DEPGRAPH_H

#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>

class DepGraph {
 public:
  DepGraph();
  ~DepGraph();

  void addFunction(const llvm::Function *F);
  void addIPDFFuncNode(const llvm::Function *F, const llvm::Value *);
  const llvm::Value *getIPDFFuncNode(const llvm::Function *F);
  void addEdge(const llvm::Value *, const llvm::Value *);
  void addSource(const llvm::Value *src);
  void addSink(const llvm::Value *src);
  void computeTaintedValues(llvm::Pass *pass);
  void toDot(llvm::StringRef filename);
  void toDot(llvm::raw_fd_ostream &stream);

 private:
  std::map<const llvm::Value *, std::set<const llvm::Value *> *> graph;
  std::set<const llvm::Function *> functions;
  std::set<const llvm::Value *> sources;
  std::set<const llvm::Value *> taintedValues;
  std::set<const llvm::Value *> sinks;
  std::set<const llvm::Value *> taintedSinks;
  std::map<const llvm::Function *, const llvm::Value *> IPDFFuncNodes;
  void taintRec(const llvm::Value *v);
  std::string getNodeStyle(const llvm::Value *v);
};

#endif /* DEPGRAPH_H */
