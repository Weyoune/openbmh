// SPDX-License-Identifier: GPL-3.0-only
#include "obdeddi/document.hpp"
#include "cli/json.hpp"
#include "formats/palette.hpp"
#include "miniz.h"
#include "Sha256.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace bmh::obdeddi {
namespace {
using Entries=std::map<std::string,Bytes>;
std::string upper(std::string s){for(char&c:s)if(c>='a'&&c<='z')c=char(c-'a'+'A');return s;}
std::string key(const Entries& entries,std::string name){for(const auto&[k,v]:entries)if(upper(k)==upper(name))return k;throw FormatError("Fehlende OBD-Ressource: "+name);}
Entries unpack(const Bytes& bytes) {
    mz_zip_archive zip{};
    if(bytes.size()>64*1024*1024 || !mz_zip_reader_init_mem(&zip,bytes.data(),bytes.size(),0))throw FormatError("OBD-Archiv ist ungueltig oder zu gross.");
    Entries result;std::vector<std::string> names;uint64_t total=0;
    try {
        const auto count=mz_zip_reader_get_num_files(&zip);if(count>4096)throw FormatError("Zu viele OBD-Eintraege.");
        for(mz_uint i=0;i<count;++i){mz_zip_archive_file_stat st{};if(!mz_zip_reader_file_stat(&zip,i,&st))throw FormatError("OBD-Verzeichnis beschaedigt.");if(st.m_is_directory)continue;
            std::string name=st.m_filename;const std::filesystem::path path(name);
            if(path.is_absolute()||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos)throw FormatError("Ungueltiger Ressourcenpfad.");
            for(const auto&part:path)if(part==".."||part==".")throw FormatError("Ungueltiger Ressourcenpfad.");
            if(std::find(names.begin(),names.end(),upper(name))!=names.end())throw FormatError("Doppelte OBD-Ressource.");names.push_back(upper(name));
            total+=st.m_uncomp_size;if(st.m_uncomp_size>32*1024*1024||total>128*1024*1024)throw FormatError("OBD-Ressourcen zu gross.");
            Bytes data(size_t(st.m_uncomp_size));if(!mz_zip_reader_extract_to_mem(&zip,i,data.data(),data.size(),0))throw FormatError("OBD-Pruefsumme fehlerhaft.");result.emplace(name,std::move(data));
        }
    }catch(...){mz_zip_reader_end(&zip);throw;}
    mz_zip_reader_end(&zip);return result;
}
Bytes pack(const Entries& entries){
    mz_zip_archive zip{};if(!mz_zip_writer_init_heap(&zip,0,0))throw FormatError("Archiv kann nicht erstellt werden.");
    void*data=nullptr;size_t size=0;
    try{for(const auto&[name,value]:entries)if(!mz_zip_writer_add_mem(&zip,name.c_str(),value.data(),value.size(),MZ_BEST_COMPRESSION))throw FormatError("Ressource kann nicht gespeichert werden.");
        if(!mz_zip_writer_finalize_heap_archive(&zip,&data,&size))throw FormatError("Archivabschluss fehlgeschlagen.");
    }catch(...){mz_zip_writer_end(&zip);throw;}
    Bytes result(static_cast<uint8_t*>(data),static_cast<uint8_t*>(data)+size);mz_free(data);mz_zip_writer_end(&zip);return result;
}
void write(const std::filesystem::path& path,const Bytes& bytes){std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!f)throw FormatError("Datei kann nicht geschrieben werden.");f.close();if(!f)throw FormatError("Dateiabschluss fehlgeschlagen.");}
void word(Bytes& data,size_t pos,uint16_t value){data.at(pos)=uint8_t(value);data.at(pos+1)=uint8_t(value>>8);}
void rename(Bytes& pool,Bytes& offsets,size_t index,const std::string& name){
    if(name.empty()||name.size()>255||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c>=127;}))throw FormatError("Name muss 1 bis 255 darstellbare Original-Zeichen enthalten.");
    const size_t at=le16(offsets,index*2);std::string old;for(size_t i=at;i<pool.size()&&pool[i];++i)old+=char(pool[i]);if(old==name)return;
    if(pool.size()+name.size()+1>65535)throw FormatError("Namenspool ist voll.");
    word(offsets,index*2,uint16_t(pool.size()));pool.insert(pool.end(),name.begin(),name.end());pool.push_back(0);
}
struct Lock {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit Lock(std::filesystem::path path){path+=".obdeddi.lock";handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);if(handle==INVALID_HANDLE_VALUE)throw FormatError("Datei ist gesperrt oder nicht beschreibbar.");}
    ~Lock(){CloseHandle(handle);}
#else
    int fd=-1;std::filesystem::path path;
    explicit Lock(std::filesystem::path p):path(std::move(p)){path+=".obdeddi.lock";fd=open(path.c_str(),O_CREAT|O_RDWR,0600);if(fd<0)throw FormatError("Datei ist nicht beschreibbar.");if(flock(fd,LOCK_EX|LOCK_NB)){close(fd);fd=-1;throw FormatError("Datei ist gesperrt.");}}
    ~Lock(){if(fd>=0){unlink(path.c_str());close(fd);}}
#endif
};
}
std::string sha256(const Bytes& bytes){CSha256 state;Sha256_Init(&state);Sha256_Update(&state,bytes.data(),bytes.size());Byte digest[SHA256_DIGEST_SIZE];Sha256_Final(&state,digest);std::ostringstream text;for(auto b:digest)text<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return text.str();}
Document::Document(const std::filesystem::path& path):path_(std::filesystem::absolute(path)),original_(read_file(path)),entries_(unpack(original_)){
    profile_=decode_data_profile(entries_.at(key(entries_,"OBMHPROF.INI")));profile_.validate();
    const auto&raw=entries_.at(key(entries_,"PACKAGE.JSON"));const auto manifest=cli::parse_json(std::string(raw.begin(),raw.end()));
    if(!manifest.object())throw FormatError("Ungueltiges OBD-Manifest.");
    const auto it=manifest.object()->find("files");if(it==manifest.object()->end()||!it->second.object())throw FormatError("OBD-Pruefsummen fehlen.");
    for(const auto&[name,hash]:*it->second.object())if(!hash.string()||sha256(entries_.at(key(entries_,name)))!=*hash.string())throw FormatError("OBD-Ressource wurde extern veraendert: "+name);
    const auto& runtime=entries_.at(key(entries_,"RUNTIME.DAT"));if(runtime.size()<36 || std::string(runtime.begin(),runtime.begin()+8)!="OBMHDAT1")throw FormatError("Kein unterstuetztes eigenstaendiges OBD-Datenpaket.");
    db_=decode_database(entries_.at(key(entries_,"M3D.MAN")),profile_.counts);refresh_owners();
}
void Document::refresh_owners(){owners_.assign(db_.players.size(),-1);for(uint32_t team=0;team<profile_.counts.league_teams;++team)for(uint32_t row=0;row<24;++row){int id=le16(db_.squad_list,(team*24+row)*2)&4095;if(id!=4095){if(size_t(id)>=owners_.size()||owners_[size_t(id)]!=-1)throw FormatError("Ungueltige oder doppelte Kaderzuordnung.");owners_[size_t(id)]=int(team);}}}
int Document::club_of(size_t id)const{return owners_.at(id);}
PlayerValues Document::player(size_t id)const{const auto&p=db_.players.at(id);PlayerValues v;v.name=p.name;v.position=p.position();v.nation=p.nationality()+((db_.raw_player_bonus_values.at(id)&128)?32:0);v.age=p.age();v.form=p.form();v.technique=p.technique();v.condition=p.condition();v.bonus=db_.raw_player_bonus_values.at(id)&127;for(int i=0;i<7;++i)v.skills[i]=p.skill(Player::Skill(i));return v;}
void Document::set_player(size_t id,const PlayerValues& v){
    if(v.position<0||v.position>3||v.nation<0||v.nation>=62||v.age<16||v.age>47||v.form<0||v.form>99||v.technique<0||v.technique>99||v.condition<0||v.condition>99||v.bonus<0||v.bonus>127)throw FormatError("Spielerwert ausserhalb des Original-Bereichs.");
    for(auto skill:v.skills)if(skill<0||skill>3)throw FormatError("Faehigkeiten liegen zwischen 0 und 3.");
    auto next=db_;auto&p=next.players.at(id);rename(next.pools[1],next.raw_player_name_offsets,id,v.name);p.name=v.name;
    p.w35c=(p.w35c&~0x1fffffu)|uint32_t(v.form)|uint32_t(v.technique)<<7|uint32_t(v.condition)<<14;
    p.w364=(p.w364&~(3u|(31u<<21)))|uint32_t(v.position)|uint32_t(v.age-16)<<21;
    uint32_t skills=0;for(int i=0;i<7;++i)skills|=uint32_t(v.skills[i])<<(i*2);
    p.w368=(p.w368&0x07ffc000u)|skills|uint32_t(v.nation&31)<<27;
    next.raw_player_bonus_values.at(id)=uint8_t(v.bonus|((v.nation>>5)<<7));
    if(encode_database(next)!=encode_database(db_)){db_=std::move(next);dirty_=true;changes_.push_back("player:"+std::to_string(id));}
}
void Document::set_club(size_t id,std::string name,int kit,int start,int length){
    if(kit<0||kit>255||start<0||start>15||length<1||length>16||size_t(start+length)>name.size())throw FormatError("Ungueltige Farbe oder Kurznamens-Auswahl.");
    auto next=db_;rename(next.pools[0],next.raw_club_name_offsets,id,name);next.club_names.at(id)=std::move(name);next.raw_club_kit_selectors.at(id)=uint8_t(kit);
    if(id<next.counts.n42)next.raw_club_short_name_offsets.at(id)=uint8_t((start<<4)|(length-1));
    if(encode_database(next)!=encode_database(db_)){db_=std::move(next);dirty_=true;changes_.push_back("club:"+std::to_string(id));}
}
void Document::move_player(size_t id,int destination,int swap){
    const int source=club_of(id);if(source<0||destination<0||destination>=int(profile_.counts.league_teams)||source==destination)throw FormatError("Kaderwechsel nur zwischen zwei Ligavereinen moeglich.");
    auto find=[&](int team,int pid){for(int row=0;row<24;++row)if((le16(db_.squad_list,size_t(team*24+row)*2)&4095)==pid)return team*24+row;return -1;};
    int from=find(source,int(id)),to=find(destination,swap<0?4095:swap);
    if(to<0)throw FormatError("Zielkader ist voll; Tauschpartner auswaehlen.");
    if(swap>=0 && (size_t(swap)>=owners_.size()||club_of(size_t(swap))!=destination))throw FormatError("Ungueltiger Tauschpartner.");
    if(swap<0 && std::count(owners_.begin(),owners_.end(),source)<=11)throw FormatError("Der Quellverein braucht mindestens elf Spieler.");
    auto slots=db_.squad_list;
    if(swap>=0){const auto a=le16(slots,size_t(from)*2),b=le16(slots,size_t(to)*2);word(slots,size_t(from)*2,uint16_t((a&0xf000)|(b&4095)));word(slots,size_t(to)*2,uint16_t((b&0xf000)|(a&4095)));}
    else{word(slots,size_t(to)*2,le16(slots,size_t(from)*2));word(slots,size_t(from)*2,4095);for(int team:{source,destination}){std::vector<uint16_t> list;for(int row=0;row<24;++row){auto v=le16(slots,size_t(team*24+row)*2);if((v&4095)!=4095)list.push_back(v);}for(int row=0;row<24;++row)word(slots,size_t(team*24+row)*2,row<int(list.size())?uint16_t((list[size_t(row)]&0x7fff)|(row<11?0x8000:0)):4095);}}
    db_.squad_list=std::move(slots);refresh_owners();dirty_=true;changes_.push_back("roster:player:"+std::to_string(id)+":"+std::to_string(source)+">"+std::to_string(destination));
}
std::array<uint32_t,256> Document::palette() const {
    const auto& data=entries_.at(key(entries_,"M3PC.COL"));if(data.size()!=768)throw FormatError("Ungueltige Palette.");
    std::array<uint32_t,256> colors{};for(size_t i=0;i<256;++i)colors[i]=0xff000000u|uint32_t(vga_dac_rgb8(data[i*3]))<<16|uint32_t(vga_dac_rgb8(data[i*3+1]))<<8|vga_dac_rgb8(data[i*3+2]);return colors;
}
Image Document::logo_sheet(int page) const {
    const auto name="W/"+std::to_string(page)+".VGA";
    const auto changed=changed_assets_.find(name);if(changed!=changed_assets_.end())return decode_vga(changed->second);
    return decode_vga(entries_.at(key(entries_,name)));
}
std::array<uint32_t,1444> Document::logo(size_t club) const {
    if(club>=profile_.national_first)throw FormatError("Kein Vereinslogo.");
    const int cell=le16(db_.raw_club_badge_selectors,club*2)&511;const auto sheet=logo_sheet(cell/8);if(sheet.width!=304||sheet.height!=38)throw FormatError("Ungueltiger Wappenbogen.");
    const auto colors=palette();std::array<uint32_t,1444> out{};for(int y=0;y<38;++y)for(int x=0;x<38;++x)out[size_t(y*38+x)]=colors[sheet.pixels[size_t(y*304+(cell%8)*38+x)]];return out;
}
Image Document::convert_logo(const std::array<uint32_t,1444>& rgba) const {
    const auto colors=palette();Image image(38,38,94);std::map<int,Image> sheets;
    for(int y=0;y<38;++y)for(int x=0;x<38;++x){int votes[4]{};for(int club=0;club<90;++club){int cell=le16(db_.raw_club_badge_selectors,size_t(club)*2)&511;auto it=sheets.find(cell/8);if(it==sheets.end())it=sheets.emplace(cell/8,logo_sheet(cell/8)).first;if(it->second.width!=304||it->second.height!=38)throw FormatError("Ungueltiger Wappenbogen.");auto value=it->second.pixels[size_t(y*304+(cell%8)*38+x)];if(value>=92&&value<=95)++votes[value-92];}
        const int background=92+int(std::max_element(votes,votes+4)-votes);auto c=rgba[size_t(y*38+x)];int alpha=int(c>>24);int red=(int(c>>16&255)*alpha+int(colors[size_t(background)]>>16&255)*(255-alpha)+127)/255;int green=(int(c>>8&255)*alpha+int(colors[size_t(background)]>>8&255)*(255-alpha)+127)/255;int blue=(int(c&255)*alpha+int(colors[size_t(background)]&255)*(255-alpha)+127)/255;
        int best=0,distance=2147483647;for(int i=0;i<256;++i){int dr=int(colors[size_t(i)]>>16&255)-red,dg=int(colors[size_t(i)]>>8&255)-green,db=int(colors[size_t(i)]&255)-blue;int d=dr*dr+dg*dg+db*db;if(d<distance){best=i;distance=d;}}image.at(x,y)=uint8_t(best);
    }return image;
}
std::array<uint32_t,1444> Document::preview_logo(const std::array<uint32_t,1444>& rgba) const {auto image=convert_logo(rgba);auto colors=palette();std::array<uint32_t,1444> out{};for(size_t i=0;i<1444;++i)out[i]=colors[image.pixels[i]];return out;}
void Document::set_logo(size_t club,const std::array<uint32_t,1444>& rgba){
    if(club>=profile_.national_first)throw FormatError("Kein Vereinslogo.");
    auto selectors=db_.raw_club_badge_selectors;const auto old=le16(selectors,club*2);int cell=old&511;int references=0;bool used[512]{};
    for(size_t i=0;i<db_.counts.clubs;++i){int c=le16(selectors,i*2)&511;used[c]=true;references+=c==cell;}
    if(references>1){auto free=std::find(used,used+512,false);if(free==used+512)throw FormatError("Keine freie Wappenzelle vorhanden.");cell=int(free-used);}
    Image sheet;try{sheet=logo_sheet(cell/8);}catch(const std::out_of_range&){sheet=Image(304,38,94);}catch(const FormatError&){if(entries_.count("W/"+std::to_string(cell/8)+".VGA"))throw;sheet=Image(304,38,94);}
    if(sheet.width!=304||sheet.height!=38)throw FormatError("Ungueltiger Wappenbogen.");auto image=convert_logo(rgba);
    for(int y=0;y<38;++y)for(int x=0;x<38;++x)sheet.at((cell%8)*38+x,y)=image.at(x,y);
    Bytes encoded;const auto put=[&](uint32_t v,int count){for(int i=0;i<count;++i)encoded.push_back(uint8_t(v>>(8*i)));};Bytes payload;
    for(size_t i=0;i<sheet.pixels.size();i+=128){auto count=std::min(size_t(128),sheet.pixels.size()-i);payload.push_back(uint8_t(count-1));payload.insert(payload.end(),sheet.pixels.begin()+i,sheet.pixels.begin()+i+count);}payload.push_back(0);put(304,2);put(38,2);put(uint32_t(payload.size()),4);encoded.insert(encoded.end(),payload.begin(),payload.end());
    const auto name="W/"+std::to_string(cell/8)+".VGA";changed_assets_[name]=std::move(encoded);word(db_.raw_club_badge_selectors,club*2,uint16_t((old&~511)|cell));dirty_=true;changes_.push_back("logo:club:"+std::to_string(club));
}
std::filesystem::path Document::save(){
    if(!dirty_)return {};
    Lock lock(path_);if(read_file(path_)!=original_)throw FormatError("OBD wurde seit dem Oeffnen extern geaendert. Bitte neu oeffnen.");
    const auto master=encode_database(db_);
    if(changed_assets_.empty()&&master==entries_.at(key(entries_,"M3D.MAN"))){dirty_=false;changes_.clear();return {};}
    auto changed=entries_;changed[key(changed,"M3D.MAN")]=master;for(const auto&[name,data]:changed_assets_){auto found=std::find_if(changed.begin(),changed.end(),[&](const auto& item){return upper(item.first)==upper(name);});if(found==changed.end())changed[name]=data;else found->second=data;}
    auto&manifestBytes=changed.at(key(changed,"PACKAGE.JSON"));auto manifest=cli::parse_json(std::string(manifestBytes.begin(),manifestBytes.end()));cli::Json::Object hashes;
    for(const auto&[name,bytes]:changed)if(upper(name)!="PACKAGE.JSON")hashes.emplace(name,sha256(bytes));
    (*manifest.object())["files"]=std::move(hashes);
    const auto token=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    cli::Json::Array records;for(const auto& change:changes_)records.emplace_back(change);
    auto& history=(*manifest.object())["editor_history"];
    if(!history.array())history=cli::Json::Array{};
    std::get<cli::Json::Array>(history.value).emplace_back(cli::Json::Object{{"editor","obdeddi"},{"timestamp_epoch_ticks",token},{"source_sha256",sha256(original_)},{"changes",std::move(records)}});
    const auto text=cli::dump_json(manifest);manifestBytes=Bytes(text.begin(),text.end());
    const auto output=pack(changed);auto temp=path_;temp+=".tmp-"+token;
    std::filesystem::path backup;
    try{
        write(temp,output);Document verified(temp);if(encode_database(verified.database())!=encode_database(db_))throw FormatError("Pruefung des neuen Archivs fehlgeschlagen.");
        const auto backups=path_.parent_path()/"backups";std::filesystem::create_directories(backups);auto backupName=path_.filename();backupName+="."+token+".bak";backup=backups/backupName;
        std::filesystem::copy_file(path_,backup);if(read_file(backup)!=original_)throw FormatError("Backup-Pruefung fehlgeschlagen.");
        if(read_file(path_)!=original_)throw FormatError("OBD wurde waehrend des Speicherns extern geaendert.");
#ifdef _WIN32
        if(!ReplaceFileW(path_.c_str(),temp.c_str(),nullptr,0,nullptr,nullptr))throw FormatError("OBD konnte nicht atomar ersetzt werden. Original und Backup bleiben erhalten.");
#else
        std::filesystem::rename(temp,path_);
#endif
    }catch(...){std::error_code ec;std::filesystem::remove(temp,ec);throw;}
    entries_=std::move(changed);original_=output;dirty_=false;changes_.clear();changed_assets_.clear();return backup;
}
}
