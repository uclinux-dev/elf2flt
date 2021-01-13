# elf2flt

[![Build Status](https://travis-ci.org/uclinux-dev/elf2flt.svg?branch=main)](https://travis-ci.org/uclinux-dev/elf2flt)

Copyright (C) 2001-2003, SnapGear (www.snapgear.com)
David McCullough <ucdevel@gmail.com>
Greg Ungerer <gerg@uclinux.org>

This is Free Software, under the GNU Public License v2 or greater.  See
[LICENSE.TXT](LICENSE.TXT) for more details.

Elf2flt with PIC, ZFLAT and full reloc support. Currently supported
targets include: m68k/ColdFire, ARM, Sparc, NEC v850, MicroBlaze, 
h8300, SuperH, and Blackfin.

## Compiling

You need an appropriate libbfd.a and libiberty.a for your target to 
build this tool. They are normally part of the binutils package.

To compile elf2flt do:

    ./configure --target=<ARCH> --with-libbfd=<libbfd.a> --with-libiberty=<libiberty.a>
    make
    make install

The <ARCH> argument to configure specifies what the target architecture is.
This should be the same target as you used to build the binutils and gcc
cross development tools. The `--with-libbfd` and `--with-libiberty` arguments
specify where the libbfd.a and libiberty.a library files are to use.

## Files

* README.md    - this file
* configure    - autoconf configuration shell script
* configure.ac - original autoconf file
* config.*     - autoconf support scripts
* Makefile.in  - Makefile template used by configure
* elf2flt.c    - the source
* flthdr.c     - flat header manipulation program
* flat.h       - header from uClinux kernel sources
* elf2flt.ld   - an example linker script that works for C/C++ and uClinux
* ld-elf2flt   - A linker replacement that implements a `-elf2flt` option for
                 the linker and runs elf2flt automatically for you.  It auto
                 detects PIC/non-PIC code and adjusts its option accordingly.
                 It uses the environment variable `FLTFLAGS` when running
                 elf2flt.  It runs /.../<ARCH>-ld.real to do the actual linking.
* stubs.c      - Support for various functions that your OS might be missing.

## Tips

The ld-elf2flt produces 2 files as output.  The binary flat file X, and
X.gdb which is used for debugging and PIC purposes.

The `-p` option requires an elf executable linked at address 0.  The
elf2flt.ld provided will generate the correct format binary when linked
with the real linker with *no* `-r` option for the linker.

The `-r` flag can be added to PIC builds to get contiguous code/data.  This
is good for loading application symbols into gdb (add-symbol-file XXX.gdb).

## Support

You can use the github site to file issues and send pull requests, and the
[uclinux-dev@uclinux.org](http://mailman.uclinux.org/mailman/listinfo/uclinux-dev)
mailing list to contact the developers.
