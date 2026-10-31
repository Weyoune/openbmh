#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build private classic-plus-1 data from a supplied German installation and OBW donor.

No data are downloaded. Pillow and CairoSVG are build-time image tools, not game dependencies.
Original installations and source archives are never modified. The output must not exist.
"""
import argparse
import collections
import datetime
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import unicodedata
import zipfile

SIZES = (18, 18, 18, 18, 18, 18, 20, 18, 18)
OLD_SIZES = (18, 18, 18, 18, 18, 12, 12, 12, 12)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def u16(data):
    return list(struct.unpack('<' + 'H' * (len(data) // 2), data))

def words(values, width=2):
    return struct.pack('<' + ('H' if width == 2 else 'I') * len(values), *values)

def read_master(data, clubs=419, players=2574, records=168, extra=3096):
    at = 0
    def take(n):
        nonlocal at
        if at + n > len(data):
            raise ValueError('Master database is truncated or has unsupported dimensions')
        result = data[at:at+n]
        at += n
        return result
    lengths = u16(take(8))
    result = {'bonus': take(extra+50), 'pools': [take(n) for n in lengths]}
    for key, n in [('clubs', clubs*2), ('names', players*2), ('meta', 100), ('aux', records*2),
                   ('badges', clubs*2), ('strength', 251), ('league_aux', 189), ('refs', 50),
                   ('kits', clubs), ('short', records), ('squads', (extra+50)*2),
                   ('form', players*4), ('skills', players*4), ('identity', players*4)]:
        result[key] = take(n)
    if at != len(data):
        raise ValueError('Unsupported master database dimensions/trailing bytes')
    return result

def write_master(db):
    return words([len(p) for p in db['pools']]) + db['bonus'] + b''.join(db['pools']) + b''.join(
        db[k] for k in ('clubs', 'names', 'meta', 'aux', 'badges', 'strength', 'league_aux',
                       'refs', 'kits', 'short', 'squads', 'form', 'skills', 'identity'))

def legacy_name(name):
    # Preserve Original ISO 646-DE and use a documented transliteration for other letters.
    aliases = str.maketrans({'ø':'o', 'Ø':'O', 'ł':'l', 'Ł':'L', 'đ':'d', 'Đ':'D',
                            'ß':'~', 'ä':'{', 'ö':'|', 'ü':'}', 'Ä':'[', 'Ö':'\\', 'Ü':']'})
    value = unicodedata.normalize('NFKD', name.translate(aliases))
    return ''.join(c for c in value if not unicodedata.combining(c)).upper().encode('ascii', errors='replace')

def add_string(pool, name):
    data = legacy_name(name) + b'\0'
    if len(pool) + len(data) > 65535:
        raise ValueError('String pool exceeds Original u16 offsets')
    offset = len(pool)
    pool.extend(data)
    return offset

def decode_vga(data):
    width, height, length = struct.unpack_from('<HHI', data)
    at = 8
    pixels = bytearray()
    while len(pixels) < width*height:
        c = data[at]; at += 1
        if c >= 128:
            pixels.extend([data[at]] * (c-127)); at += 1
        else:
            pixels.extend(data[at:at+c+1]); at += c+1
    if len(pixels) != width*height or at+1 != 8+length:
        raise ValueError('Malformed VGA input')
    return width, height, pixels

def encode_vga(width, height, pixels):
    if len(pixels) != width*height:
        raise ValueError('VGA dimensions do not match pixels')
    # Valid literal packets, including the one trailing byte required by the Original reader.
    payload = b''.join(bytes([len(pixels[i:i+128])-1]) + bytes(pixels[i:i+128])
                       for i in range(0, len(pixels), 128)) + b'\0'
    return struct.pack('<HHI', width, height, len(payload)) + payload

def find_file(root, name):
    return next(p for p in root.iterdir() if p.name.upper() == name.upper())

def build(source, donor, output):
    from PIL import Image, ImageDraw, ImageOps
    import cairosvg
    source, donor, output = source.resolve(), donor.resolve(), output.resolve()
    if output.exists() or source == output or source in output.parents or output in source.parents:
        raise ValueError('Output must be a new directory outside the input installation')
    master = find_file(source, 'M3D.MAN').read_bytes()
    db = read_master(master)
    with zipfile.ZipFile(donor) as archive:
        table = lambda name: json.loads(archive.read('world/' + name + '.json'))['items']
        clubs, players, competitions, nations = (table(n) for n in ('clubs','players','competitions','nations'))
        assets = {a['key']:a for a in json.loads(archive.read('assets/index.json'))['items']}
        by_team = collections.defaultdict(list)
        for p in players:
            if p.get('team'): by_team[p['team']].append(p)
        original = {c['x-legacy']['team_index']:c for c in clubs if 'team_index' in c.get('x-legacy', {})}
        old_new = {}
        new_clubs = []
        selected = []
        new_first = old_first = 0
        for league, (old_size, size) in enumerate(zip(OLD_SIZES, SIZES)):
            for index in range(old_first, old_first + old_size):
                old_new[index] = new_first + index-old_first
                new_clubs.append(original[index])
            comp = next(c for c in competitions if c.get('x-legacy', {}).get('league_index') == league)
            candidates = [c for c in clubs if c['id'] in comp['members'] and 'x-legacy' not in c]
            candidates.sort(key=lambda c: (-sum(p['attributes'].get('strength', 0) for p in by_team[c['id']]) / max(1,len(by_team[c['id']])), c['id']))
            additions = candidates[:size-old_size]
            if len(additions) != size-old_size:
                raise ValueError('Donor lacks required international clubs')
            new_clubs.extend(additions)
            selected += [c['id'] for c in additions]
            new_first += size; old_first += old_size
        for old in range(138, 419):
            old_new[old] = len(new_clubs)
            new_clubs.append(original[old])
        if len(new_clubs) != 445: raise ValueError('Unexpected club total')
        ids = {c['id']:i for i,c in enumerate(new_clubs)}
        old_squads = u16(db['squads'])
        runtime_squads = old_squads[:2968] + [4095]*4 + old_squads[2968:]
        new_squads = [4095]*4158
        player_additions = []
        roster_report = []
        for index, club in enumerate(new_clubs[:164]):
            old = club.get('x-legacy', {}).get('team_index')
            if old is not None:
                first, count = (old*24,24) if old<90 else (old*16+90*8,16)
                roster = runtime_squads[first:first+count]
            else: roster = []
            supplements = [p for p in by_team[club['id']] if 'x-legacy' not in p]
            supplements.sort(key=lambda p:(p.get('squad_order',999),p['id']))
            needed = max(0,20-sum((v&4095)!=4095 for v in roster))
            if len(supplements) < needed:
                raise ValueError('Donor lacks supplemental roster records for ' + club['id'])
            roster = [v for v in roster if (v&4095)!=4095]
            for player in supplements[:needed]:
                pid = 2574+len(player_additions)
                player_additions.append(player)
                roster.append(pid | (32768 if len(roster)<11 else 0))
            new_squads[index*24:index*24+len(roster)] = roster
            roster_report.append({'id':club['id'],'runtime_team':index,'occupied':len(roster),'slots':24})
        if len(player_additions) != 760:
            raise ValueError(f'Expected 760 additional players, received {len(player_additions)}')
        new_squads[3936:] = runtime_squads[2928:]
        # Original master layout lacks the four temporary market rows.
        db['squads'] = words(new_squads[:3976]+new_squads[3980:])
        for key in ('clubs','aux','badges','kits','short'):
            db[key] = bytearray(db[key])
        old_offsets, old_aux, old_badges = u16(db['clubs']), u16(db['aux']), u16(db['badges'])
        old_kits, old_short = db['kits'], db['short']
        pool = bytearray(db['pools'][0]); offsets=[]; aux=[]; badges=[]; kits=[]; shorts=[]
        for index,club in enumerate(new_clubs):
            old=club.get('x-legacy',{}).get('team_index')
            offsets.append(old_offsets[old] if old is not None else add_string(pool,club['name']['default']))
            badges.append(old_badges[old] if old is not None else 0)
            kits.append(old_kits[old] if old is not None else index%20)
            if index<194:
                aux.append(old_aux[old] if old is not None and old<168 else 0)
                shorts.append(old_short[old] if old is not None and old<168 else min(15, len(legacy_name(club['name']['default'])) - 1))
        db['pools'][0]=bytes(pool); db['clubs']=words(offsets); db['aux']=words(aux)
        db['kits']=bytes(kits); db['short']=bytes(shorts)
        names=u16(db['names']); pool=bytearray(db['pools'][1]); new_words={k:[] for k in ('form','skills','identity')}
        nation_codes={n['id']:n.get('x-legacy',{}).get('nationality_index',0) for n in nations}
        field_provenance=[]
        for p in player_additions:
            name=p['name']; display=(name.get('first','')[:1]+'.'+name.get('last','')).strip('.')
            names.append(add_string(pool,display))
            a=p['attributes']; nat=min(63,nation_codes.get(p.get('nationalities',['nation:de'])[0],0))
            w1=int(a['form'])+(int(a['technique'])<<7)+(int(a['condition'])<<14)
            skill=sum(int(a.get(k,0))<<(i*2) for i,k in enumerate(('teamplay','speed','shooting','passing','tackling','free_kick','corner')))
            age=24
            if p.get('born'):
                born=datetime.date.fromisoformat(p['born']); age=2026-born.year-((7,1)<(born.month,born.day))
            pos={'gk':0,'def':1,'mid':2,'fwd':3}[p['positions'][0]]
            new_words['form'].append(w1);new_words['skills'].append(skill+((nat&31)<<27));new_words['identity'].append(pos+(max(0,min(31,age-16))<<21))
            field_provenance.append({'id':p['id'],'name':name,'source':p.get('x-diamond-modern',{}),'legacy_transliteration':display,'nationality_code':nat,'bonus_bit':nat>>5})
        db['names']=words(names);db['pools'][1]=bytes(pool)
        for k in new_words: db[k]+=words(new_words[k],4)
        bonus=bytearray(4154);bonus[:2574]=db['bonus'][:2574]
        for i,p in enumerate(player_additions):
            nat=field_provenance[i]['nationality_code'];bonus[2574+i]=(int(p['attributes'].get('bonus',0))&127)|((nat>>5)<<7)
        db['bonus']=bytes(bonus)
        # Keep Original national flags/domestic crests. Assign private foreign crests to free cells.
        used={b&511 for i,b in enumerate(old_badges) if i<90 or 138<=i<168 or i>=357}
        free=iter(i for i in range(512) if i not in used)
        palette=find_file(source,'M3PC.COL').read_bytes()
        rgb=[tuple(((v&63)*255+31)//63 for v in palette[i:i+3]) for i in range(0,768,3)]
        sheets={}; crest_report=[]; cache={}
        w_dir=find_file(source,'W')
        def sheet(n):
            if n not in sheets:
                file=next((p for p in w_dir.iterdir() if p.name.upper()==str(n)+'.VGA'),None)
                sheets[n]=decode_vga(file.read_bytes()) if file else (304,38,bytearray([0]*11552))
                if sheets[n][:2]!=(304,38):raise ValueError('Unexpected crest sheet geometry')
            return sheets[n][2]
        for index,club in enumerate(new_clubs):
            if not (90 <= index < 164 or 194 <= index < 383): continue
            cell=next(free,None)
            if cell is None: raise ValueError('512-cell badge space exhausted')
            asset=assets.get(club.get('crest'),{});files=asset.get('files',{}); image=None; error=None
            for kind in ('svg','png','native'):
                path=files.get(kind)
                if not path:continue
                try:
                    data=archive.read(path)
                    if path.lower().endswith('.svg'):data=cairosvg.svg2png(bytestring=data,output_width=152,output_height=152)
                    image=Image.open(io.BytesIO(data)).convert('RGBA');break
                except Exception as exc:error=type(exc).__name__
            fictional=image is None
            if fictional:
                image=Image.new('RGBA',(38,38));d=ImageDraw.Draw(image);d.polygon([(6,3),(31,3),(30,24),(19,35),(7,24)],fill=rgb[64+index%16],outline='white');d.text((12,11),str(index%100),fill='white')
            image=ImageOps.contain(image,(36,36),method=Image.Resampling.LANCZOS)
            canvas=Image.new('RGBA',(38,38));canvas.alpha_composite(image,((38-image.width)//2,(38-image.height)//2))
            pixels=bytearray()
            for r,g,b,a in canvas.getdata():
                if a<128:pixels.append(0);continue
                key=(r,g,b)
                if key not in cache:cache[key]=min(range(1,256),key=lambda n:sum((x-y)**2 for x,y in zip(rgb[n],key)))
                pixels.append(cache[key])
            dst=sheet(cell//8)
            for y in range(38):dst[y*304+(cell%8)*38:y*304+(cell%8)*38+38]=pixels[y*38:(y+1)*38]
            badges[index]=(badges[index]&~511)|cell
            crest_report.append({'club':club['id'],'runtime_team':index,'cell':cell,'asset':club.get('crest'),'source':asset.get('source'),'license':asset.get('license'),'fictional':fictional,'last_decode_error':error})
        db['badges']=words(badges)
        # Every Original player word and all German starting rosters remain unchanged.
        original_db = read_master(master)
        for key in ('form', 'skills', 'identity'):
            assert db[key][:2574*4] == original_db[key]
        for team in range(90):
            assert new_squads[team*24:(team+1)*24] == runtime_squads[team*24:(team+1)*24]
        occupied = [v & 4095 for v in new_squads[:3936] if (v & 4095) != 4095]
        if len(occupied) != 3280 or len(set(occupied)) != len(occupied):
            raise ValueError('Expanded club rosters contain missing or duplicated players')
        encoded=write_master(db)
        parsed=read_master(encoded,445,3334,194,4104)
        assert write_master(parsed)==encoded
        shutil.copytree(source,output,ignore=shutil.ignore_patterns('.git','.openeddi','*.FBM','*.fbm'))
        find_file(output,'M3D.MAN').write_bytes(encoded)
        out_w=find_file(output,'W')
        for n,(width,height,pix) in sheets.items():
            target=next((p for p in out_w.iterdir() if p.name.upper()==str(n)+'.VGA'),out_w/(str(n)+'.VGA'))
            vga=encode_vga(width,height,pix); assert decode_vga(vga)[2]==pix;target.write_bytes(vga)
        (output/'OBMHPROF.INI').write_text('[OpenBMH]\nprofile=classic-plus-1\n',encoding='ascii')
        report={'profile':'classic-plus-1','base_sha256':sha(master),'donor_sha256':sha(donor.read_bytes()),'output_sha256':sha(encoded),'league_sizes':SIZES,'club_count':445,'player_count':3334,'source_to_runtime':old_new,'selected_additions':selected,'rosters':roster_report,'fictional_club_fields':{'new_clubs':['/kit/selector','/short_name_offset'],'policy':'deterministic existing Classic kit palette and automatic stadium initialization'},'additional_players':field_provenance,'crests':crest_report,'content_license':json.loads(archive.read('manifest.json')).get('content_license'),'distribution':'Private prepared data; content rights/provenance unchanged. No game content belongs in public source/binary artifacts.'}
        (output/'IMPORT-REPORT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
        print(json.dumps({'output':str(output),'clubs':445,'players':3334,'foreign_crests':len(crest_report),'fictional_crests':sum(c['fictional'] for c in crest_report),'sha256':sha(encoded)}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',required=True,type=Path);p.add_argument('--supplement',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();build(a.source,a.supplement,a.output)
