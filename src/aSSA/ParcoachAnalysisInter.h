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
  using ComCollMap = std::map<llvm::Value const *, CollSet>;

  using BBVisitedMap = std::map<llvm::BasicBlock const *, Visited>;

  // FIXME: if we have a way to represent an empty "CollList", we don't
  // event need the unique_ptr.
  using VCollListMap = std::map<llvm::Value const *, std::unique_ptr<CollList>>;
  using CollListMap = std::map<llvm::BasicBlock const *, VCollListMap>;

  using CollMap = std::map<llvm::BasicBlock const *, CollSet>;

  using CollperFuncMap = std::map<llvm::Function const *, CollSet>;

public:
  ParcoachAnalysisInter(llvm::Module &M, DepGraphDCF *DG,
                        PTACallGraph const &PTACG,
                        llvm::ModuleAnalysisManager &AM, bool EmitDotDG)
      : ParcoachAnalysis(M, DG), PTACG(PTACG), MAM(AM), EmitDotDG_(EmitDotDG) {
    id++;
  }

  virtual ~ParcoachAnalysisInter() = default;

  void run() override;
  CallToWarningMapTy const &getWarnings() { return Warnings; };
  bool useDataflow() const;

private:
  PTACallGraph const &PTACG;
  llvm::ModuleAnalysisManager &MAM;
  bool const EmitDotDG_;
  CallToWarningMapTy Warnings;

  void setCollSet(llvm::BasicBlock *BB);
  void setMPICollSet(llvm::BasicBlock *BB);
  void cmpAndUpdateMPICollSet(llvm::BasicBlock *header, llvm::BasicBlock *pred);
  void MPI_BFS_Loop(llvm::Loop *L);
  void BFS_Loop(llvm::Loop *L);
  bool isExitNode(llvm::BasicBlock *BB);
  bool mustWait(llvm::BasicBlock *bb, llvm::Loop *l = nullptr);
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
