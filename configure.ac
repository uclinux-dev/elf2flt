dnl Process this file with autoconf to produce a configure script.
AC_INIT(elf2flt.c)

AC_ARG_WITH(zlib-prefix,
	AS_HELP_STRING([--with-zlib-prefix=<dir>], [path to installed zlib]),
	[ ac_zlib_prefix=$withval ],
	[ ac_zlib_prefix=NONE ]
)

AC_ARG_WITH(libbfd,
	AS_HELP_STRING([--with-libbfd=<file>], [path to libbfd.a library to use]),
	[ ac_libbfd=$withval ],
	[ ac_libbfd=NONE ]
)

AC_ARG_WITH(libiberty,
	AS_HELP_STRING([--with-libiberty=<file>], [path to libiberty.a library to use]),
	[ ac_libiberty=$withval ],
	[ ac_libiberty=NONE ]
)

AC_ARG_WITH(bfd-include-dir,
	AS_HELP_STRING([--with-bfd-include-dir=<dir>], [include path for correct bfd.h]),
	[ ac_bfd_include_dir=$withval ],
	[ ac_bfd_include_dir=NONE ]
)

AC_ARG_WITH(binutils-include-dir,
	AS_HELP_STRING([--with-binutils-include-dir=<dir>], [include path for binutils headers]),
	[ ac_binutils_include_dir=$withval ],
	[ ac_binutils_include_dir=NONE ]
)

AC_ARG_WITH(binutils-ldscript-dir,
	AS_HELP_STRING([--with-binutils-ldscript-dir=<dir>], [path to install elf2flt.ld]),
	[ ac_binutils_ldscript_dir=$withval ],
	[ ac_binutils_ldscript_dir=NONE ]
)

AC_ARG_WITH(binutils-build-dir,
	AS_HELP_STRING([--with-binutils-build-dir=<dir>], [path to compiled binutils tree]),
	[ ac_binutils_build_dir=$withval ],
	[ ac_binutils_build_dir=NONE ]
)

AC_ARG_ENABLE(ld-elf2flt-binary,
	AS_HELP_STRING([--disable-ld-elf2flt-binary], [use ld-elf2flt shell script]),
	[ use_ld_elf2flt_binary=$enableval ],
	[ use_ld_elf2flt_binary=yes ]
)
if test "$use_ld_elf2flt_binary" != "yes"; then
	AC_MSG_WARN([--disable-ld-elf2flt-binary is deprecated.
If you still need this, please let us know before we delete it.])
fi

dnl convert a yes/no variable to 1/0 for C code
var_yn_to_10() {
	if eval test \"\$$1\" = yes ; then
		eval $1=1
	else
		eval $1=0
	fi
}

AC_ARG_ENABLE(werror,
	AS_HELP_STRING([--disable-werror], [do not build with -Werror]),
	[ werror=$enableval ],
	[ werror=yes ]
)
var_yn_to_10 werror

AC_ARG_ENABLE(got-check,
	AS_HELP_STRING([--disable-got-check], [disable check for GOT (needed on H8)]),
	[ got_check=$enableval ],
	[ got_check=yes ]
)
var_yn_to_10 got_check

AC_ARG_ENABLE(emit-relocs,
	AS_HELP_STRING([--disable-emit-relocs], [don't use the --emit-relocs (-q) linker option]),
	[ emit_relocs=$enableval ],
	[ emit_relocs=yes ]
)
var_yn_to_10 emit_relocs

AC_ARG_ENABLE(emit-ctor-dtor,
	AS_HELP_STRING([--enable-emit-ctor-dtor], [manually create ctor/dtor list]),
	[ emit_ctor_dtor=$enableval ],
	[ emit_ctor_dtor=no ]
)
var_yn_to_10 emit_ctor_dtor

AC_ARG_ENABLE(always-reloc-text,
	AS_HELP_STRING([--enable-always-reloc-text], [always process text relocs ignoring pic/got (needed on Blackfin)]),
	[ always_reloc_text=$enableval ],
	[
	case $target in
		bfin*) always_reloc_text=yes;;
		*)     always_reloc_text=no;;
	esac
	]
)
var_yn_to_10 always_reloc_text

AC_CANONICAL_HOST
AC_CANONICAL_TARGET

dnl Checks for programs.
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_INSTALL

if test "$ac_binutils_build_dir" != "NONE"; then
  test "$ac_libbfd"               = "NONE" && ac_libbfd="$ac_binutils_build_dir/bfd/libbfd.a"
  test "$ac_libiberty"            = "NONE" && ac_libiberty="$ac_binutils_build_dir/libiberty/libiberty.a"
  test "$ac_bfd_include_dir"      = "NONE" && ac_bfd_include_dir="$ac_binutils_build_dir/bfd"
  test "$ac_binutils_include_dir" = "NONE" && ac_binutils_include_dir="$ac_binutils_build_dir/include"
fi

dnl Checks for libraries.  Look for zlib before binutils libs in case they
dnl need zlib themselves.
if test "$ac_zlib_prefix" = "NONE"; then
  AC_CHECK_LIB(z, deflate)
else
  LIBS="-L$ac_zlib_prefix/lib -lz $LIBS"
fi
if test "$ac_libiberty" = "NONE"; then
  AC_CHECK_LIB(iberty, objalloc_create)
  ac_libiberty=auto
else
  LIBS="$ac_libiberty $LIBS"
fi
if test "$ac_libbfd" = "NONE"; then
  AC_CHECK_LIB(bfd, bfd_openr)
  ac_libbfd=auto
else
  LIBS="$ac_libbfd $LIBS"
fi

bfd_include_dir=
if test "$ac_bfd_include_dir" != "NONE"; then
  bfd_include_dir=$(cd "$ac_bfd_include_dir" && pwd)
fi

binutils_include_dir=
if test "$ac_binutils_include_dir" != "NONE"; then
  binutils_include_dir=$(cd "$ac_binutils_include_dir" && pwd)
fi

zlib_include_dir=
if test "$ac_zlib_prefix" != "NONE"; then
  zlib_include_dir="-I$ac_zlib_prefix/include"
fi

binutils_ldscript_dir=
if test "$ac_binutils_ldscript_dir" = "NONE"; then
  ac_binutils_ldscript_dir="\${TOOLDIR}/../${target_alias}/lib"
fi
binutils_ldscript_dir="$ac_binutils_ldscript_dir"

if test "$ac_libbfd" = "NONE" -o "$ac_libiberty" = "NONE" ; then
	AC_MSG_ERROR([

You need to specify the location of the libfd.a and libiberty.a
host libraries from the binutils package.

Run configure again specifying these options:

  ./configure --target=<ARCH> --with-bfd-include-dir=<dir> --with-libbfd=<libbfd.a> --with-libiberty=<libiberty.a>
])
fi

if test "$ac_bfd_include_dir" = "NONE" ; then
	AC_MSG_ERROR([

You need to specify the location of the bfd.h header from a
configured/compiled version of the binutils package for your target.
Without this your elf2flt may crash as it will try to use the
systems bfd.h which may be from a different binutils package.

Run configure again specifying these options:

  ./configure --target=<ARCH> --with-bfd-include-dir=<dir> --with-libbfd=<libbfd.a> --with-libiberty=<libiberty.a>
])
fi

SYMBOL_PREFIX=
case $target in
	bfin*)
		SYMBOL_PREFIX=_
		;;
esac

dnl Make sure we resolve system symbols before libiberty/libbfd ones.
dnl Otherwise, things like getopt get screwed up because the system headers
dnl redirect some functions to the system symbols, but other local symbols
dnl come from libiberty/libbfd.
dnl int getopt(int, char * const [], const char *) __asm("_" "getopt" "$UNIX2003");
AC_CHECK_LIB(c, malloc, LIBS="-lc $LIBS")
AC_CHECK_LIB(dl, dlopen, LIBS="$LIBS -ldl")

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h unistd.h bfd.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

HAVE_BFD_XTENSA_PDIFF_RELOCS=0
case $target in
	xtensa*)
		AS_VAR_COPY([OLD_CPPFLAGS], [CPPFLAGS])
		AS_VAR_SET([CPPFLAGS], ["-I$bfd_include_dir -I$binutils_include_dir $CPPFLAGS"])
		AC_CHECK_DECL([R_XTENSA_PDIFF8],
			      [HAVE_BFD_XTENSA_PDIFF_RELOCS=1],,
			      [#include "bfd.h"
			       #include "elf/xtensa.h"])
		AS_VAR_COPY([CPPFLAGS], [OLD_CPPFLAGS])
		;;
esac

dnl Checks for library functions.
AC_FUNC_VPRINTF

AC_CHECK_FUNCS([ \
	dcgettext \
	getline \
	libintl_dgettext \
	strsignal \
])

dnl Various bfd section macros and functions like bfd_section_size() have been
dnl modified starting with binutils >= 2.34.
dnl Check if the prototypes take a bfd argument.
if test "$binutils_build_dir" != "NONE"; then
    CFLAGS="-I$binutils_include_dir -I$bfd_include_dir $CFLAGS"
fi

AC_TRY_COMPILE([#include <bfd.h>],
 [const asection *sec; bfd_section_size(sec);],
 bfd_section_api_takes_bfd=no,
 bfd_section_api_takes_bfd=yes)
if test "$bfd_section_api_takes_bfd" = "yes" ; then
  AC_DEFINE(HAVE_BFD_SECTION_API_TAKES_BFD, 1,
   [define to 1 for binutils < 2.34])
fi

if test "$GCC" = yes ; then
	CFLAGS="-Wall $CFLAGS"
	if test "$werror" = 1 ; then
		CFLAGS="-Werror -Wno-error=unused-label $CFLAGS"
	fi
fi

dnl Subsitute values
AC_SUBST(target)
AC_SUBST(target_alias)
AC_SUBST(target_cpu)
AC_SUBST(target_os)
AC_SUBST(target_vendor)
AC_SUBST(bfd_include_dir)
AC_SUBST(binutils_include_dir)
AC_SUBST(zlib_include_dir)
AC_SUBST(binutils_ldscript_dir)
AC_SUBST(use_ld_elf2flt_binary)
AC_SUBST(got_check)
AC_SUBST(emit_relocs)
AC_SUBST(emit_ctor_dtor)
AC_SUBST(always_reloc_text)
AC_SUBST(SYMBOL_PREFIX)
AC_SUBST(HAVE_BFD_XTENSA_PDIFF_RELOCS)

AC_OUTPUT(ld-elf2flt.sh:ld-elf2flt.in Makefile elf2flt.ld)

