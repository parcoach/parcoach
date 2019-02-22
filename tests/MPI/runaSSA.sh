OPT=opt
LLVM_PASS_DIR=/Users/esaillar/Documents/PARCOACH/parcoach_bitbucket/build/lib
PASSLIB=LLVMaSSA.dylib
REQUIREDPASS=-postdomtree

$OPT -load $LLVM_PASS_DIR/$PASSLIB $REQUIREDPASS -parcoach -dot-depgraph -debug < $1 > /dev/null
