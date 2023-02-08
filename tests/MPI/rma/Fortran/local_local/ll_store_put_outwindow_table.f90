! REQUIRES: fortran
! RUN: %mpifort -g -S -emit-llvm %s -o %t.ll
! RUN: %parcoach -check=rma %t.ll 2>&1 | %filecheck %s
! CHECK-NOT: LocalConcurrency detected
PROGRAM main
include 'mpif.h'

   INTEGER, ALLOCATABLE, SAVE, DIMENSION(:)    :: window_buffer
   REAL, ALLOCATABLE, SAVE, DIMENSION(:,:,:,:) :: t3sn
   INTEGER                                     :: myrank, comm_size, ierr, my_value, jpi, jprecj, jpk, imigr
   INTEGER (KIND=MPI_ADDRESS_KIND)             :: disp_int, window
   INTEGER (KIND=MPI_ADDRESS_KIND)             :: lowerbound, size_window, realextent, deplacement

   deplacement = 0

   call MPI_init_thread( MPI_THREAD_MULTIPLE, provided, ierr )

   CALL mpi_comm_rank( MPI_COMM_WORLD, myrank, ierr )
   CALL mpi_comm_size( MPI_COMM_WORLD, comm_size, ierr )

   IF (comm_size .ne. 3) THEN
    print *, "This application is meant to be run with 3 MPI processes, not ",comm_size
    call MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE, ierr);
   END IF

   jprecj = 1
   jpi = 1
   jpk = 0
   ALLOCATE( t3sn(jpi,jprecj,jpk+1,2))
   t3sn(1,1,1,1) = 1234
   t3sn(1,1,1,2) = 4321

   call MPI_TYPE_GET_EXTENT(MPI_real, lowerbound, realextent, ierr)
   IF( ierr /= MPI_SUCCESS) THEN
      print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
   END IF

   disp_int = realextent
   size_window = jprecj*(jpk+1)*jpi*realextent

   call MPI_WIN_CREATE(t3sn(1,1,1,2), size_window, disp_int, MPI_INFO_NULL, MPI_COMM_WORLD, window, ierr)
   IF( ierr /= MPI_SUCCESS) THEN
      print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
   END IF

   call MPI_Win_lock_all(0, window, ierr)
   IF( ierr /= MPI_SUCCESS) THEN
      print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
   END IF


  imigr = jprecj * jpi * (jpk+1)
  if (myrank == 0) then
      CALL mpi_put(t3sn(1,1,1,1), imigr, mpi_real, 1, deplacement, imigr, mpi_real, window, ierr)
      ! print *, "Put from ",myrank," to 1"
      IF( ierr /= MPI_SUCCESS) THEN
         print *, "MPI_ERROR line:", 0 , ", rank: ", myrank
      END IF
  end if

  call mpi_win_unlock_all(window, ierr)

  call mpi_win_free(window, ierr)

  if (myrank == 1) then
     print *, "Value received: ",t3sn(1,1,1,2)
  end if

  DEALLOCATE(t3sn)

  if (myrank == 0) then
     print *, "Test finished successfully"
  end if

  CALL mpi_finalize(ierr)

END PROGRAM main
