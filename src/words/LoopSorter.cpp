#include <vector>
#include <llvm/Analysis/LoopInfo.h>
#include <algorithm>
#include "LoopSorter.h"

using namespace std;
using namespace llvm;

bool compare_loop(Loop *left, Loop *right) {
    return left -> getLoopDepth() > right -> getLoopDepth();
}