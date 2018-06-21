#ifndef PARCOACHANALYSISINTER_H
#define PARCOACHANALYSISINTER_H

#include "ParcoachAnalysis.h"
#include "PTACallGraph.h"


class ParcoachAnalysisInter : public ParcoachAnalysis {

 //typedef std::set<const llvm::Function *F> CollSet;
 typedef std::string CollSet;
 typedef bool Visited;
 using ComCollMap = std::map<const llvm::Value *, CollSet>;

 typedef std::map<const llvm::BasicBlock *, Visited> BBVisitedMap;

 typedef std::map<const llvm::BasicBlock *, ComCollMap > MPICollMap;
 typedef std::map<const llvm::BasicBlock *, CollSet> CollMap;

 typedef std::map<const llvm::Function *, ComCollMap > MPICollperFuncMap;
 typedef std::map<const llvm::Function *, CollSet> CollperFuncMap;

public:
  ParcoachAnalysisInter(llvm::Module &M, DepGraph *DG, PTACallGraph &PTACG,
			bool disableInstru = false)
    : ParcoachAnalysis(M, DG, disableInstru), PTACG(PTACG) {
    id++;
  }

  virtual ~ParcoachAnalysisInter() {}

  virtual void run();


private:
  PTACallGraph &PTACG;

	void setCollSet(llvm::BasicBlock *BB);
	void setMPICollSet(llvm::BasicBlock *BB);

  void BFS(llvm::Function *F);
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
