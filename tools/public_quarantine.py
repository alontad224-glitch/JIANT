"""Restore the two owner-selected public firmwares without touching beta."""
from pathlib import Path
import hashlib, json, shutil, sys
sys.path.insert(0, 'web')
import make_site
pages=Path('build/pages')
m=json.loads(Path('web/PUBLIC_CHANNEL.json').read_text())
assert m['mode']=='stable-only'
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def protected():
    return {p.relative_to(pages).as_posix():digest(p) for p in pages.rglob('*') if p.is_file()
            and not p.relative_to(pages).as_posix().startswith(('firmware/','webapp/installer/','emu/'))}
before=protected()
fw=pages/'firmware'
old={v['version']:v for v in json.loads((fw/'versions.json').read_text())}
keep=Path('build/public-references');keep.mkdir(parents=True,exist_ok=True)
entries=[]
for v in m['versions']:
    entry=old[v['version']]
    assert digest(fw/entry['file'])==v['sha256'], 'reference package changed'
    shutil.copyfile(fw/entry['file'],keep/entry['file'])
    entries.append(entry)
(keep/'versions.json').write_text(json.dumps(entries))
# No rebuild of the historical firmware: use the exact previously published bytes.
make_site.main(keep/old[m['default']]['file'],m['default'],'build/quarantined',keep)
for sub in ('firmware','webapp/installer'):
    shutil.rmtree(pages/sub)
    shutil.copytree(Path('build/quarantined')/sub,pages/sub)
versions=json.loads((fw/'versions.json').read_text())
assert [v['version'] for v in versions]==[v['version'] for v in m['versions']]
assert len(list(fw.glob('*.fwsc')))==2
for v in m['versions']:assert digest(fw/old[v['version']]['file'])==v['sha256']
(fw/'SOURCE_COMMIT').write_text(m['source']+'\n')
(fw/'SHA256SUMS').write_text(''.join(digest(p)+'  '+p.name+'\n' for p in sorted(fw.glob('*.fwsc'))))
# The emulator is built from the same frozen V10.7.23 source, in its own shell.
emu=Path('build/stable-source/build/emu')
assert '<small class="version">V10.7.23</small>' in (emu/'index.html').read_text()
assert (emu/'felucca.wasm').read_bytes()[:8]==b'\x00asm\x01\x00\x00\x00'
shutil.rmtree(pages/'emu')
shutil.copytree(emu,pages/'emu')
(pages/'emu/SOURCE_COMMIT').write_text(m['source']+'\n')
(pages/'emu/SHA256SUMS').write_text(''.join(digest(p)+'  '+p.relative_to(pages/'emu').as_posix()+'\n' for p in sorted((pages/'emu').rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
assert protected()==before,'beta or unrelated site files changed'
print('PASS public only V10.7.23 and v0.9.9-par12; exact historical bytes; beta preserved')
