// SPDX-License-Identifier: GPL-3.0-only
#include "obdeddi/document.hpp"
#include "cli/json.hpp"
#include "app/scene_timing.hpp"
#include "miniz.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
using namespace bmh;
using namespace bmh::obdeddi;
using Files=std::map<std::string,Bytes>;
void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
Bytes bytes(std::string s){return Bytes(s.begin(),s.end());}
void put(Bytes&data,size_t at,uint16_t x){data.at(at)=uint8_t(x);data.at(at+1)=uint8_t(x>>8);}
Bytes archive(Files entries){cli::Json::Object hashes;for(const auto&[name,data]:entries)hashes.emplace(name,sha256(data));auto manifest=cli::dump_json(cli::Json::Object{{"format","openbmh-community-1"},{"profile","classic-plus-1"},{"files",std::move(hashes)}});entries["PACKAGE.json"]=bytes(manifest);mz_zip_archive zip{};check(mz_zip_writer_init_heap(&zip,0,0),"writer init");for(const auto&[name,data]:entries)check(mz_zip_writer_add_mem(&zip,name.c_str(),data.data(),data.size(),MZ_BEST_COMPRESSION),"writer entry");void*p=nullptr;size_t n=0;check(mz_zip_writer_finalize_heap_archive(&zip,&p,&n),"writer finish");Bytes out(static_cast<uint8_t*>(p),static_cast<uint8_t*>(p)+n);mz_free(p);mz_zip_writer_end(&zip);return out;}
void write(const std::filesystem::path&p,const Bytes&data){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));check(bool(f),"write failed");}
Bytes entry(const std::filesystem::path&path,const char*name){auto data=read_file(path);mz_zip_archive zip{};check(mz_zip_reader_init_mem(&zip,data.data(),data.size(),0),"reader init");size_t n=0;void*p=mz_zip_reader_extract_file_to_heap(&zip,name,&n,0);check(p!=nullptr,"missing fixture entry");Bytes out(static_cast<uint8_t*>(p),static_cast<uint8_t*>(p)+n);mz_free(p);mz_zip_reader_end(&zip);return out;}
Database synthetic(){Database d;d.counts=DataProfile::international_11().counts;d.pools[0]=bytes(std::string("C\0",2));d.pools[1]=bytes(std::string("P\0",2));d.pools[2]=bytes(std::string("R\0",2));d.pools[3]=bytes(std::string("S\0",2));d.club_names.assign(445,"C");d.players.assign(3334,Player{"P",0xa5200000u|42u|(55u<<7)|(65u<<14),0x01234000u,9u<<21});d.raw_player_bonus_values.resize(4154);d.raw_club_name_offsets.resize(890);d.raw_player_name_offsets.resize(6668);d.raw_database_metadata.resize(100);d.raw_club_auxiliary_words.resize(388);d.raw_club_badge_selectors.resize(890);d.raw_foreign_club_strength.resize(251);d.raw_league_auxiliary_bytes.resize(189);d.raw_referee_properties.resize(50);d.raw_club_kit_selectors.resize(445);d.raw_club_short_name_offsets.resize(194);d.squad_list.resize(4154*2);for(size_t i=0;i<4154;++i)put(d.squad_list,i*2,4095);int player=0;for(int team=0;team<164;++team)for(int row=0;row<20;++row)put(d.squad_list,size_t(team*24+row)*2,uint16_t(player++|(row<11?0x8000:0)));return d;}
int main()try{
 check(sha256(bytes("abc"))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 vector failed");
 check(app::scene_frame_delay(80)==100&&app::scene_frame_delay(0)==16,"scene pacing mapping");
 auto db=synthetic();auto master=encode_database(db);check(encode_database(decode_database(master,db.counts))==master,"master roundtrip");
 auto root=std::filesystem::temp_directory_path()/("obdeddi-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);
 auto runtime=bytes("OBMHDAT1");runtime.resize(36);const Bytes asset{3,17,91,0,255};auto input=archive({{"M3D.MAN",master},{"RUNTIME.DAT",runtime},{"OBMHPROF.INI",bytes("[OpenBMH]\nprofile=classic-plus-1\n")},{"KB/1.VGA",asset}});auto path=root/std::filesystem::path(std::u8string(u8"Saison-ä.obd"));write(path,input);
 Document doc(path);check(!doc.dirty()&&doc.save().empty()&&read_file(path)==input,"no-op wrote a file");
 auto player=doc.player(0);auto high=db.players[0].w35c&~0x1fffffu;player.name="M}LLER";player.nation=61;player.age=30;player.form=75;player.skills[2]=3;doc.set_player(0,player);check((doc.database().players[0].w35c&~0x1fffffu)==high,"unrelated player bits changed");
 auto invalid=player;invalid.skills[0]=4;bool rejected=false;try{doc.set_player(1,invalid);}catch(const FormatError&){rejected=true;}check(rejected,"invalid skill accepted");
 doc.set_club(0,"NEUER VEREIN",3,0,12);doc.move_player(0,1);check(doc.club_of(0)==1&&doc.club_of(20)==1,"move failed");doc.move_player(1,1,20);check(doc.club_of(1)==1&&doc.club_of(20)==0,"swap failed");
 auto backup=doc.save();check(!backup.empty()&&read_file(backup)==input,"backup not exact");check(entry(path,"KB/1.VGA")==asset,"asset modified");Document loaded(path);check(loaded.player(0).name=="M}LLER"&&loaded.player(0).nation==61&&loaded.database().club_names[0]=="NEUER VEREIN","edited data not persisted");check(loaded.club_of(0)==1&&loaded.club_of(20)==0,"saved roster changed");
 auto changed=loaded.player(2);changed.form=40;loaded.set_player(2,changed);auto outside=read_file(path);outside.push_back(0);write(path,outside);rejected=false;try{loaded.save();}catch(const FormatError&){rejected=true;}check(rejected&&read_file(path)==outside,"external edit overwritten");
 auto failRoot=root/"backup-failure";std::filesystem::create_directory(failRoot);auto failPath=failRoot/"COMMUNITY.OBD";write(failPath,input);write(failRoot/"backups",bytes("not a directory"));Document failure(failPath);auto value=failure.player(0);value.form=12;failure.set_player(0,value);rejected=false;try{failure.save();}catch(const std::exception&){rejected=true;}check(rejected&&read_file(failPath)==input,"backup failure changed original");
 auto corrupt=input;corrupt.resize(12);write(root/"bad.obd",corrupt);rejected=false;try{Document bad(root/"bad.obd");}catch(const std::exception&){rejected=true;}check(rejected,"truncated archive accepted");
 std::filesystem::remove_all(root);std::cout<<"Editor roundtrips, unknown bits, roster changes, assets, verified backups and failure safety passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
