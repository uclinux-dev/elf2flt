#!/bin/bash
# Test behavior of flthdr.
# Written by Mike Frysinger <vapier@gentoo.org>
# Distributed under the terms of the GNU General Public License v2

. "${0%/*}"/../lib.sh "flthdr"

start 'cli no flags'
if flthdr >stdout 2>stderr; then
	fail 'exit'
elif [[ -s stdout || ! -s stderr ]]; then
	fail 'output'
else
	rm stdout stderr
	pass
fi

start 'cli help'
if ! flthdr -h >stdout 2>stderr; then
	fail 'exit'
elif [[ -s stderr || ! -s stdout ]]; then
	fail 'output'
else
	rm stdout stderr
	pass
fi

start 'basic flags'
generate -o basic.bin \
	-e 0x1234 \
	-d 0x5678 \
	-D 0xabcd \
	-r 0x3456 \
	-B 0xdef14565 \
	-s 0x10aa \
	-f 0xff
if ! flthdr basic.bin > basic.out; then
	fail 'run'
elif ! diff -u basic.out "${srcdir}"/basic.good; then
	fail 'diff'
else
	rm basic.bin basic.out
	pass
fi

start 'multi output'
generate -o multi.bin
if ! flthdr multi.bin multi.bin multi.bin > multi.out; then
	fail 'run'
elif ! diff -u multi.out "${srcdir}"/multi.good; then
	fail 'diff'
else
	rm multi.bin multi.out
	pass
fi
