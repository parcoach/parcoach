%global maj_ver 2
%global min_ver 2
%global patch_ver 0
#global rc_ver 3
%global parcoach_version %{maj_ver}.%{min_ver}.%{patch_ver}
%global parcoach_srcdir parcoach-%{parcoach_version}
%global llvm_version 15

%global pkg_name parcoach
%global install_prefix /usr
%global pkg_libdir %{_libdir}

# Disable debug info
%define debug_package %{nil}
%undefine _disable_source_fetch

Name:		%pkg_name
Version:	%{parcoach_version}%{?rc_ver:~rc%{rc_ver}}
Release:	1%{?dist}
Summary:	PARallel COntrol flow Anomaly CHecker

License:	LGPLv2.1
URL:      https://parcoach.github.io
Source0:  https://gitlab.inria.fr/parcoach/parcoach/-/archive/%{parcoach_version}/parcoach-%{parcoach_version}.tar.gz

BuildRequires:	gcc-c++
BuildRequires:	cmake
BuildRequires:	llvm-%{llvm_version}-devel
BuildRequires:	clang-%{llvm_version}
BuildRequires:	clang-%{llvm_version}-devel
BuildRequires:	libomp-%{llvm_version}-devel
BuildRequires:	python38
BuildRequires:	openmpi
BuildRequires:	git
BuildRequires:	libzstd-devel

Requires: libzstd
Requires: zlib
Requires: openmpi


%description
PARCOACH (PARallel COntrol flow Anomaly CHecker) automatically checks parallel
applications to verify the correct use of collectives. This tool uses an
inter-procedural control and data-flow analysis to detect potential errors at compile-time.

%prep
%autosetup -n %{parcoach_srcdir}

%build

# We should be able to enable fortran, but the job to build flang 15 oom in
# Atos' ci.
%__cmake	-S . -B %{_vpath_builddir} \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DPARCOACH_BUILD_SHARED=OFF \
  -DPARCOACH_ENABLE_INSTRUMENTATION=ON \
  -DPARCOACH_ENABLE_FORTRAN=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DMPIEXEC_EXECUTABLE=/opt/mpi/openmpi/4.1.4.4/bin/mpiexec \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DPARCOACH_ENABLE_TESTS=OFF

cd %{_vpath_builddir}
%cmake_build

%install
cd %{_vpath_builddir}
%cmake_install

%files
%license LICENSE
%{_bindir}/parcoach
%{_bindir}/parcoachcc
%{_libdir}/libParcoachInstrumentation.so

%changelog
* Thu Apr 13 2023 Philippe Virouleau <philippe.virouleau.external@atos.net> - 2.2.0
- Initial version with PARCOACH 2.2.0
