#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assemble a small local Windows community ZIP from a verified program and private data."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

def create(program, data, destination, version):
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?',version):
        raise ValueError('Invalid version')
    bundle=data/'COMMUNITY.OBD'
    with zipfile.ZipFile(bundle) as z:
        for name in z.namelist():
            if Path(name).suffix.lower() in ('.exe','.com','.dll','.png','.jpg','.jpeg'):
                raise ValueError('Original executables or raster studies found in data bundle')
        if z.read('RUNTIME.DAT')[:8]!=b'OBMHDAT1':
            raise ValueError('Expected a data-only runtime')
        receipt=json.loads(z.read('PACKAGE.json'))
        for name,digest in receipt['files'].items():
            if hashlib.sha256(z.read(name)).hexdigest()!=digest:
                raise ValueError('Resource checksum mismatch: '+name)
    files={}
    with zipfile.ZipFile(program) as z:
        for info in z.infolist():
            if info.is_dir():continue
            path=Path(info.filename)
            if path.is_absolute() or '..' in path.parts or len(path.parts)<2:
                raise ValueError('Invalid program archive path')
            name=Path(*path.parts[1:]).as_posix()
            if name=='openbmh_cli.exe':continue
            if path.suffix.lower() in ('.zip','.cpp','.hpp','.c','.h','.png','.jpg','.jpeg'):
                raise ValueError('Unexpected source/archive/image in program package')
            files[name]=z.read(info)
    for required in ('OpenBMH.exe','vcruntime140.dll','msvcp140.dll','LICENSE'):
        if required not in files:raise ValueError('Missing runtime component: '+required)
    files['COMMUNITY.OBD']=bundle.read_bytes()
    report=data/'IMPORT-REPORT.json'
    if report.exists():files[report.name]=report.read_bytes()
    source_url=f'https://github.com/Weyoune/openbmh/releases/download/v{version}/OpenBMH-{version}-source.zip'
    files['COMMUNITY_README.txt']=('OpenBMH '+version+' Community Edition\n\n'
        'ZIP vollstaendig in einen eigenen Ordner entpacken und OpenBMH.exe starten.\n'
        'Alle benoetigten Spieldaten, Grafiken und Sounds liegen in COMMUNITY.OBD.\n'
        'Keine Original-DOS-Installation und kein Startskript erforderlich.\n'
        'Eigene Spielstaende und Einstellungen bleiben ausserhalb des Datenarchivs.\n\n'
        '263 auslaendische Vereinswappen: 175 echte und 88 fiktive Ersatzwappen.\n'
        'Quellen und gekennzeichnete Ergaenzungen: IMPORT-REPORT.json.\n\n'
        'Anwendung: GPL-3.0-only. Vollstaendiger korrespondierender Quellcode und\n'
        'Abhaengigkeiten kostenlos und separat unter:\n'+source_url+'\n'
        'Lizenzen der Abhaengigkeiten siehe THIRD_PARTY_NOTICES.md und docs/licenses.\n'
        'Inhaltsrechte der Spieldaten und Wappen bleiben bei ihren jeweiligen Quellen.\n').encode('utf-8')
    files['PACKAGE-SHA256SUMS']=''.join(hashlib.sha256(b).hexdigest()+'  '+name+'\n' for name,b in sorted(files.items())).encode()
    destination.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,content in files.items():z.writestr(name,content)
    if destination.stat().st_size>=10_000_000:
        destination.unlink()
        raise ValueError('Community ZIP exceeds the 10 MB budget')
    print(json.dumps({'file':str(destination),'bytes':destination.stat().st_size,'sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),'source_included':False}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--program',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--version',required=True);a=p.parse_args();create(a.program,a.data,a.out,a.version)
