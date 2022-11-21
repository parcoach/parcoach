#ifndef PARCOACHANALYSIS_H
#define PARCOACHANALYSIS_H

#include <map>
#include <set>

namespace parcoach {
class DepGraphDCF;
}
namespace llvm {
class Module;
class BasicBlock;
class Instruction;
} // namespace llvm

class ParcoachAnalysis {
public:
  ParcoachAnalysis(llvm::Module &M, parcoach::DepGraphDCF *DG,
                   bool disableInstru = false)
      : M(M), DG(DG), nbCollectivesFound(0), nbCollectivesCondCalled(0),
        nbCollectivesFoundParcoachOnly(0), disableInstru(disableInstru) {}

  virtual ~ParcoachAnalysis() {}

  virtual void run() = 0;

  unsigned getNbCollectivesFound() const { return nbCollectivesFound; }

  unsigned getNbCollectivesCondCalled() const {
    return nbCollectivesCondCalled;
  }

  unsigned getNbCollectivesFoundParcoachOnly() const {
    return nbCollectivesFoundParcoachOnly;
  }

  unsigned getNbWarnings() const { return warningSet.size(); }

  unsigned getNbWarningsParcoachOnly() const {
    return warningSetParcoachOnly.size();
  }

  unsigned getNbConds() const { return conditionSet.size(); }

  unsigned getNbCondsParcoachOnly() const {
    return conditionSetParcoachOnly.size();
  }

  std::set<const llvm::BasicBlock *> const &getConditionSet() const {
    return conditionSet;
  }

  std::set<const llvm::BasicBlock *> const &
  getConditionSetParcoachOnly() const {
    return conditionSetParcoachOnly;
  }

  std::set<const llvm::Instruction *> const &getWarningSet() const {
    return warningSet;
  }

  std::set<const llvm::Instruction *> const &getWarningSetParcoachOnly() const {
    return warningSetParcoachOnly;
  }

protected:
  llvm::Module &M;
  parcoach::DepGraphDCF *DG;

  unsigned nbCollectivesFound;
  unsigned nbCollectivesCondCalled;
  unsigned nbCollectivesFoundParcoachOnly;

  std::set<const llvm::BasicBlock *> conditionSet;
  std::set<const llvm::BasicBlock *> conditionSetParcoachOnly;
  std::set<const llvm::Instruction *> warningSet;
  std::set<const llvm::Instruction *> warningSetParcoachOnly;

  bool disableInstru;
};

#endif /* PARCOACHANALYSIS */
