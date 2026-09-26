"""Validate a native iOS device bundle and package it for TrollStore.
Package validity does not establish gameplay or iOS 14 device acceptance.
"""
import argparse, hashlib, json, plistlib, re, shutil, subprocess, tempfile, zipfile
from pathlib import Path

def run(*args):
    return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT).strip()
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): h.update(block)
    return h.hexdigest()

parser=argparse.ArgumentParser()
parser.add_argument('app',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
app=args.app.resolve(); output=args.output.resolve()
if output.exists(): raise SystemExit('Choose a new output filename; refusing to overwrite an existing IPA')
info=plistlib.loads((app/'Info.plist').read_bytes())
if info.get('CFBundleIdentifier')!='org.th10.native': raise SystemExit('Unexpected bundle identifier')
if info.get('MinimumOSVersion')!='14.0': raise SystemExit('Unexpected minimum iOS version')
if info.get('TH10Diagnostics') != 'OFF': raise SystemExit('Release IPA must disable diagnostic inputs')
for family in ('CFBundleIcons','CFBundleIcons~ipad'):
    primary=info.get(family,{}).get('CFBundlePrimaryIcon',{})
    if primary.get('CFBundleIconName')!='AppIcon' or not primary.get('CFBundleIconFiles'):
        raise SystemExit('Missing compiled application icon: '+family)
if not (app/'Assets.car').is_file(): raise SystemExit('Missing compiled icon asset catalog')
executable=app/info['CFBundleExecutable']
if executable.parent!=app or not executable.is_file(): raise SystemExit('Invalid executable')
if run('xcrun','lipo','-archs',str(executable)).split()!=['arm64']: raise SystemExit('Not a pure ARM64 device executable')
commands=run('xcrun','otool','-l',str(executable))
if not re.search(r'platform\s+(2|IOS)\b',commands): raise SystemExit('Not an iOS device Mach-O')
if not re.search(r'minos\s+14\.0\b',commands): raise SystemExit('Mach-O does not target iOS 14.0')
if re.search(r'(WebKit|JavaScriptCore)\.framework',run('xcrun','otool','-L',str(executable))):
    raise SystemExit('Unexpected browser runtime dependency')
for file in app.rglob('*'):
    if file.suffix.lower() in {'.exe','.dll','.wasm','.html','.js','.mjs','.rpy','.rpyx'}:
        raise SystemExit('Unexpected payload: '+file.name)
    if file.is_file() and file.name.lower().startswith('score') and file.suffix.lower()=='.dat':
        raise SystemExit('User save embedded in application')
required=['game/th10c.dat','fonts/msgothic.ttc','fonts/simhei.ttf','fonts/blend.bin','fonts/codepages.bin']
required += ['music/%02d.flac'%i for i in range(18)]
required += ['music-layout.json','music-verification.json']
assets=[]
manifest=json.loads((app/'assets/manifest.json').read_text(encoding='utf-8'))
actual_names={p.relative_to(app/'assets').as_posix() for p in (app/'assets').rglob('*') if p.is_file()}
if actual_names != set(required)|{'manifest.json'}: raise SystemExit('Unexpected or missing asset file set')
if set(manifest['files']) != set(required): raise SystemExit('Unexpected resource manifest entries')
if manifest.get('language')!='chs': raise SystemExit('Unexpected resource language')
for name in required:
    file=app/'assets'/name
    if not file.is_file() or not file.stat().st_size: raise SystemExit('Missing resource: '+name)
    actual={'bytes':file.stat().st_size,'sha256':sha(file)}
    if actual != manifest['files'].get(name): raise SystemExit('Prepared resource hash mismatch: '+name)
    assets.append({'name':name,**actual})
for name in ['SDL.txt','SDL_ttf.txt','FreeType.txt','miniaudio.txt','TH10-SoftFloat.txt']:
    if not (app/'licenses'/name).is_file(): raise SystemExit('Missing component notice: '+name)
output.parent.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='th10-ipa-') as temp:
    staging=Path(temp); bundle=staging/'Payload'/'th10.app'
    bundle.parent.mkdir();shutil.copytree(app,bundle)
    run('/usr/bin/codesign','--force','--sign','-','--timestamp=none',str(bundle))
    run('/usr/bin/codesign','--verify','--strict',str(bundle))
    partial=staging/'TH10.ipa'
    with zipfile.ZipFile(partial,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for file in sorted(bundle.rglob('*')):
            if file.is_file(): archive.write(file,file.relative_to(staging).as_posix())
    with zipfile.ZipFile(partial) as archive:
        if archive.testzip():raise SystemExit('IPA integrity check failed')
    executable_sha=sha(bundle/info['CFBundleExecutable'])
    shutil.copyfile(partial,output)
report={'artifact':output.name,'sha256':sha(output),'bytes':output.stat().st_size,
        'bundle_identifier':info['CFBundleIdentifier'],'version':info['CFBundleShortVersionString'],
        'build':info['CFBundleVersion'],'minimum_ios':'14.0','architecture':'arm64','platform':'iphoneos',
        'executable_sha256':executable_sha,'signature':'ad-hoc for user-selected TrollStore installation',
        'app_icon':'Original Japanese th10.exe resource IDI_ICON3; compiled AppIcon catalog',
        'diagnostic_logs':'Local current/previous runs; native settings export enabled; scripted inputs disabled',
        'language':'chs','network_multiplayer':False,'assets':assets,
        'scope':'Bundle/architecture/resources/signature verified. Physical-device installation and gameplay are separate tests.'}
output.with_suffix('.manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
