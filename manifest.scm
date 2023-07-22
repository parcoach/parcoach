;; What follows is a "manifest" equivalent to the command line you gave.
;; You can store it in a file that you may then pass to any 'guix' command
;; that accepts a '--manifest' (or '-m') option.

(specifications->manifest
  (list "clang-toolchain@15"
        "llvm@15"
        "flang@15"
        "cmake"
        "ninja"
        "bash"
        "coreutils"
        "which"
        "openmpi"
        "python-lit"
        "findutils"
        "googletest"
        "sed"
        "grep"))
