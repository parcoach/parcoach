// RUN: %mpicc -g -S -emit-llvm %s -fno-builtin -o %t_libc.ll
// RUN: %parcoach -check-mpi -disable-output %t_libc.ll 2>&1 | %filecheck %s
// CHECK: warning: MPI_Reduce line {{[0-9]+}} possibly not called by all processes because of conditional(s) line(s)  {{[0-9]+}}
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// This is a very simple example code using some libc functions.
// At the moment it's mostly meant to check the analysis works fine with such
// external functions, and make sure the corresponding lines are covered.
extern char *tab;
extern size_t tabsize;

void parcoach_var_mod(const char *fmt, ...);

void g(int s, char *tabcopy) {
  int res = 0;
  int i = 12;

  memcpy(tabcopy + i, tab, i);
  parcoach_var_mod("%s %s %i", tabcopy, tabcopy, i);

  if (s > 256)
    MPI_Reduce(&i, &res, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
}

void f() {
  int s, r, n;

  char *tabcopy = (char *)malloc(tabsize * sizeof(char));
  memcpy(tabcopy, tab, tabsize * sizeof(char));

  MPI_Comm_rank(MPI_COMM_WORLD, &r);
  MPI_Comm_size(MPI_COMM_WORLD, &s);

  if (r % 2)
    n = 1;
  else
    n = 2;

  if (n == 1)
    g(s, tabcopy);

  MPI_Barrier(MPI_COMM_WORLD);
}

int main(int argc, char **argv) {

  MPI_Init(&argc, &argv);
  f();
  MPI_Finalize();

  return 0;
}
