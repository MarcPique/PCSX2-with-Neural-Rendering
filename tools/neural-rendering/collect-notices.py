"""Collect original notices from upstream's hash-pinned dependency archives.

Downloads source archives into --cache; does not build or execute their contents.
Only license/notice files are extracted, with paths constrained below --output.
"""
import argparse
import concurrent.futures
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import urllib.request
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--cache', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
script = repo / '.github/workflows/scripts/windows/build-dependencies.bat'
text = script.read_text(encoding='utf8')
variables = dict(re.findall(r'^set ([A-Z0-9_]+)=([^\r\n]+)', text, re.M))
def expand(value):
    return re.sub(r'%([A-Z0-9_]+)%', lambda m: variables[m[1]], value)
records = [dict(zip(('name','url','sha256'), map(expand, m))) for m in
           re.findall(r'^call :downloadfile "([^"]+)" "?(https?://[^"\s]+)"? ([a-f0-9]{64})', text, re.M)]
records = [r for r in records if not r['name'].startswith(('make-','meson-','pkgconf-')) and '.patch.' not in r['name']]
args.cache.mkdir(parents=True, exist_ok=True)
args.output.mkdir(parents=True, exist_ok=True)
def fetch(record):
    cached = args.cache / record['name']
    if not cached.exists():
        request = urllib.request.Request(record['url'], headers={'User-Agent': 'PCSX2-Neural-release-notices'})
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                cached.write_bytes(response.read())
        except urllib.error.URLError:
            # Windows curl uses the system certificate store. TLS verification
            # stays enabled, and every archive is additionally hash-checked.
            subprocess.run(['curl.exe','--fail','--location','--retry','2','--max-time','180',
                            '--output',str(cached),record['url']], check=True)
    data = cached.read_bytes()
    if hashlib.sha256(data).hexdigest() != record['sha256']:
        raise RuntimeError('SHA256 mismatch: ' + record['name'])
    destination = args.output / record['name']
    count = 0
    def save(name, read):
        nonlocal count
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or any(':' in p for p in relative.parts):
            raise RuntimeError('Unsafe archive path: ' + name)
        if not (re.match(r'(?i)^(license|licence|copying|copyright|notice|authors|FTL|GPL|LGPL|OFL|BSD|MIT|Apache)([._0-9-]|$)', relative.name)
                or (record['name'].startswith(('amf-headers-','nv-codec-headers-')) and relative.suffix == '.h')
                or any(p.lower() in ('licenses','licences') for p in relative.parts)):
            return
        output = destination.joinpath(*relative.parts)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(read())
        count += 1
    if zipfile.is_zipfile(io.BytesIO(data)):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for entry in archive.infolist():
                if not entry.is_dir(): save(entry.filename, lambda e=entry: archive.read(e))
    else:
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:*') as archive:
            for entry in archive.getmembers():
                if entry.isfile(): save(entry.name, lambda e=entry: archive.extractfile(e).read())
    if not count: raise RuntimeError('No notices in ' + record['name'])
    print(f'{record["name"]}: {count} notices', flush=True)
    return record
completed, errors = [], []
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    pending = {pool.submit(fetch, r): r for r in records}
    for future in concurrent.futures.as_completed(pending):
        try: completed.append(future.result())
        except Exception as error: errors.append(pending[future]['name'] + ': ' + str(error))
if errors: raise RuntimeError('\n'.join(errors))
(args.output / 'DEPENDENCY-SOURCES.json').write_text(json.dumps(completed, indent=2), encoding='utf8')
(args.output / 'README.txt').write_text(
    'Original licenses and notices extracted from the exact dependency source archives pinned by PCSX2.\n'
    'DEPENDENCY-SOURCES.json lists complete source download URLs and verified SHA256 values.\n'
    'Build recipes and patches: .github/workflows/scripts/windows in the corresponding fork source.\n'
    'Qt is dynamically linked and can be replaced/rebuilt under its applicable open-source licenses.\n', encoding='utf8')
# Preserve notices for libraries built directly from the emulator repository.
for root in (repo / '3rdparty', repo / 'bin/resources'):
    for source in root.rglob('*'):
        if source.is_file() and re.match(r'(?i)^(license|licence|copying|copyright|notice|FTL|GPL|LGPL|OFL|BSD|MIT|Apache)([._0-9-]|$)', source.name):
            output = args.output / 'pcsx2-source' / source.relative_to(repo)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(source.read_bytes())
