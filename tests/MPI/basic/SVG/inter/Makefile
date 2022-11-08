# Don't forget to export LLVM 3.7.1
# ## ex.: export PATH=/Users/emmanuellesaillard/Documents/LLVM_371/build/bin:$PATH
#


#############################################################################################
#  LLVM Passes you can use:
#
#      -dot-cfg-only: Print CFG of function to dot file (with no function bodies)
#      -dot-cfg: Print CFG of function to dot file
#      -dot-callgraph: Print Call Graph to dot file
#      -view-dom/postdom: View dominance/postdominance tree of function
#      -postdomtree: Post-Dominator Tree Construction
#      -debug: print debug info ( DEBUG(...) )
#
############################################################################################



## CHANGE THE PATHS
LLVM_PASS_DIR=/home/phuchant/parcoach/build/lib
LIB_DIR=/home/phuchant/parcoach/src/lib
PASSLIB=LLVMParcoachInter.so

CFLAGS=-I/opt/mpich/include/
LDFLAGS=

MPICC=/opt/mpich/bin/mpicc
MPIRUN=/opt/mpich/bin/mpirun

CC=/home/phuchant/mytools/llvm-3.7/bin/clang
CXX=/home/phuchant/mytools/llvm-3.7/bin/clang++

OPT=/home/phuchant/mytools/llvm-3.7/bin/opt
LLVMDIS=/home/phuchant/mytools/llvm-3.7/bin/llvm-dis

EXEC = barrier_noerror \
	intra_flow \
	intra_struct \
	intra_linkedlist \
	intra_loop \
	intra_memtransfer \
	inter_ret \
	inter_arg \
	inter_arg2 \
	inter_sideeffect

EXECPARCOACH = $(EXEC:%=%_parcoach)

REQUIREDPASS= -postdomtree -tbaa -scev-aa -basicaa -cfl-aa

PROCS=4

all: $(EXECPARCOACH)

$(EXEC) : % : %.c
	$(MPICC) -o $@ $^ $(LDFLAGS)

$(EXECPARCOACH): %_parcoach : %.c
	$(MPICC) $(CFLAGS) -c $(LIB_DIR)/MPI_DynamicCheck.c
	$(CC) -g -emit-llvm -c -o $^.bc $^ $(CFLAGS)
	$(LLVMDIS) -o $^.ll $^.bc
	$(OPT) -load $(LLVM_PASS_DIR)/$(PASSLIB) $(REQUIREDPASS) -parcoach -debug < $^.bc > ./$@_optimized.bc
	$(LLVMDIS) -o $@_optimized-readable.ll $@_optimized.bc
	@echo "===> See Parcoach modifications in $@_optimized-readable.ll <==="
	$(CC) -c $@_optimized.bc
	$(MPICC) $@_optimized.o MPI_DynamicCheck.o -o $@ -lmpi #-Wl,-L/opt/local/lib/mpich-mp/ #-lstdc++

run:
	$(MPIRUN) -np $(PROCS) ./$(EXEC)


%.png: %.dot
	dot -Tpng $^ -o $@

clean:
	rm -rf *.o rm -rf *.ll
	rm -rf *.bc
	rm -rf *.dot
	rm -rf *~
	rm -rf $(EXEC) $(EXECPARCOACH)
