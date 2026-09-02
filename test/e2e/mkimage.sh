#!/bin/bash
#
# Build a NanoOs FAT32 disk image for end-to-end tests, and (re)build the
# simulator binary it runs on.
#
# This is the image-construction half of ../../buildsim, factored out so the
# E2E harness can produce a *named*, persistent image instead of a mktemp
# file that buildsim deletes on exit.  Keep the two in sync if buildsim's
# partition / mtools invocation changes.
#
# Usage:  test/e2e/mkimage.sh <output-image> [hostname] [blockFilesystem]
#
set -e

outImage="${1}"
hostname="${2:-nanoe2e}"
blockFilesystem="${3:-overlay}"

if [ -z "${outImage}" ]; then
	echo "Usage: ${0} <output-image> [hostname] [blockFilesystem]" >&2
	exit 1
fi

# Resolve repo root from this script's location.
repoRoot="$(cd "$(dirname "${0}")/../.." && pwd)"
cd "${repoRoot}"

filesystem="fat32"
imageSizeMB=100
partitionOffsetMB=2
blocksPerOverlay=16

if [ ! -f "usr/src/filesystems/${blockFilesystem}/makefile" ]; then
	echo "ERROR: No block filesystem at usr/src/filesystems/${blockFilesystem}." >&2
	exit 1
fi

if [ "${blockFilesystem}" = "contiguous" ]; then
	filesystemLinkerScript="NanoOsSimContiguous.ld"
else
	filesystemLinkerScript="NanoOsSim.ld"
fi

echo "mkimage: building applications + simulator..."
make -j "$(nproc)" -C usr/src clean
make -j "$(nproc)" -C "usr/src/filesystems/${blockFilesystem}" clean
make -j "$(nproc)" -C usr/src COMPILE=gcc LINK=ld OBJCOPY=objcopy \
	OBJDUMP=objdump SIZE=size LINKER_SCRIPT=NanoOsSim.ld overlays
make -j "$(nproc)" -C "usr/src/filesystems/${blockFilesystem}" COMPILE=gcc \
	LINK=ld OBJCOPY=objcopy OBJDUMP=objdump SIZE=size \
	LINKER_SCRIPT="${filesystemLinkerScript}" FILESYSTEM="${filesystem}" overlays
make -j "$(nproc)" -C sim

echo "mkimage: writing ${outImage}..."
dd if=/dev/zero of="${outImage}" bs=1M count=${imageSizeMB} status=none

osRodataBin="sim/bin/NanoOs_rodata.bin"

echo "${partitionOffsetMB}MiB,+,c" | sfdisk "${outImage}" >/dev/null

partitionOffsetBytes=$(( partitionOffsetMB * 1024 * 1024 ))
fatImage="${outImage}@@${partitionOffsetBytes}"

MTOOLS_SKIP_CHECK=1 mformat -i "${fatImage}" -F -v NanoOs ::
MTOOLS_SKIP_CHECK=1 mmd    -i "${fatImage}" ::/usr
MTOOLS_SKIP_CHECK=1 mcopy  -i "${fatImage}" -s usr/bin ::/usr/bin
MTOOLS_SKIP_CHECK=1 mcopy  -i "${fatImage}" -s usr/lib ::/usr/lib
MTOOLS_SKIP_CHECK=1 mcopy  -i "${fatImage}" "${osRodataBin}" ::/usr/lib/
MTOOLS_SKIP_CHECK=1 mmd    -i "${fatImage}" ::/etc
printf '%s' "${hostname}" \
	| MTOOLS_SKIP_CHECK=1 mcopy -i "${fatImage}" - ::/etc/hostname
printf '\\s \\r \\n \\l\n\n' \
	| MTOOLS_SKIP_CHECK=1 mcopy -i "${fatImage}" - ::/etc/issue

if [ "${blockFilesystem}" = "contiguous" ]; then
	dd if="usr/filesystem/contiguous-filesystem/0.overlay" of="${outImage}" \
		bs=512 seek=1 conv=notrunc status=none
else
	for blockOverlay in usr/filesystem/${blockFilesystem}-filesystem/*; do
		overlayId="$(basename "${blockOverlay}" .overlay)"
		startBlock=$(( (overlayId * blocksPerOverlay) + 1 ))
		dd if="${blockOverlay}" of="${outImage}" bs=512 seek=${startBlock} \
			conv=notrunc status=none
	done
fi

echo "mkimage: done -> ${outImage}"
