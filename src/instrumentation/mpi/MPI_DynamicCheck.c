#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "mpi.h"

int NbCc = 0;
int NbCollI = 0;
int NbColl = 0;

/* the user-defined function for the new operator */
void areequals(int *In, int *Inout, int const *Len, MPI_Datatype *Type) {
  int I;
  for (I = 0; I < *Len; I++) {
#ifndef NDEBUG
    // printf("Color %d called with color %d\n", *Inout, *In);
#endif
    if (*Inout != *In) {
      *Inout = ~0;
    }
    In++;
    Inout++;
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
// NOLINTNEXTLINE
void check_collective_MPI(int OP_color, char const *OP_name, int OP_line,
                          char *Warnings, char *FileName) {
  int Rank;
  int SizeComm;

  // make sure MPI_Init has been called
  int Flag;
  MPI_Initialized(&Flag);

  if (Flag) {
    NbCc++;
    // Fortran programs are not handled
    MPI_Comm IniComm = MPI_COMM_WORLD;

    MPI_Comm_rank(IniComm, &Rank);
    MPI_Comm_size(IniComm, &SizeComm);

    int Res = 0;
    MPI_Op Equalsop;
    int Commutatif = 1;
    MPI_Op_create((void *)areequals, Commutatif, &Equalsop);

    MPI_Reduce(&OP_color, &Res, 1, MPI_INT, Equalsop, 0, IniComm);
    MPI_Op_free(&Equalsop);

    if (Rank == 0) {
      if (Res == ~0) {
        printf("PARCOACH DYNAMIC-CHECK : Error detected on rank %d\n"
               "PARCOACH DYNAMIC-CHECK : Abort is invoking line %d before "
               "calling %s in %s\n"
               "PARCOACH DYNAMIC-CHECK : see warnings about conditionals line "
               "%s\n",
               Rank, OP_line, OP_name, FileName, Warnings);
        MPI_Abort(MPI_COMM_WORLD, 0);
      } else {
        printf("PARCOACH DYNAMIC-CHECK : OK\n");
      }
    }
  }
}

// NOLINTNEXTLINE
void check_collective_return(int OP_color, char const *OP_name, int OP_line,
                             char *Warnings, char *FileName) {
  int Rank;
  int SizeComm;

  // make sure MPI_Init has been called
  int flagend;
  int flagstart;
  MPI_Initialized(&flagstart);
  MPI_Finalized(&flagend);

  if (!flagend && flagstart) {
    // Fortran programs are not handled
    MPI_Comm IniComm = MPI_COMM_WORLD;

    MPI_Comm_rank(IniComm, &Rank);
    MPI_Comm_size(IniComm, &SizeComm);

    int Res = 0;
    MPI_Op Equalsop;
    int Commutatif = 1;
    MPI_Op_create((void *)areequals, Commutatif, &Equalsop);

    MPI_Reduce(&OP_color, &Res, 1, MPI_INT, Equalsop, 0, IniComm);
    MPI_Op_free(&Equalsop);

    if (Rank == 0) {
      if (Res == ~0) {
        printf("PARCOACH DYNAMIC-CHECK : Error detected on rank %d\n"
               "PARCOACH DYNAMIC-CHECK : Abort is invoking line %d before "
               "calling %s in %s\n"
               "PARCOACH DYNAMIC-CHECK : see warnings %s\n",
               Rank, OP_line, OP_name, FileName, Warnings);
        MPI_Abort(MPI_COMM_WORLD, 0);
      } else {
        printf("PARCOACH DYNAMIC-CHECK : OK\n");
      }
    }
  }
}
