#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PYTHONDONTWRITEBYTECODE=1
python3 "$SCRIPT_DIR/script/overlay_package.py" install "$@"
if [[ " $* " != *" --check "* ]]; then
    echo 'Source overlay includes camera/input/UI, Metal 3 and iOS build dependencies.'
    echo 'Rebuild required; copying source does not update existing executables or resources.'
    echo 'For a prepared target, run from its directory:'
    echo '  work/venv/bin/python tools/prepare_modules.py'
    echo '  work/venv/bin/python script/prepare_render_resources.py'
    echo '  ./script/build_and_run.sh --load-only'
    echo 'For first-time setup and iOS, see README.md.'
fi
