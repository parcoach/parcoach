# Unfortunately there are a few other build dependencies which are not in the
# repositories: llvm ~ 15, any MPI.
set(CPACK_RPM_BUILDREQUIRES "cmake >= 3.18, make")
set(CPACK_RPM_PACKAGE_AUTOREQ OFF)
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_PACKAGE_LICENSE "LGPLv2.1")
