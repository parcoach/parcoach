#pragma once

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Value.h"
#include "llvm/Passes/PassBuilder.h"

#include <map>
#include <set>

class Andersen;

class MemRegEntry {
  unsigned id_;
  bool cudaShared_;
  std::string name_;

public:
  llvm::Value const *Val;
  MemRegEntry(llvm::Value const *V);
  llvm::StringRef getName() const { return name_; }
  bool isCudaShared() const { return cudaShared_; }
  static unsigned generateId();
};

using MemRegSet = std::set<MemRegEntry *>;
using MemRegVector = std::vector<MemRegEntry *>;
using FunctionToMemRegSetMap =
    llvm::ValueMap<llvm::Function const *, MemRegSet>;
using FunctionToValueSetMap =
    llvm::ValueMap<llvm::Function const *, std::set<llvm::Value const *>>;

class MemReg {
  llvm::ValueMap<const llvm::Value *, MemRegEntry *> valueToRegMap;
  MemRegSet sharedCudaRegions;
  FunctionToMemRegSetMap func2SharedOmpRegs;

  void createRegion(const llvm::Value *v);
  void setOmpSharedRegions(const llvm::Function *F, MemRegVector &regs);

public:
  void dumpRegions() const;
  MemRegEntry *getValueRegion(const llvm::Value *v) const;
  void getValuesRegion(std::vector<const llvm::Value *> &ptsSet,
                       MemRegVector &regs) const;
  const MemRegSet &getCudaSharedRegions() const;
  const FunctionToMemRegSetMap &getOmpSharedRegions() const;
  MemReg(llvm::Module &M, Andersen const &A);
  static FunctionToValueSetMap func2SharedOmpVar;
};

class MemRegAnalysis : public llvm::AnalysisInfoMixin<MemRegAnalysis> {
  friend llvm::AnalysisInfoMixin<MemRegAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = std::unique_ptr<MemReg>;

  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};
