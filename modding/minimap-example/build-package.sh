#!/bin/bash
# Package the source-only Lua example. No generated payload is stored in Git.
set -euo pipefail
ROOT=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")
python3 - "$ROOT" <<'PY'
from pathlib import Path
import zipfile
import sys
root = Path(sys.argv[1])
package = root / "modding/minimap-example/package"
out = root / "build/protodroid-minimap-example.zip"
out.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(package.rglob("*")):
        if path.is_file():
            archive.write(path, path.relative_to(package).as_posix())
print(out)
PY
