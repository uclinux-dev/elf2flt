#!/bin/bash
# List of all supported arches.
# Written by Mike Frysinger <vapier@gentoo.org>
# Distributed under the terms of the GNU General Public License v2

# The e1/nios targets are dead and no longer configure.
ARCHES=(
	arm
	bfin
#	e1
	h8300
	m68k
	microblaze
#	nios
	nios2
	sh
	sparc
	v850
)

# Expanded list of targets that we can use with configure.
TARGETS=${ARCHES[@]/%/-elf}
