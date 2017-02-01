#include "MemoryRegion.h"

#include "llvm/Support/raw_ostream.h"

#include <set>

using namespace std;
using namespace llvm;

llvm::DenseMap<const llvm::Value *, MemReg *> MemReg::valueToRegMap;

void
MemReg::createRegion(const llvm::Value *v) {
  valueToRegMap[v] = new MemReg(v);
}

void
MemReg::dumpRegions() {
    llvm::errs() << valueToRegMap.size() << " regions :\n";
    for (auto I : valueToRegMap)
      llvm::errs() << *I.second->value << "\n";
}

MemReg *
MemReg::getValueRegion(const llvm::Value *v) {
    auto I = valueToRegMap.find(v);
    if (I == valueToRegMap.end())
      return NULL;
    return I->second;
}

void
MemReg::getValuesRegion(std::vector<const Value *> &ptsSet,
		     std::vector<MemReg *> &regs) {
  std::set<MemReg *> regions;
  for (const Value *v : ptsSet) {
    MemReg *r = getValueRegion(v);
    assert(r);
    regions.insert(r);
  }

  regs.insert(regs.begin(), regions.begin(), regions.end());
}

std::string
MemReg::getName() const {
  return name;
}
