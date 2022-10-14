#ifndef PARCOACHANALYSISINTER_H
#define PARCOACHANALYSISINTER_H

#include "CollList.h"

#include "PTACallGraph.h"
#include "ParcoachAnalysis.h"
#include <llvm/Analysis/LoopInfo.h>

class ParcoachAnalysisInter : public ParcoachAnalysis {

  typedef bool Latches;
  typedef std::map<const llvm::BasicBlock *, Latches> BBLatchesMap;
  BBLatchesMap bbLatchesMap;

  // typedef std::set<const llvm::Function *F> CollSet;
  typedef std::string CollSet;
  // typedef bool Visited;
  enum Visited { white, grey, black };
  using ComCollMap = std::map<const llvm::Value *, CollSet>;

  typedef std::map<const llvm::BasicBlock *, Visited> BBVisitedMap;

  typedef std::map<const llvm::Value *, CollList *> VCollListMap;
  typedef std::map<const llvm::BasicBlock *, VCollListMap> CollListMap;

  typedef std::map<const llvm::Function *, VCollListMap> CollListperFuncMap;

  typedef std::map<const llvm::BasicBlock *, ComCollMap> MPICollMap;
  typedef std::map<const llvm::BasicBlock *, CollSet> CollMap;

  typedef std::map<const llvm::Function *, ComCollMap> MPICollperFuncMap;
  typedef std::map<const llvm::Function *, CollSet> CollperFuncMap;

public:
  ParcoachAnalysisInter(llvm::Module &M, DepGraph *DG, PTACallGraph &PTACG,
                        llvm::Pass *pass, bool disableInstru = false)
      : ParcoachAnalysis(M, DG, disableInstru), PTACG(PTACG), pass(pass) {
    id++;
  }

  virtual ~ParcoachAnalysisInter() { CollList::freeAll(); }

  virtual void run();

private:
  PTACallGraph &PTACG;
  llvm::LoopInfo *curLoop;
  llvm::Pass *pass;

  void setCollSet(llvm::BasicBlock *BB);
  void setMPICollSet(llvm::BasicBlock *BB);
  void cmpAndUpdateMPICollSet(llvm::BasicBlock *header, llvm::BasicBlock *pred);
  void MPI_BFS_Loop(llvm::Loop *L);
  void BFS_Loop(llvm::Loop *L);
  void Tag_LoopLatches(llvm::Loop *L);
  bool isExitNode(llvm::BasicBlock *BB);
  bool mustWait(llvm::BasicBlock *bb);
  bool mustWaitLoop(llvm::BasicBlock *bb, llvm::Loop *l);
  void BFS(llvm::Function *F);
  void MPI_BFS(llvm::Function *F);
  void checkCollectives(llvm::Function *F);
  void countCollectivesToInst(llvm::Function *F);
  void instrumentFunction(llvm::Function *F);
  void insertCC(llvm::Instruction *I, int OP_color, std::string OP_name,
                int OP_line, llvm::StringRef WarningMsg, llvm::StringRef File);
  void insertCountColl(llvm::Instruction *I, std::string OP_name, int OP_line,
                       llvm::StringRef File, int inst);
  std::string getWarning(llvm::Instruction &inst);

  static int id;

protected:
  BBVisitedMap bbVisitedMap;
  CollListMap mpiCollListMap;
  CollMap collMap;
  // MPICollperFuncMap mpiCollperFuncMap;
  CollListperFuncMap mpiCollListperFuncMap;
  CollperFuncMap collperFuncMap;
};

#endif /* PARCOACHANALYSISINTER_H */
