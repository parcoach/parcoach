#ifndef COLLECTIVES_H
#define COLLECTIVES_H

#include "llvm/IR/Module.h"

#include <vector>

extern std::vector<const char*> v_coll;
extern std::vector<const char*> MPI_v_coll;
extern std::vector<const char*> OMP_v_coll;
extern std::vector<const char*> UPC_v_coll;

int isMPIcollective(const llvm::Function *F);
int isCollective(const llvm::Function *F);

#endif /* COLLECTIVES_H */
