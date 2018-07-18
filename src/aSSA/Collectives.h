#ifndef COLLECTIVES_H
#define COLLECTIVES_H

#include "llvm/IR/Function.h"

#include <vector>

extern std::vector<const char*> v_coll;

int Com_arg_id(int color);
void initCollectives();
bool isCollective(const llvm::Function *F);
int getCollectiveColor(const llvm::Function *F);
bool isMpiCollective(int color);
bool isOmpCollective(int color);
bool isUpcCollective(int color);
bool isCudaCollective(int color);

#endif /* COLLECTIVES_H */
