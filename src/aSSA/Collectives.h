#ifndef COLLECTIVES_H
#define COLLECTIVES_H

#include "llvm/IR/Function.h"

#include <vector>

extern std::vector<const char*> MPI_v_coll;
bool isCollective(const llvm::Function *F);

#endif /* COLLECTIVES_H */
