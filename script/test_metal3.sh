#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p work/tmp
export TMPDIR="$PWD/work/tmp/"
export ZELDA_PIPELINE_CACHE=0
binary=work/tmp/test_nvn_m3_index
uniform_binary=work/tmp/test_nvn_m3_uniform
metalfx_binary=work/tmp/test_nvn_m3_metalfx
predicate_binary=work/tmp/test_nvn_m3_predicate
dispatch_binary=work/tmp/test_nvn_m3_dispatch
trap 'rm -f "$binary" "$uniform_binary" "$metalfx_binary" "$predicate_binary" "$dispatch_binary"; rm -rf "$binary.dSYM" "$uniform_binary.dSYM" "$metalfx_binary.dSYM" "$predicate_binary.dSYM" "$dispatch_binary.dSYM"' EXIT
python=${ZELDA_PYTHON:-work/venv/bin/python}
"$python" script/prepare_graphics.py
"$python" - <<'PY'
from pathlib import Path
source = Path('host/nvn_conditional_draw.inc').read_text()
planner = source.split('#if !DM_NVN_M3\nstatic uint64_t zelda_conditional_encode(')[0]
Path('work/tmp/m3_conditional_plan.inc').write_text(planner)
PY
clang -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -fobjc-arc \
    -ivfsoverlay work/build/graphics-overlay.json -I "$PWD/core/gfx/nvn" -I "$PWD/core/gfx/nvn/include" \
    tests/test_nvn_m3_index.m -framework Foundation -framework Metal -o "$binary"
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 "$binary"
clang -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -fobjc-arc \
    -ivfsoverlay work/build/graphics-overlay.json -I "$PWD/core/gfx/nvn" -I "$PWD/core/gfx/nvn/include" \
    -I "$PWD/work/tmp" -I "$PWD/host" \
    tests/test_nvn_m3_predicate.m -framework Foundation -framework Metal -o "$predicate_binary"
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 "$predicate_binary"
clang -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -fobjc-arc \
    tests/test_nvn_m3_dispatch.m -framework Foundation -framework Metal -o "$dispatch_binary"
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 "$dispatch_binary"
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ZELDA_M3_DISPATCH_BATCH=0 "$dispatch_binary"
clang -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -fobjc-arc \
    -ivfsoverlay work/build/graphics-overlay.json -I "$PWD/core/gfx/nvn" -I "$PWD/core/gfx/nvn/include" \
    tests/test_nvn_m3_uniform.m -framework Foundation -framework Metal -o "$uniform_binary"
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 "$uniform_binary"
clang -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -fobjc-arc \
    tests/test_nvn_m3_metalfx.m -framework Foundation -framework Metal -framework MetalFX -o "$metalfx_binary"
MTL_DEBUG_LAYER=1 "$metalfx_binary"
