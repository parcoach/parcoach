#ifndef MEMORYREGION_H
#define MEMORYREGION_H

#include "Utils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Value.h"

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
  static std::set<MemReg *> sharedRegions;
  const llvm::Value *value;
  bool isShared;

public:
  std::string getName() const;

  static void createRegion(const llvm::Value *v);
  static void dumpRegions();
  static MemReg *getValueRegion(const llvm::Value *v);
  static void getValuesRegion(std::vector<const llvm::Value *> &ptsSet,
			      std::vector<MemReg *> &regs);
  static const std::set<MemReg *> & getSharedRegions();
};



#endif /* MEMORYREGION_H */
