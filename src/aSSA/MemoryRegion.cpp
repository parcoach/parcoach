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

namespace {
cl::opt<bool> optDumpRegions("dump-regions",
                             cl::desc("Dump the regions found by the "
                                      "Andersen PTA"),
                             cl::cat(ParcoachCategory));
cl::opt<bool>
    optWithRegName("with-reg-name",
                   cl::desc("Compute human readable names of regions"),
                   cl::cat(ParcoachCategory));

} // namespace

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
    GlobalValue const *GV = dyn_cast<GlobalValue>(V);
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
  TimeTraceScope TTS("MemRegAnalysis");
  // Create regions from allocation sites.
  std::vector<Value const *> regions;
  AA.getAllAllocationSites(regions);

  LLVM_DEBUG(dbgs() << regions.size() << " regions\n");
  for (Value const *r : regions) {
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
      Function const *F = I.first;

      for (Value const *v : I.second) {
        std::vector<Value const *> ptsSet;
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

void MemReg::createRegion(llvm::Value const *v) {
  auto [ItEntry, _] =
      valueToRegMap.insert({v, std::make_unique<MemRegEntry>(v)});
  auto &Entry = ItEntry->second;
  if (Entry->isCudaShared()) {
    sharedCudaRegions.insert(Entry.get());
  }
}

void MemReg::setOmpSharedRegions(Function const *F, MemRegVector &regs) {
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

MemRegEntry *MemReg::getValueRegion(llvm::Value const *v) const {
  auto I = valueToRegMap.find(v);
  if (I == valueToRegMap.end())
    return NULL;

  return I->second.get();
}

void MemReg::getValuesRegion(std::vector<Value const *> &ptsSet,
                             MemRegVector &regs) const {
  MemRegSet regions;
  for (Value const *v : ptsSet) {
    MemRegEntry *r = getValueRegion(v);

    if (r)
      regions.insert(r);
  }

  regs.insert(regs.begin(), regions.begin(), regions.end());
}

MemRegSet const &MemReg::getCudaSharedRegions() const {
  return sharedCudaRegions;
}

FunctionToMemRegSetMap const &MemReg::getOmpSharedRegions() const {
  return func2SharedOmpRegs;
}

AnalysisKey MemRegAnalysis::Key;

MemRegAnalysis::Result MemRegAnalysis::run(llvm::Module &M,
                                           llvm::ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("MemRegAnalysisPass");
  Andersen const &AA = AM.getResult<AndersenAA>(M);
  return std::make_unique<MemReg>(M, AA);
}
