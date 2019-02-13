#include "ExtInfo.h"
#include "Utils.h"

#include "llvm/IR/Module.h"

using namespace llvm;
using namespace std;

struct funcModPair {
  const char *name;
  const extModInfo modInfo;
};

static const funcModPair funcModPairs[] = {
  // {"func_name", { <nb_params>, <retval_is_pointer>, { <param_1_is_modified_pointer, ..., <param_n-1_is_modified_pointer> } } }

  // MILC HACK
  {"myrand", { 1, false, {false} } },

  /* LLVM intrinsics */
  {"llvm.bswap.v4i32", { 1, false, {false} } },
  {"llvm.bswap.i32", { 1, false, {false} } },
  {"llvm.fabs.v2f64", { 1, false, {false} } },
  {"llvm.lifetime.end", { 2, false, {false, false} } },
  {"llvm.lifetime.start", { 2, false, {false, false} } },
  {"llvm.memcpy.p0i8.p0i8.i32", { 5, false, {true, false, false, false, false}
    } },
  {"llvm.memcpy.p0i8.p0i8.i64", { 5, false, {true, false, false, false, false}
    } },
  {"llvm.memmove.p0i8.p0i8.i32", { 5, false, {true, true, false, false, false}
    } },
  {"llvm.memmove.p0i8.p0i8.i64", { 5, false, {true, true, false, false, false}
    } },
  {"llvm.memset.p0i8.i32", { 5, false, {true, false, false, false, false} } },
  {"llvm.memset.p0i8.i64", { 5, false, {true, false, false, false, false} } },
  {"llvm.minnum.v2f64", { 2, false, {false, false} } },
  {"llvm.sqrt.f64", { 1, false, {false} } },
  {"llvm.stackrestore", { 1, false, {false} } },
  {"llvm.stacksave", { 0, true, {} } },
  {"llvm.trap", { 0, false, {} } },
  {"llvm.va_start", { 1, false, {true} } },
  {"llvm.va_end", { 1, false, {true} } },
  {"llvm.x86.sse2.mfence", {0 ,false , {} } },
  {"llvm.prefetch", {4 ,false , {true, false,false,false} } },

  /* libc */
  {"abort", { 0, false, {} } },
  {"abs", { 1, false, {false} } },
  {"acos", { 1, false, {false} } },
  {"access", { 2, false, {false, false} } },
  {"asprintf", { 3, false, {true, false, false} } },
  {"atan", { 1, false, {false} } },
  {"atanf", { 1, false, {false} } },
  {"atan2", { 2, false, {false, false} } },
  {"atof", { 1, false, {false} } },
  {"atoi", { 1, false, {false} } },
  {"atol", { 1, false, {false} } },
  {"asctime", { 1, true, {false} } },
  {"calloc", { 2, true, {false, false} } },
  {"ceil", { 1, false, {false} } },
  {"clock", { 0, false, {} } },
  {"clock_gettime", { 2, false, {false, true} } },
  {"close", { 1, false, {false} } },
  {"ctime", { 1, true, {false} } },
  {"cos", { 1, false, {false} } },
  {"cosh", { 1, false, {false} } },
  {"erfc", { 1, false, {false} } },
  {"exit", { 1, false, {false} } },
  {"exp", { 1, false, {false} } },
  {"fabs", { 1, false, {false} } },
  {"fabsf", { 1, false, {false} } },
  {"fclose", { 1, false, {true} } },
  {"feof", { 1, false, {false} } },
  {"fflush", { 1, false, {false} } },
  {"fgetc", { 1, false, {false} } },
  {"fgets", { 3, true, {true, false, false} } },
  {"fgetpos", { 2, false, {false, true} } },
  {"floor", { 1, false, {false} } },
  {"fmax", { 2, false, {false, false} } },
  {"fmin", { 2, false, {false, false} } },
  {"fopen", { 2, true, {false, false} } },
  {"fprintf", {3, false, {true, false, false} } },
  {"fputc", { 2, false, {false, true} } },
  {"fputs", { 2, false, {false, false} } },
  {"fread", { 4, false, {true, false, false, true} } },
  {"free", { 1, false, {true} } },
  {"freopen", { 3, true, {false, false, false} } },
  {"fseeko", { 3, false, {false, false, false} } },
  {"fsetpos", { 2, false, {false, false} } },
  {"fsync", { 1, false, {false} } },
  {"fwrite", { 4, false, {false, false, false, true} } },
  {"getchar", { 0, false, {} } },
  {"getenv", { 1, true, {false} } },
  {"gethostname", { 2, false, {true, false} } },
  {"getopt", { 3, false, {false, false, false} } },
  {"getopt_long", { 5, false, {false, false, false,false, true} } },
  {"getpagesize", { 0, false, {} } },
  {"getrusage", { 2, false, {false, true} } },
  {"gettimeofday", { 2, false, {true, true} } },
  {"gmtime", { 1, true, {false} } },
  {"hypot", { 2, false, {false, false} } },
  {"ldexp", {2, false, {false, false} } },
  {"localtime", {1, true, {false} } },
  {"log", {1, false, {false} } },
  {"malloc", { 1, true, {false} } },
  {"mkdir", { 2, false, {false, false} } },
  {"move_pages", { 6, false, {false, false, false, false, true, false} } },
  {"numa_num_configured_nodes", { 0, false, {} } },
  {"pow", { 2, false, {false, false} } },
  {"printf", { 2, false, {false, false} } },
  {"putchar", { 1, false, {false} } },
  {"putenv", { 1, false, {true} } },
  {"puts", { 1, false, {false} } },
  {"qsort", { 4, false, {true, false, false, false} } },
  {"rand", { 0, false, {} } },
  {"random", { 0, false, {} } },
  {"read", { 3, false, {false, true, false} } },
  {"realloc", { 2, true, {true, false} } },
  {"realpath", { 2, true, {false, true} } },
  {"regcomp", { 3,false, {true, false, false} } },
  {"regexec", { 5,false, {false, false, false, false, false} } },
  {"regfree", { 1,false, {true} } },
  {"rewind", { 1, false, {false} } },
  {"signal", { 2, false, {false} } },
  {"sin", { 1, false, {false} } },
  {"sleep", { 1, false, {false} } },
  {"snprintf", { 4, false, {true, false, false, false} } },
  {"sprintf", { 3, false, {true, false, false} } },
  {"sqrt", { 1, false, {false} } },
  {"sqrtf", { 1, false, {false} } },
  {"srand", { 1, false, {false} } },
  {"srandom", { 1, false, {false} } },
  {"strcasecmp", { 2, false, {false, false} } },
  {"strcat", { 2, true, {true, false} } },
  {"strchr", { 2, true, {false, false} } },
  {"strcmp", { 2, false, {false, false} } },
  {"strcpy", { 2, true, {true, false} } },
  {"strcspn", { 2, false, {false, false} } },
  {"strftime", { 4, false, {true, false, false, false} } },
  {"strerror", { 1, true, {false} } },
  {"strlen", { 1, false, {false} } },
  {"strncasecmp", { 3, false, {false, false, false} } },
  {"strncat", { 3, true, {true, false, false} } },
  {"strncmp", { 3, false, {false, false, false} } },
  {"strncpy", { 3, true, {true, false, false} } },
  {"strpbrk", { 2, true, {false, false} } },
  {"strrchr", { 2, true, {false, false} } },
  {"strspn", { 2, false, {false, false} } },
  {"strstr", { 2, true, {false, false} } },
  {"strtod", { 2, false, {false, true} } },
  {"strtok", { 2, true, {true, false} } },
  {"strtol", { 3, false, {false, true, false} } },
  {"system", { 1, false, {false} } },
  {"tanh", { 1, false, {false} } },
  {"time", { 1, false, {true} } },
  {"tolower", { 1, false, {false} } },
  {"toupper", { 1, false, {false} } },
  {"uname", { 1, false, {true} } },
  {"ungetc", { 2, false, {false, false} } },
  {"unlink", { 1, false, {false} } },
  {"vsprintf", { 3, false, {true, false, false} } },
  {"write", { 3, false, {false, false, false} } },
  {"_IO_getc", { 1, false, {false} } },
  {"_IO_putc", { 2, false, {false, false} } },
  {"__assert_fail", { 4, false, {false, false, false, false} } },
  {"__ctype_b_loc", { 0, true, {} } },
  {"__errno_location", { 0, true, {} } },
  {"__isnan", { 1, false, {false} } },
  {"__isnanf", { 1, false, {false} } },
  {"__isoc99_fscanf", { 3, false, {true, false, true} } },
  {"__isoc99_scanf", {2, false, {false, true} } },
  {"__isoc99_sscanf", {3, false, {false, false, true} } },
  {"__log_finite", {1, false, {false} } },
  {"__strdup", {1, true, {false} } },
  {"perror", {4, false, {true,false,false,false} } },

  {"fopen64", { 2, true, {false, false} } },
  {"freopen64", { 3, true, {false, false, false} } },
  {"fseeko64", { 3, false, {false, false, false} } },
  {"open64", {3, false, {false, false, false} } },
  {"lseek64", {3, false, {false, false, false} } },
  {"stat64", {2, false, {false, true} } },
  {"statfs64", { 2, false, {false, true} } },

  /* MPI */
  {"MPI_Abort", { 2, false, {false, false} } },
  {"MPI_Address", { 2, false, {false, true} } },
  {"MPI_Allgather", { 7, false, {false, false, false, true, false, false,
				 false} } },
  {"MPI_Allgatherv", { 8, false, {false, false, false, true, false, false,
				  false, false} } },
  {"MPI_Allreduce", { 6, false, {false, true, false, false, false, false} } },
  {"MPI_Alltoall", { 7, false, {false, false, false, true, false, false, true}
    } },
  {"MPI_Alltoallv", { 9, false, {false, false, false, false, true, false, false,
				 false, false} } },
  {"MPI_Attr_get", { 4, false, {false, false, true, true} } },
  {"MPI_Barrier", { 1, false, {false} } },
  {"MPI_Bcast", { 5, false, {true, false, false, false, false} } },
  {"MPI_Comm_create", { 3, false, {false, false, true} } },
  {"MPI_Comm_dup", { 2, false, {false, true} } },
  {"MPI_Comm_free", { 1, false, {true} } },
  {"MPI_Comm_group", { 2, false, {false, true} } },
  {"MPI_Comm_group_incl", { 4, false, {false, false, false, true} } },
  {"MPI_Comm_rank", { 2, false, {false, true} } },
  {"MPI_Comm_size", { 2, false, {false, true} } },
  {"MPI_Comm_split", { 4, false, {false, false, false, true} } },
  {"MPI_Errhandler_create", { 2, false, {false, true} } },
  {"MPI_Errhandler_set", { 2, false, {false, false} } },
  {"MPI_Error_string", { 3, false, {false, true, true} } },
  {"MPI_Get_version", { 2, false, {true, true} } },
  {"MPI_File_close", {1, false, {true} } },
  {"MPI_File_delete", {2, false, {false, false} } },
  {"MPI_File_get_info", {2, false, {false, true} } },
  {"MPI_File_get_size", {2, false, {false, false} } },
  {"MPI_File_open", {5, false, {false, false, false, false, true} } },
  {"MPI_File_preallocate", {2, false, {false, false} } },
  {"MPI_File_read", {5, false, {false, true, false, false, true} } },
  {"MPI_File_read_all", {5, false, {false, true, false, false, true} } },
  {"MPI_File_read_at", {6, false, {false, false, true, false, false, true} } },
  {"MPI_File_read_at_all", {6, false, {false, false, true, false, false, true} }
  },
  {"MPI_File_set_view", {6, false, {false, false, false, false, false, false} }
  },
  {"MPI_File_seek", {3, false, {false, false, false} } },
  {"MPI_File_write", {5, false, {false, false, false, false, true} } },
  {"MPI_File_write_all", {5, false, {false, false, false, false, true} } },
  {"MPI_File_write_at", {6, false, {false, false, false, false, false, true} }
  },
  {"MPI_File_write_at_all", {6, false, {false, false, false, false, false, true}
    } },
  {"MPI_Finalize", {0, false, {} } },
  {"MPI_Gather", { 8, false, {false, false, false, true, false, false, false,
			      false} } },
  {"MPI_Gatherv", { 10, false, {false, false, false, true, false, false, false,
				false, false, true} } },
  {"MPI_Get_count", { 3, false, {false, false, true} } },
  {"MPI_Get_processor_name", { 2, false, {true, true} } },
  {"MPI_Group_free", { 1, false, {true} } },
  {"MPI_Group_incl", { 4, false, {false, false, false, true} } },
  {"MPI_Group_range_incl", { 4, false, {false, false, false, true} } },
  {"MPI_Info_create", { 1, false, {true} } },
  {"MPI_Info_get", { 5, false, {false, false, false, true, true} } },
  {"MPI_Info_get_nkeys", { 2, false, {false, true} } },
  {"MPI_Info_get_nthkey", { 3, false, {false, false, true} } },
  {"MPI_Info_set", { 3, false, {true, false, false} } },
  {"MPI_Init", { 2, false, {false, false} } },
  {"MPI_Init_thread", { 4, false, {false, false, false, true} } },
  {"MPI_Iprobe", {5, false, {false, false, false, true, true} } },
  {"MPI_Irecv", {7, false, {true, false, false, false, false, false, true} } },
  {"MPI_Isend", {7, false, {false, false, false, false, false, false, true} } },
  {"MPI_Issend", {7, false, {false, false, false, false, false, false, true} }
  },
  {"MPI_Op_create", {3, false, {false, false, true} } },
  {"MPI_Op_free", {1, false, {true} } },
  {"MPI_Probe", {4, false, {false, false, false, true} } },
  {"MPI_Recv", { 7, false, {true, false, false, false, false, false, true} } },
  {"MPI_Reduce", { 7, false, {false, true, false, false, false , false, false}
    } },
  {"MPI_Scan", { 6, false, {false, true, false, false, false, false} } },
  {"MPI_Scatter", { 8, false, {false, false, false, true, false, false, false,
			       false} } },
  {"MPI_Scatterv", { 9, false, {false, false, false, false, true, false, false,
				false, false} } },
  {"MPI_Send", { 6, false, {false, false, false, false, false, false} } },
  {"MPI_Sendrecv", { 12, false, {false, false, false, false, false, true, false,
				 false, false, false, false, true} } },
  {"MPI_Ssend", { 6, false, {false, false, false, false, false, false} } },
  {"MPI_Test", { 3, false, {false, false, true} } },
  {"MPI_Testall", { 4, false, {false, false, true, true} } },
  {"MPI_Type_create_subarray", { 7, false, {false, false, false, false, false,
					    false, true} } },
  {"MPI_Type_commit", { 1, false, {false} } },
  {"MPI_Type_contiguous", { 3, false, {false, false, true} } },
  {"MPI_Type_free", { 1, false, {true} } },
  {"MPI_Type_struct", { 5, false, {false, false, false, false, true} } },
  {"MPI_Type_vector", { 5, false, {false, false, false, false, true} } },
  {"MPI_Wait", { 2 , false, {true, true} } },
  {"MPI_Waitall", { 3 , false, {false, true, true} } },
  {"MPI_Waitany", { 4 , false, {false, false, true, true} } },
  {"MPI_Waitsome", { 5 , false, {false, false, true, true, true} } },
  {"MPI_Wtick", { 0 , false, {} } },
  {"MPI_Wtime", { 0 , false, {} } },

  /* OpenMP */
  {"omp_get_num_threads", { 0, false, {} } },
  {"omp_get_thread_num", { 0, false, {} } },
  {"__kmpc_fork_call", { 4, false, {false, false, false, false} } },
  {"__kmpc_barrier", { 2, false, {false,false} } },
  {"__kmpc_single", { 2, false, {false,false} } },
  {"__kmpc_end_single", { 2, false, {false,false} } },
  {"__kmpc_for_static_init_4", { 9, false, {false,false,false,false,false,false,false,false,false} } },
  {"__kmpc_for_static_fini", { 2, false, {false,false}} },
  {"__kmpc_global_thread_num", { 1, false, {false}} },
  {"__kmpc_reduce_nowait", { 7, false, {false, false, false, false, false,
					false, false}} },
  {"__kmpc_end_reduce_nowait", { 3, false, {false, false, false}} },
  {"omp_get_max_threads", {0 ,false , {}} },
  {"__kmpc_master", { 2, false, {true,false}} },
  {"__kmpc_end_master", { 2, false, {true, false}} },
  {"omp_get_wtime", { 0, false , {}} },
  {"__kmpc_dispatch_init_4", {7 ,false , {true, false,false,false,false,false,false}} },
  {"__kmpc_dispatch_next_4", {6 ,false , {true, false, true,true,true,true}} },
  {"__kmpc_critical", { 3, false, {true, false, true}} },
  {"__kmpc_end_critical", { 3, false, {true, false, true}} },


	/* UPC */
  {"upcri_err", { 4, false, {true, false, false, false}} },
  {"upcri_stack_check", { 4, false, {true, false, true, false}} },
  {"gasneti_checkinit", { 0, false, {}} },
  {"_upcr_notify", { 2, false, {false,false}} },
  {"_upcr_wait", { 2, false, {false,false}} },
  {"_upcr_startup_pshalloc", { 1, false , {true}} },
  {"_upcr_all_alloc", { 2 ,false , {false,false}} },
  {"_upcr_free", {1 ,false , {false}} },
  {"_upcr_memget_nb", {3 , true , {true,false,false}} },
  {"_upcr_waitsync", {1 ,false , {true}} },
  {"gasnete_get_bulk", {4 , false, {true,false,true,false}} },
  {"gasnete_put_bulk", {4 ,false , {false,true,true,false}} },
  {"gasneti_trace_setsourceline", { 1, false, {true}} },
  {"_upcri_checkvalid_pshared", { 4, false, {false,false,true,false}} },
  {"gasneti_stat_intval_accumulate", { 2, false, {true,false}} },
  {"gasneti_dynsprintf", {7 ,true , {true,false,false,false,false,false,false}} },
  {"gasneti_trace_output", {3 ,false , {true,true,false}} },
  {"gasneti_build_loc_str", {3 , false, {false,false,false}} },
  {"gasneti_fatalerror", {1 ,false , {true}} }, //may not be correct
  {"gasnete_get_nb_bulk", { 4, true, {true, false,true,false}} },
  {"gasnete_try_syncnb", { 1,false , {false}} },
  {"sched_yield", { 0, false, {}} },
  {"_gasneti_memcheck_one", { 1,false , {true}} },
  {"gasnetc_AMPoll", {0 ,false , {}} },
  {"gasneti_vis_progressfn", {0 ,false , {}} },
  {"_bupc_dump_shared", {3 ,false , {false,true,false}} },
  {"_upcri_checkvalid_shared", { 4,false , {false,false,true,false}} },
  {"_gasneti_extern_free", {2 ,false , {true,true}} },
  {"posix_memalign", { 3,false , {true,false,false}} },
  {"_upcr_alloc", { 1,false , {false}} },
  {"pthread_mutex_init", {2 ,false , {true,true}} },
  {"upcr_global_exit", {1 , false, {false}} },
  {"pthread_mutex_destroy", {1 ,false , {true}} },
  {"_upcr_memput_nb", { 3, true, {true,false,false}} },
  {"_bupc_waitsync_all", { 2, false, {true,false}} },
  {"_bupc_sem_alloc", {1 ,true , {false}} },
  {"_bupc_sem_postN", {2 ,false , {true,false}} },
  {"_upcr_all_reduceD", { 8,false , {true,false,false,false,false,true,false,false}} },
  {"_upcr_all_broadcast", {4 ,false , {true,false,false,false}} },
  //{"", { , , {}} },
  //{"", { , , {}} },


  /* gsl */
  {"gsl_integration_qag", { 10, true, {false, false, false, false, false, false,
				       false, true, true, true} } },
  {"gsl_integration_workspace_alloc", { 1, true, {false} } },
  {"gsl_integration_workspace_free", { 1, false, {true} } },
  {"gsl_rng_alloc", { 1, true, {false} } },
  {"gsl_rng_set", { 2, false, {false, false} } },
  {"gsl_rng_size", { 1, false, {false} } },
  {"gsl_rng_state", { 1, true, {false} } },
  {"gsl_rng_uniform", { 1, false, {false} } },

  /* cblas */
  {"cblas_idamax", { 3 , false, {false, false, false} } },
  {"cblas_dgemv", { 12 , false, {false, false, false, false, false, false,
				 false, false, false, false, true, false} } },
  {"cblas_dscal", { 4 , false, {false, false, true, false} } },
  {"cblas_daxpy", { 5 , false, {false, false, false, false, true, false} } },
  {"cblas_dger", { 10 , false, {false, false, false, false, false,
				false, false, false, true, false} } },
  {"cblas_dtrsm", { 12 , false, {false, false, false, false, false, false,
				 false, false, false, false, true, false} } },
  {"cblas_dtrsv", { 9 , false, {false, false, false, false, false, false,
				false, true, false} } },
  {"cblas_dgemm", { 14 , false, {false, false, false, false, false, false,
				 false, false, false, false, false, false,
				 true, false} } },
  {"cblas_dcopy", { 5 , false, {false, false, false, false, true, false} } },

  /* CUDA
   * see: http://llvm.org/docs/NVPTXUsage.html#llvm-nvvm-read-ptx-sreg
   */
  {"llvm.nvvm.read.ptx.sreg.tid.x", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.tid.y", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.tid.z", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.ctaid.x", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.ctaid.y", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.ctaid.z", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.nctaid.x", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.nctaid.y", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.nctaid.z", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.ntid.x", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.ntid.y", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.ntid.z", 0, false, {}, },
  {"llvm.nvvm.read.ptx.sreg.warpsize", 0, false, {}, },
  {"llvm.nvvm.barrier0", 0, false, {}, },
  {"llvm.nvvm.fabs.ftz.f", 1, false, {false}, },
  {"llvm.nvvm.fabs.f", 1, false, {false}, },
  {"llvm.nvvm.fabs.d", 1, false, {false}, },
  {"llvm.nvvm.fmax.d", 2, false, {false, false}, },
  {"llvm.nvvm.fmax.ftz.f", 2, false, {false, false}, },
  {"llvm.nvvm.fmax.f", 2, false, {false, false}, },
  {"llvm.nvvm.min.ui", 2, false, {false, false}, },
  {"llvm.nvvm.min.i", 2, false, {false, false}, },
  {"llvm.nvvm.max.i", 2, false, {false, false}, },
  {"llvm.annotation.i1", 4, false, {false, true, true, false}, },
  {"llvm.nvvm.fmin.ftz.f", 2, false, {false, false}, },
  {"llvm.nvvm.fmin.f", 2, false, {false, false}, },
  {"llvm.nvvm.lg2.approx.ftz.f", 1, false, {false}, },
  {"llvm.nvvm.lg2.approx.f", 1, false, {false}, },
  {"llvm.nvvm.ex2.approx.ftz.f", 1, false, {false}, },
  {"llvm.nvvm.ex2.approx.f", 1, false, {false}, },
  {"llvm.nvvm.sqrt.f", 1, false, {false}, },
  {"llvm.nvvm.sqrt.rn.d", 1, false, {false}, },
  {"llvm.nvvm.rsqrt.approx.ftz.f", 1, false, {false}, },
  {"llvm.nvvm.rsqrt.approx.f", 1, false, {false}, },
  {"llvm.nvvm.floor.ftz.f", 1, false, {false}, },
  {"llvm.nvvm.floor.f", 1, false, {false}, },
  {"llvm.nvvm.mul24.i", 2, false, {false, false}, },
  {"llvm.nvvm.mul.rn.ftz.f", 2, false, {false, false}, },
  {"llvm.nvvm.mul.rn.f", 2, false, {false, false}, },
  {"llvm.nvvm.d2i.hi", 1, false, {false}, },
  {"llvm.nvvm.d2i.lo", 1, false, {false}, },
  {"llvm.nvvm.d2i.rn", 1, false, {false}, },
  {"llvm.nvvm.mul.rn.d", 2, false, {false, false}, },
  {"llvm.nvvm.add.rn.d", 2, false, {false, false}, },
  {"llvm.nvvm.add.rn.ftz.f", 2, false, {false, false}, },
  {"llvm.nvvm.add.rn.f", 2, false, {false, false}, },
  {"llvm.nvvm.fma.rn.d", 3, false, {false, false, false}, },
  {"llvm.nvvm.fma.rn.ftz.f", 3, false, {false, false, false}, },
  {"llvm.nvvm.fma.rn.f", 3, false, {false, false, false}, },
  {"llvm.nvvm.abs.i", 1, false, {false}, },
  {"llvm.nvvm.lohi.i2d", 2, false, {false, false}, },
  {"llvm.nvvm.clz.ll", 1, false, {false}, },
  {"llvm.nvvm.ceil.d", 1, false, {false}, },
  {"llvm.nvvm.trunc.ftz.f", 1, false, {false}, },
  {"llvm.nvvm.trunc.f", 1, false, {false}, },
  {"__nvvm_reflect", 1, false, {true}, },
  {"__assertfail", 5, false, {false, false, false, false, false}, },
  {"llvm.nvvm.atomic.load.add.f32.p0f32", 2, false, {true, false}, },
  {"llvm.nvvm.membar.cta", 0, false, {}, },
  {"llvm.nvvm.brev32", 1, false, {false}, },
  {"llvm.nvvm.clz.i", 1, false, {false}, },
  {"llvm.nvvm.popc.i", 1, false, {false}, },

  /* Functions from NAS */
  {"memset_pattern16", { 3, false, {false,false,false } } },
  {"\01_fopen", { 2, true, {false, false} } },
  {"initADCpar", { 1, false , {true} } },
  {"ParseParFile", { 2 ,false , {true,true} } },
  {"ShowADCPar", { 1, false, {true} } },
  {"GenerateADC", {1 ,false , {false} } },
  {"NewAdcViewCntl", {2 , true, {true, false} } },
  {"PartitionCube", { 1, false, {true} } },
  {"ComputeGivenGroupbys", {1 ,false , {true} } },
  {"CloseAdcView", {1 ,false , {true} } },
  {"timer_stop", {1 ,false , {false} } },
  {"timer_start", {1 ,false , {false} } },
  {"timer_clear", {1 ,false , {false}} },
  {"timer_read", {1 ,false , {false}} },
  {"c_print_results", {20 ,false , {true,false,false,false,false,false,false,false,false,true,false,true,true,true,true,true,true,true,true,true}} },



  /* Functions from Coral AMG */
	{"\01_strtod",{ 2, false, {false, true} }},	
	{"sscanf",{3, false, {false, false, true} }},	
	{"llvm.objectsize.i64.p0i8",{2,false, {false,false}}},	
	{"__memcpy_chk",{3,false,{true,false,false}}},	
	{"\01_clock", { 0, false, {} }},	
	{"__strncpy_chk",{ 3, true, {true, false, false} }},	
	{"__sprintf_chk",{ 3, false, {true, false, false} }},	
	{"llvm.pow.f64",{2, false, {false,false}}},	
	{"fscanf",{3,false,{false,false,true}}},	

  /* Functions from MPI-PHYLIP */
	{"scanf",{2, false, {false, true} }},	
	{"putc",{2, false, {false, false} }},	
	{"getc",{1, false, {false} }},	
	{"islower",{1, false, {false} }},	
	{"isdigit",{1, false, {false} }},	
	{"isalpha",{1, false, {false} }},	
  {"\01_fputs", { 2, false, {false, false} } },
  {"\01_freopen", { 3, true, {false, false, false} } },
  {"__strcpy_chk", { 3, false, {true, false, false} } }, // not sure for this one..
  {"__memset_chk", { 4, false, {true, false, false, false} } }, // not sure for this one..
	{"uppercase",{1, false, {true} }},	
	{"isupper",{1, false, {true} }},	
   
 /* Functions from Gadget2 */
  {"\01_fwrite", { 4, false, {false, false, false, true} } },
  {"\01_strerror", { 1, true, {false} } },
  {"\01_system", { 1, false, {true} } },
  {"__strcat_chk", { 3, true, {true, true, false} } },
  {"__error", { 0, false, {} } },
  {"__memmove_chk", { 4, true, {true,true,false,false} } },

  /* Functions from IOR */
  {"\01_sleep", { 1 , false , {false} } },
  {"\01_putenv", { 1 , false , {true} } },
  {"statvfs", { 2, false , {true, true} } },
  {"\01_realpath$DARWIN_EXTSN", { 2, true, {true,true} } },
  {"\01_regcomp", { 3, false, {true,true,false} } },
  {"\01_open", {2 ,false , {true, false} } },
  {"lseek", { 3, false, {false,false,false} } },
  {"\01_read", { 3, false, {false, true, false} } },
  {"\01_write", { 3, false, {false, true, false} } },
  {"\01_close", {1 , false , {false} } },
  {"isspace", {1 , false, {false} } },
  {"\01_getopt", { 3,false , {false,true,true} } },
  {"\01_fsync", { 1,false , {false} } },
  {"\01_stat$INODE64", {2 ,false , {true,true} } },


  /* Functions from CoMD */
  {"__assert_rtn", { 4 , false , {true,true,false,true} } },

  /* Functions from miniAMR */
  {"__exp10", { 1 , false , {false } } },

  /* Functions from Hydro */
	{"ComputeQEforRow",{12,false,{false,false,false,false,false,false,false,false,false,false,false,false}}},

  {NULL, {0, false, {} } }
};

struct funcDepPair {
  const char *name;
  const extDepInfo depInfo;
};


// TO DO
static const funcDepPair funcDepPairs[] = {
  /* LLVM intrinsics */
  {"llvm.fabs.v2f64", { 1, {}, {0} } },
  {"llvm.lifetime.end", { 2, {}, {} } },
  {"llvm.lifetime.start", { 2, {}, {} } },
  {"llvm.memcpy.p0i8.p0i8.i32", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.memcpy.p0i8.p0i8.i64", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.memmove.p0i8.p0i8.i32", { 5, { {0, {1, 2} }, {1, {2} } }, {} } },
  {"llvm.memmove.p0i8.p0i8.i64", { 5, { {0, {1, 2} }, {1, {2} } }, {} } },
  {"llvm.memset.p0i8.i32", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.memset.p0i8.i64", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.minnum.v2f64", { 2, {}, {0, 1} } },
  {"llvm.trap", { 0, {}, {} } },

  /* libc */
  {"abort", { 0, {}, {} } },
  {"atof", { 1, {}, {0} } },
  {"atoi", { 1, {}, {0} } },
  {"calloc", { 2, {}, {0, 1} } },
  {"ceil", { 1, {}, {0} } },
  {"clock", { 0, {}, {} } },
  {"clock_gettime", { 2, { {1, {0} } }, {} } },
  {"cos", { 1, {}, {0} } },
  {"erfc", { 1, {}, {0} } },
  {"exit", { 1, {}, {} } },
  {"exp", { 1, {}, {0} } },
  {"fabs", { 1, {}, {0} } },
  {"fabsf", { 1, {}, {0} } },
  {"fclose", { 1, {}, {} } },
  {"feof", { 1, {}, {} } },
  {"fflush", { 1, {}, {} } },
  {"fgets", { 3, { {0, {1, 2} } }, {1, 2} } },
  {"floor", { 1, {}, {0} } },
  {"fmax", { 2, {}, {0, 1} } },
  {"fmin", { 2, {}, {0, 1} } },
  {"fopen", { 2, {}, {} } },
  {"fprintf", {3, {}, {} } },
  {"fputc", { 2, {}, {} } },
  {"fread", { 4, { {0, {1, 2, 3} } }, {} } },
  {"free", { 1, {}, {} } },
  {"fwrite", { 4, { {3, {0, 1, 2} } }, {} } },
  {"gethostname", { 2, { {1, {0} } }, {} } },
  {"ldexp", {2, {}, {0, 1} } },
  {"log", {1, {}, {0} } },
  {"malloc", { 1, {}, {} } },
  {"mkdir", { 2, {}, {} } },
  {"move_pages", { 6, {}, {} } },
  {"numa_num_configured_nodes", { 0, {}, {} } },
  {"pow", { 2, {}, {0, 1} } },
  {"printf", { 2, {}, {} } },
  {"putchar", { 1, {}, {} } },
  {"puts", { 1, {}, {} } },
  {"qsort", { 4, {}, {} } },
  {"realloc", { 2, { {0, {0, 1} } }, {0, 1} } },
  {"sin", { 1, {}, {0} } },
  {"sprintf", { 3, { {0, {1, 2} } }, {} } },
  {"sqrt", { 1, {}, {0} } },
  {"strcat", { 2, { {0, {1} } }, {0, 1} } },
  {"strchr", { 2, {}, {0, 1} } },
  {"strcmp", { 2, {}, {0, 1} } },
  {"strcpy", { 2, {}, {} } },
  {"strcspn", { 2, {}, {0, 1} } },
  {"strerror", { 1, {}, {} } },
  {"strlen", { 1, {}, {0} } },
  {"strncmp", { 3, {}, {0, 1, 2} } },
  {"strncpy", { 3, { {0, {1, 2} } }, {1, 2} } },
  {"strspn", { 2, {}, {0, 1} } },
  {"strtod", { 2, {}, {0} } },
  {"strtol", { 3, {}, {} } },
  {"system", { 1, {}, {} } },
  {"unlink", { 1, {}, {} } },
  {"__assert_fail", { 4, {}, {} } },
  {"__errno_location", { 0, {}, {} } },
  {"__isoc99_fscanf", { 3, { {2, {0, 1} } }, {} } },
  {"__isoc99_scanf", {2, { {1, {0} } }, {} } },
  {"__isoc99_sscanf", {3, { {2, {0, 1} } }, {} } },

  /* MPI */
  {"MPI_Abort", { 2, {}, {} } },
  {"MPI_Address", { 2, {}, {} } },
  {"MPI_Allgather", { 7, {}, {} } },
  {"MPI_Allgatherv", { 8, {}, {} } },
  {"MPI_Allreduce", { 6, {}, {} } },
  {"MPI_Alltoall", { 7, {}, {} } },
  {"MPI_Alltoallv", { 9, {}, {} } },
  {"MPI_Barrier", { 1, {}, {} } },
  {"MPI_Bcast", { 5, {}, {} } },
  {"MPI_Comm_create", { 3, {}, {} } },
  {"MPI_Comm_group", { 2, {}, {} } },
  {"MPI_Comm_group_incl", { 4, {}, {} } },
  {"MPI_Comm_rank", { 2, {}, {} } },
  {"MPI_Comm_size", { 2, {}, {} } },
  {"MPI_Finalize", {0, {}, {} } },
  {"MPI_Gather", { 8, {}, {} } },
  {"MPI_Gatherv", { 10, {}, {} } },
  {"MPI_Get_count", { 3, {}, {} } },
  {"MPI_Group_free", { 1, {}, {} } },
  {"MPI_Group_incl", { 4, {}, {} } },
  {"MPI_Init", {2, {}, {} } },
  {"MPI_Iprobe", {5, {}, {} } },
  {"MPI_Irecv", {7, {}, {} } },
  {"MPI_Isend", {7, {}, {} } },
  {"MPI_Op_create", {3, {}, {} } },
  {"MPI_Op_free", {1, {}, {} } },
  {"MPI_Probe", {4, {}, {} } },
  {"MPI_Recv", { 7, {}, {} } },
  {"MPI_Reduce", { 7, {}, {} } },
  {"MPI_Scan", { 6, {}, {} } },
  {"MPI_Scatter", { 8, {}, {} } },
  {"MPI_Scatterv", { 9, {}, {} } },
  {"MPI_Send", { 6, {}, {} } },
  {"MPI_Sendrecv", { 12, {}, {} } },
  {"MPI_Ssend", { 6, {}, {} } },
  {"MPI_Test", { 3, {}, {} } },
  {"MPI_Testall", { 4, {}, {} } },
  {"MPI_Type_commit", { 1, {}, {} } },
  {"MPI_Type_free", { 1, {}, {} } },
  {"MPI_Type_struct", { 5, {}, {} } },
  {"MPI_Wait", { 2 , {}, {} } },
  {"MPI_Waitall", { 3 , {}, {} } },
  {"MPI_Wtime", { 0 , {}, {} } },

  /* OpenMP */
  {"omp_get_num_threads", { 0 , {}, {} } },
  {"omp_get_thread_num", { 0 , {}, {} } },
  {"__kmpc_fork_call", { 4, {}, {} } },
  {"__kmpc_barrier", { 2, {}, {} } },
  {"__kmpc_single", { 2, {}, {} } },
  {"__kmpc_end_single", { 2, {}, {} } },
  {"__kmpc_for_static_init_4", { 9, {}, {} } },
  {"__kmpc_for_static_fini", { 2, {}, {}} },

  /* gsl */
  {"gsl_integration_qag", { 10, {}, {} } },
  {"gsl_integration_workspace_alloc", { 1, {}, {} } },
  {"gsl_integration_workspace_free", { 1, {}, {} } },
  {"gsl_rng_alloc", { 1, {}, {} } },
  {"gsl_rng_set", { 2, {}, {} } },
  {"gsl_rng_size", { 1, {}, {} } },
  {"gsl_rng_state", { 1, {}, {} } },
  {"gsl_rng_uniform", { 1, {}, {} } },

  {NULL, {0, {}, {} } }
};


ExtInfo::ExtInfo(Module &m) : m(m) {
  for (const funcModPair *i = funcModPairs; i->name; ++i)
    extModInfoMap[i->name] = &i->modInfo;

  for (const funcDepPair *i = funcDepPairs; i->name; ++i)
    extDepInfoMap[i->name] = &i->depInfo;

  bool missingInfo = false;

  for (Function &F : m) {
    if (!F.isDeclaration() || isIntrinsicDbgFunction(&F))
      continue;

    if (!getExtModInfo(&F)) {
      missingInfo = true;
      errs() << "missing info for external function " << F.getName() << "\n";
    }
  }

  if (missingInfo) {
    errs() << "Error: you have to fill the funcModPairs array in ExtInfo.cpp"
	   << " with the missing functions. exiting..\n";
    exit(EXIT_FAILURE);
  }
}

ExtInfo::~ExtInfo() {
}

const extModInfo *
ExtInfo::getExtModInfo(const llvm::Function *F) {
  auto I = extModInfoMap.find(F->getName());

  if (I != extModInfoMap.end())
    return extModInfoMap[F->getName()];

  return NULL;
}

const extDepInfo *
ExtInfo::getExtDepInfo(const llvm::Function *F) {
  auto I = extDepInfoMap.find(F->getName());

  if (I != extDepInfoMap.end())
    return extDepInfoMap[F->getName()];

  return NULL;
}
