#include "globals.h"


#include <map>
#include <llvm/IR/Function.h>
#include "WordsFunction.h"

std::map< llvm::Function *, WordsFunction > fun2set;