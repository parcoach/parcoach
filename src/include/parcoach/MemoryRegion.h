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
  llvm::ValueMap<llvm::Value const *, std::unique_ptr<MemRegEntry>>
      valueToRegMap;
  MemRegSet sharedCudaRegions;
  FunctionToMemRegSetMap func2SharedOmpRegs;

  void createRegion(llvm::Value const *v);
  void setOmpSharedRegions(llvm::Function const *F, MemRegVector &regs);

public:
#ifndef NDEBUG
  void dumpRegions() const;
#endif
  MemRegEntry *getValueRegion(llvm::Value const *v) const;
  void getValuesRegion(std::vector<llvm::Value const *> &ptsSet,
                       MemRegVector &regs) const;
  MemRegSet const &getCudaSharedRegions() const;
  FunctionToMemRegSetMap const &getOmpSharedRegions() const;
  MemReg(llvm::Module &M, Andersen const &A);
  static FunctionToValueSetMap func2SharedOmpVar;
};

class MemRegAnalysis : public llvm::AnalysisInfoMixin<MemRegAnalysis> {
  friend llvm::AnalysisInfoMixin<MemRegAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = std::unique_ptr<MemReg>;

  static Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};
