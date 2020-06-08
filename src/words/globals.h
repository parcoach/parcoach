#ifndef GLOBALS_H
#define GLOBALS_H

#include <map>

#include <llvm/IR/Function.h>
#include "WordsFunction.h"

extern std::map<llvm::Function *, WordsFunction> fun2set;

#endif//GLOBALS_H