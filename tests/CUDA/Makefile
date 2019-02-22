include Makefile.inc

SRC=$(wildcard *.cu)

LLFILES= $(SRC:.cu=.ll)

all: $(LLFILES)

$(LLFILES) : %.ll : %.cu
	$(CLANGPATH) -S -emit-llvm -g --cuda-path=$(CUDAPATH) $^

.PHONY: test clean

test : $(LLFILES)
	@for i in *-device*.ll ; do \
		$(OPTPATH) -load $(PARCOACHLIBPATH) -parcoach -check-cuda -strong-update < $$i ; \
	done

clean :
	rm -f *.ll *~

