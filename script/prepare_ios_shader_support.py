"""Bundle verified movie interface variants so iOS never needs the Mac translator for them."""
import json
from pathlib import Path
import shutil

from build_shaders import ROOT, chain
from shader_pairs import MOVIE_PAIRS, name


def prepare(root=ROOT):
    shaders = root / 'work/shaders'
    version = chain()
    library = shaders / ('b-' + version)
    requests = shaders / 'variants'
    requests.mkdir(parents=True, exist_ok=True)
    for vs, ps in MOVIE_PAIRS:
        (requests / (name(vs, 0, ps) + '.json')).write_text(json.dumps(dict(base=vs, mask=0, next=ps)) + '\n')
    keys = {key for vs, ps in MOVIE_PAIRS for key in (name(vs, 0, ps), ps)}
    files = [key + '_b' + suffix for key in sorted(keys) for suffix in ('.metallib', '.json')]
    if any(not (library / file).is_file() for file in files):
        from compile_runtime_shaders import compile_pending
        compile_pending()
    output = root / 'work/ios/shader-support'
    if output.exists():
        shutil.rmtree(output)
    target = output / ('b-' + version)
    target.mkdir(parents=True)
    for file in files:
        source = library / file
        if not source.is_file() or not source.stat().st_size:
            raise RuntimeError('Missing iOS movie shader support: ' + str(source))
        shutil.copyfile(source, target / file)
    print('iOS movie shader support:', version, len(files), 'files')


if __name__ == '__main__':
    prepare()
