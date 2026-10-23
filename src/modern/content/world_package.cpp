// SPDX-License-Identifier: GPL-3.0-only
#include "modern/content/world_package.hpp"

#include <algorithm>

#include "import/classic.hpp"
#include "import/detail.hpp"

namespace bmh::modern {

namespace {
std::shared_ptr<const World>& current() {
    static std::shared_ptr<const World> world;
    return world;
}

// The entries the engine needs. High-resolution artwork (crests, portraits, trophies) stays in
// the archive: interfaces read it from there by asset key, the engine never does.
bool engine_entry(const std::string& name) {
    for (const char* prefix : {"world/", "x-classic/", "x-legacy/", "locale/", "assets/classic/"})
        if (name.rfind(prefix, 0) == 0) return true;
    return name == "manifest.json" || name == "assets/index.json";
}

obw::Documents read_engine_entries(const std::filesystem::path& file) {
    using obw::detail::fail;
    namespace fs = std::filesystem;
    if (fs::is_symlink(fs::symlink_status(file)) || !fs::is_regular_file(file)) fail("invalid world archive");
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, file.string().c_str(), 0)) fail("invalid ZIP archive");
    struct End {
        mz_zip_archive* zip;
        ~End() { mz_zip_reader_end(zip); }
    } end{&zip};
    obw::Documents out;
    uint64_t total = 0;
    const auto count = mz_zip_reader_get_num_files(&zip);
    if (count > 65536) fail("too many archive entries");
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) fail("cannot read entry metadata");
        if (stat.m_is_directory || !engine_entry(stat.m_filename)) continue;
        const std::string name = stat.m_filename;
        if (!obw::detail::safe_path(name) || stat.m_uncomp_size > 64ull * 1024 * 1024 ||
            (total += stat.m_uncomp_size) > 256ull * 1024 * 1024)
            fail("unsafe or oversized archive entry");
        std::string value(size_t(stat.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, i, value.data(), value.size(), 0)) fail("damaged ZIP entry");
        if (!out.emplace(name, std::move(value)).second) fail("duplicate archive entry");
    }
    return out;
}
}  // namespace

WorldPackage open_world_package(const std::filesystem::path& file) {
    using obw::detail::fail;
    auto docs = read_engine_entries(file);
    World world = read_world(docs);
    const auto needs = [&](const char* r) {
        return std::find(world.requires_.begin(), world.requires_.end(), r) != world.requires_.end();
    };
    // Data revision 1.4 marks playable worlds with runtime:modern-v1; earlier editor worlds
    // (editor:world-v1) are accepted for development (docs/new_engine/datenformat.md F1).
    if (!needs("runtime:modern-v1") && !needs("editor:world-v1"))
        fail("not a Modern world: requires neither runtime:modern-v1 nor editor:world-v1");
    if (needs("runtime:classic-v1")) fail("a Classic package runs in Classic, not in the Modern engine");
    auto classic = std::make_shared<content::Classic>(obw::load_classic_payload(docs));
    classic->content_id = world.id + (world.data_revision.empty() ? "" : "@" + world.data_revision);
    classic->start_year = world.start_year;
    classic->executable.set_display_start_year(world.start_year);
    classic->documents = std::move(docs);
    WorldPackage out;
    out.classic = std::move(classic);
    out.world = std::make_shared<const World>(std::move(world));
    return out;
}

const World* current_world() { return current().get(); }
void set_current_world(std::shared_ptr<const World> world) { current() = std::move(world); }

}  // namespace bmh::modern
