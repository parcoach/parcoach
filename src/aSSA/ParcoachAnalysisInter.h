#ifndef PARCOACHANALYSISINTER_H
#define PARCOACHANALYSISINTER_H

#include "ParcoachAnalysis.h"
#include "PTACallGraph.h"

class ParcoachAnalysisInter : public ParcoachAnalysis {
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

  void BFS(llvm::Function *F);

  void checkCollectives(llvm::Function *F);

  void instrumentFunction(llvm::Function *F);

  std::string getFuncSummary(llvm::Function &F);

  std::string getBBcollSequence(const llvm::Instruction &inst);

  std::string getCollectivesInBB(llvm::BasicBlock *BB);

  void insertCC(llvm::Instruction *I, int OP_color,
		std::string OP_name, int OP_line, llvm::StringRef WarningMsg,
		llvm::StringRef File);

  std::string getWarning(llvm::Instruction &inst);

  static int id;
};

#endif /* PARCOACHANALYSISINTER_H */
