#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

. ${SCRIPT_DIR}/lib-versions.sh

ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CODE="${ROOT}/code"

prepare()
{
    local URL="$1"
    local COMMAND="$2"
    local INCLUDE_PATTERN="$3"
    local EXCLUDE_PATTERN="$4"

    local FILENAME=$(basename ${URL})

    echo ${FILENAME}

    local EXTRACT_LOG=$(mktemp)
    curl -sL "${URL}" | tar -xvz -C "${CODE}" | tee "${EXTRACT_LOG}"
    local DIR=$(head -n1 "${EXTRACT_LOG}")

    echo $DIR
    if [ ! -d "${CODE}/${DIR}" ]; then
        DIR=$(dirname "$DIR")
    fi
    echo $DIR

    (
        cd ${CODE}/${DIR}
        [[ -n "$COMMAND" ]] && eval "$COMMAND"
        find . -type f -not -regex ${INCLUDE_PATTERN} -delete
        find . -type f -regex ${EXCLUDE_PATTERN} -delete
        find . -type d -empty -delete
        find . -xtype l -delete
    )
}

prepare "https://downloads.xiph.org/releases/ogg/libogg-${OGG_VERSION}.tar.gz" "./configure" "\./\(include\|src\)/.*\.[ch]"
prepare "https://downloads.xiph.org/releases/vorbis/libvorbis-${VORBIS_VERSION}.tar.gz" "./configure" "\./\(include\|lib\)/.*\.[ch]"
prepare "https://downloads.xiph.org/releases/opus/opus-${OPUS_VERSION}.tar.gz" "./configure" "\./\(celt\|include\|silk\|src\)/.*\.[ch]"
prepare "https://downloads.xiph.org/releases/opus/opusfile-${OPUSFILE_VERSION}.tar.gz" "./configure" "\./\(include\|src\)/.*\.[ch]"
prepare "https://zlib.net/zlib-${ZLIB_VERSION}.tar.gz" "./configure" "\./[^/]*\.[ch]" "\./gz.*\.[c]"
prepare "https://www.ijg.org/files/jpegsrc.v${JPEG_VERSION}.tar.gz" "./configure" "\./\(j.*\.c\|.*\.h\)" "\./\(jmem\(ansi\|dos\|mac\|name\)\|jpegtran\)\.c"
prepare "https://curl.se/download/curl-${CURL_VERSION}.tar.gz" "./configure --with-openssl" "\.*/include/.*\.h"
prepare "https://github.com/kcat/openal-soft/archive/refs/tags/${OPENAL_VERSION}.tar.gz" "" "\./include/AL/.*\.h"
