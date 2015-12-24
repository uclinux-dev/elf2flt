#!/bin/bash
# Common test code.
# Written by Mike Frysinger <vapier@gentoo.org>
# Distributed under the terms of the GNU General Public License v2

set -e

SUBDIR=$1

if [ -z "${abs_top_srcdir}" ]; then
	abs_top_srcdir=$(realpath "$(dirname "$(realpath "$0")")/../..")
	abs_top_builddir=${abs_top_srcdir}
fi
srcdir="${abs_top_srcdir}/tests/${SUBDIR}"
builddir="${abs_top_builddir}/tests/${SUBDIR}"
cd "${builddir}"

PATH="${builddir}:${abs_top_builddir}:${PATH}"

FAIL_COUNT=0

start() {
	printf '%s: testing %s ... ' "${SUBDIR}" "$1"
}

pass() {
	echo 'OK'
}

fail() {
	echo "FAIL ($*)"
	: $(( FAIL_COUNT += 1 ))
}

test_runner_exit() {
	if [ ${FAIL_COUNT} -ne 0 ]; then
		echo "${SUBDIR}: some tests (${FAIL_COUNT}) failed!"
		exit 1
	else
		echo "${SUBDIR}: all tests passed!"
	fi
}
trap test_runner_exit EXIT
