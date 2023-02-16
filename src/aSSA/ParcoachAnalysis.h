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
  ParcoachAnalysis(llvm::Module &M, parcoach::DepGraphDCF *DG)
      : M(M), DG(DG), nbCollectivesFound(0), nbCollectivesCondCalled(0),
        nbCollectivesFoundParcoachOnly(0) {}

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

  std::set<llvm::BasicBlock const *> const &getConditionSet() const {
    return conditionSet;
  }

  std::set<llvm::BasicBlock const *> const &
  getConditionSetParcoachOnly() const {
    return conditionSetParcoachOnly;
  }

  std::set<llvm::Instruction const *> const &getWarningSet() const {
    return warningSet;
  }

  std::set<llvm::Instruction const *> const &getWarningSetParcoachOnly() const {
    return warningSetParcoachOnly;
  }

protected:
  llvm::Module &M;
  parcoach::DepGraphDCF *DG;

  unsigned nbCollectivesFound;
  unsigned nbCollectivesCondCalled;
  unsigned nbCollectivesFoundParcoachOnly;

  std::set<llvm::BasicBlock const *> conditionSet;
  std::set<llvm::BasicBlock const *> conditionSetParcoachOnly;
  std::set<llvm::Instruction const *> warningSet;
  std::set<llvm::Instruction const *> warningSetParcoachOnly;
};

#endif /* PARCOACHANALYSIS */
