#ifndef PARCOACHANALYSISINTER_H
#define PARCOACHANALYSISINTER_H

#include "CollList.h"

#include "PTACallGraph.h"
#include "ParcoachAnalysis.h"
#include "parcoach/Analysis.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/Analysis/LoopInfo.h>

namespace parcoach {

class ParcoachAnalysisInter : public ParcoachAnalysis {
  // typedef std::set<const llvm::Function *F> CollSet;
  using CollSet = std::string;
  // typedef bool Visited;
  enum Visited { white, grey, black };
  using ComCollMap = std::map<const llvm::Value *, CollSet>;

  using BBVisitedMap = std::map<const llvm::BasicBlock *, Visited>;

  // FIXME: if we have a way to represent an empty "CollList", we don't
  // event need the unique_ptr.
  using VCollListMap = std::map<const llvm::Value *, std::unique_ptr<CollList>>;
  using CollListMap = std::map<const llvm::BasicBlock *, VCollListMap>;

  using CollMap = std::map<const llvm::BasicBlock *, CollSet>;

  using CollperFuncMap = std::map<const llvm::Function *, CollSet>;

public:
  ParcoachAnalysisInter(llvm::Module &M, DepGraphDCF *DG,
                        PTACallGraph const &PTACG,
                        llvm::ModuleAnalysisManager &AM,
                        bool disableInstru = false)
      : ParcoachAnalysis(M, DG, disableInstru), PTACG(PTACG), MAM(AM) {
    id++;
  }

  virtual ~ParcoachAnalysisInter() = default;

  void run() override;
  CallToWarningMapTy const &getWarnings() { return Warnings; };

private:
  PTACallGraph const &PTACG;
  llvm::ModuleAnalysisManager &MAM;
  CallToWarningMapTy Warnings;

  void setCollSet(llvm::BasicBlock *BB);
  void setMPICollSet(llvm::BasicBlock *BB);
  void cmpAndUpdateMPICollSet(llvm::BasicBlock *header, llvm::BasicBlock *pred);
  void MPI_BFS_Loop(llvm::Loop *L);
  void BFS_Loop(llvm::Loop *L);
  bool isExitNode(llvm::BasicBlock *BB);
  bool mustWait(llvm::BasicBlock *bb);
  bool mustWaitLoop(llvm::BasicBlock *bb, llvm::Loop *l);
  void BFS(llvm::Function *F);
  void MPI_BFS(llvm::Function *F);
  void checkCollectives(llvm::Function *F);
  void instrumentFunction(llvm::Function *F);
  void insertCC(llvm::Instruction *I, int OP_color, std::string OP_name,
                int OP_line, llvm::StringRef WarningMsg, llvm::StringRef File);
#ifndef NDEBUG
  void dump();
#endif
  static int id;

protected:
  BBVisitedMap bbVisitedMap;
  CollListMap mpiCollListMap;
  CollMap collMap;
  CollperFuncMap collperFuncMap;
};

} // namespace parcoach

#endif /* PARCOACHANALYSISINTER_H */
