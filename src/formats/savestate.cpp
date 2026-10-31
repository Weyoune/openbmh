// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "formats/savestate.hpp"

#include <algorithm>
#include <string>

namespace bmh {

namespace {

// Size of a block, either fixed or derived from a database table size.
enum class Count { None, Clubs, Players, N42, N66, Teams, TeamsPlusCups };

struct BlockDef {
    uint32_t address;
    bool heap;
    uint32_t fixed;        // fixed part of the size
    Count count;           // table size multiplied by factor, added to fixed
    uint32_t factor;
    bool only_if_pointer;  // block exists only if the preceding pointer value is non-zero
};

// The exact order of NationalCoachAfterLoad. Addresses are the globals in BMH.exe V2.24.
// Sizes marked with a Count depend on the master database:
//   Clubs   = CareerSession_DataDirectory_RuntimeField (419)     Players = CareerSession_DataDirectory_RuntimeField3 (2574)
//   N42     = CareerSession_DataDirectory_RuntimeField2 (168)     N66     = CareerSession_DataDirectory_RuntimeField4 (3096)
//   Teams   = CareerSession_DispatchLineup_RuntimeField (138, teams in league play)
//   TeamsPlusCups = CareerSession_DispatchLineup_RuntimeField + CareerSession_InstallLeagueSizes_RuntimeField6 (138 + 30)
constexpr BlockDef kBlocks[] = {
    {::bmh::layout::Cup_AdvanceCup_RuntimeField3, false, 1, Count::None, 0, false},       {::bmh::layout::LiveMatchday_FormatError_RuntimeField14, false, 6, Count::None, 0, false},
    {::bmh::layout::ManagerRecords, false, 0x373c, Count::None, 0, false},  // 5 manager records of 0xB0C bytes
    {::bmh::layout::Loans, false, 0x1e0, Count::None, 0, false},
    {::bmh::layout::ManagerMatchHistory, false, 0, Count::N42, 50, false},  // form history: 5 managers x 168 teams x 10
    {::bmh::layout::MatchdayGroupDescriptors, false, 0x4e0, Count::None, 0, false},   {::bmh::layout::ManagerAutomation, false, 0x14, Count::None, 0, false},
    {::bmh::layout::Desktops, false, 0x10e0, Count::None, 0, false},  // desktop layouts (.MEN), 5 x 0x360
    {::bmh::layout::CareerSession_DispatchToto_RuntimeField, false, 1, Count::None, 0, false},       {::bmh::layout::Cup_AdvanceCup_RuntimeField4, false, 1, Count::None, 0, false},
    {::bmh::layout::CalendarState_FormatError_RuntimeField3, false, 2, Count::None, 0, false},       {::bmh::layout::ActiveManager, false, 1, Count::None, 0, false},  // active manager
    {::bmh::layout::CareerSession_RuntimeError_RuntimeField4, false, 1, Count::None, 0, false},
    {::bmh::layout::LeaguePresentationOptions, false, 0xb4, Count::None, 0, false},    // button settings (.EIN)
    {::bmh::layout::FriendlyTeams, false, 0x78, Count::None, 0, false},    {::bmh::layout::FriendlyDays, false, 0x3c, Count::None, 0, false},
    {::bmh::layout::Cup_AdvanceCup_ExecutableField, false, 0x50, Count::None, 0, false},    {::bmh::layout::MatchdaySetup_IndoorTournamentDraw_RuntimeField, false, 0x50, Count::None, 0, false},
    {::bmh::layout::Placements, false, 0x50, Count::None, 0, false},    {::bmh::layout::ActiveSelectors, false, 8, Count::None, 0, false},
    {::bmh::layout::Career_CareerAddManager_RuntimeField2, false, 0x90, Count::None, 0, false},    {::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField, false, 0x2c, Count::None, 0, false},
    {::bmh::layout::NextDeskManager, false, 8, Count::None, 0, false},       {::bmh::layout::Savestate_Layout_ExecutableField8, false, 10, Count::None, 0, false},
    {::bmh::layout::DeskPosition, false, 1, Count::None, 0, false},       {::bmh::layout::DeskRecords_Teams_RuntimeField, false, 0, Count::Teams, 2, false},
    {::bmh::layout::CareerSession_FormatError_RuntimeField7, false, 2, Count::None, 0, false},       {::bmh::layout::Cup_AdvanceCup_RuntimeField6, false, 6, Count::None, 0, false},
    {::bmh::layout::CurrentMatchday, false, 4, Count::None, 0, false},       // seed of the game RNG (GameRandom)
    {::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3, false, 1, Count::None, 0, false},       {::bmh::layout::CoachActive, false, 1, Count::None, 0, false},
    {::bmh::layout::OriginalManagerCount, false, 1, Count::None, 0, false},       {::bmh::layout::CareerSession_Invalid_RuntimeField2, false, 4, Count::None, 0, false},  // saved RNG seed
    {::bmh::layout::CareerDuration, false, 1, Count::None, 0, false},       {::bmh::layout::GameMode, false, 1, Count::None, 0, false},
    {::bmh::layout::CompetitionSelection, false, 4, Count::None, 0, false},       {::bmh::layout::ManagerCount, false, 1, Count::None, 0, false},  // number of managers
    {::bmh::layout::SavedCompetition, false, 4, Count::None, 0, false},       {::bmh::layout::Difficulty, false, 1, Count::None, 0, false},
    {::bmh::layout::StockExchange, false, 0x450, Count::None, 0, false},   {::bmh::layout::FundBlock, false, 0, Count::N42, 1, false},
    {::bmh::layout::Coupon, false, 100, Count::None, 0, false},     {::bmh::layout::Savestate_Layout_ExecutableField2, false, 3, Count::None, 0, false},
    {::bmh::layout::Savestate_Layout_ExecutableField, false, 1, Count::None, 0, false},       {::bmh::layout::AdGroupId, false, 1, Count::None, 0, false},
    {::bmh::layout::OfferSlots, false, 0x10, Count::None, 0, false},    {::bmh::layout::ForeignClubStrength, false, 0xfb, Count::None, 0, false},
    {::bmh::layout::Career_Put_ExecutableField3, false, 0xbd, Count::None, 0, false},    {::bmh::layout::ClubShortNameOffsets, false, 0, Count::N42, 1, false},
    {::bmh::layout::ClubKitSelectors, false, 0, Count::Clubs, 1, false},   {::bmh::layout::Savestate_Layout_ExecutableField7, false, 0x18, Count::None, 0, false},
    {::bmh::layout::ClubInfo_FormatError_RuntimeField, false, 0x420, Count::None, 0, false},   {::bmh::layout::Career_CareerManagersEnd_RuntimeField, false, 0x18, Count::None, 0, false},
    {::bmh::layout::ClubInfo_FormatError_RuntimeField2, false, 0xc, Count::None, 0, false},     {::bmh::layout::DeskActions_Unsupported_RuntimeField, false, 4, Count::None, 0, false},
    {::bmh::layout::Career_RemoveJoinLoans_RuntimeField, false, 800, Count::None, 0, false},     {::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField, false, 200, Count::None, 0, false},
    {::bmh::layout::ManagerRatings, false, 0xf0, Count::None, 0, false},    {::bmh::layout::DaysIntoSeason, false, 2, Count::None, 0, false},  // day since 1 July
    {::bmh::layout::Career_CompetitionPointers_ExecutableField, false, 0x7e, Count::None, 0, false},    {::bmh::layout::Field, false, 0x100, Count::None, 0, false},
    {::bmh::layout::CalendarDay, false, 4, Count::None, 0, false},       {::bmh::layout::SeasonIndex, false, 2, Count::None, 0, false},
    {::bmh::layout::Savestate_Layout_ExecutableField5, false, 2, Count::None, 0, false},       {::bmh::layout::CompetitionCalendar, false, 0x2dc, Count::None, 0, false},  // calendar, u16/day
    {::bmh::layout::Postponed, false, 0xf0, Count::None, 0, false},    {::bmh::layout::LeagueTableDisplayModes, false, 8, Count::None, 0, false},
    {::bmh::layout::CareerSession_FormatError_ExecutableField, false, 8, Count::None, 0, false},       {::bmh::layout::ManagerSettings, false, 8, Count::None, 0, false},
    {::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField, false, 4, Count::None, 0, false},       {::bmh::layout::CareerSession_AnyOf_ExecutableField, false, 8, Count::None, 0, false},
    {::bmh::layout::Career_NewCareerMarketsAndMoods_RuntimeField2, false, 4, Count::None, 0, false},       {::bmh::layout::ManagerMailbox, true, 0x1900, Count::None, 0, false},
    {::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField, false, 4, Count::None, 0, false},       {::bmh::layout::Savestate_Layout_ExecutableField3, false, 8, Count::None, 0, false},
    {::bmh::layout::Flags, false, 0xc, Count::None, 0, false},     {::bmh::layout::Formations, false, 0x580, Count::None, 0, false},  // M3.DAT
    {::bmh::layout::PlayerDevelopmentBalance, false, 4, Count::None, 0, false},       {::bmh::layout::PlayerBonuses, true, 0x36, Count::N66, 1, false},
    {::bmh::layout::EditorList, false, 0xf20, Count::None, 0, false},   {::bmh::layout::Field0, false, 0x70, Count::None, 0, false},
    {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField13, false, 0x68, Count::None, 0, false},    {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, false, 0x100, Count::None, 0, false},
    {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField22, false, 0x80, Count::None, 0, false},    {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField23, false, 0x80, Count::None, 0, false},
    {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19, false, 0x80, Count::None, 0, false},    {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField26, false, 0x40, Count::None, 0, false},
    {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField21, false, 0x40, Count::None, 0, false},    {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14, false, 0x6c, Count::None, 0, false},
    {::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField17, false, 0x9a, Count::None, 0, false},    {::bmh::layout::Savestate_Layout_ExecutableField4, false, 0x62, Count::None, 0, false},
    {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField28, false, 4, Count::None, 0, false},       {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField31, false, 2, Count::None, 0, false},
    {::bmh::layout::Teams, false, 0x20, Count::None, 0, false},    {::bmh::layout::MatchdayGroupWorkspace, false, 0x20, Count::None, 0, false},
    {::bmh::layout::Savestate_Layout_ExecutableField6, false, 1, Count::None, 0, false},       {::bmh::layout::EventPasses, false, 1, Count::None, 0, false},
    {::bmh::layout::LeagueResults, false, 0xd5c, Count::None, 0, false},   // match results, see game/schedule.hpp
    {::bmh::layout::LeagueRoundCounters, false, 9, Count::None, 0, false},
    {::bmh::layout::TeamMorale, true, 0, Count::Clubs, 1, false},       {::bmh::layout::ClubBadgeSelectors, true, 0, Count::Clubs, 2, false},
    {::bmh::layout::TeamFlags, true, 0, Count::Clubs, 2, false},       {::bmh::layout::ClubIdentities, true, 0, Count::Clubs, 2, false},  // club name offsets
    {::bmh::layout::PlayerNameOffsets, true, 0, Count::Players, 2, false},     // player name offsets
    {::bmh::layout::MasterDatabaseMetadata, true, 100, Count::None, 0, false},      {::bmh::layout::ClubAuxiliaryWords, true, 0, Count::N42, 2, false},
    {::bmh::layout::LeagueRankHistory, true, 0, Count::Teams, 0x1a, false},    // table position history, 26 bytes per team
    {::bmh::layout::Titles, false, 0x384, Count::None, 0, false},   {::bmh::layout::Cup_AdvanceCup_RuntimeField6, false, 6, Count::None, 0, false},
    {::bmh::layout::ClubAuxiliaryBytes, true, 0, Count::TeamsPlusCups, 1, false},
    {::bmh::layout::CupTable, false, 0x5a0, Count::None, 0, false},   {::bmh::layout::SquadSlots, true, 0x6c, Count::N66, 2, false},  // squad lists
    {::bmh::layout::PlayerConditionTechniqueForm, true, 0, Count::Players, 4, false},     // player word 1: form, technique, condition
    {::bmh::layout::PlayerCards, true, 0, Count::Players, 2, false},
    {::bmh::layout::PlayerSkillWords, true, 0, Count::Players, 4, false},     // player word 2: skills, nationality
    {::bmh::layout::PlayerIdentityWords, true, 0, Count::Players, 4, false},     // player word 3: position, age
    {::bmh::layout::TacticsBoards, false, 0x2940, Count::None, 0, false},  {::bmh::layout::TournamentName, true, 0x24, Count::None, 0, false},
    {::bmh::layout::Career_EuropeanClub_ExecutableField, false, 1, Count::None, 0, false},       {::bmh::layout::Career_EuropeanClub_ExecutableField2, false, 1, Count::None, 0, false},
    {::bmh::layout::Career_EuropeanClub_ExecutableField3, false, 1, Count::None, 0, false},       {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField24, false, 0x28, Count::None, 0, false},
    {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField29, false, 0x14, Count::None, 0, false},    {::bmh::layout::ClubSeasonRecords, false, 4, Count::None, 0, false},
    {::bmh::layout::ClubSeasonRecords, true, 0, Count::N42, 74, true},    // club records (168 x 74), only if pointer != 0
};

}  // namespace

SaveState::SaveState(const Bytes& stream, const DbCounts& c) : stream_(stream) {
    // Teams in league play and cup-only teams are not part of DbCounts yet;
    // these are the V2.24 values (CareerSession_DispatchLineup_RuntimeField, CareerSession_InstallLeagueSizes_RuntimeField6).
    const uint32_t kTeams = c.league_teams, kCupTeams = c.cup_teams;
    size_t pos = 0;
    uint32_t last_pointer = 0;
    for (const BlockDef& d : kBlocks) {
        if (d.only_if_pointer && last_pointer == 0) continue;
        uint32_t n = 0;
        switch (d.count) {
            case Count::None: n = 0; break;
            case Count::Clubs: n = c.clubs; break;
            case Count::Players: n = c.players; break;
            case Count::N42: n = c.n42; break;
            case Count::N66: n = c.n66; break;
            case Count::Teams: n = kTeams; break;
            case Count::TeamsPlusCups: n = kTeams + kCupTeams; break;
        }
        size_t size = d.fixed + size_t(n) * d.factor;
        if (pos + size > stream.size()) throw FormatError("savegame: state blocks exceed stream");
        if (d.address == ::bmh::layout::ClubSeasonRecords && !d.heap) last_pointer = le32(stream, pos);
        index_.try_emplace(d.address, blocks_.size());
        blocks_.push_back({d.address, d.heap, pos, size});
        pos += size;
    }
    // trailer: 4 string pools, each u16 length + data
    for (int i = 0; i < 4; i++) {
        if (pos + 2 > stream.size()) throw FormatError("savegame: trailer missing");
        size_t len = le16(stream, pos);
        if (pos + 2 + len > stream.size()) throw FormatError("savegame: trailer exceeds stream");
        pools_.emplace_back(stream.begin() + pos + 2, stream.begin() + pos + 2 + len);
        pos += 2 + len;
    }
    if (pos < stream.size()) {
        // the OpenBMH extension (savestate.hpp)
        static const char kMagic[8] = {0x4f, 0x42, 0x4d, 0x48, 0x45, 0x58, 0x54, 0x31};  // "OBMHEXT1"
        if (stream.size() - pos < 10 || !std::equal(kMagic, kMagic + 8, stream.begin() + long(pos)))
            throw FormatError("savegame: block sizes do not match stream length");
        size_t count = le16(stream, pos + 8);
        pos += 10;
        for (size_t i = 0; i < count; ++i) {
            if (pos + 8 > stream.size()) throw FormatError("savegame: extension cut off");
            const uint32_t address = le32(stream, pos), size = le32(stream, pos + 4);
            pos += 8;
            if (pos + size > stream.size()) throw FormatError("savegame: extension block exceeds stream");
            extension_.push_back({address, Bytes(stream.begin() + long(pos), stream.begin() + long(pos + size))});
            pos += size;
        }
    }
    if (pos != stream.size()) throw FormatError("savegame: block sizes do not match stream length");
}

void append_save_extension(Bytes& stream, const std::vector<SaveState::ExtensionBlock>& blocks) {
    if (blocks.empty()) return;
    for (char ch : std::string("OBMHEXT1")) stream.push_back(uint8_t(ch));
    stream.push_back(uint8_t(blocks.size())), stream.push_back(uint8_t(blocks.size() >> 8));
    for (const auto& b : blocks) {
        for (uint32_t v : {b.address, uint32_t(b.bytes.size())})
            for (int k = 0; k < 4; ++k) stream.push_back(uint8_t(v >> (8 * k)));
        stream.insert(stream.end(), b.bytes.begin(), b.bytes.end());
    }
}

const SaveBlock& SaveState::block(uint32_t address) const {
    auto it = index_.find(address);
    if (it == index_.end()) throw FormatError("savegame: unknown block");
    return blocks_[it->second];
}

}  // namespace bmh

namespace bmh {

std::vector<SaveBlock> save_layout(const DbCounts& c, bool club_records) {
    const uint32_t kTeams = c.league_teams, kCupTeams = c.cup_teams;
    std::vector<SaveBlock> out;
    size_t pos = 0;
    for (const BlockDef& d : kBlocks) {
        if (d.only_if_pointer && !club_records) continue;
        uint32_t n = 0;
        switch (d.count) {
            case Count::None: n = 0; break;
            case Count::Clubs: n = c.clubs; break;
            case Count::Players: n = c.players; break;
            case Count::N42: n = c.n42; break;
            case Count::N66: n = c.n66; break;
            case Count::Teams: n = kTeams; break;
            case Count::TeamsPlusCups: n = kTeams + kCupTeams; break;
        }
        const size_t size = d.fixed + size_t(n) * d.factor;
        out.push_back({d.address, d.heap, pos, size});
        pos += size;
    }
    return out;
}

}  // namespace bmh
