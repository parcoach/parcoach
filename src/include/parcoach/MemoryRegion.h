#ifndef MEMORYREGION_H
#define MEMORYREGION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Value.h"

#include <map>
#include <set>

class MemReg;

typedef std::set<MemReg *> MemRegSet;

class MemReg {
  std::string name;
  static unsigned count;
  unsigned id;

protected:
  MemReg(const llvm::Value *value);
  ~MemReg() {}
  static std::map<const llvm::Value *, MemReg *> valueToRegMap;
  static std::set<MemReg *> sharedCudaRegions;
  static std::map<const llvm::Function *, std::set<MemReg *>>
      func2SharedOmpRegs;
  const llvm::Value *value;
  bool isCudaShared;

public:
  std::string getName() const;

  static void createRegion(const llvm::Value *v);
  static void setOmpSharedRegions(const llvm::Function *F,
                                  std::vector<MemReg *> &regs);
  static void dumpRegions();
  static MemReg *getValueRegion(const llvm::Value *v);
  static void getValuesRegion(std::vector<const llvm::Value *> &ptsSet,
                              std::vector<MemReg *> &regs);
  static const std::set<MemReg *> &getCudaSharedRegions();
  static const std::set<MemReg *> &getOmpSharedRegions(const llvm::Function *F);
};

#endif /* MEMORYREGION_H */
