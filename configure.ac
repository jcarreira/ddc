#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT(CIRRUS, 0.1, joao@berkeley.edu)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/server/RDMAServer.h])
#AC_CONFIG_HEADERS([config.h])

# Checks for presence of dependencies
AC_CHECK_PROG(CPPLINT_CHECK, cpplint, yes)
if test x"$CPPLINT_CHECK" != x"yes" ; then
    AC_MSG_ERROR([Please install cpplint with 'sudo pip install cpplint' before installing.])
fi

# Checks for programs.
AC_PROG_CXX([g++-6])
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AX_CXX_COMPILE_STDCXX_14

CXXFLAGS="-O3 -g -fPIC -std=c++1z -Werror"

# Check for libraries
AC_LIB_HAVE_LINKFLAGS(rdmacm)
AC_LIB_HAVE_LINKFLAGS(ibverbs)
AC_LIB_HAVE_LINKFLAGS(mpi)

AM_CONDITIONAL([USE_RDMA], [test "x${HAVE_LIBRDMACM}" = "xyes"])
AM_CONDITIONAL([USE_MPI], [test "x${HAVE_LIBMPI}" = "xyes"])

# Checks for header files.
AC_CHECK_HEADERS([ arpa/inet.h netdb.h string.h stdint.h\
        stdlib.h sys/types.h sys/socket.h sys/epoll.h\
        sys/time.h syslog.h unistd.h google/dense_hash_map])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_STRCOLL
AC_CHECK_FUNCS([gettimeofday localtime_r memchr memmove memset mkdir stpcpy strchr strcspn strdup strerror strpbrk strrchr strspn strstr])
AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/authentication/Makefile
                 src/utils/Makefile
                 src/common/Makefile
                 src/object_store/Makefile
                 src/iterator/Makefile
                 src/client/Makefile
                 src/server/Makefile
                 src/cache_manager/Makefile
                 src/common/schemas/Makefile
                 tests/object_store/Makefile
		 tests/client/Makefile
                 tests/Makefile
                 examples/Makefile
                 examples/graphs/Makefile
                 examples/graphs/page_rank/Makefile
                 examples/graphs/wcc/Makefile
                 benchmarks/Makefile])


AC_CONFIG_SUBDIRS([tests/mpi])
AC_CONFIG_SUBDIRS([examples/ml])

AC_OUTPUT
                 
#examples/ml/Makefile
