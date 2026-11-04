#!/usr/bin/env bash
# Reconstruct the exact pre-candidate presenter from pinned DXVK and the Phase 10 baseline patch.
set -euo pipefail

if [[ $# -lt 2 || $# -gt 6 ]]; then
  echo "usage: $0 PATCHED_DXVK_ROOT OUTPUT [CALLS [ROUNDS [SANITIZER [WORK_ROOT]]]]" >&2
  exit 2
fi

DXVK_ROOT=$1
OUTPUT=$2
CALLS=${3:-200000}
ROUNDS=${4:-16}
SANITIZER=${5:-none}
WORK_ROOT=${6:-/tmp/protodroid-present-stats-baseline}
BASELINE=6e470f4eeb6efa570d8b3bb0006cc089bb4f0825
REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

[[ $(git -C "$DXVK_ROOT" rev-parse HEAD) == c707d9026f33b6ab89639f154b6ac5f6326fa037 ]]
if ! git -C "$REPO_ROOT" cat-file -e "$BASELINE^{commit}" 2>/dev/null; then
  git -C "$REPO_ROOT" fetch -q --depth=1 origin "$BASELINE"
fi
rm -rf "$WORK_ROOT"
mkdir -p "$OUTPUT"
git -c advice.detachedHead=false clone -q --shared "$DXVK_ROOT" "$WORK_ROOT"
git -C "$REPO_ROOT" show "$BASELINE:android/patches/dxvk-android.patch" > "$OUTPUT/baseline-dxvk-android.patch"
git -C "$WORK_ROOT" apply \
  --include=src/dxvk/dxvk_presenter.cpp --include=src/d3d9/d3d9_swapchain.cpp \
  "$OUTPUT/baseline-dxvk-android.patch"
cp "$WORK_ROOT/src/dxvk/dxvk_presenter.cpp" "$OUTPUT/baseline-dxvk_presenter.cpp"
cp "$WORK_ROOT/src/d3d9/d3d9_swapchain.cpp" "$OUTPUT/baseline-d3d9_swapchain.cpp"

python3 "$REPO_ROOT/tools/test_dxvk_present_stats.py" \
  "$OUTPUT/baseline-dxvk_presenter.cpp" "$DXVK_ROOT/src/dxvk/dxvk_presenter.cpp" \
  --before-d3d9 "$OUTPUT/baseline-d3d9_swapchain.cpp" --after-d3d9 "$DXVK_ROOT/src/d3d9/d3d9_swapchain.cpp" \
  --output "$OUTPUT/result" --calls "$CALLS" --rounds "$ROUNDS" --sanitizer "$SANITIZER"
