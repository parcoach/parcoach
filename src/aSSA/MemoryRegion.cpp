#include "parcoach/MemoryRegion.h"
#include "Utils.h"
#include "parcoach/Options.h"
#include "parcoach/andersen/Andersen.h"

#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/raw_ostream.h"

#include <set>

#define DEBUG_TYPE "memreg"

using namespace llvm;
using namespace parcoach;

llvm::ValueMap<llvm::Function const *, std::set<llvm::Value const *>>
    MemReg::func2SharedOmpVar;

unsigned MemRegEntry::generateId() {
  static unsigned count{};
  return count++;
}

MemRegEntry::MemRegEntry(Value const *V)
    : id_(generateId()), cudaShared_(false), Val(V) {
#ifdef PARCOACH_ENABLE_CUDA
  // Cuda shared region
  if (Options::get().isActivated(Paradigm::CUDA)) {
    const GlobalValue *GV = dyn_cast<GlobalValue>(V);
    if (GV && GV->getType()->getPointerAddressSpace() == 3) {
      cudaShared_ = true;
      // FIXME
      // sharedCudaRegions.insert(this);
    }
  }
#endif

  if (!optWithRegName) {
    name_ = std::to_string(id_);
    return;
  }

  name_ = getValueLabel(V);
  if (auto const *I = dyn_cast<Instruction>(V)) {
    name_.append(I->getFunction()->getName());
  }
}

MemReg::MemReg(Module &M, Andersen const &AA) {
  // Create regions from allocation sites.
  std::vector<const Value *> regions;
  AA.getAllAllocationSites(regions);

  LLVM_DEBUG(dbgs() << regions.size() << " regions\n");
  for (const Value *r : regions) {
    createRegion(r);
  }

  LLVM_DEBUG({
    if (optDumpRegions)
      dumpRegions();
    dbgs() << "* Regions creation done\n";
  });

#ifdef PARCOACH_ENABLE_OPENMP
  // Compute shared regions for each OMP function.
  if (Options::get().isActivated(Paradigm::OMP)) {
    FunctionToMemRegSetMap func2SharedOmpReg;
    for (auto I : func2SharedOmpVar) {
      const Function *F = I.first;

      for (const Value *v : I.second) {
        std::vector<const Value *> ptsSet;
        if (AA.getPointsToSet(v, ptsSet)) {
          MemRegVector regs;
          getValuesRegion(ptsSet, regs);
          setOmpSharedRegions(F, regs);
        }
      }
    }
  }
#endif
}

void MemReg::createRegion(const llvm::Value *v) {
  auto *Entry = new MemRegEntry(v);
  if (Entry->isCudaShared()) {
    sharedCudaRegions.insert(Entry);
  }
  valueToRegMap[v] = Entry;
}

void MemReg::setOmpSharedRegions(const Function *F, MemRegVector &regs) {
  func2SharedOmpRegs[F].insert(regs.begin(), regs.end());
}

#ifndef NDEBUG
void MemReg::dumpRegions() const {
  dbgs() << valueToRegMap.size() << " regions :\n";
  for (auto I : valueToRegMap) {
    dbgs() << *I.second->Val
           << (I.second->isCudaShared() ? " (shared)\n" : "\n");
  }
}
#endif

MemRegEntry *MemReg::getValueRegion(const llvm::Value *v) const {
  auto I = valueToRegMap.find(v);
  if (I == valueToRegMap.end())
    return NULL;

  return I->second;
}

void MemReg::getValuesRegion(std::vector<const Value *> &ptsSet,
                             MemRegVector &regs) const {
  MemRegSet regions;
  for (const Value *v : ptsSet) {
    MemRegEntry *r = getValueRegion(v);

    if (r)
      regions.insert(r);
  }

  regs.insert(regs.begin(), regions.begin(), regions.end());
}

const MemRegSet &MemReg::getCudaSharedRegions() const {
  return sharedCudaRegions;
}

const FunctionToMemRegSetMap &MemReg::getOmpSharedRegions() const {
  return func2SharedOmpRegs;
}

AnalysisKey MemRegAnalysis::Key;

MemRegAnalysis::Result MemRegAnalysis::run(llvm::Module &M,
                                           llvm::ModuleAnalysisManager &AM) {
  Andersen const &AA = AM.getResult<AndersenAA>(M);
  return std::make_unique<MemReg>(M, AA);
}
