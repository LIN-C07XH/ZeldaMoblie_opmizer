#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PYTHONDONTWRITEBYTECODE=1
python3 "$SCRIPT_DIR/script/overlay_package.py" uninstall "$@"
if [[ " $* " != *" --check "* ]]; then
    echo 'Rebuild modules and executables after restoring source.'
    echo 'Regenerate resources with the restored script/prepare_render_resources.py.'
fi
