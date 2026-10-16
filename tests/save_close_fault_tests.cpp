// SPDX-License-Identifier: GPL-3.0-only
#include "game/save.hpp"
#include "formats/fbm.hpp"
#include <sys/resource.h>
#include <signal.h>
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){
 if(argc!=2)return 2;
 const auto output=std::filesystem::temp_directory_path() / ("openbmh-close-fault-"+std::to_string(reinterpret_cast<uintptr_t>(&argc))+".FBM");
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove(p,e);}}cleanup{output};
 auto s=bmh::load_savegame(argv[1]);if(!s.checksum_ok)return 3;
 const auto expected=bmh::savegame_bytes(s).size();
 signal(SIGXFSZ,SIG_IGN);rlimit limit{expected-1,expected-1};
 if(setrlimit(RLIMIT_FSIZE,&limit))return 4;
 bool threw=false;try{bmh::write_savegame(output,s);}catch(const std::exception&){threw=true;}
 auto actual=std::filesystem::file_size(output);
 std::cout<<"{\"threw\":"<<(threw?"true":"false")<<",\"expected_bytes\":"<<expected<<",\"actual_bytes\":"<<actual<<",\"process_local_file_limit\":true,\"Native_patch\":false,\"RNG_hook\":false}\n";
 return threw&&actual<expected?0:1;
}
