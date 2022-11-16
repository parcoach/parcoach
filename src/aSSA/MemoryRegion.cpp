#include "parcoach/MemoryRegion.h"
#include "Options.h"
#include "Utils.h"

#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/raw_ostream.h"

#include <set>

using namespace std;
using namespace llvm;

map<const llvm::Value *, MemReg *> MemReg::valueToRegMap;

set<MemReg *> MemReg::sharedCudaRegions;
map<const Function *, set<MemReg *>> MemReg::func2SharedOmpRegs;

unsigned MemReg::count = 0;

MemReg::MemReg(const llvm::Value *value) : value(value), isCudaShared(false) {
  id = count++;

  // Cuda shared region
  if (optCudaTaint) {
    const GlobalValue *GV = dyn_cast<GlobalValue>(value);
    if (GV && GV->getType()->getPointerAddressSpace() == 3) {
      isCudaShared = true;
      sharedCudaRegions.insert(this);
    }
  }

  if (!optWithRegName) {
    name = std::to_string(id);
    return;
  }

  name = getValueLabel(value);
  const llvm::Instruction *inst = llvm::dyn_cast<llvm::Instruction>(value);
  if (inst)
    name.append(inst->getParent()->getParent()->getName().str());
}

void MemReg::createRegion(const llvm::Value *v) {
  valueToRegMap[v] = new MemReg(v);
}

void MemReg::setOmpSharedRegions(const Function *F, vector<MemReg *> &regs) {
  func2SharedOmpRegs[F].insert(regs.begin(), regs.end());
}

void MemReg::dumpRegions() {
  llvm::errs() << valueToRegMap.size() << " regions :\n";
  for (auto I : valueToRegMap) {
    llvm::errs() << *I.second->value
                 << (I.second->isCudaShared ? " (shared)\n" : "\n");
  }
}

MemReg *MemReg::getValueRegion(const llvm::Value *v) {
  auto I = valueToRegMap.find(v);
  if (I == valueToRegMap.end())
    return NULL;

  return I->second;
}

void MemReg::getValuesRegion(std::vector<const Value *> &ptsSet,
                             std::vector<MemReg *> &regs) {
  std::set<MemReg *> regions;
  for (const Value *v : ptsSet) {
    MemReg *r = getValueRegion(v);

    if (r)
      regions.insert(r);
  }

  regs.insert(regs.begin(), regions.begin(), regions.end());
}

const std::set<MemReg *> &MemReg::getCudaSharedRegions() {
  return sharedCudaRegions;
}

const std::set<MemReg *> &MemReg::getOmpSharedRegions(const llvm::Function *F) {
  return func2SharedOmpRegs[F];
}

std::string MemReg::getName() const { return name; }
