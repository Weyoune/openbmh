#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare a compact data-only community directory from private supplied content."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

def build(source, destination, exporter):
    if destination.exists():
        raise ValueError('Destination must not exist')
    source=source.resolve();destination.mkdir(parents=True)
    runtime=destination/'RUNTIME.DAT'
    try:
        exe=next(p for p in source.iterdir() if p.name.upper()=='BMH.EXE')
        subprocess.run([str(exporter.resolve()),str(exe),str(runtime)],check=True)
        data=runtime.read_bytes()
        if data[:8]!=b'OBMHDAT1':raise ValueError('Exporter did not produce a data-only runtime')
        # Only resources actually used by OpenBMH; no DOS programs, manuals, backups or unused original graphics formats.
        names=('M3D.MAN','TEXTD.BLK','M3.DAT','M3.MEN','M3PC.COL','OBMHPROF.INI')
        entries={'RUNTIME.DAT':data}
        for name in names:
            file=next(p for p in source.iterdir() if p.name.upper()==name)
            entries[name]=file.read_bytes()
        for directory in ('KB','W','F','S'):
            root=next(p for p in source.iterdir() if p.name.upper()==directory)
            for file in sorted(root.iterdir()):
                if not file.is_file():continue
                if directory in ('KB','W') and file.suffix.upper()!='.VGA':continue
                if directory=='F' and file.name.upper() not in ('BMH2.FNT','BMH3.FNT'):continue
                if directory=='S' and file.name not in ('2000','2001','2002','2003'):continue
                entries[directory+'/'+file.name.upper()]=file.read_bytes()
        receipt={'format':'openbmh-community-1','files':{name:hashlib.sha256(value).hexdigest() for name,value in entries.items()},
                 'executable_code_included':False,'profile':'classic-plus-1'}
        entries['PACKAGE.json']=(json.dumps(receipt,indent=2)+'\n').encode()
        archive=destination/'COMMUNITY.OBD'
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for name,value in entries.items():z.writestr(name,value)
        report=source/'IMPORT-REPORT.json'
        if report.exists():(destination/report.name).write_bytes(report.read_bytes())
        print(json.dumps({'bundle_bytes':archive.stat().st_size,'uncompressed_resource_bytes':sum(map(len,entries.values())),
                          'resource_count':len(entries),'runtime_bytes':len(data),'bundle_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}))
    finally:runtime.unlink(missing_ok=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--exporter',type=Path,required=True);a=p.parse_args();build(a.source,a.out,a.exporter)
