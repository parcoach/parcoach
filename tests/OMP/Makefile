# Don't forget to export LLVM in your path

#############################################################################################
#  LLVM Passes you could use:
#
#      -dot-cfg-only: Print CFG of function to dot file (with no function bodies)
#      -dot-cfg: Print CFG of function to dot file
#      -dot-callgraph: Print Call Graph to dot file
#      -view-dom/postdom: View dominance/postdominance tree of function
#      -postdomtree: Post-Dominator Tree Construction
#
############################################################################################


# OpenMP SPECIFIC
OMP_DIR=$(HOME)/Documents/TOOLS/LLVM_391/build
OMP_INC=$(OMP_DIR)/projects/openmp/runtime/src
#OMP_LIB=/Users/emmanuellesaillard/Documents/LLVM_390/build/lib
OMP_LIB=$(OMP_DIR)/lib/
#/opt/local/lib/libomp/

CFLAGS= -fopenmp=libomp -I$(OMP_INC)
LDFLAGS= -fopenmp=libomp -I$(OMP_INC) -L $(OMP_LIB) -Wl,-rpath,$(OMP_LIB)/libomp.dylib


# PARCOACH SPECIFIC
PARCOACH_DIR=../../
PARCOACH_BUILD_DIR=$(PARCOACH_DIR)/build
PARCOACH_PASS_DIR=${PARCOACH_DIR}/build/lib
PARCOACH_DYNLIB_DIR=${PARCOACH_DIR}/src/lib
PASSLIB=LLVMaSSA.dylib
REQUIREDPASS= -postdomtree -loweratomic
PASS=-parcoach --check-omp

CC=clang
CXX=clang++
OPT=$(PARCOACH_BUILD_DIR)/parcoach
LLVMDIS=llvm-dis
LLVMLINK=llvm-link


# Program to test
SRC=motivating_example.c loop_barrier.c loop_barrier2.c
EXEC=motivating_example

LLFILES = $(SRC:.c=.ll)
LLREGFILES = $(SRC:.c=-reg.bc)
LLREGDIS = $(SRC:.c=-reg.ll)
DOTFILES = $(SRC:.c=.dot)
DOTREGFILES = $(SRC:.c=-reg.dot)
EXECREGFILES = $(SRC:.c=-reg.o)

#all: $(EXEC)
#all: $(LLFILES) $(LLREGFILES) $(LLREGDIS) $(DOTFILES) $(DOTREGFILES)
all: OMP_DynamicCheck.o $(LLFILES) $(LLREGFILES) $(LLREGDIS) $(EXECREGFILES)



OMP_DynamicCheck.o: $(PARCOACH_DYNLIB_DIR)/OMP_DynamicCheck.c
	$(CC) $(CFLAGS) -c $^ -DDEBUG



################## PARCOACH INTER - To use with: PASSLIB=LLVMaSSA.dylib

$(LLFILES) : %.ll : %.c
	$(CC) -g -S -emit-llvm $^ $(CFLAGS)

$(LLREGFILES): %-reg.bc : %.ll
	$(OPT) -mem2reg < $^ > $@

$(EXECREGFILES): %-reg.o : %-reg.bc
	$(CC) -c $^
	$(CC) $@ OMP_DynamicCheck.o $(LDFLAGS)


$(LLREGDIS): %-reg.ll : %-reg.bc
	$(OPT) -load $(PARCOACH_PASS_DIR)/$(PASSLIB) $(REQUIREDPASS) $(PASS) $^
	$(LLVMDIS) $^


$(DOTFILES) : %.dot : %.ll
	./runaSSA.sh $^
	mv dg.dot $@

$(DOTREGFILES) : %.dot : %.ll
	./runaSSA.sh $^
	mv dg.dot $@

%.png: %.dot



##################  TEST

## COMPILATION WITH PARCOACH

mytest_noerror: test_noerror.c
	@echo "   COMPILING OMP_DynamicCheck.c ..."
	$(CC) $(CFLAGS) -c $(PARCOACH_DYNLIB_DIR)/OMP_DynamicCheck.c
	@echo "   COMPILING $@ with PARCOACH ..."
	$(CC) -g -S -emit-llvm -c -o test_noerror.bc $^ $(CFLAGS)
	$(OPT) -mem2reg < test_noerror.bc > test_noerror-reg.bc
	$(OPT) -load $(PARCOACH_PASS_DIR)/$(PASSLIB) $(REQUIREDPASS) $(PASS) < test_noerror-reg.bc > ./test_noerror_optimized.bc
	$(LLVMDIS) -o test_noerror_optimized-readable.ll test_noerror_optimized.bc
	 @echo "===> See Parcoach modifications in test_noerror_optimized-readable.ll <==="
	$(CC) -c test_noerror_optimized.bc
	$(CC) test_noerror_optimized.o OMP_DynamicCheck.o $(LDFLAGS)


## COMPILATION WITHOUT PARCOACH

test_noerror: test_noerror.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

motivating_example_sections: motivating_example_sections.c
	$(CC) -g -S -emit-llvm -c -o motivating_example_sections.bc $^ $(CFLAGS)
	$(OPT) -dot-cfg  motivating_example_sections.bc

##################  RUN

run:
	./$(EXEC)


##################  CLEAN

clean:
	rm -rf *.o rm -rf *.ll
	rm -rf *.bc
	rm -rf *.dot
	rm -rf $(EXEC)
