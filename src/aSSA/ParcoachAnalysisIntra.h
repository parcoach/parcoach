#ifndef PARCOACHANALYSISINTRA_H
#define PARCOACHANALYSISINTRA_H

#include "ParcoachAnalysis.h"
#include <llvm/Analysis/LoopInfo.h>
#include "llvm/Pass.h"

class ParcoachAnalysisIntra : public ParcoachAnalysis {

 typedef bool Preheader;
 typedef std::map<const llvm::BasicBlock *, Preheader> BBPreheaderMap;
 BBPreheaderMap bbPreheaderMap;
 
 typedef std::string CollSet;
 enum Visited{white, grey, black};
 using ComCollMap = std::map<const llvm::Value *, CollSet>; 

 typedef std::map<const llvm::BasicBlock *, Visited> BBVisitedMap;
 typedef std::map<const llvm::BasicBlock *, ComCollMap > MPICollMap;
 typedef std::map<const llvm::BasicBlock *, CollSet> CollMap;
 

public:
  ParcoachAnalysisIntra(llvm::Module &M, DepGraph *DG,
			llvm::Pass *pass,
			bool disableInstru = false)
    : ParcoachAnalysis(M, DG, disableInstru), pass(pass) {}

  virtual ~ParcoachAnalysisIntra() {}

  virtual void run();

private:
  llvm::Pass *pass;
  llvm::LoopInfo *curLoop;

  void setCollSet(llvm::BasicBlock *BB);
  void setMPICollSet(llvm::BasicBlock *BB);
  void MPI_BFS_Loop(llvm::Function *F);
  void BFS_Loop(llvm::Function *F);
  void Tag_LoopPreheader(llvm::Loop *L);
  bool mustWait(llvm::BasicBlock *bb);
  void BFS(llvm::Function *F);
  void MPI_BFS(llvm::Function *F);
  void checkCollectives(llvm::Function *F);

  void instrumentFunction(llvm::Function *F);
  void insertCC(llvm::Instruction *I, int OP_color,std::string OP_name, int OP_line, llvm::StringRef WarningMsg,llvm::StringRef File);

  std::string getWarning(llvm::Instruction &inst);


protected:
 BBVisitedMap bbVisitedMap;
 MPICollMap mpiCollMap;
 CollMap collMap;

};

#endif /* PARCOACHANALYSISINTRA_H */
