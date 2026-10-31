// SPDX-License-Identifier: GPL-3.0-only
#include "formats/resource_bundle.hpp"
#include "miniz.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
namespace bmh {
namespace {
std::string folded(std::string name) {
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char ch){return char(std::toupper(ch));});
    return name;
}
struct Bundle {
    Bytes file;
    mz_zip_archive zip{};
    std::map<std::string,mz_uint> entries;
    explicit Bundle(const std::filesystem::path& path) {
        const auto size=std::filesystem::file_size(path);
        if(size>64*1024*1024)throw FormatError("community bundle exceeds size limit");
        std::ifstream input(path,std::ios::binary);
        file.assign(std::istreambuf_iterator<char>(input),{});
        if(!input.is_open() || file.size()!=size || !mz_zip_reader_init_mem(&zip,file.data(),file.size(),0))
            throw FormatError("cannot read community bundle");
        try {
            uint64_t total=0;
            const auto count=mz_zip_reader_get_num_files(&zip);
            if(count>4096)throw FormatError("community bundle has too many entries");
            for(mz_uint i=0;i<count;++i) {
                mz_zip_archive_file_stat st{};
                if(!mz_zip_reader_file_stat(&zip,i,&st))throw FormatError("invalid community directory");
                if(st.m_is_directory)continue;
                const std::string name=st.m_filename;
                const std::filesystem::path relative(name);
                if(name.empty()||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos||relative.is_absolute())
                    throw FormatError("unsafe community resource name");
                for(const auto& part:relative)if(part==".."||part==".")throw FormatError("unsafe community resource path");
                total+=st.m_uncomp_size;
                if(st.m_uncomp_size>32*1024*1024 || total>128*1024*1024 ||
                   !entries.emplace(folded(name),i).second)throw FormatError("ambiguous or oversized community resources");
            }
        }catch(...){mz_zip_reader_end(&zip);throw;}
    }
    ~Bundle(){mz_zip_reader_end(&zip);}
};
std::mutex bundle_mutex;
struct Cached { std::filesystem::file_time_type modified; uintmax_t size; std::shared_ptr<Bundle> bundle; };
std::map<std::filesystem::path,Cached> cache;
std::pair<std::shared_ptr<Bundle>,std::string> locate(const std::filesystem::path& requested) {
    auto path=std::filesystem::absolute(requested).lexically_normal();
    auto root=path.parent_path();
    for(int depth=0;depth<4 && !root.empty();++depth) {
        const auto archive=root/kCommunityBundle;
        std::error_code ec;
        if(std::filesystem::is_regular_file(archive,ec)) {
            const auto modified=std::filesystem::last_write_time(archive);
            const auto size=std::filesystem::file_size(archive);
            auto found=cache.find(archive);
            if(found==cache.end()||found->second.modified!=modified||found->second.size!=size) {
                auto bundle=std::make_shared<Bundle>(archive);
                if(cache.size()>=4)cache.clear();
                found=cache.insert_or_assign(archive,Cached{modified,size,std::move(bundle)}).first;
            }
            auto name=folded(path.lexically_relative(root).generic_string());
            // Compatibility address-view API; the archive contains no EXE image.
            if(name=="BMH.EXE")name="RUNTIME.DAT";
            return {found->second.bundle,std::move(name)};
        }
        const auto parent=root.parent_path();if(parent==root)break;root=parent;
    }
    return {};
}
}
std::optional<Bytes> bundled_resource(const std::filesystem::path& path) {
    std::lock_guard lock(bundle_mutex);
    auto [bundle,name]=locate(path);if(!bundle)return std::nullopt;
    const auto found=bundle->entries.find(name);if(found==bundle->entries.end())return std::nullopt;
    mz_zip_archive_file_stat st{};mz_zip_reader_file_stat(&bundle->zip,found->second,&st);
    Bytes result(size_t(st.m_uncomp_size));
    if(!mz_zip_reader_extract_to_mem(&bundle->zip,found->second,result.data(),result.size(),0))
        throw FormatError("damaged community resource: "+name);
    return result;
}
bool resource_exists(const std::filesystem::path& path) {
    std::error_code ec;if(std::filesystem::is_regular_file(path,ec))return true;
    std::lock_guard lock(bundle_mutex);auto [bundle,name]=locate(path);
    return bundle && bundle->entries.count(name)!=0;
}
}
