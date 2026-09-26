"""Package portable native sources only; never include private resources/toolchains."""
import argparse
from pathlib import Path
import tarfile

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
roots = ['ios', 'portable', 'th10_web/cpp', 'th10_web/docs']
blocked = {'.local', 'artifacts', 'node_modules', '__pycache__', '.git', 'build', 'dist', 'validation'}
allowed = {'.c', '.cpp', '.h', '.hpp', '.inc', '.mm', '.m', '.py', '.sh', '.ps1', '.in', '.md', '.txt', '.json', '.mjs', '.js', '.plist', '.storyboard', '.cmake', '.LICENSE'}
files = [root / 'README.md', root / '.gitignore']
files.append(root/'th10_web/assets/sdl-native/music-layout.json')
for base in roots:
    for path in (root/base).rglob('*'):
        relative=path.relative_to(root)
        if path.is_file() and not any(part in blocked for part in relative.parts) and (path.suffix in allowed or path.name.startswith('LICENSE')):
            if '.local.' not in path.name:
                files.append(path)
license_file=root/'th10_web/assets/vendor/th10-rebuilt.LICENSE'
if license_file.exists(): files.append(license_file)
args.output.parent.mkdir(parents=True,exist_ok=True)
with tarfile.open(args.output,'w:gz',format=tarfile.PAX_FORMAT) as archive:
    for path in sorted(set(files)):
        archive.add(path,arcname=path.relative_to(root).as_posix(),recursive=False)
print(f'Packaged {len(set(files))} source files; assets and local configuration excluded')
