// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/highscore.hpp"
#include "formats/native_state.hpp"
#include "formats/native_file.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "modern/game/weekly.hpp"

namespace bmh::modern {
namespace {
constexpr size_t kRecord = 0xb0c;
// OpenBMH deviates from the original: reads the supported edition-specific high-score format from the loaded image.
// supported rule profile executable HighscoreRuntimeField..a5144, verified against the installed LE image.
constexpr std::array<uint8_t, 8> kHistoryWeights{3, 5, 4, 30, 20, 10, 5, 40};
constexpr std::array<uint8_t, 4> kNationalWeights{30, 20, 10, 5};
constexpr std::array<uint8_t, 3> kTitleWeights{40, 30, 50};
int32_t signed32(uint32_t v) { return std::bit_cast<int32_t>(v); }
uint16_t rec16(const ModernState& g, int m, size_t offset) {
    const size_t at = size_t(m) * kRecord + offset;
    return uint16_t(g.manager_records.at(at) | g.manager_records.at(at + 1) << 8);
}
uint32_t rec32(const ModernState& g, int m, size_t offset) {
    return uint32_t(rec16(g, m, offset)) | uint32_t(rec16(g, m, offset + 2)) << 16;
}
uint8_t rec8(const ModernState& g, int m, size_t offset) {
    return g.manager_records.at(size_t(m) * kRecord + offset);
}
std::string record_name(const ModernState& g, int m) {
    std::string name;
    for (size_t i = 0; i < 30; ++i) {
        const auto c = rec8(g, m, 0xa8 + i);
        if (!c) return name;
        name.push_back(char(c));
    }
    throw std::invalid_argument("highscore: unterminated manager name");
}
// HighscoreScore(0x1f,0,0xff), deliberately using the Source global asset-manager.
int32_t net_assets(const ModernState& g, int m) {
    uint32_t sum = rec32(g, m, 0x314) + rec32(g, m, 0x318);
    for (uint32_t k = 0; k < 6; ++k)
        sum += uint32_t(g.memory.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(m) * 2 + k * 0x1c)) *
               g.memory.u32(::bmh::layout::FundBlock + k * 0x1c);
    for (size_t k = 0; k < 12; ++k) {
        const size_t at = k * 0x5c;
        const uint32_t held = uint16_t(g.stocks.at(at + 0x50 + size_t(m) * 2) |
                                      g.stocks.at(at + 0x51 + size_t(m) * 2) << 8);
        const uint32_t price = uint16_t(g.stocks.at(at + 0x4e) | g.stocks.at(at + 0x4f) << 8);
        sum += held * price;
    }
    for (uint32_t k = 0; k < 5; ++k)
        sum -= g.memory.u32(::bmh::layout::Analytics_SourceAssetMaskE_RuntimeField + uint32_t(m) * 0x78 + k * 0x18);
    return signed32(sum);
}
uint32_t read32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void put32(uint8_t* p, uint32_t v) {
    for (int k = 0; k < 4; ++k) p[k] = uint8_t(v >> (8 * k));
}
std::string read_name(const uint8_t* p) {
    size_t n = 0;
    while (n < 30 && p[n]) ++n;
    if (n == 30) throw std::invalid_argument("highscore: unterminated file name");
    return {reinterpret_cast<const char*>(p), n};
}
void put_name(uint8_t* p, const std::string& s) {
    if (s.size() >= 30 || s.find('\0') != std::string::npos)
        throw std::invalid_argument("highscore: name does not fit Source row");
    // strcpy preserves bytes beyond the terminator of an existing row.
    std::copy(s.begin(), s.end(), p);
    p[s.size()] = 0;
}
} // namespace

HighscoreScore highscore_score(const ModernState& g, int manager, HighscoreContext context) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size())
        throw std::invalid_argument("highscore: manager is outside active slots");
    HighscoreScore result;
    const int team = g.manager_teams.at(size_t(manager));
    if (team == no_team()) {
        // CareerSession_DispatchLineup_RuntimeField+CareerSession_InstallLeagueSizes_RuntimeField6+ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField3 == no_team() in the supported Original database;
        // national honours store (team-no_team())|0x80, so Germany's index is0.
        for (uint32_t year = 0; year < 20; ++year)
            for (uint32_t place = 0; place < 4; ++place) {
                const uint8_t v = g.memory.u8(::bmh::layout::Placements + year * 4 + place);
                if ((v & 0x80) && (v & 0x7f) == 0)
                    result.history += int32_t(kNationalWeights[place]) * 10;
            }
    } else {
        if (g.season < 0) throw std::invalid_argument("highscore: negative season index");
        const int asset_manager = context == HighscoreContext::SeasonBoundary
            ? int(g.manager_teams.size()) : int(g.memory.u8(::bmh::layout::ActiveManager));
        result.assets = std::min(net_assets(g, asset_manager) / 50000, 100);
        result.patience = rec8(g, manager, 0x26f);
        ModernState facility_state = g; // facility_rating's public API takes mutable state.
        int second = 0;
        result.facilities = modern::facility_rating(facility_state, manager, &second) * 4;
        result.team = int(uint16_t(team_average(g, team, 0x2ff).value)) / 10;
        const uint32_t count = std::min(uint32_t(g.season) + 1, 50u);
        uint32_t history = 0;
        for (uint32_t year = 0; year < count; ++year) {
            const size_t at = 0xda + year * 3;
            const uint8_t v = rec8(g, manager, at + 2);
            history += uint32_t(rec8(g, manager, at)) + uint32_t(rec8(g, manager, at + 1)) * 2 +
                       uint32_t(kHistoryWeights[v >> 5]) * (v & 0x1f);
        }
        result.history = int32_t((history & 0xffff) / count);
    }
    uint32_t total = uint32_t(result.assets) + uint32_t(result.patience) +
                     uint32_t(result.facilities) + uint32_t(result.team) + uint32_t(result.history);
    for (size_t k = 0; k < 3; ++k)
        result.titles += int32_t(kTitleWeights[k]) * rec16(g, manager, 0x8a4 + k * 2);
    total += uint32_t(result.titles);
    result.total = std::max(signed32(total), 0);
    return result;
}

HighscoreRow highscore_candidate(const ModernState& g, int manager, HighscoreContext context) {
    if (g.memory.u16(::bmh::layout::CareerSession_FormatError_RuntimeField7) == 0) return {};
    HighscoreRow row;
    row.score = modern::highscore_score(g, manager, context).total;
    row.club_name = g.club_name(g.manager_teams.at(size_t(manager)));
    row.manager_name = record_name(g, manager);
    for (size_t k = 0; k < 3; ++k) row.titles[k] = rec16(g, manager, 0x8a4 + k * 2);
    return row;
}

void insert_highscore(HighscoreTable& rows, const HighscoreRow& candidate) {
    size_t target = rows.size();
    for (size_t k = 0; k < rows.size(); ++k)
        if (rows[k].manager_name == candidate.manager_name && rows[k].club_name == candidate.club_name) {
            target = k;
            break;
        }
    if (target == rows.size() && rows.back().score < candidate.score) target = rows.size() - 1;
    if (target == rows.size()) return;
    rows[target] = candidate;
    // Swap uses these pairwise swaps; preserve equal-score ordering exactly.
    for (size_t a = 0; a < 4; ++a)
        for (size_t b = a + 1; b < 5; ++b)
            if (rows[a].score < rows[b].score) std::swap(rows[a], rows[b]);
}

HighscoreProjection make_highscore_projection(const ModernState& g, HighscoreTable rows, HighscoreContext context) {
    HighscoreProjection out;
    out.signature = g.memory.u16(::bmh::layout::CareerSession_FormatError_RuntimeField7);
    out.ranked = out.signature != 0;
    out.rows = std::move(rows);
    if (!out.ranked) return out;
    // GameEndAlternateScoreScreen gates the entire manager loop using slot0, not each candidate's team.
    if (!g.manager_teams.empty() &&
        (g.manager_teams[0] != no_team() || (g.memory.u8(::bmh::layout::CompetitionSelection) & 1) == 0))
        for (size_t m = 0; m < g.manager_teams.size(); ++m)
            modern::insert_highscore(out.rows, modern::highscore_candidate(g, int(m), context));
    return out;
}

HighscoreBytes encode_highscore_table(const HighscoreTable& rows) {
    HighscoreBytes bytes{};
    for (size_t k = 0; k < rows.size(); ++k) {
        uint8_t* p = bytes.data() + k * 72;
        std::copy(rows[k].original_bytes.begin(), rows[k].original_bytes.end(), p);
        put32(p, uint32_t(rows[k].score));
        put_name(p + 4, rows[k].club_name);
        put_name(p + 0x22, rows[k].manager_name);
        for (size_t t = 0; t < 3; ++t) {
            p[0x40 + t * 2] = uint8_t(rows[k].titles[t]);
            p[0x41 + t * 2] = uint8_t(rows[k].titles[t] >> 8);
        }
    }
    return bytes;
}
HighscoreTable decode_highscore_table(const HighscoreBytes& bytes) {
    HighscoreTable rows{};
    for (size_t k = 0; k < rows.size(); ++k) {
        const uint8_t* p = bytes.data() + k * 72;
        std::copy(p, p + 72, rows[k].original_bytes.begin());
        rows[k].score = signed32(read32(p));
        rows[k].club_name = read_name(p + 4);
        rows[k].manager_name = read_name(p + 0x22);
        for (size_t t = 0; t < 3; ++t) rows[k].titles[t] = uint16_t(p[0x40 + t * 2] | p[0x41 + t * 2] << 8);
    }
    return rows;
}
std::filesystem::path highscore_file(const std::filesystem::path &base, uint16_t signature,
                                     const std::string &prefix, bool native) {
    if (!signature) throw std::invalid_argument("highscore: signature0 has no ranked file");
    if (native)
        return base / ("highscore-" + std::to_string(signature) + ".obscore");
    return base / (prefix + std::to_string(signature));
}
HighscoreTable load_highscore_table(const std::filesystem::path& file) {
    if (file.extension() == ".obscore") {
        const auto data = read_file(file);
        const auto j = cli::parse_json(std::string(data.begin(), data.end()));
        if (!j.object() || !j.object()->contains("schema") || !j.object()->at("schema").string() ||
            *j.object()->at("schema").string() != "classic-highscore@1.0" || !j.object()->contains("payload"))
            throw FormatError("Invalid native highscore");
        const auto raw = decode_native_bytes(j.object()->at("payload"), 360);
        HighscoreBytes bytes;
        std::copy(raw.begin(), raw.end(), bytes.begin());
        return modern::decode_highscore_table(bytes);
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        HighscoreTable empty{};
        modern::save_highscore_table(file, empty); // Read creates a zero table on a missing file.
        return empty;
    }
    HighscoreBytes bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    return modern::decode_highscore_table(bytes);
}
bool save_highscore_table(const std::filesystem::path& file, const HighscoreTable& rows) {
    const auto bytes = modern::encode_highscore_table(rows);
    if (file.extension() == ".obscore") {
        try {
            content::write_atomic(file,
                                  cli::dump_json(cli::Json::Object{
                                      {"schema", "classic-highscore@1.0"},
                                      {"payload", encode_native_bytes(Bytes(bytes.begin(), bytes.end()))}}),
                                  true);
            return true;
        } catch (const std::exception &) {
            return false;
        }
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return bool(out);
}
} // namespace bmh::modern
