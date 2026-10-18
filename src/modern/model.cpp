// SPDX-License-Identifier: GPL-3.0-only
#include "modern/model.hpp"
#include "launch/archive.hpp"
#include "launch/mods.hpp"
#include "modern/json.hpp"
#include <algorithm>
namespace bmh::modern {
using namespace detail;
Model::Model(std::filesystem::path data, std::filesystem::path saves,
             std::shared_ptr<const content::Classic> content, uint32_t seed)
    : data_(std::move(data)), saves_(std::move(saves)), content_(std::move(content)), seed_(seed) {
    currency_ = str(get(get(parse(content_->documents, "manifest.json"), "currency"), "code"));
    if (currency_.empty())
        throw FormatError("Die Währungsangabe der Spielwelt fehlt.");
}
const app::CareerSession &Model::session() const {
    if (!session_)
        throw FormatError("Keine Karriere geöffnet.");
    return *session_;
}
void Model::begin_new(int difficulty, int managers) {
    if (difficulty < 0 || difficulty > 9 || managers < 1 || managers > 4)
        throw FormatError("Ungültige Spieloptionen.");
    auto candidate = std::make_unique<app::CareerSession>(data_, seed_);
    options_ = ui::SetupOptions{};
    options_.difficulty = difficulty;
    options_.managers = managers;
    pending_ = std::move(candidate);
    managers_.clear();
}
std::vector<ClubChoice> Model::clubs() const {
    if (!pending_)
        throw FormatError("Keine Vereinsauswahl geöffnet.");
    ui::ManagerScreen picker(data_, int(managers_.size()), managers_, pending_->selection_context(revision_));
    std::vector<ClubChoice> result;
    for (int group = 0; group < 5; ++group)
        for (int i = picker.group_first(group); i <= picker.group_last(group); ++i) {
            const auto master = picker.master_of(i);
            if (std::any_of(managers_.begin(), managers_.end(),
                            [&](const auto &manager) { return manager.master_club == master; }))
                continue;
            result.push_back({i, master, group, ui::original_to_utf8(picker.club_name(master))});
        }
    return result;
}
bool Model::confirm_manager(std::string name, int selection) {
    if (!pending_)
        throw FormatError("Keine Vereinsauswahl geöffnet.");
    if (name.empty() || name.size() > 80)
        throw FormatError("Bitte einen Managernamen eingeben.");
    const auto available = clubs();
    const auto it = std::find_if(available.begin(), available.end(),
                                 [&](const auto &club) { return club.selection == selection; });
    if (it == available.end())
        throw FormatError("Vereinsauswahl ist veraltet.");
    ui::ManagerChoice manager;
    manager.slot = int(managers_.size());
    manager.name = ui::utf8_to_original(name);
    if (manager.name.empty() || manager.name.size() > 24 || ui::original_to_utf8(manager.name) != name)
        throw FormatError("Der Name muss in den importierten Zeichensatz passen "
                          "(maximal 24 Zeichen).");
    manager.master_club = it->master;
    manager.selection_club = it->selection;
    manager.start_league = it->league;
    manager.roster_revision = revision_;
    std::string error;
    if (!pending_->confirm_manager(options_, manager, error))
        throw FormatError(error);
    managers_.push_back(manager);
    if (int(managers_.size()) < options_.managers)
        return false;
    ui::CareerInitializationResult initial;
    if (!pending_->finish(initial, error)) {
        cancel_new();
        throw FormatError(error);
    }
    // Projection must succeed before replacing the currently open career.
    (void)pending_->desktop_projection(initial.desktop.revision);
    session_ = std::move(pending_);
    revision_ = initial.desktop.revision;
    dirty_ = true;
    prepared_ = false;
    return true;
}
void Model::load(const std::filesystem::path &file) {
    const auto candidate_path = saves_ / file.filename();
    if (file != candidate_path || file.extension() != ".obs" ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(file)))
        throw FormatError("Ungültiger Spielstandspfad.");
    auto candidate = std::make_unique<app::CareerSession>(data_, seed_, file);
    (void)candidate->desktop_projection(revision_ + 1);
    session_ = std::move(candidate);
    ++revision_;
    dirty_ = false;
    prepared_ = false;
    cancel_new();
}
void Model::save(std::string name, bool overwrite) {
    (void)session();
    if (name.ends_with(".obs"))
        name.resize(name.size() - 4);
    launch::ModLibrary::validate_name(name);
    std::filesystem::create_directories(saves_);
    std::string error;
    if (!session_->save_game(saves_ / launch::path_from_utf8(name + ".obs"), snapshot().desk.manager_slot,
                             overwrite, error))
        throw FormatError(error);
    dirty_ = false;
}
std::vector<std::filesystem::path> Model::saves() const {
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::is_directory(saves_))
        return out;
    for (const auto &entry : std::filesystem::directory_iterator(saves_))
        if (entry.is_regular_file() && !entry.is_symlink() && entry.path().extension() == ".obs")
            out.push_back(entry.path());
    std::sort(out.begin(), out.end());
    return out;
}
Snapshot Model::snapshot() const {
    const auto &current = session();
    Snapshot out;
    out.currency = currency_;
    out.desk = current.desktop_projection(revision_);
    out.squad = ui::lineup_from_game(current.state(), content_->executable, revision_);
    out.squad.entry_prepared = prepared_;
    for (const auto &player : out.squad.players)
        if ((player.slot & 0xc000) == 0x8000 && player.player_id >= 0 && player.player_id != 0xfff)
            out.pitch.push_back({player.row, std::clamp(int(player.board[0x41]), 0, 19) / 19.f,
                                 std::clamp(int(player.board[0x42]), 0, 8) / 8.f});
    auto tables = ui::league_tables_from_game(current.state(), out.desk.manager_slot);
    out.tables.assign(tables.begin(), tables.end());
    return out;
}
void Model::prepare() {
    if (prepared_)
        return;
    auto observed = snapshot().squad;
    std::string error;
    if (!app::LineupActions(*session_).lineup(app::LineupActions::prepare(observed), revision_, error,
                                              [&] { (void)snapshot(); }))
        throw FormatError(error);
    prepared_ = true;
    dirty_ = true;
}
void Model::swap(const ui::LineupState &observed, int first, int second) {
    if (!prepared_)
        throw FormatError("Aufstellung zuerst öffnen.");
    auto find = [&](int row) -> const ui::LineupPlayer & {
        auto at = std::find_if(observed.players.begin(), observed.players.end(),
                               [&](const auto &player) { return player.row == row; });
        if (at == observed.players.end() || at->player_id < 0)
            throw FormatError("Ungültige Spielerwahl.");
        return *at;
    };
    auto request = app::LineupActions::swap(observed, find(first), find(second));
    std::string error;
    if (!app::LineupActions(*session_).lineup(request, revision_, error, [&] { (void)snapshot(); }))
        throw FormatError(error);
    dirty_ = true;
}
std::string Model::crest(int identity) const {
    const auto clubs = items(content_->documents, "clubs");
    for (const auto &club : clubs) {
        const auto index = size_t(num(get(get(club, "x-legacy"), "team_index")));
        if (index * 2 + 1 < content_->database.raw_club_name_offsets.size() &&
            le16(content_->database.raw_club_name_offsets, index * 2) == identity)
            return str(get(club, "crest"));
    }
    return "asset:placeholder";
}
std::string Model::photo(int player) const {
    const auto players = items(content_->documents, "players");
    if (player >= 0 && size_t(player) < players.size())
        return str(get(players[size_t(player)], "photo"));
    return "asset:placeholder";
}
std::optional<rml::Bitmap> Model::image(const std::string &key) const {
    const auto index = parse(content_->documents, "assets/index.json");
    auto decode = [&](const std::string &asset) -> std::optional<rml::Bitmap> {
        for (const auto &row : arr(get(index, "items")))
            if (str(get(row, "key")) == asset) {
                const auto source = str(get(row, "source"));
                const auto at = content_->images.find(source);
                if (at == content_->images.end())
                    return {};
                const auto &image = at->second;
                rml::Bitmap out{image.width, image.height, {}};
                out.rgba.reserve(image.pixels.size() * 4);
                bool transparent = source.starts_with("crests/") || source.starts_with("portraits/layer-");
                for (auto pixel : image.pixels) {
                    auto argb = content_->palette.argb[pixel];
                    out.rgba.insert(out.rgba.end(), {uint8_t(argb >> 16), uint8_t(argb >> 8), uint8_t(argb),
                                                     uint8_t(transparent && pixel == 0 ? 0 : 255)});
                }
                return out;
            }
        return {};
    };
    if (auto direct = decode(key))
        return direct;
    for (const auto &player : items(content_->documents, "players"))
        if (str(get(player, "photo")) == key) {
            const auto &appearance = get(player, "appearance");
            auto out = decode("asset:portrait/bmh-classic/background/" +
                              std::to_string(num(get(appearance, "background"))));
            if (!out)
                break;
            const auto set = parse(content_->documents, "assets/portrait-sets/bmh-classic@1/set.json");
            const auto &parts = arr(get(appearance, "layers"));
            const auto &layers = arr(get(set, "layers"));
            for (size_t i = 0; i < layers.size(); ++i) {
                const auto &layer = layers[i];
                auto bitmap = decode(str(arr(get(layer, "parts")).at(size_t(num(parts[i])))));
                if (!bitmap)
                    continue;
                const int dx = int(num(get(layer, "x"))), dy = int(num(get(layer, "y")));
                for (int y = 0; y < bitmap->height; ++y)
                    for (int x = 0; x < bitmap->width; ++x) {
                        if (x + dx < 0 || y + dy < 0 || x + dx >= out->width || y + dy >= out->height)
                            continue;
                        const size_t from = size_t(y * bitmap->width + x) * 4,
                                     to = size_t((y + dy) * out->width + x + dx) * 4;
                        if (bitmap->rgba[from + 3])
                            std::copy_n(bitmap->rgba.begin() + from, 4, out->rgba.begin() + to);
                    }
            }
            return out;
        }
    // A small neutral silhouette; no guessed photo or name-derived random
    // portrait.
    rml::Bitmap out{48, 64, std::vector<Rml::byte>(48 * 64 * 4)};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 48; ++x) {
            const bool figure = (x - 24) * (x - 24) + (y - 19) * (y - 19) < 100 ||
                                (y > 35 && (x - 24) * (x - 24) < (y - 30) * 13);
            const size_t at = size_t(y * 48 + x) * 4;
            out.rgba[at] = figure ? 115 : 20;
            out.rgba[at + 1] = figure ? 144 : 38;
            out.rgba[at + 2] = figure ? 164 : 53;
            out.rgba[at + 3] = 255;
        }
    return out;
}
} // namespace bmh::modern
