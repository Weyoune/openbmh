// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/m3d.hpp"
#include "formats/data_profile.hpp"
#include <map>
namespace bmh::obdeddi {
struct PlayerValues {
    std::string name;
    int position=0,nation=0,age=16,form=0,technique=0,condition=0,bonus=0;
    int skills[7]{};
};
class Document {
public:
    explicit Document(const std::filesystem::path&);
    const Database& database() const {return db_;}
    const DataProfile& profile() const{return profile_;}
    const std::filesystem::path& path() const{return path_;}
    bool dirty() const{return dirty_;}
    PlayerValues player(size_t) const;
    int club_of(size_t player) const;
    void set_player(size_t,const PlayerValues&);
    void set_club(size_t,std::string name,int kit,int short_start,int short_length);
    void move_player(size_t player,int club,int swap_player=-1);
    // Returns the verified backup path, or an empty path for a no-op.
    std::filesystem::path save();
private:
    std::filesystem::path path_;
    Bytes original_;
    std::map<std::string,Bytes> entries_;
    Database db_;
    DataProfile profile_;
    bool dirty_=false;
    std::vector<int> owners_;
    std::vector<std::string> changes_;
    void refresh_owners();
};
std::string sha256(const Bytes&);
}
