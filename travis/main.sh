#!/bin/bash -e
# Main Travis test script.
# Written by Mike Frysinger <vapier@gentoo.org>
# Distributed under the terms of the GNU General Public License v2

. "${0%/*}"/lib.sh
. "${0%/*}"/arches.sh

BINUTILS_VERS=(
	2.25.1
	2.26.1
	2.27
	2.28.1
	2.29.1
	2.30
	2.31.1
)

build_one() {
	local arch=$1
	local bver=$2
	local S=${PWD}
	local build="build/${arch}-${bver}"

	travis_fold start ${arch} "binutils-${bver}"
	v mkdir -p "${build}"
	pushd "${build}" >/dev/null
	if ! v "${S}"/configure \
		--enable-werror \
		--with-binutils-build-dir="${S}"/../prebuilts-binutils-libs/output/${bver} \
		--target=${arch}-elf ; then
		cat config.log
		exit 1
	fi
	m
	m check
	popd >/dev/null
	travis_fold end ${arch}
}

main() {
	if [[ ! -d ../prebuilts-binutils-libs ]] ; then
		v --fold="git_clone_binutils" \
			git clone --depth=1 https://github.com/uclinux-dev/prebuilts-binutils-libs ../prebuilts-binutils-libs
	fi

	# Newer compilers default to PIE which the prebuilts aren't using.
	export CFLAGS='-O2 -pipe -no-pie'

	local a b
	for a in "${ARCHES[@]}" ; do
		for b in "${BINUTILS_VERS[@]}" ; do
			build_one "${a}" "${b}"
		done
	done
}
main "$@"
