#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
image=${WING_DIAG_BUILDER:-wing-usb-console-builder:trixie}
cache=${1:-$root/build/linux-t55-recovery}
out=$root/build/output-t55-diag
mkdir -p "$out"
# Linux's source tree needs a case-sensitive filesystem; use a disposable volume.
volume=$(docker volume create)
trap 'docker volume rm "$volume" >/dev/null' EXIT
docker run --rm --network none \
    -v "$root:/src:ro" -v "$(realpath "$cache"):/archives:ro" \
    -v "$out:/out" -v "$volume:/work" \
    "$image" bash /src/tools/wing-t55-diag/build-linux.sh
