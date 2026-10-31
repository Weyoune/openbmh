#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the native Windows OBD data editor separately from the game."""
import argparse
from pathlib import Path
import zipfile

def package(build,game,out):
    executable=build/'obdeddi.exe'
    if not executable.is_file():executable=build/'Release/obdeddi.exe'
    if not executable.is_file():raise RuntimeError('Native editor executable missing')
    files={'obdeddi.exe':executable.read_bytes()}
    with zipfile.ZipFile(game) as z:
        for name in z.namelist():
            parts=Path(name).parts
            if len(parts)<2:continue
            relative=Path(*parts[1:]).as_posix()
            if relative.lower().endswith('.dll') or relative in ('LICENSE','THIRD_PARTY_NOTICES.md','ACKNOWLEDGEMENTS.md') or relative.startswith('docs/licenses/'):
                if not name.endswith('/'):files[relative]=z.read(name)
    files['README.txt']=('obdeddi 0.1.0 – nativer OpenBMH-1.1-Dateneditor\n\n'
        'Vollstaendig entpacken und obdeddi.exe starten. COMMUNITY.OBD ueber Oeffnen waehlen.\n'
        'Spieler und Vereine bearbeiten; Suche, Sortierung und Kaderwechsel stehen zur Verfuegung.\n'
        'Speichern ersetzt die OBD erst nach geprueftem Backup im Unterverzeichnis backups.\n'
        'Assets werden nicht bearbeitet. Festes Ligaset und feste Datensatzanzahlen bleiben erhalten.\n'
        'Classic-Eigenheit: Viele Werte werden beim Karrierestart neu erzeugt. Bestehende Spielstaende bleiben unveraendert.\n'
        'Tasten: Strg+O oeffnen, Strg+S speichern. Alternativ: obdeddi.exe "Pfad\\COMMUNITY.OBD".\n\n'
        'GPL-3.0-only; vollstaendige passende Quellen und Abhaengigkeiten separat:\n'
        'https://github.com/Weyoune/openbmh/releases/tag/obdeddi-v0.1.0\n').encode('utf-8')
    out.mkdir(parents=True,exist_ok=True);target=out/'obdeddi-0.1.0-windows-x64.zip'
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in files.items():z.writestr('obdeddi-0.1.0/'+name,data)
    print(target)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--game',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();package(a.build,a.game,a.out)
