dnl AC_PREREQ([2.71])

AC_INIT([hio],[0.1.0],[Chung, Hyung-Hwan (hyunghwan.chung@gmail.com)],[],[http://code.miflux.com/@hio])

AC_CONFIG_HEADERS([lib/hio-cfg.h])
AC_CONFIG_AUX_DIR([ac])
AC_CONFIG_MACRO_DIR([m4])

dnl define feature-enabling macros
AC_USE_SYSTEM_EXTENSIONS()


dnl use CC and CPP for compilation tests with .c test programs.
AC_LANG(C)

dnl initialize automake
AM_INIT_AUTOMAKE([-Wall -Werror subdir-objects foreign])
AM_PROG_AR

AC_REQUIRE_AUX_FILE([tap-driver.sh])

dnl enable silent rules if automake supports them.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([no])])
dnl m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl determine a C compiler to use
AC_PROG_CC

dnl determine some key programs
AC_PROG_INSTALL
AC_PROG_EGREP
AC_PROG_LN_S
AC_PROG_SED

dnl initialize libtool
LT_INIT
AC_SUBST(LIBTOOL_DEPS)

dnl overrides the default CFLAGS setting
if test "$ac_test_CFLAGS" != "set"
then
	if test "$GCC" = "yes"
	then
		CFLAGS="$CFLAGS -Wall"
	fi
fi

dnl indicate the existence of config.h
CFLAGS="$CFLAGS -DHIO_HAVE_CFG_H"

dnl make visible the 64-bit interface to the file system
AC_SYS_LARGEFILE()

dnl define the WIN32 conditional if necessary
case "$host" in
*-*-mingw*|*-*-cygwin*)
	#CFLAGS="$CFLAGS -D_WIN32_WINNT=0x0400"
	platform_win32=yes
	platform_macosx=no
	;;
*-*-darwin*)
	platform_win32=no
	platform_macosx=yes
	;;
*)
	platform_win32=no
	platform_macosx=no
	;;
esac
AM_CONDITIONAL(WIN32, test "x${platform_win32}" = "xyes" )
AM_CONDITIONAL(MACOSX, test "x${platform_macosx}" = "xyes" )

dnl check the math library (check if -lm is needed)
LT_LIB_M
AC_SUBST(LIBM, $LIBM)

dnl check some compiler builtins
AC_MSG_CHECKING([__builtin_memset])
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([], [__builtin_memset ((void*)1, ' ', 10);])],
	[AC_MSG_RESULT(yes)
	 AC_DEFINE([HAVE___BUILTIN_MEMSET], [1], [__builtin_memset])],
	[AC_MSG_RESULT(no)]
)

AC_MSG_CHECKING([__builtin_memcpy])
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([], [__builtin_memcpy ((void*)1, (void*)2, 10);])],
	[AC_MSG_RESULT(yes)
	 AC_DEFINE([HAVE___BUILTIN_MEMCPY], [1], [__builtin_memcpy])],
	[AC_MSG_RESULT(no)]
)

AC_MSG_CHECKING([__builtin_memmove])
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([], [__builtin_memmove ((void*)1, (void*)2, 10);])],
	[AC_MSG_RESULT(yes)
	 AC_DEFINE([HAVE___BUILTIN_MEMMOVE], [1], [__builtin_memmove])],
	[AC_MSG_RESULT(no)]
)

AC_MSG_CHECKING([__builtin_memcmp])
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([], [int a = __builtin_memcmp ((void*)1, (void*)2, 10);])],
	[AC_MSG_RESULT(yes)
	 AC_DEFINE([HAVE___BUILTIN_MEMCMP], [1], [__builtin_memcmp])],
	[AC_MSG_RESULT(no)]
)

dnl check header files.
AC_CHECK_INCLUDES_DEFAULT
AC_PROG_EGREP

AC_CHECK_HEADERS([stddef.h wchar.h wctype.h errno.h signal.h fcntl.h dirent.h])
AC_CHECK_HEADERS([time.h sys/time.h utime.h spawn.h execinfo.h ucontext.h])
AC_CHECK_HEADERS([sys/resource.h sys/wait.h sys/syscall.h sys/ioctl.h])
AC_CHECK_HEADERS([sys/sendfile.h sys/epoll.h sys/event.h sys/poll.h sys/select.h])
AC_CHECK_HEADERS([sys/sysctl.h sys/socket.h sys/sockio.h sys/un.h])
AC_CHECK_HEADERS([ifaddrs.h tiuser.h linux/netfilter_ipv4.h netinet/in.h netinet/sctp.h])
AC_CHECK_HEADERS([net/if.h net/if_dl.h netinet/if_ether.h netpacket/packet.h net/bpf.h], [], [], [
	#include <sys/types.h>
	#include <sys/socket.h>])
AC_CHECK_HEADERS([sys/stropts.h sys/macstat.h linux/ethtool.h linux/sockios.h])
AC_CHECK_HEADERS([quadmath.h crt_externs.h sys/prctl.h paths.h pty.h])

dnl check data types
dnl AC_CHECK_TYPE([wchar_t], 
dnl 	[ AC_DEFINE([HAVE_WCHAR_T_IN_STDDEF_H],
dnl	           [],[wchar_t is available in stddef.h]) ],
dnl	[],
dnl	[#include <stddef.h>])

dnl check functions
AC_CHECK_FUNCS([uselocale clearenv])
AC_CHECK_FUNCS([mbrlen mbrtowc wcrtomb])
AC_CHECK_FUNCS([mbsnrtowcs mbsrtowcs wcsnrtombs wcsrtombs])
AC_CHECK_FUNCS([wctype iswctype wctrans towctrans])
AC_CHECK_FUNCS([isblank iswblank])
AC_CHECK_FUNCS([lseek64 stat64 fstat64 lstat64 ftruncate64 readdir64 dirfd])
AC_CHECK_FUNCS([lstat fchmod fsync ftruncate posix_fadvise])
AC_CHECK_FUNCS([timegm timelocal localtime_r gettimeofday settimeofday clock_gettime clock_settime])
AC_CHECK_FUNCS([utime utimes futimes lutimes futimens])
AC_CHECK_FUNCS([sysconf prctl fdopendir setrlimit getrlimit getpgid getpgrp])
AC_CHECK_FUNCS([backtrace backtrace_symbols])
AC_CHECK_FUNCS([fork vfork posix_spawn gettid nanosleep select])
AC_CHECK_FUNCS([openpty posix_openpt])
AC_CHECK_FUNCS([makecontext swapcontext getcontext setcontext])
AC_CHECK_FUNCS([snprintf _vsnprintf _vsnwprintf])
AC_CHECK_FUNCS([pipe2 accept4 paccept sendmsg recvmsg writev readv])
AC_CHECK_FUNCS([isatty ptsname_r mmap munmap])
AC_CHECK_LIB([rt], [clock_gettime], [LIBS="$LIBS -lrt"])

dnl OLDLIBS="$LIBS"
dnl AC_SEARCH_LIBS([connect], [socket])
dnl LIBS="$OLDLIBS"
dnl if test "$ac_cv_search_connect" != "none required" -a "$ac_cv_search_connect" != "no"
dnl then
dnl	SOCKET_LIBS="$SOCKET_LIBS -lsocket"
dnl fi
dnl AC_SUBST(SOCKET_LIBS)
AC_CHECK_FUNCS([connect gethostbyname])
if test "x$ac_cv_func_gethostbyname" = "xno"
then
	AC_CHECK_LIB([nsl], [gethostbyname], [
		SOCKET_LIBS="$SOCKET_LIBS -lnsl"
		AC_DEFINE(HAVE_GETHOSTBYNAME, 1)
	])
fi
if test "x$ac_cv_func_connect" = "xno"
then
	AC_CHECK_LIB([socket], [connect], [
		SOCKET_LIBS="$SOCKET_LIBS -lsocket"
		AC_DEFINE(HAVE_CONNECT, 1)
	]) 

	if test "x$ac_cv_lib_socket_connect" = xno
	then
		AC_CHECK_FUNCS([t_connect])
		if test "x$ac_cv_func_t_connect" = "xno"
		then
			AC_CHECK_LIB([nsl_s], [t_connect], [
				SOCKET_LIBS="$SOCKET_LIBS -lnsl_s"
				AC_DEFINE(HAVE_T_CONNECT, 1)
			])
		fi
	fi
fi
if test "x${platform_win32}" = "xyes"
then
	SOCKET_LIBS="$SOCKET_LIBS -lws2_32"
fi
AC_SUBST(SOCKET_LIBS)

OLDLIBS="$LIBS"
LIBS="$SOCKET_LIBS $LIBS"
AC_CHECK_FUNCS([if_nametoindex if_indextoname getifaddrs])
LIBS="$OLDLIBS"

dnl check if sendfile and its variants exist in the standard c library
dnl it inspects the existence of each missing function in the sendfile library.
AC_CHECK_FUNCS([sendfile sendfile64 sendfilev sendfilev64])
if test "$ac_cv_func_sendfile" = "no"
then
	AC_CHECK_LIB([sendfile], [sendfile], [
		SENDFILE_LIBS="-lsendfile"
		dnl only for consistency with AC_CHECK_FUNCS
		AC_DEFINE(HAVE_SENDFILE, 1)
	])
fi
if test "$ac_cv_func_sendfile64" = "no"
then
	AC_CHECK_LIB([sendfile], [sendfile64], [
		SENDFILE_LIBS="-lsendfile"
		dnl only for consistency with AC_CHECK_FUNCS
		AC_DEFINE(HAVE_SENDFILE64, 1)
	])
fi
if test "$ac_cv_func_sendfilev" = "no"
then
	AC_CHECK_LIB([sendfile], [sendfilev], [
		SENDFILE_LIBS="-lsendfile"
		dnl only for consistency with AC_CHECK_FUNCS
		AC_DEFINE(HAVE_SENDFILEV, 1)
	])
fi
if test "$ac_cv_func_sendfilev64" = "no"
then
	AC_CHECK_LIB([sendfile], [sendfilev64], [
		SENDFILE_LIBS="-lsendfile"
		dnl only for consistency with AC_CHECK_FUNCS
		AC_DEFINE(HAVE_SENDFILEV64, 1)
	])
fi
AC_SUBST(SENDFILE_LIBS)

AC_CHECK_FUNCS([epoll_create epoll_create1])
if test "$ac_cv_func_epoll_create" = "yes"
then
	AC_DEFINE(HAVE_EPOLL, 1, [epoll support])
fi

AC_CHECK_FUNCS([kqueue kqueue1 kevent])

AX_PTHREAD()
AC_CHECK_LIB([pthread], [pthread_mutex_timedlock],  [
	AC_DEFINE([HAVE_PTHREAD_MUTEX_TIMEDLOCK],1,[pthreads has pthread_mutex_timedlock()])
])
AC_CHECK_LIB([pthread], [pthread_mutex_trylock],  [
	AC_DEFINE([HAVE_PTHREAD_MUTEX_TRYLOCK],1,[pthreads has pthread_mutex_trylock()])
])

dnl ===== enable-all-static =====
AC_ARG_ENABLE([all-static], 
	[AS_HELP_STRING([--enable-all-static],[build the full static binaries(default. no)])],
	enable_all_static_is=$enableval,
	enable_all_static_is=no
)
AM_CONDITIONAL(ENABLE_ALL_STATIC, test "x${enable_all_static_is}" = "xyes")

AC_ARG_WITH([all-static-libs], 
	[AS_HELP_STRING([--with-all-static-libs[[=lib-list]]],[specify extra libraries to add for building the full static binaries])],
	[],
	[with_all_static_libs=]
)
ALL_STATIC_LIBS="$with_all_static_libs"
AC_SUBST(ALL_STATIC_LIBS)

dnl ===== enable-ssl =====
AC_ARG_ENABLE([ssl], 
	[AS_HELP_STRING([--enable-ssl],[build the library in the ssl mode (default. yes)])],
	enable_ssl_is=$enableval,
	enable_ssl_is=yes
)
if test "x$enable_ssl_is" = "xyes"
then
	AC_CHECK_HEADERS([openssl/ssl.h openssl/err.h openssl/engine.h])

	dnl check for an SSL library
	dnl i used SSL_library_init at first.
	dnl starting from  openssl 1.1.0, SSL_library_init was made to a macro 
	dnl aliasing OPENSSL_init_ssl.
	dnl   #define SSL_library_init() OPENSSL_init_ssl(0, NULL)
	dnl instead of checking both, now i use SSL_new.
	AC_CHECK_FUNCS([SSL_new])

	if test "x$ac_cv_func_SSL_new" = "xyes" 
	then
		AC_DEFINE(HAVE_SSL, 1, [ssl support])
	else
		AC_CHECK_LIB([ssl], [SSL_new], [
			SSL_LIBS="-lssl"
			AC_DEFINE(HAVE_SSL, 1, [ssl support])

			dnl additional check
			AC_CHECK_LIB([crypto], [ERR_get_error], [
				SSL_LIBS="${SSL_LIBS} -lcrypto"
			])

			dnl only for consistency with AC_CHECK_FUNCS
			AC_DEFINE(HAVE_SSL_NEW, 1)
		])
	fi

	OLDLIBS="$LIBS"
	LIBS="$SSL_LIBS $LIBS"
	AC_CHECK_FUNCS([ENGINE_cleanup CRYPTO_cleanup_all_ex_data])
	LIBS="$OLDLIBS"
fi
AC_SUBST(SSL_LIBS)
AM_CONDITIONAL(ENABLE_SSL, test "${enable_ssl_is}" = "yes")

dnl ===== enable-mariadb =====
AC_ARG_ENABLE([mariadb],
        [AS_HELP_STRING([--enable-mariadb],[enable mariadb support (default. no)])],
        enable_mariadb_is=$enableval,
        enable_mariadb_is=no
)
if test "${enable_mariadb_is}" = "yes"
then
        AC_DEFINE([HIO_ENABLE_MARIADB],[1],[build the mariadb device])
fi
AM_CONDITIONAL(ENABLE_MARIADB, test "${enable_mariadb_is}" = "yes")


if test "x${ac_cv_header_libunwind_h}" = "xyes"
then
	AC_CHECK_LIB([unwind], [unw_backtrace],
		[
			UNWIND_LIBS="-lunwind"
			AC_DEFINE([HAVE_UNWIND_LIB], [1], [libunwind is available])
		]
	)
	AC_SUBST(UNWIND_LIBS)
fi

dnl libmariadb
AX_LIB_MARIADB
dnl X11 library
AC_CHECK_LIB([X11], [XOpenDisplay], [AC_DEFINE([HAVE_X11_LIB], [1], [libX11 is available])] )
AM_CONDITIONAL(HAVE_X11_LIB, test "x${ac_cv_lib_X11_XOpenDisplay}" = "xyes")

AC_STRUCT_DIRENT_D_TYPE
AC_CHECK_MEMBERS([DIR.d_fd, DIR.dd_fd],,,[[#include <dirent.h>]])
AC_CHECK_MEMBERS([struct stat.st_mtim])
AC_CHECK_MEMBERS([struct stat.st_birthtime])
AC_CHECK_MEMBERS([struct stat.st_mtim.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_birthtim.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_mtimespec.tv_nsec])
AC_CHECK_MEMBERS([struct stat.st_birthtimespec.tv_nsec])


AC_CHECK_MEMBERS([struct ifreq.ifr_ifindex, struct ifreq.ifr_mtu], [], [],
	[#include <sys/types.h>
	 #include <sys/socket.h>
	 #ifdef HAVE_NET_IF_H
	 #include <net/if.h>
	 #endif])

AC_CHECK_TYPES([struct lifconf, struct lifreq, struct if_laddrreq], [], [],
	[#include <sys/types.h>
	 #include <sys/socket.h>
	 #ifdef HAVE_NET_IF_H
	 #include <net/if.h>
	 #endif])

AC_CHECK_MEMBERS([struct sockaddr_in6.sin6_scope_id], [], [],
	[#include <sys/types.h> 
	 #include <sys/socket.h> 
	 #include <netinet/in.h>])

AC_CHECK_TYPES([struct timespec], [], [],
	[#include <sys/types.h>
	 #ifdef HAVE_TIME_H
	 #include <time.h>
	 #endif
	 #ifdef HAVE_SYS_TIME_H
	 #include <sys/time.h>
	 #endif])

AC_CHECK_TYPES([struct ip_mreqn], [], [],
	[#include <sys/types.h> 
	 #include <sys/socket.h> 
	 #include <netinet/in.h>])

AC_MSG_CHECKING([for va_copy])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdarg.h>]], [[
		va_list x, y;
		va_copy(x,y);
	]])],[
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_VA_COPY], [1], [va_copy is available])
	],[AC_MSG_RESULT(no)
])

AC_MSG_CHECKING([for __va_copy])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdarg.h>]], [[
		va_list x, y;
		__va_copy(x,y); 
	]])],[
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE___VA_COPY], [1], [__va_copy is available])
	],[AC_MSG_RESULT(no)
])

AC_MSG_CHECKING([for strftime %z])
AC_RUN_IFELSE([AC_LANG_SOURCE([[#include <string.h>
	 #if defined(HAVE_SYS_TIME_H)
	 #include <sys/time.h>
	 #endif
	 #if defined(HAVE_TIME_H)
	 #include <time.h>
	 #endif
	int main ()
	{
		char buf[100];
		time_t t = 0;
		strftime (buf, sizeof(buf), "%z", gmtime(&t));
		return (strcmp(buf, "%z") == 0 || strcmp(buf, "z") == 0)? 1: 0;
	}]])],[
	 	AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_STRFTIME_SMALL_Z], [1], [strftime supports %z])
	],[AC_MSG_RESULT(no)],[
	 	dnl cross-compiling, assume yes
	 	AC_MSG_RESULT(unknown)
		AC_DEFINE([HAVE_STRFTIME_SMALL_Z], [1], [strftime supports %z])
	
])

dnl check the size of primitive data types
AC_CHECK_SIZEOF(char,,[[]])
AC_CHECK_SIZEOF(short,,[[]])
AC_CHECK_SIZEOF(int,,[[]])
AC_CHECK_SIZEOF(long,,[[]])
AC_CHECK_SIZEOF(long long,,[[]])
AC_CHECK_SIZEOF(__int8,,[[]])
AC_CHECK_SIZEOF(__int16,,[[]])
AC_CHECK_SIZEOF(__int32,,[[]])
AC_CHECK_SIZEOF(__int64,,[[]])
AC_CHECK_SIZEOF(__int128,,[[]])
AC_CHECK_SIZEOF(__int8_t,,[[]])
AC_CHECK_SIZEOF(__int16_t,,[[]])
AC_CHECK_SIZEOF(__int32_t,,[[]])
AC_CHECK_SIZEOF(__int64_t,,[[]])
AC_CHECK_SIZEOF(__int128_t,,[[]])
AC_CHECK_SIZEOF(__uint128_t,,[[]])
dnl AC_CHECK_SIZEOF doesn't work without white-space between void and *
AC_CHECK_SIZEOF(void *,,[[]])
AC_CHECK_SIZEOF(float,,[[]])
AC_CHECK_SIZEOF(double,,[[]])
AC_CHECK_SIZEOF(long double,,[[]])
AC_CHECK_SIZEOF(__float128,,[[]])

AC_CHECK_SIZEOF(wchar_t)
AC_CHECK_SIZEOF(off_t)
AC_CHECK_SIZEOF(off64_t)

AC_CHECK_SIZEOF(mbstate_t,,[#include <wchar.h>])
AX_CHECK_NUMVALOF(MB_LEN_MAX,[32],[#include <limits.h>])

AX_CHECK_NUMVALOF(PATH_MAX,[0],[#include <limits.h>])
AX_CHECK_NUMVALOF(NSIG,[32],[#include <signal.h>])

AX_CHECK_NUMVALOF(AF_UNSPEC,   [0],    [#include <sys/types.h>
                                        #include <sys/socket.h>
                                        #include <netinet/in.h>])
AX_CHECK_NUMVALOF(AF_INET,     [-1],   [#include <sys/types.h>
                                        #include <sys/socket.h>
                                        #include <netinet/in.h>])
AX_CHECK_NUMVALOF(AF_INET6,    [-2],   [#include <sys/types.h>
                                        #include <sys/socket.h>
                                        #include <netinet/in.h>])
AX_CHECK_NUMVALOF(AF_PACKET,   [-3],   [#include <sys/types.h>
                                        #include <sys/socket.h>
                                        #include <netinet/in.h>])
AX_CHECK_NUMVALOF(AF_UNIX,     [-4],   [#include <sys/types.h>
                                        #include <sys/socket.h>
                                        #include <sys/un.h>])

if test "x${platform_win32}" = "xyes"
then
	AC_CHECK_SIZEOF(struct sockaddr_in,,[
		#include <winsock2.h>
		#include <ws2tcpip.h>])
	AC_CHECK_SIZEOF(struct sockaddr_in6,,[
		#include <winsock2.h>
		#include <ws2tcpip.h>])
	AC_CHECK_SIZEOF(struct sockaddr_in_x,,[
		#include <winsock2.h>
		#include <ws2tcpip.h>
		struct sockaddr_extra_t
		{
			unsigned short chan;
		}
		struct sockaddr_in_x { 
			struct sockaddr_in a; 
			struct sockaddr_extra_t u; 
		};
	])
	AC_CHECK_SIZEOF(struct sockaddr_in6_x,,[
		#include <winsock2.h>
		#include <ws2tcpip.h>
		struct sockaddr_extra_t
		{
			unsigned short chan;
		};
		struct sockaddr_in6_x { 
			struct sockaddr_in6 a;
			struct sockaddr_extra_t u; 
		};
	])

	AC_CHECK_SIZEOF(struct sockaddr_un,,[
		#include <winsock2.h>
		#include <ws2tcpip.h>])
	AC_CHECK_SIZEOF(struct sockaddr_ll,,[
		#include <winsock2.h>
		#include <ws2tcpip.h>])
	AC_CHECK_SIZEOF(struct sockaddr_dl,,[
		#include <winsock2.h>
		#include <ws2tcpip.h>])

	AC_CHECK_SIZEOF(socklen_t,, [
		#include <winsock2.h>
		#include <ws2tcpip.h>])
	if test ${ac_cv_sizeof_socklen_t} -gt 0
	then
		AX_CHECK_SIGN([socklen_t],
			[ AC_DEFINE(HIO_SOCKLEN_T_IS_SIGNED, 1, [Define if socklen_t is signed]) ],
			[ AS_ECHO_N("") ],
			[#include <winsock2.h>
			 #include <ws2tcpip.h>])
	fi

	AC_CHECK_SIZEOF(sa_family_t,, [
		#include <winsock2.h>
		#include <ws2tcpip.h>])
	if test ${ac_cv_sizeof_sa_family_t} -gt 0
	then
		AX_CHECK_SIGN([sa_family_t],
			[ AC_DEFINE(HIO_SA_FAMILY_T_IS_SIGNED, 1, [Define if sa_family_t is signed]) ],
			[ AS_ECHO_N("") ],
			[#include <winsock2.h>
			 #include <ws2tcpip.h>])
	fi

	AC_MSG_CHECKING([offset of sa_family in struct sockaddr])
	AC_COMPUTE_INT([ac_cv_offsetof_sa_family],
		[[offsetof(struct sockaddr, sa_family)]],
		[#include <stddef.h>
		 #include <winsock2.h>
		 #include <ws2tcpip.h>], [ac_cv_offsteof_sa_family=0])
	AC_MSG_RESULT([$ac_cv_offsetof_sa_family])
	AC_DEFINE_UNQUOTED(HIO_OFFSETOF_SA_FAMILY, ${ac_cv_offsetof_sa_family}, [offsetof(struct sockaddr, sa_family)])

else
	AC_CHECK_SIZEOF(struct sockaddr_in,,[
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netinet/in.h>])
	AC_CHECK_SIZEOF(struct sockaddr_in6,,[
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netinet/in.h>])
	AC_CHECK_SIZEOF(struct sockaddr_in_x,,[
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netinet/in.h>
		struct sockaddr_extra_t
		{
			unsigned short chan;
		};
		struct sockaddr_in_x { 
			struct sockaddr_in a; 
			struct sockaddr_extra_t u; 
		};
	])
	AC_CHECK_SIZEOF(struct sockaddr_in6_x,,[
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netinet/in.h>
		struct sockaddr_extra_t
		{
			unsigned short chan;
		};
		struct sockaddr_in6_x { 
			struct sockaddr_in6 a;
			struct sockaddr_extra_t u; 
		};
	])

	AC_CHECK_SIZEOF(struct sockaddr_un,,[
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <sys/un.h>])
	AC_CHECK_SIZEOF(struct sockaddr_ll,,[
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netpacket/packet.h>])
	AC_CHECK_SIZEOF(struct sockaddr_dl,,[
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <net/if_dl.h>])

	AC_CHECK_SIZEOF(socklen_t,, [
		#include <sys/types.h>
		#include <sys/socket.h>])
	if test ${ac_cv_sizeof_socklen_t} -gt 0
	then
		AX_CHECK_SIGN([socklen_t],
			[ AC_DEFINE(HIO_SOCKLEN_T_IS_SIGNED, 1, [Define if socklen_t is signed]) ],
			[ AS_ECHO_N("") ],
			[#include <sys/types.h>
			 #include <sys/socket.h>])
	fi

	AC_CHECK_SIZEOF(sa_family_t,, [
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netinet/in.h>])
	if test ${ac_cv_sizeof_sa_family_t} -gt 0
	then
		AX_CHECK_SIGN([sa_family_t],
			[ AC_DEFINE(HIO_SA_FAMILY_T_IS_SIGNED, 1, [Define if sa_family_t is signed]) ],
			[ AS_ECHO_N("") ],
			[#include <sys/types.h>
			 #include <sys/socket.h>
			 #include <netinet/in.h>])
	fi

	AC_MSG_CHECKING([offset of sa_family in struct sockaddr])
	AC_COMPUTE_INT([ac_cv_offsetof_sa_family],
		[[offsetof(struct sockaddr, sa_family)]],
		[#include <stddef.h>
		 #include <sys/types.h>
		 #include <sys/socket.h>], [ac_cv_offsteof_sa_family=0])
	AC_MSG_RESULT([$ac_cv_offsetof_sa_family])
	AC_DEFINE_UNQUOTED(HIO_OFFSETOF_SA_FAMILY, ${ac_cv_offsetof_sa_family}, [offsetof(struct sockaddr, sa_family)])
fi

dnl gcc 3.4.3 on opensolaris x86 gave this warning without -msse or 
dnl something similar.
dnl    SSE vector argument without SSE enabled changes the ABI
dnl it also ended with an internal compiler error while compiling 
dnl some expressions like 'v % base' where v is of the __int128_t type.
dnl so i've added this check to determine if the compiler has
dnl proper support for __int128_t.
dnl 
dnl Changing ac_cv_sizeof___int128_t here doesn't corret SIZEOF___INT128_T
dnl since it's already generated. It only affects HIO_SIZEOF___INT128_T below.
if test ${ac_cv_sizeof___int128_t} -gt 0
then
	AC_MSG_CHECKING([__int128_t with % and va_arg])
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([#include <stdarg.h>], [
			volatile __int128_t x, base;
			va_list ap;
			va_arg(ap, __int128_t);
			x = (__int128_t)&base;
			base = (__int128_t)&x;
			x = x % base;
			return (int)x;
		])],

		[
			AC_MSG_RESULT(yes)
		],
		[
			AC_MSG_RESULT(no)
			ac_cv_sizeof___int128_t=0
			ac_cv_sizeof___uint128_t=0
		]
	)
fi

dnl Some compilers doesn't seem to have full support for __float128 
dnl even if the type is available.
if test ${ac_cv_sizeof___float128} -gt 0
then
	AC_MSG_CHECKING([__float128 with linking])
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdio.h>]], [[
			__float128 x, y, z;
			z = x * y + x;
			printf ("%p", (char*)(int)z);
		]])],[AC_MSG_RESULT(yes)],[
			AC_MSG_RESULT(no)
			ac_cv_sizeof___float128=0
		
	])
fi

dnl Quadmath functions may not be in the default c library.
dnl Find a library containing them. Disable the float128 type
dnl if they are not available.
if test ${ac_cv_sizeof___float128} -gt 0
then
	AC_CHECK_FUNCS([quadmath_snprintf])
	if test "$ac_cv_func_quadmath_snprintf" = "no"
	then
		OLDLIBS="$LIBS"
		LIBS="$LIBM $LIBS"
		AC_CHECK_FUNCS([quadmath_snprintf])
		LIBS="$OLDLIBS"
	
		if test "$ac_cv_func_quadmath_snprintf" = "no"
		then
			dnl quadmath_snprintf not avalable in the 
			dnl standard math lib.

			AC_CHECK_LIB([quadmath], [quadmath_snprintf], [
				QUADMATH_LIBS="-lquadmath"
				LIBM="$LIBM -lquadmath"
				AC_DEFINE(HAVE_QUADMATH_SNPRINTF, 1)
			])

			if test "$ac_cv_lib_quadmath_quadmath_snprintf" = "no"
			then
				dnl quadmath_snprintf not avalable in the 
				dnl quadmath lib.

				ac_cv_sizeof___float128=0
			fi

		else
			QUADMATH_LIBS="$LIBM"
		fi
	fi
fi
AC_SUBST(QUADMATH_LIBS)

AC_DEFINE_UNQUOTED(HIO_SIZEOF_CHAR, ${ac_cv_sizeof_char}, [sizeof(char)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_SHORT, ${ac_cv_sizeof_short}, [sizeof(short)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_INT, ${ac_cv_sizeof_int}, [sizeof(int)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_LONG, ${ac_cv_sizeof_long}, [sizeof(long)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_LONG_LONG, ${ac_cv_sizeof_long_long}, [sizeof(long long)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF___INT8, ${ac_cv_sizeof___int8}, [sizeof(__int8)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF___INT16, ${ac_cv_sizeof___int16}, [sizeof(__int16)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF___INT32, ${ac_cv_sizeof___int32}, [sizeof(__int32)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF___INT64, ${ac_cv_sizeof___int64}, [sizeof(__int64)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF___INT128, ${ac_cv_sizeof___int128}, [sizeof(__int128)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF___INT128_T, ${ac_cv_sizeof___int128_t}, [sizeof(__int128_t)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF___UINT128_T, ${ac_cv_sizeof___uint128_t}, [sizeof(__uint128_t)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_VOID_P, ${ac_cv_sizeof_void_p}, [sizeof(void*)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_FLOAT, ${ac_cv_sizeof_float}, [sizeof(float)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_DOUBLE, ${ac_cv_sizeof_double}, [sizeof(double)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_LONG_DOUBLE, ${ac_cv_sizeof_long_double}, [sizeof(long double)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF___FLOAT128, ${ac_cv_sizeof___float128}, [sizeof(__float128)])

AC_DEFINE_UNQUOTED(HIO_SIZEOF_WCHAR_T, ${ac_cv_sizeof_wchar_t}, [sizeof(wchar_t)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_OFF_T, ${ac_cv_sizeof_off_t}, [sizeof(off_t)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_OFF64_T, ${ac_cv_sizeof_off64_t}, [sizeof(off64_t)])

AC_DEFINE_UNQUOTED(HIO_SIZEOF_MBSTATE_T, ${ac_cv_sizeof_mbstate_t}, [sizeof(mbstate_t)])
AC_DEFINE_UNQUOTED(HIO_MBLEN_MAX, ${ax_cv_numvalof_MB_LEN_MAX}, [MB_LEN_MAX])
AC_DEFINE_UNQUOTED(HIO_NSIG, ${ax_cv_numvalof_NSIG}, [NSIG])

AC_DEFINE_UNQUOTED(HIO_AF_UNSPEC, (${ax_cv_numvalof_AF_UNSPEC}), [AF_UNSPEC])
AC_DEFINE_UNQUOTED(HIO_AF_INET, (${ax_cv_numvalof_AF_INET}), [AF_INET])
AC_DEFINE_UNQUOTED(HIO_AF_INET6, (${ax_cv_numvalof_AF_INET6}), [AF_INET6])
AC_DEFINE_UNQUOTED(HIO_AF_PACKET, (${ax_cv_numvalof_AF_PACKET}), [AF_PACKET])
AC_DEFINE_UNQUOTED(HIO_AF_UNIX, (${ax_cv_numvalof_AF_UNIX}), [AF_UNIX])

AC_DEFINE_UNQUOTED(HIO_SIZEOF_STRUCT_SOCKADDR_IN, ${ac_cv_sizeof_struct_sockaddr_in}, [sizeof(struct sockaddr_in)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_STRUCT_SOCKADDR_IN6, ${ac_cv_sizeof_struct_sockaddr_in6}, [sizeof(struct sockaddr_in6)])

AC_DEFINE_UNQUOTED(HIO_SIZEOF_STRUCT_SOCKADDR_IN_X, ${ac_cv_sizeof_struct_sockaddr_in_x}, [sizeof(struct sockaddr_in_x)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_STRUCT_SOCKADDR_IN6_X, ${ac_cv_sizeof_struct_sockaddr_in6_x}, [sizeof(struct sockaddr_in6_x)])

AC_DEFINE_UNQUOTED(HIO_SIZEOF_STRUCT_SOCKADDR_UN, ${ac_cv_sizeof_struct_sockaddr_un}, [sizeof(struct sockaddr_un)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_STRUCT_SOCKADDR_LL, ${ac_cv_sizeof_struct_sockaddr_ll}, [sizeof(struct sockaddr_ll)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_STRUCT_SOCKADDR_DL, ${ac_cv_sizeof_struct_sockaddr_dl}, [sizeof(struct sockaddr_dl)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_SOCKLEN_T, ${ac_cv_sizeof_socklen_t}, [sizeof(socklen_t)])
AC_DEFINE_UNQUOTED(HIO_SIZEOF_SA_FAMILY_T, ${ac_cv_sizeof_sa_family_t}, [sizeof(sa_family_t)])


hio_package_version_major="`echo ${PACKAGE_VERSION} | cut -d. -f1`"
hio_package_version_minor="`echo ${PACKAGE_VERSION} | cut -d. -f2`"
hio_package_version_patch="`echo ${PACKAGE_VERSION} | cut -d. -f3`"
AC_SUBST(PACKAGE_VERSION_MAJOR, ${hio_package_version_major})
AC_SUBST(PACKAGE_VERSION_MINOR, ${hio_package_version_minor})
AC_SUBST(PACKAGE_VERSION_PATCH, ${hio_package_version_patch})

AC_DEFINE_UNQUOTED(HIO_PACKAGE_VERSION, "${PACKAGE_VERSION}", [Package version])
dnl AC_DEFINE_UNQUOTED(HIO_PACKAGE_VERSION_MAJOR, $(echo ${VERSION} | cut -d. -f1), [Major version number])
dnl AC_DEFINE_UNQUOTED(HIO_PACKAGE_VERSION_MINOR, $(echo ${VERSION} | cut -d. -f2), [Minor version number])
dnl AC_DEFINE_UNQUOTED(HIO_PACKAGE_VERSION_PATCH, $(echo ${VERSION} | cut -d. -f3), [Patch level])
AC_DEFINE_UNQUOTED(HIO_PACKAGE_VERSION_MAJOR, ${hio_package_version_major}, [Major version number])
AC_DEFINE_UNQUOTED(HIO_PACKAGE_VERSION_MINOR, ${hio_package_version_minor}, [Minor version number])
AC_DEFINE_UNQUOTED(HIO_PACKAGE_VERSION_PATCH, ${hio_package_version_patch}, [Patch level])

AC_DEFINE_UNQUOTED(HIO_PACKAGE_AUTHOR,"${PACKAGE_BUGREPORT}", [Author])
AC_DEFINE_UNQUOTED(HIO_PACKAGE_NAME,"${PACKAGE_NAME}", [package name])
AC_DEFINE_UNQUOTED(HIO_PACKAGE_URL, "${PACKAGE_URL}", [Project URL])

AC_C_BIGENDIAN(
	[AC_DEFINE([HIO_ENDIAN_BIG],[],[Big Endian])],
	[AC_DEFINE([HIO_ENDIAN_LITTLE],[],[Little Endian])],
	[AC_DEFINE([HIO_ENDIAN_UNKNOWN],[],[Unknown Endian])])

AC_ARG_ENABLE([debug], [AS_HELP_STRING([--enable-debug],[build the library in the debug mode (default. no)])],
	enable_debug_is=$enableval,enable_debug_is=no)
if test "$enable_debug_is" = "yes"
then
	[CFLAGS="$CFLAGS -g -D_DEBUG -UNDEBUG -DHIO_BUILD_DEBUG -U_FORTIFY_SOURCE"]
	AC_SUBST(BUILD_MODE, "debug")
else
	[CFLAGS="$CFLAGS -DNDEBUG -U_DEBUG -DHIO_BUILD_RELEASE -U_FORTIFY_SOURCE"]
	AC_SUBST(BUILD_MODE, "release")
fi

AC_ARG_ENABLE([wide-char], 
	[AS_HELP_STRING([--enable-wide-char],[Use the wide-character type as the default character type. one of yes, no, yes:2, yes:4, no:2, no:4 (default. yes)])],
	enable_wide_char_is=$enableval,
	enable_wide_char_is=yes
)
if test "${enable_wide_char_is}" = "yes:4"
then
	AC_DEFINE([HIO_WIDE_CHAR_SIZE],[4],[Wide-character type size])
	AC_DEFINE([HIO_ENABLE_WIDE_CHAR],[1],[Use the wide-character type as the default character type])
	if test "${platform_win32}" = "yes"
	then
		[CFLAGS="$CFLAGS -DUNICODE -D_UNICODE"]
	fi 
elif test "${enable_wide_char_is}" = "yes:2"
then
	AC_DEFINE([HIO_WIDE_CHAR_SIZE],[2],[Wide-character type size])
	AC_DEFINE([HIO_ENABLE_WIDE_CHAR],[1],[Use the wide-character type as the default character type])
	if test "${platform_win32}" = "yes"
	then
		[CFLAGS="$CFLAGS -DUNICODE -D_UNICODE"]
	fi
elif test "${enable_wide_char_is}" = "yes"
then
	AC_DEFINE([HIO_WIDE_CHAR_SIZE],[1],[Wide-character type size])
	AC_DEFINE([HIO_ENABLE_WIDE_CHAR],[1],[Use the wide-character type as the default character type])
	if test "${platform_win32}" = "yes"
	then
		[CFLAGS="$CFLAGS -DUNICODE -D_UNICODE"]
	fi
elif test "${enable_wide_char_is}" = "no:4"
then
	AC_DEFINE([HIO_WIDE_CHAR_SIZE],[4],[Wide-character type size])
elif test "${enable_wide_char_is}" = "no:2"
then
	AC_DEFINE([HIO_WIDE_CHAR_SIZE],[2],[Wide-character type size])
else
	AC_DEFINE([HIO_WIDE_CHAR_SIZE],[1],[Wide-character type size])
fi

AC_ARG_ENABLE([libunwind],
        [AS_HELP_STRING([--enable-libunwind],[use libunwind(default. no)])],
        enable_libunwind_is=$enableval,
        enable_libunwind_is=no
)
if test "x${enable_libunwind_is}" = "xyes"
then
        if test "x${ac_cv_header_libunwind_h}" = "xyes"  -a "${UNWIND_LIBS}" != ""
        then
                AC_DEFINE([HIO_ENABLE_LIBUNWIND],[],[use libunwind for backtracing stack frames])
        else
                enable_libunwind_is="no"
        fi
fi
AM_CONDITIONAL(ENABLE_LIBUNWIND, test "x${enable_libunwind_is}" = "xyes")

dnl ==== include pthread options to the default flags ====
dnl keep this as  the last option as it changes the default compile flags.
dnl otherwise, other tests may get affected if this option is on.
AC_ARG_ENABLE([pthread-flags], [AS_HELP_STRING([--enable-pthread-flags],[add thread flags to CFLAGS, LIBS(default. yes)])],
	enable_pthread_flags_is=$enableval,enable_pthread_flags_is=yes)
if test "$enable_pthread_flags_is" = "yes"
then
	[CFLAGS="$CFLAGS $PTHREAD_CFLAGS"]
	[LIBS="$LIBS $PTHREAD_LIBS"]
fi

dnl ====  subsititution of some key items ====
AC_SUBST(HIO_PROJECT_AUTHOR, "${PACKAGE_BUGREPORT}")
AC_SUBST(HIO_PROJECT_URL, "${PACKAGE_URL}")

AC_CONFIG_FILES([
	Makefile 
	Dockerfile
	lib/Makefile 
	bin/Makefile
	t/Makefile
	pkgs/hio.spec
])
AC_OUTPUT

[
echo 
echo "-[SUMMARY]---------------------------------------------------------------"
echo "Configured for ${host}"
echo "  Build mode : ${BUILD_MODE}"
echo "  Source directory: ${srcdir}"
echo "  Installation directory: ${prefix}"
echo "  C compiler: ${CC} ${CFLAGS}"
echo "  Math library: ${LIBM}"
echo "  Socket library: ${SOCKET_LIBS}"
echo "  Sendfile library: ${SENDFILE_LIBS}"
echo "  SSL library: ${SSL_LIBS}"
echo "-------------------------------------------------------------------------"
]
