#ifndef ASSA_H
#define ASSA_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"

#include <map>
#include <set>

typedef std::map<llvm::PHINode *, std::set<const llvm::Value *> *> ASSA;

#endif /* ASSA_H */
