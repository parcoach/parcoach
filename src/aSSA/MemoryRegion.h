#ifndef MEMORYREGION_H
#define MEMORYREGION_H

#include "Utils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Value.h"

class MemReg;

typedef std::set<MemReg *> MemRegSet;

class MemReg {
  std::string name;

protected:
  MemReg(const llvm::Value *value) : value(value) {
    name = getValueLabel(value);
  }
  ~MemReg() {}
  static llvm::DenseMap<const llvm::Value *, MemReg *> valueToRegMap;
  const llvm::Value *value;

public:
  std::string getName() const;

  static void createRegion(const llvm::Value *v);
  static void dumpRegions();
  static MemReg *getValueRegion(const llvm::Value *v);
  static void getValuesRegion(std::vector<const llvm::Value *> &ptsSet,
			      std::vector<MemReg *> &regs);
};



#endif /* MEMORYREGION_H */
