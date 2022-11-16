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

  using VCollListMap = std::map<const llvm::Value *, CollList *>;
  using CollListMap = std::map<const llvm::BasicBlock *, VCollListMap>;

  using CollListperFuncMap = std::map<const llvm::Function *, VCollListMap>;

  using CollMap = std::map<const llvm::BasicBlock *, CollSet>;

  using CollperFuncMap = std::map<const llvm::Function *, CollSet>;

public:
  ParcoachAnalysisInter(llvm::Module &M, DepGraphDCF *DG,
                        PTACallGraph const &PTACG,
                        llvm::FunctionAnalysisManager &AM,
                        bool disableInstru = false)
      : ParcoachAnalysis(M, DG, disableInstru), PTACG(PTACG), FAM(AM) {
    id++;
  }

  virtual ~ParcoachAnalysisInter() { CollList::freeAll(); }

  virtual void run();

  // FIXME: this should be const, or simply be returned by run!
  IAResult &getResult() { return Output_; };

private:
  PTACallGraph const &PTACG;
  llvm::FunctionAnalysisManager &FAM;

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

  IAResult Output_;

  static int id;

protected:
  BBVisitedMap bbVisitedMap;
  CollListMap mpiCollListMap;
  CollMap collMap;
  CollListperFuncMap mpiCollListperFuncMap;
  CollperFuncMap collperFuncMap;
};

} // namespace parcoach

#endif /* PARCOACHANALYSISINTER_H */
