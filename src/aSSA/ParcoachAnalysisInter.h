#ifndef PARCOACHANALYSISINTER_H
#define PARCOACHANALYSISINTER_H

#include "ParcoachAnalysis.h"
#include "PTACallGraph.h"
#include <llvm/Analysis/LoopInfo.h>

class ParcoachAnalysisInter : public ParcoachAnalysis {


 typedef bool Preheader;
 typedef std::map<const llvm::BasicBlock *, Preheader> BBPreheaderMap;
 BBPreheaderMap bbPreheaderMap;


 //typedef std::set<const llvm::Function *F> CollSet;
 typedef std::string CollSet;
// typedef bool Visited;
 enum Visited{white, grey, black};
 using ComCollMap = std::map<const llvm::Value *, CollSet>;

 typedef std::map<const llvm::BasicBlock *, Visited> BBVisitedMap;

 typedef std::map<const llvm::BasicBlock *, ComCollMap > MPICollMap;
 typedef std::map<const llvm::BasicBlock *, CollSet> CollMap;

 typedef std::map<const llvm::Function *, ComCollMap > MPICollperFuncMap;
 typedef std::map<const llvm::Function *, CollSet> CollperFuncMap;

public:
  ParcoachAnalysisInter(llvm::Module &M, DepGraph *DG, PTACallGraph &PTACG, llvm::Pass *pass, 
			bool disableInstru = false)
    : ParcoachAnalysis(M, DG, disableInstru), PTACG(PTACG), pass(pass) {
    id++;
  }

  virtual ~ParcoachAnalysisInter() {}

  virtual void run();

private:
  PTACallGraph &PTACG;
  llvm::LoopInfo *curLoop;
	llvm::Pass *pass;

	void setCollSet(llvm::BasicBlock *BB);
	void setMPICollSet(llvm::BasicBlock *BB);
	void MPI_BFS_Loop(llvm::Function *F);
	void BFS_Loop(llvm::Function *F);
  void BFS(llvm::Function *F);
  void RecBFS(std::vector<llvm::BasicBlock *> Unvisited);
  void InitRecBFS(llvm::Function *F);
  void MPI_BFS(llvm::Function *F);
  void checkCollectives(llvm::Function *F);

  void instrumentFunction(llvm::Function *F);
  void insertCC(llvm::Instruction *I, int OP_color,
		std::string OP_name, int OP_line, llvm::StringRef WarningMsg,
		llvm::StringRef File);

  std::string getWarning(llvm::Instruction &inst);

  static int id;


protected:
 BBVisitedMap bbVisitedMap;
 MPICollMap mpiCollMap;
 CollMap collMap;
 MPICollperFuncMap mpiCollperFuncMap;
 CollperFuncMap collperFuncMap;
};

#endif /* PARCOACHANALYSISINTER_H */
