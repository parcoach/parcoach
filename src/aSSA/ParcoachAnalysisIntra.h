#ifndef PARCOACHANALYSISINTRA_H
#define PARCOACHANALYSISINTRA_H

#include "ParcoachAnalysis.h"

class ParcoachAnalysisIntra : public ParcoachAnalysis {
public:
  ParcoachAnalysisIntra(llvm::Module &M, DepGraph *DG,
			llvm::Pass *pass,
			bool disableInstru = false)
    : ParcoachAnalysis(M, DG, disableInstru), pass(pass) {}

  virtual ~ParcoachAnalysisIntra() {}

  virtual void run();

private:
  llvm::Pass *pass;

  void BFS(llvm::Function *F);

  void checkCollectives(llvm::Function *F);

  void instrumentFunction(llvm::Function *F);

  std::string getBBcollSequence(const llvm::Instruction &inst);

  std::string getCollectivesInBB(llvm::BasicBlock *BB);

  void instrumentCC(llvm::Instruction *I, int OP_color,
		    std::string OP_name, int OP_line, llvm::StringRef WarningMsg,
		    llvm::StringRef File);

  std::string getWarning(llvm::Instruction &inst);
};

#endif /* PARCOACHANALYSISINTRA_H */
