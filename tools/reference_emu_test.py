"""Use V10.7.23's SAVE navigation in its older emulator test fixture.

The source's tests/ui_test.c:test_save enters TIMELINE, PROJECT, USER,
then returns via HOME to arm SAVE. This changes only the JS test, not firmware.
"""
from pathlib import Path
p=Path('build/stable-source/web/emu/emu_test.mjs')
s=p.read_text()
old='a.tap(B.SAVE);\na.tap(B.OCTUP);\na.tap(B.OCTUP);'
new='a.tap(B.SAVE); // TIMELINE\na.tap(B.SAVE); // PROJECT\na.tap(B.SAVE); // USER\na.tap(B.HOME);\na.tap(B.SAVE); // arm SAVE on USER\na.tap(B.OCTUP);\na.tap(B.OCTUP);'
assert s.count(old)==1,'unexpected reference fixture'
p.write_text(s.replace(old,new))
