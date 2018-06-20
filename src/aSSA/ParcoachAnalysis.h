#ifndef PARCOACHANALYSIS_H
#define PARCOACHANALYSIS_H

#include "DepGraph.h"
#include <map>

class ParcoachAnalysis {
 public:
  ParcoachAnalysis(llvm::Module &M, DepGraph *DG, bool disableInstru=false)
    : M(M), DG(DG),
      nbCollectivesFound(0), nbCollectivesFoundParcoachOnly(0),
      nbWarnings(0), nbWarningsParcoachOnly(0),
      nbConds(0), nbCondsParcoachOnly(0),
      nbCC(0),
      disableInstru(disableInstru) {
  }

  virtual ~ParcoachAnalysis() {}

  virtual void run() = 0;

  unsigned getNbCollectivesFound() const {
    return nbCollectivesFound;
  }

  unsigned getNbCollectivesFoundParcoachOnly() const {
    return nbCollectivesFoundParcoachOnly;
  }

  unsigned getNbWarnings() const {
    return nbWarnings;
  }

  unsigned getNbWarningsParcoachOnly() const {
    return nbWarningsParcoachOnly;
  }

  unsigned getNbConds() const {
    return nbConds;
  }

  unsigned getNbCondsParcoachOnly() const {
    return nbCondsParcoachOnly;
  }

  unsigned getNbCC() const {
    return nbCC;
  }

  std::set<const llvm::BasicBlock *> getConditionSet() const {
    return conditionSet;
  }

  std::set<const llvm::BasicBlock *> getConditionSetParcoachOnly() const {
    return conditionSetParcoachOnly;
  }

  std::set<const llvm::Instruction *> getWarningSet() const {
    return warningSet;
  }

  std::set<const llvm::Instruction *> getWarningSetParcoachOnly() const {
    return warningSetParcoachOnly;
  }

 protected:
  llvm::Module &M;
  DepGraph *DG;

  unsigned nbCollectivesFound;
  unsigned nbCollectivesFoundParcoachOnly;
  unsigned nbWarnings;
  unsigned nbWarningsParcoachOnly;
  unsigned nbConds;
  unsigned nbCondsParcoachOnly;
  unsigned nbCC;

  std::set<const llvm::BasicBlock *> conditionSet;
  std::set<const llvm::BasicBlock *> conditionSetParcoachOnly;
  std::set<const llvm::Instruction *> warningSet;
  std::set<const llvm::Instruction *> warningSetParcoachOnly;



  bool disableInstru;
};

#endif /* PARCOACHANALYSIS */
