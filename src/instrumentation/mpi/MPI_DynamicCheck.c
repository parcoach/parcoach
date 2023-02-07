#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "mpi.h"

int nbCC = 0;
int nbCollI = 0;
int nbColl = 0;

/* the user-defined function for the new operator */
void areequals(int *in, int *inout, int *len, MPI_Datatype *type) {
  int i;
  for (i = 0; i < *len; i++) {
    if (*inout != *in) {
      // FIXME: restore with NDEBUG
      /*printf("%d called with %d\n", *inout, *in);*/
      *inout = ~0;
    }
    in++;
    inout++;
  }
}

// Count collectives at execution time
void count_collectives(const char *OP_name, int OP_line, char *FILE_name,
                       int inst) {
  int rank;

  int flag;
  MPI_Initialized(&flag);

  if (flag) {
    MPI_Comm ini_comm = MPI_COMM_WORLD;
    MPI_Comm_rank(ini_comm, &rank);

    if (inst == 1) {
      nbCollI++;
      printf("P%d: collinst=%d (%s - %s - %d)\n", rank, nbCollI, FILE_name,
             OP_name, OP_line);
    }
    nbColl++;
    if (rank == 0)
      printf("P%d: coll=%d; collinst=%d (%s - %s - %d)\n", rank, nbColl,
             nbCollI, FILE_name, OP_name, OP_line);
  }
}

/* Check Collective MPI Function
 *
 *  color = type of collective (unique per collective)
 *  pcom = communicator
 *  OP_name = collective name
 *  OP_line = line in the source code of the collective
 *  warnings = warnings emitted at compile-time
 *  FILE_name = name of the file
 */
void check_collective_MPI(int OP_color, const char *OP_name, int OP_line,
                          char *warnings, char *FILE_name) {
  int rank;
  int sizeComm;

  // make sure MPI_Init has been called
  int flag;
  MPI_Initialized(&flag);

  if (flag) {
    nbCC++;
    // Fortran programs are not handled
    MPI_Comm ini_comm = MPI_COMM_WORLD;

    MPI_Comm_rank(ini_comm, &rank);
    MPI_Comm_size(ini_comm, &sizeComm);

    if (rank == 0)
      printf("nbCC=%d\n", nbCC);

    int res = 0;
    MPI_Op equalsop;
    int commutatif = 1;
    MPI_Op_create((void *)areequals, commutatif, &equalsop);

    MPI_Reduce(&OP_color, &res, 1, MPI_INT, equalsop, 0, ini_comm);
    MPI_Op_free(&equalsop);

#ifndef NDEBUG
    printf("Proc %d has color %d\n", rank, OP_color);
#endif
    if (rank == 0) {
#ifndef NDEBUG
      printf("CHECK CC OK\n");
#endif
      if (res == ~0) {
#ifndef NDEBUG
        printf("CHECK CC NOK\n");
#endif
        printf("PARCOACH DYNAMIC-CHECK : Error detected on rank %d\n"
               "PARCOACH DYNAMIC-CHECK : Abort is invoking line %d before "
               "calling %s in %s\n"
               "PARCOACH DYNAMIC-CHECK : see warnings about conditionals line "
               "%s\n",
               rank, OP_line, OP_name, FILE_name, warnings);
        MPI_Abort(MPI_COMM_WORLD, 0);
      }
    }
#ifndef NDEBUG
    printf("DYNAMIC-CHECK : OK\n");
#endif
  }
}

void check_collective_return(int OP_color, const char *OP_name, int OP_line,
                             char *warnings, char *FILE_name) {
  int rank;
  int sizeComm;

  // make sure MPI_Init has been called
  int flagend, flagstart;
  MPI_Initialized(&flagstart);
  MPI_Finalized(&flagend);

  if (!flagend && flagstart) {
    // Fortran programs are not handled
    MPI_Comm ini_comm = MPI_COMM_WORLD;

    MPI_Comm_rank(ini_comm, &rank);
    MPI_Comm_size(ini_comm, &sizeComm);

    int res = 0;
    MPI_Op equalsop;
    int commutatif = 1;
    MPI_Op_create((void *)areequals, commutatif, &equalsop);

    MPI_Reduce(&OP_color, &res, 1, MPI_INT, equalsop, 0, ini_comm);
    MPI_Op_free(&equalsop);

#ifndef NDEBUG
    printf(" Proc %d has color %d\n", rank, OP_color);
#endif
    if (rank == 0) {
#ifndef NDEBUG
      printf(" CHECK CC OK\n");
#endif
      if (res == ~0) {
#ifndef NDEBUG
        printf(" CHECK CC NOK\n");
#endif
        printf("PARCOACH DYNAMIC-CHECK : Error detected on rank %d\n"
               "PARCOACH DYNAMIC-CHECK : Abort is invoking line %d before "
               "calling %s in %s\n"
               "PARCOACH DYNAMIC-CHECK : see warnings %s\n",
               rank, OP_line, OP_name, FILE_name, warnings);
        MPI_Abort(MPI_COMM_WORLD, 0);
      }
    }
#ifndef NDEBUG
    printf("DYNAMIC-CHECK : OK\n");
#endif
  }
}
