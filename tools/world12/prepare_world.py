#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare private typed 1.2 world data; no Original assets are distributed here.

Inputs are the supplied 1.1 community OBD and existing EA table exports. This is
an offline data preparation tool, not a claim of integrated 1.2 gameplay.
"""
import argparse
import collections
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import unicodedata
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'poc'))
from prepare_international import read_master, u16, legacy_name

SIZES=(18,18,18,18,18,20,20,20,20,10,18,18,18,18)
COUNTRIES=(0,0,0,0,0,2,2,2,2,2,1,3,4,5)

def sha(data):return hashlib.sha256(data).hexdigest()
def rows(folder,name):return json.loads(gzip.decompress((folder/(name+'.json.gz')).read_bytes()))
def normalize(value):
    value=unicodedata.normalize('NFKD',value.upper())
    value=''.join(c for c in value if not unicodedata.combining(c))
    value=re.sub(r'\b(?:FC|FK|CF|SC|SL|CP|AC|AFC|CA)\b','',value)
    return re.sub('[^A-Z0-9]','',value)
def pool_string(pool,offset):return pool[offset:].split(b'\0',1)[0].decode('ascii')
def names_from_table(folder):
    values=rows(folder,'playernames');return {int(x['nameid']):x['name'] for x in values}

def prepare(base,tables,output,english):
    if output.exists():raise ValueError('Output must not exist')
    from prepare_setup_symbol import prepare as prepare_symbol
    setup_symbol,setup_report=prepare_symbol(base,english)
    with zipfile.ZipFile(base) as archive:
        raw=archive.read('M3D.MAN');db=read_master(raw,445,3334,194,4104)
        asset_entries={n:archive.read(n) for n in archive.namelist() if n.startswith(('W/','KB/','F/','S/')) or n in ('M3PC.COL','M3.DAT','M3.MEN','TEXTD.BLK','RUNTIME.DAT')}
    club_names=[pool_string(db['pools'][0],n) for n in u16(db['clubs'])]
    player_names=[pool_string(db['pools'][1],n) for n in u16(db['names'])]
    w1=[x[0] for x in struct.iter_unpack('<I',db['form'])];w2=[x[0] for x in struct.iter_unpack('<I',db['skills'])];w3=[x[0] for x in struct.iter_unpack('<I',db['identity'])]
    members=[{'league':i,'clubs':[]} for i in range(14)];crests=[];clubs=[];players=[];provenance={'base_sha256':sha(base.read_bytes()),'master_sha256':sha(raw),'new_clubs':[],'new_players':[],'limits':['Supplied EA database membership is source data, not an independently verified 2026/27 registration claim.','League membership is adapted to the reduced FLTD sizes; those adaptations are fictional.','Classic age/skills generation at career start remains unchanged.']}
    badges=u16(db['badges']);slots=u16(db['squads']);next_club=445;next_player=3334
    for i in range(3334):players.append({'id':i,'name':player_names[i],'form_word':w1[i],'skill_word':w2[i],'identity_word':w3[i],'bonus':db['bonus'][i]})
    empty=lambda:[{'player':None,'flags':0} for _ in range(24)]
    for i in range(445):
        if i<90:league=i//18;country=0
        elif i<108:league=10;country=1
        elif i<128:league=5;country=2
        elif i<146:league=11;country=3
        elif i<164:league=12;country=4
        elif i<194:league=None;country=0
        elif i<383:league=None;country=5+(i-194)//7
        else:league=None;country=i-383
        cell=badges[i]&511;crests.append({'id':i,'resource':'W/'+str(cell//8)+'.VGA','cell':cell%8})
        roster=empty()
        if i<164:
            for j,value in enumerate(slots[i*24:(i+1)*24]):
                if value&4095!=4095:roster[j]={'player':value&4095,'flags':1 if value&32768 else 2}
            members[league]['clubs'].append(i)
        clubs.append({'id':i,'name':club_names[i],'country':country,'kind':1 if i>=383 else 0,'league':league,'crest':i,'kit':db['kits'][i],'rating':db['strength'][i-194] if i>=194 else 0,'short_span':db['short'][i] if i<194 else 0,'roster':roster})
    teams={int(t['teamid']):t for t in rows(tables,'teams')};people={int(p['playerid']):p for p in rows(tables,'players')};names=names_from_table(tables)
    links=collections.defaultdict(list)
    for link in rows(tables,'teamplayerlinks'):
        if int(link['playerid']) in people:links[int(link['teamid'])].append(people[int(link['playerid'])])
    ea_leagues=collections.defaultdict(list)
    for link in rows(tables,'leagueteamlinks'):
        team=teams.get(int(link['teamid']))
        if team:ea_leagues[int(link['leagueid'])].append(team)
    def strength(team):return int(team.get('overallrating',team.get('matchdayoverallrating',60)))
    male_ids={int(t['teamid']) for lid in (13,14,60,61,308) for t in ea_leagues[lid]}
    ea_by_name={normalize(t['teamname']):t for t in teams.values() if int(t['teamid']) in male_ids}
    mapped=set()
    # Explicit club identity aliases belong to the generated private receipt.
    aliases={'BENFICALISSABON':'Benfica','SPORTINGLISSABON':'Sporting CP','SPORTINGBRAGA':'Sporting de Braga','VITORIAGUIMARAES':'Vitória de Guimarães','FCMOREIRENSE':'Moreirense FC','FCFAMALICAO':'FC Famalicão','MANCHESTERUNITED':'Manchester United','MANCHESTERCITY':'Manchester City','BRIGHTON':'Brighton & Hove Albion'}
    def existing_team(club):
        key=normalize(club['name']);name=aliases.get(key,club['name']);return ea_by_name.get(normalize(name))
    for club in clubs:
        team=existing_team(club)
        if team:mapped.add(int(team['teamid']))
    # The 1.1 English first division remains intact; lower tiers use the strongest
    # remaining supplied English clubs, keeping source tier before strength.
    old_eng=set()
    for cid in members[5]['clubs']:
        t=existing_team(clubs[cid])
        if t:old_eng.add(int(t['teamid']))
    if len(old_eng)!=20:
        missing=[clubs[cid]['name'] for cid in members[5]['clubs'] if not existing_team(clubs[cid])]
        raise ValueError('English base identity mapping incomplete: '+str(missing))
    candidates=[]
    for tier,lid in enumerate((13,14,60,61)):
        for t in ea_leagues[lid]:
            if int(t['teamid']) not in old_eng:candidates.append((tier,-strength(t),int(t['teamid']),t))
    candidates.sort(key=lambda item:item[:3]);unique=[];seen=set()
    for row in candidates:
        if row[2] not in seen:unique.append(row[3]);seen.add(row[2])
    def position(person):
        value=int(person.get('preferredposition1',25));return 0 if value==0 else 1 if value<=8 else 2 if value<=18 else 3
    def add_roster(club,team):
        nonlocal next_player
        source=list({int(p['playerid']):p for p in links.get(int(team['teamid']),[])}.values());source.sort(key=lambda p:(-int(p.get('overallrating',50)),int(p['playerid'])))
        selected=[]
        for pos,need in ((0,2),(1,6),(2,7),(3,5)):
            selected.extend([p for p in source if position(p)==pos][:need])
        selected_ids={int(p['playerid']) for p in selected}
        selected.extend(p for p in source if int(p['playerid']) not in selected_ids)
        selected=selected[:20]
        for order in range(20):
            p=selected[order] if order<len(selected) else None;pos=position(p) if p else (0 if order<2 else 1 if order<8 else 2 if order<15 else 3)
            if p:
                first=names.get(int(p.get('firstnameid',0)),'');last=names.get(int(p.get('lastnameid',0)),'');common=names.get(int(p.get('commonnameid',0)),'');name=(first[:1]+'.'+last).strip('.') if last else common or first or 'N.Spieler'
                rating=max(20,min(95,int(p.get('overallrating',strength(team)))))
            else:name='N.Spieler '+str(next_player);rating=max(20,min(90,strength(team)-8))
            name=legacy_name(name).decode('ascii');nation=club['country'];te=max(10,min(99,rating-4));ko=max(10,min(99,rating+4));player={'id':next_player,'name':name,'form_word':55|(te<<7)|(ko<<14),'skill_word':(nation&31)<<27,'identity_word':pos,'bonus':128 if nation>=32 else 0};players.append(player);club['roster'][order]={'player':next_player,'flags':1 if order<11 else 2};provenance['new_players'].append({'id':next_player,'source_player':int(p['playerid']) if p else None,'source_team':int(team['teamid']),'fictional_fields':['attributes','nationality','age','skills','squad_order']+(['name'] if not p else []),'policy':'classic-calibrated-and-engine-generated'});next_player+=1
    # New crests are assigned after all base cells, with explicit fictional
    # placeholders until supplied source artwork has been matched independently.
    next_crest=max(x&511 for x in badges)+1
    def crest(club):
        nonlocal next_crest
        cid=club['id'];crests[cid]={'id':cid,'resource':'W/'+str(next_crest//8)+'.VGA','cell':next_crest%8};next_crest+=1
    def new_club(team,league,country):
        nonlocal next_club
        club={'id':next_club,'name':legacy_name(team['teamname']).decode('ascii'),'country':country,'kind':0,'league':league,'crest':next_club,'kit':next_club%20,'rating':strength(team),'short_span':15,'roster':empty()};clubs.append(club);crests.append({'id':next_club,'resource':'','cell':0});crest(club);provenance['new_clubs'].append({'id':next_club,'source_team':int(team['teamid']),'fictional_fields':['league_membership','kit','crest']});next_club+=1;add_roster(club,team);return club
    selected_english=set(old_eng)
    for league,lid in ((6,14),(7,60),(8,61)):
        eligible=sorted((t for t in ea_leagues[lid] if int(t['teamid']) not in selected_english),key=lambda t:(-strength(t),int(t['teamid'])))
        if len(eligible)<20:raise ValueError('Insufficient supplied English clubs for source tier')
        for team in eligible[:20]:
            club=new_club(team,league,2);members[league]['clubs'].append(club['id']);selected_english.add(int(team['teamid']))
    remainder=[t for t in unique if int(t['teamid']) not in selected_english]
    if len(remainder)<10:raise ValueError('Insufficient clubs for reduced fifth English group')
    for team in remainder[:10]:
        club=new_club(team,9,2);members[9]['clubs'].append(club['id']);selected_english.add(int(team['teamid']))
    portuguese=sorted(ea_leagues[308],key=lambda t:(-strength(t),int(t['teamid'])))
    existing_pt={normalize(clubs[i]['name']):i for i in range(194,201)}
    claimed=set()
    for team in portuguese[:18]:
        match=None
        for i in range(194,201):
            t=existing_team(clubs[i])
            if t and int(t['teamid'])==int(team['teamid']):match=i;break
        if match is not None and match not in claimed:
            club=clubs[match];club['league']=13;club['country']=5;add_roster(club,team);claimed.add(match)
        else:club=new_club(team,13,5)
        members[13]['clubs'].append(club['id'])
    if len(members[13]['clubs'])!=18:raise ValueError('Portugal needs eighteen supplied clubs')
    # Named English cup/amateur pool, separate from the 90 league clubs.
    extras=[t for t in unique if int(t['teamid']) not in selected_english]
    invented=['Northbridge FC','Eastford Town','Westhaven United','Southwick Athletic','Kingsmere Rovers','Oakchester City','Millborough FC','Riverford Wanderers','Hillmere Albion','Brookvale Town','Stonebridge Athletic','Lakeside Rovers']
    for i in range(12):
        team=extras[i] if i<len(extras) else {'teamid':-1-i,'teamname':invented[i-len(extras)],'overallrating':40+i%8}
        club={'id':next_club,'name':legacy_name(team['teamname']).decode('ascii'),'country':2,'kind':0,'league':None,'crest':next_club,'kit':next_club%20,'rating':strength(team),'short_span':15,'roster':empty()}
        clubs.append(club);crests.append({'id':next_club,'resource':'','cell':0});crest(club);provenance['new_clubs'].append({'id':next_club,'source_team':team['teamid'] if team['teamid']>=0 else None,'fictional_fields':['cup_membership','kit','crest']+(['name','rating'] if team['teamid']<0 else [])});next_club+=1
    # Asset placeholders are temporary and explicitly identified; the dataset is
    # not marked playable until country/progression adapters are integrated.
    from PIL import Image,ImageDraw
    palette=list(asset_entries['M3PC.COL']);rgb=[tuple(((v&63)*255+31)//63 for v in palette[i:i+3]) for i in range(0,768,3)]
    from prepare_international import decode_vga,encode_vga
    pages={}
    for c in crests:
        if c['id']<445:continue
        page=int(Path(c['resource']).stem)
        if page not in pages:
            if c['resource'] in asset_entries:pages[page]=decode_vga(asset_entries[c['resource']])[2]
            else:pages[page]=bytearray([94]*(304*38))
        im=Image.new('RGB',(38,38),rgb[94]);draw=ImageDraw.Draw(im);draw.polygon([(6,3),(31,3),(30,24),(19,35),(7,24)],fill=rgb[96],outline=rgb[0]);draw.text((9,12),str(c['id']%1000),fill=rgb[95]);pixels=bytearray()
        for color in im.getdata():pixels.append(min(range(256),key=lambda i:sum((a-b)**2 for a,b in zip(rgb[i],color))))
        for y in range(38):pages[page][y*304+c['cell']*38:y*304+c['cell']*38+38]=pixels[y*38:(y+1)*38]
    for page,pixels in pages.items():asset_entries['W/'+str(page)+'.VGA']=encode_vga(304,38,pixels)
    world={'format':'openbmh-world-12','version':1,'profile':'dual-classic-1','players':players,'clubs':clubs,'leagues':members,'crests':crests,'loans':[],'managers':[]}
    asset_entries['WORLD12.JSON']=(json.dumps(world,separators=(',',':'))+'\n').encode();asset_entries['OBMHPROF.INI']=b'[OpenBMH]\nprofile=dual-classic-1\n'
    asset_entries['KB/SETUP12.VGA']=setup_symbol;provenance['setup_symbol']=setup_report
    asset_entries['PACKAGE.json']=(json.dumps({'format':'openbmh-community-12','profile':'dual-classic-1','playable':False,'requires':'runtime:dual-classic-1','files':{n:sha(v) for n,v in asset_entries.items()}},indent=2)+'\n').encode()
    output.mkdir(parents=True)
    with zipfile.ZipFile(output/'COMMUNITY.OBD','w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for n,v in asset_entries.items():z.writestr(n,v)
    (output/'IMPORT-REPORT.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'league_clubs':sum(len(x['clubs']) for x in members),'clubs':len(clubs),'players':len(players),'max_player_id':next_player-1,'max_crest_cell':next_crest-1,'playable':False,'obd_bytes':(output/'COMMUNITY.OBD').stat().st_size}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--ea-tables',type=Path,required=True);p.add_argument('--english-data',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();prepare(a.base,a.ea_tables,a.out,a.english_data)
