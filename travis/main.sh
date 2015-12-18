#!/bin/bash -e
# Main Travis test script.
# Written by Mike Frysinger <vapier@gentoo.org>
# Distributed under the terms of the GNU General Public License v2

. "${0%/*}"/lib.sh
. "${0%/*}"/arches.sh

BINUTILS_VER="2.25.1"

build_one() {
	local arch=$1
	local S=${PWD}

	travis_fold start ${arch}
	v mkdir -p build/${arch}
	pushd build/${arch} >/dev/null
	v "${S}"/configure \
		--enable-werror \
		--with-binutils-build-dir="${S}"/../prebuilts-binutils-libs/output/${BINUTILS_VER} \
		--target=${arch}-elf
	m
	m check
	popd >/dev/null
	travis_fold end ${arch}
}

main() {
	v --fold="git_clone_binutils" \
		git clone --depth=1 https://github.com/uclinux-dev/prebuilts-binutils-libs ../prebuilts-binutils-libs

	local a
	for a in "${ARCHES[@]}" ; do
		build_one "${a}"
	done
}
main "$@"
