#!/bin/bash

outputFile="${1}"

# Include the root header.
echo '#include "NanoOsUser.h"' > "${outputFile}"
echo '' >> "${outputFile}"

# Grab all the function prototypes.
grep "^void\* [_a-zA-Z]*(void \*args) {" ${@} | sed -e 's/.*:\(.*\) *{$/\1;/' >> "${outputFile}"
echo '' >> "${outputFile}"

# Build the exports.
echo 'NanoOsOverlayExport exports[] = {' >> "${outputFile}"
functionNames="$(grep "^void\* [_a-zA-Z]*(void \*args) {" ${@} | sed -e 's/.*:void\* \([_a-zA-Z]*\)(void \*args) {/\1/' | sort)"
for functionName in ${functionNames}; do
	echo "  {\"${functionName}\", ${functionName}}," >> "${outputFile}"
done
echo '};' >> "${outputFile}"
echo '' >> "${outputFile}"

# Build the map.
echo '__attribute__((section(".overlay_header")))' >> "${outputFile}"
echo 'NanoOsOverlayMap overlayMap = {' >> "${outputFile}"
echo '  .header = {' >> "${outputFile}"
echo '    .osApi = NULL,' >> "${outputFile}"
echo '    .env = NULL,' >> "${outputFile}"
echo '    .overlay.blockDevice = NULL,' >> "${outputFile}"
echo '    .overlay.startBlock = 0,' >> "${outputFile}"
echo '    .overlay.numBlocks = 0,' >> "${outputFile}"
echo '    .version = (0 << 24) | (0 << 16) | (1 << 8) | (0 << 0),' >> "${outputFile}"
echo '    .magic = NANO_OS_OVERLAY_MAGIC,' >> "${outputFile}"
echo '  },' >> "${outputFile}"
echo '  .exports = exports,' >> "${outputFile}"
echo '  .numExports = sizeof(exports) / sizeof(exports[0]),' >> "${outputFile}"
echo '};' >> "${outputFile}"

