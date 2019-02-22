# Don't forget to export LLVM in your path

#############################################################################################
#  LLVM Passes you could use:
#
#      -dot-cfg-only: Print CFG of function to dot file (with no function bodies)
#      -dot-cfg: Print CFG of function to dot file
#      -dot-callgraph: Print Call Graph to dot file
#      -view-dom/postdom: View dominance/postdominance tree of function 
#      -postdomtree: Post-Dominator Tree Construction
#      -debug: print debug info ( DEBUG(...) )
#
############################################################################################


# PARCOACH SPECIFIC
PARCOACH_DIR=`pwd`/../../
PARCOACH_PASS_DIR=${PARCOACH_DIR}/build/lib
PARCOACH_DYNLIB_DIR=${PARCOACH_DIR}/src/lib
PASSLIB=LLVMaSSA.dylib
REQUIREDPASS= -postdomtree
PASS=-parcoach --check-mpi --inter-only #-no-dataflow 
#  see the documentation for possible PARCOACH analyses


CFLAGS= -I/opt/local/include/mpich-mp/
LDFLAGS=
MPICC=mpicc
#-mpich-clang
#mpicc
MPIRUN=mpirun
CC=clang
CXX=clang++
OPT=opt
LLVMDIS=llvm-dis
LLVMLINK=llvm-link



# Program to test
#SRC=ex1main.c ex1func.c
SRC=phi-cond.c

EXEC=phi-cond

EXEC_OBJ= barrier_noerror \
	  inter_example \
	  inter_example_loop \
	  inter_example_mismatch \
	  inter_example_noerror \
	  mismatch_barrier \
	  mismatch_barrier_nb \
	  inter_example_false_negative \
	  inter_example_tricky1 
		


MPI_PROCS=4

LLFILES = $(SRC:.c=.ll)
LLREGFILES = $(SRC:.c=-reg.bc)
LLREGDIS = $(SRC:.c=-reg.ll)
DOTFILES = $(SRC:.c=.dot)
DOTREGFILES = $(SRC:.c=-reg.dot)



#all: $(Lib) $(LLFILES) $(LLREGFILES) $(LLREGDIS) #$(DOTFILES) $(DOTREGFILES)
all: $(LLFILES) $(LLREGFILES) $(LLREGDIS) 



Lib: $(PARCOACH_DYNLIB_DIR)/MPI_DynamicCheck.c
	$(MPICC) $(CFLAGS) -c $^ -DDEBUG


$(LLFILES) : %.ll : %.c
	$(CC) -g -S -emit-llvm $^ $(CFLAGS)

$(LLREGFILES): %-reg.bc : %.ll
	$(OPT) -mem2reg < $^ > $@

$(LLREGDIS): %-reg.ll : %-reg.bc
	$(LLVMDIS) $^
	$(OPT) -load $(PARCOACH_PASS_DIR)/$(PASSLIB) $(REQUIREDPASS) $(PASS) $^


$(DOTFILES) : %.dot : %.ll
	./runaSSA.sh $^
	mv dg.dot $@

$(DOTREGFILES) : %.dot : %.ll
	./runaSSA.sh $^
	mv dg.dot $@

%.png: %.dot
	dot -Tpng $^ -o $@	



##################  TEST

## COMPILATION WITH PARCOACH

exe: MPIcode7.c
	@echo "  COMPILING MPI_DynamicCheck.c ..."
	$(MPICC) $(CFLAGS) -c $(PARCOACH_DYNLIB_DIR)/MPI_DynamicCheck.c #-DDEBUG
	@echo "  COMPILING $@ with PARCOACH ..."
	$(CC) -g -emit-llvm -c -o MPIcode7.bc  $^ $(CFLAGS)
	$(OPT) -dot-cfg -mem2reg MPIcode7.bc
	$(OPT) -load $(PARCOACH_PASS_DIR)/$(PASSLIB) $(REQUIREDPASS) $(PASS) < MPIcode7.bc > ./MPIcode_inst.bc
	$(LLVMDIS) -o MPIcode_inst-readable.ll MPIcode_inst.bc
	@echo "===> See Parcoach modifications in MPIcode_inst-readable.ll <==="
	$(CC) -c MPIcode_inst.bc
	$(MPICC) MPIcode_inst.o MPI_DynamicCheck.o -o $@ -lmpi -Wl,-L/opt/local/lib/mpich-mp/

## COMPILATION WITHOUT PARCOACH

MPIcode1: MPIcode1.c
	$(MPICC) -o $@ $^ $(LDFLAGS)



##################  TEST WITH MULTIPLE FILES

$(REGDIS): %-reg.ll : %-reg.bc
	$(LLVMDIS) $^

merge.bc: ex1main-reg.bc ex1func-reg.bc
	$(LLVMLINK) -v $^ -o $@
	$(OPT) -load $(PARCOACH_PASS_DIR)/$(PASSLIB) $(REQUIREDPASS) $(PASS) < merge.bc > ./merge_parcoach.bc
	llvm-dis -o merge-readable.ll merge_parcoach.bc

test: $(LLFILES) $(LLREGFILES) $(REGDIS) merge.bc 
	$(MPICC) $(CFLAGS) -c $(PARCOACH_DYNLIB_DIR)/MPI_DynamicCheck.c -DDEBUG
	$(CC) -c merge_parcoach.bc
	$(MPICC) merge_parcoach.o  MPI_DynamicCheck.o -o $@ -lmpi -Wl,-L/opt/local/lib/mpich-mp/
	



##################  RUN


run:
	$(MPIRUN) -np $(MPI_PROCS) ./$(EXEC)


##################  CLEAN

clean:
	rm -rf *.o rm -rf *.ll
	rm -rf *.bc
	rm -rf *.dot
	rm -rf $(EXEC)
