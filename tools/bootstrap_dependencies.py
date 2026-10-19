#!/usr/bin/env python3
"""Explicit, hash-verified dependency preparation. Python 3.12+; stdlib only."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MAX_DOWNLOAD = 128 * 1024 * 1024


def sha256(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def within(root: Path, relative: str) -> Path:
    target = (root / relative).resolve()
    if not target.is_relative_to(root.resolve()) or target == root.resolve():
        raise ValueError('dependency destination must stay below .deps')
    return target


def prepare(item: dict, cache: Path, offline: bool) -> None:
    archive = within(cache / 'downloads', item['name'])
    archive.parent.mkdir(parents=True, exist_ok=True)
    expected = item['sha256']
    if not archive.exists() or sha256(archive) != expected:
        if offline:
            raise RuntimeError(f"{item['name']}: verified download unavailable in offline mode")
        with tempfile.NamedTemporaryFile(dir=archive.parent, delete=False) as temp:
            temporary = Path(temp.name)
            try:
                request = urllib.request.Request(item['url'], headers={'User-Agent': 'NetArena-dependency-bootstrap/1'})
                with urllib.request.urlopen(request, timeout=60) as response:
                    size = 0
                    while block := response.read(1024 * 1024):
                        size += len(block)
                        if size > MAX_DOWNLOAD:
                            raise RuntimeError('dependency download exceeds size limit')
                        temp.write(block)
                temp.flush()
                if sha256(temporary) != expected:
                    raise RuntimeError(f"{item['name']}: SHA-256 mismatch")
                temporary.replace(archive)
            finally:
                temporary.unlink(missing_ok=True)
    destination = within(cache, item['destination'])
    if item['kind'] == 'file':
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or sha256(destination) != expected:
            shutil.copyfile(archive, destination)
    elif item['kind'] in ('tar', 'macos-app-zip'):
        marker = destination / '.netarena-source-sha256'
        if marker.exists() and marker.read_text().strip() == expected:
            print(f"Ready: {item['name']} {item['version']}")
            return
        with tempfile.TemporaryDirectory(dir=cache) as temporary:
            staging = Path(temporary)
            if item['kind'] == 'tar':
                with tarfile.open(archive) as bundle:
                    bundle.extractall(staging, filter='data')
                source = within(staging, item['prefix'])
            else:
                source = staging / 'payload'
                source.mkdir()
                with zipfile.ZipFile(archive) as bundle:
                    for member in bundle.namelist():
                        within(source, member)
                subprocess.run(['/usr/bin/ditto', '-xk', str(archive), str(source)], check=True)
                if not (source / 'Godot.app/Contents/MacOS/Godot').is_file():
                    raise RuntimeError('Godot executable missing from official archive')
            if not source.is_dir():
                raise RuntimeError(f"{item['name']}: expected archive root missing")
            (source / '.netarena-source-sha256').write_text(expected + '\n')
            if destination.exists():
                shutil.rmtree(destination)
            source.rename(destination)
    else:
        raise ValueError('unsupported dependency artifact kind')
    print(f"Ready: {item['name']} {item['version']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', action='store_true', help='also prepare pinned godot-cpp bindings (not the editor)')
    parser.add_argument('--godot-runtime', action='store_true', help='also prepare official Godot 4.5.1 for macOS, locally in .deps')
    parser.add_argument('--offline', action='store_true', help='use verified .deps/downloads artifacts only')
    args = parser.parse_args()
    if args.godot_runtime and sys.platform != 'darwin':
        parser.error('--godot-runtime currently supports macOS only; supply your own Godot executable elsewhere')
    lock = json.loads((ROOT / 'deps.lock.json').read_text())
    if lock['format'] != 1:
        raise ValueError('unsupported lock format')
    cache = ROOT / '.deps'
    cache.mkdir(exist_ok=True)
    for item in lock['dependencies']:
        if (item['group'] == 'core' or
            item['group'] == 'godot' and (args.godot or args.godot_runtime) or
            item['group'] == 'godot-runtime' and args.godot_runtime):
            prepare(item, cache, args.offline)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, tarfile.TarError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f'Dependency preparation failed: {error}', file=sys.stderr)
        sys.exit(1)
