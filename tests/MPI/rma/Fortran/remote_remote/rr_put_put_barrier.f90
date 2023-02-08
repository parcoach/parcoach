! REQUIRES: fortran
! TODO: check flush instr, may be an issue
! RUN: %mpifort -g -S -emit-llvm %s -o %t.ll
! RUN: %parcoach -check=rma %t.ll 2>&1 | %filecheck %s
! CHECK-NOT: LocalConcurrency detected
PROGRAM main
include 'mpif.h'

   INTEGER, ALLOCATABLE, SAVE, DIMENSION(:)    :: window_buffer
   INTEGER                                     :: myrank, comm_size, ierr, my_value

   INTEGER (KIND=MPI_ADDRESS_KIND)             :: disp_int, window
   INTEGER (KIND=MPI_ADDRESS_KIND)             :: lowerbound, size_window, intextent, deplacement

   deplacement = 0

   call MPI_init_thread( MPI_THREAD_MULTIPLE, provided, ierr )

   CALL mpi_comm_rank( MPI_COMM_WORLD, myrank, ierr )
   CALL mpi_comm_size( MPI_COMM_WORLD, comm_size, ierr )

   IF (comm_size .ne. 3) THEN
    print *, "This application is meant to be run with 3 MPI processes, not ",comm_size
    call MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE, ierr);
   END IF

   if (myrank == 0) then
      my_value = 12345
   end if
   if (myrank == 2) then
      my_value = 23456
   end if

   ALLOCATE(window_buffer(100))
   DO i = 1, 100
      window_buffer(i) = 0
   END DO

   call MPI_TYPE_GET_EXTENT(MPI_int, lowerbound, intextent, ierr)
   IF( ierr /= MPI_SUCCESS) THEN
      print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
   END IF

   disp_int = intextent
   size_window = 100 * intextent

   call MPI_WIN_CREATE(window_buffer(1), size_window, disp_int, MPI_INFO_NULL, MPI_COMM_WORLD, window, ierr)
   IF( ierr /= MPI_SUCCESS) THEN
      print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
   END IF

   call MPI_Win_lock_all(0, window, ierr)
   IF( ierr /= MPI_SUCCESS) THEN
      print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
   END IF

  if (myrank == 0) then
      CALL mpi_put(my_value, 1, mpi_int, 1, deplacement, 1, mpi_int, window, ierr)
      ! print *, "Put from ",myrank," to 1"
      IF( ierr /= MPI_SUCCESS) THEN
         print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
      END IF
  end if

  call mpi_barrier(MPI_COMM_WORLD, ierr)
  IF( ierr /= MPI_SUCCESS) THEN
     print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
  END IF

  if (myrank == 2) then
      CALL mpi_put(my_value, 1, mpi_int, 1, deplacement, 1, mpi_int, window, ierr)
      ! print *, "Put from ",myrank," to 1"
      IF( ierr /= MPI_SUCCESS) THEN
         print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
      END IF
  end if

  call mpi_win_unlock_all(window, ierr)

  call mpi_win_free(window, ierr)

  if (myrank == 1) then
     print *, "Value received: ",window_buffer(1)
  end if

  DEALLOCATE(window_buffer)

  if (myrank == 0) then
     print *, "Test finished successfully"
  end if

  CALL mpi_finalize(ierr)

END PROGRAM main
