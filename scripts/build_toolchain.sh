#!/usr/bin/env bash
#
# Copyright (c) 2026 BOSC & ICT, CAS
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

LLVM_SUBMODULE="$REPO_ROOT/toolchain/AME_llvm"
INSTALL_DIR="$REPO_ROOT/toolchain/install"
SUBMODULE_SSH_URL="git@github.com:OpenXiangShan/AME_llvm.git"
SUBMODULE_HTTPS_URL="https://github.com/OpenXiangShan/AME_llvm.git"

LLVM_ARCHIVE="$LLVM_SUBMODULE/ame_toolchain.tar.gz"
LLVM_ARCHIVE_PART_PREFIX="$LLVM_ARCHIVE.part"
LLVM_ARCHIVE_CHECKSUM="$LLVM_SUBMODULE/ame_toolchain.sha256"
ARCHIVE_TO_EXTRACT="$LLVM_ARCHIVE"
MERGED_ARCHIVE=""

# Prefer SSH for submodule access, but keep HTTPS available for environments
# without GitHub SSH access. The rewrite is scoped to this invocation.
git -C "$REPO_ROOT" submodule sync --recursive
if ! git -C "$REPO_ROOT" submodule update --init --recursive; then
    echo "[WARN] SSH submodule access failed; retrying with HTTPS" >&2
    git -C "$REPO_ROOT" \
        -c "url.$SUBMODULE_HTTPS_URL.insteadOf=$SUBMODULE_SSH_URL" \
        submodule update --init --recursive
fi

if [[ ! -f "$LLVM_ARCHIVE" ]]; then
    MERGED_ARCHIVE=$(mktemp "$REPO_ROOT/toolchain/.ame_toolchain.XXXXXX.tar.gz")
    trap 'rm -f -- "$MERGED_ARCHIVE"' EXIT
    cat $(find "$LLVM_SUBMODULE" -maxdepth 1 -type f \
        -name "$(basename "$LLVM_ARCHIVE_PART_PREFIX")*" | sort -V) \
        > "$MERGED_ARCHIVE"
    (
        cd "$LLVM_SUBMODULE"
        printf '%s  %s\n' \
            "$(awk 'NF { print $1; exit }' "$LLVM_ARCHIVE_CHECKSUM")" \
            "$MERGED_ARCHIVE" | sha256sum -c -
    )
    ARCHIVE_TO_EXTRACT="$MERGED_ARCHIVE"
fi

rm -rf -- "$INSTALL_DIR"
mkdir -p -- "$INSTALL_DIR"
tar -xzf "$ARCHIVE_TO_EXTRACT" -C "$INSTALL_DIR"
rm -f -- "$INSTALL_DIR/ame_toolchain.tar.gz"

echo "[PASS] Toolchain extracted to $INSTALL_DIR"
