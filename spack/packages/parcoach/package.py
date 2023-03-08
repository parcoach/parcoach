from spack.package import *
import os.path


class Parcoach(CMakePackage):
    """PARCOACH (PARallel COntrol flow Anomaly CHecker) automatically checks \
parallel applications to verify the correct use of collectives. This tool uses \
an inter-procedural control and data-flow analysis to detect potential errors \
at compile-time."""

    homepage = "https://parcoach.github.io/"
    url = "https://gitlab.inria.fr/parcoach/parcoach/-/archive/2.4.0/parcoach-2.4.0.tar.gz"
    git = "https://gitlab.inria.fr/parcoach/parcoach.git"

    maintainers("viroulep", "esaillar")

    version("master", branch="master")
    version("2.4.0", sha256="7107be7fd2d814b5118d2213f5ceda48d24d3caf97c8feb19e7991e0ebaf2455", preferred=True)

    variant("fortran", default=False, description="Enable Fortran support")

    # FIXME: find a "proper" llvm package: this one has EH and RTTI enabled,
    # we get flang in a ridiculous state where fortran's runtime needs
    # libstdc++ (!!!).
    depends_on("llvm@15 +link_llvm_dylib -lldb -polly -libcxx -compiler-rt targets=x86", type=('build', 'link', 'run'))
    # Add flang variant if our fortran variant is activated
    depends_on("llvm@15 +flang", when="+fortran")
    depends_on("cmake@3.25:", type='build')
    # FIXME: check if we can make this "virtual" on mpi
    depends_on("openmpi@4.1.5")

    def cmake_args(self):
        cache_path = os.path.join(self.stage.source_path, "caches", "Release-shared.cmake")
        version_suffix = ""
        if "@master" in self.spec:
            version_suffix = "-master"

        args = [
            # Preload cmake cache for the shared release
            f"-C {cache_path}",
            # By default parcoach has a "-dev" suffix, remove it when building
            # a release.
            self.define("PARCOACH_VERSION_SUFFIX", version_suffix),
        ]

        if "+fortran" in self.spec:
            args.append(self.define("CMAKE_Fortran_FLAGS", "-flang-experimental-exec -lstdc++"))
        else:
            args.append(self.define("PARCOACH_ENABLE_FORTRAN", "OFF"))

        return args

    def setup_build_environment(self, env):
        env.set('CC', self.spec['llvm'].package.cc)
        env.set('CXX', self.spec['llvm'].package.cxx)
        # Yep, FC is incorrectly set to "flang" in llvm's package.py :')
        if "+fortran" in self.spec:
            env.set('FC', f"{self.spec['llvm'].package.fc}-new")

    def setup_run_environment(self, env):
        # Ideally we would ask user to use CMake integration, and not "pollute"
        # these common flags. This is only useful for instrumentation.
        env.prepend_path("LD_LIBRARY_PATH", self.prefix.lib)
        env.prepend_path("LIBRARY_PATH", self.prefix.lib)
