// SPDX-License-Identifier: GPL-3.0-only
#include "import/save.hpp"
#include "formats/executable_layout.hpp"
#include "game/career.hpp"
#include <algorithm>
#include <map>
namespace bmh::obw {
namespace {
struct Record {
    int target;
    uint32_t native_address;
    size_t size;
};
} // namespace
Bytes normalize_english_save(const Savegame &source, const DbCounts &counts) {
    if (source.version != "V2.20" || !source.checksum_ok)
        throw FormatError("Invalid Football Limited V2.20 save");
    const auto layout = save_layout(counts, true);
    const Record records[] = {
        {0, 0, 0},                                      // Cup_AdvanceCup_RuntimeField3
        {1, 0, 0},                                      // LiveMatchday_FormatError_RuntimeField14
        {2, 0, 0},                                      // ManagerRecords
        {3, 0, 0},                                      // Loans
        {4, 0, 0},                                      // ManagerMatchHistory
        {6, 0, 0},                                      // ManagerAutomation
        {7, 0, 0},                                      // Desktops
        {8, 0, 0},                                      // CareerSession_DispatchToto_RuntimeField
        {10, 0, 0},                                     // CalendarState_FormatError_RuntimeField3
        {11, 0, 0},                                     // ActiveManager
        {12, 0, 0},                                     // CareerSession_RuntimeError_RuntimeField4
        {13, 0, 0},                                     // LeaguePresentationOptions
        {14, 0, 0},                                     // FriendlyTeams
        {15, 0, 0},                                     // FriendlyDays
        {16, 0, 0},                                     // Cup_AdvanceCup_ExecutableField
        {17, 0, 0},                                     // MatchdaySetup_IndoorTournamentDraw_RuntimeField
        {18, 0, 0},                                     // Placements
        {19, 0, 0},                                     // ActiveSelectors
        {20, 0, 0},                                     // Career_CareerAddManager_RuntimeField2
        {21, 0, 0},                                     // DeskActions_SubmitPoolsCoupon_RuntimeField
        {22, 0, 0},                                     // NextDeskManager
        {23, 0, 0},                                     // Savestate_Layout_ExecutableField8
        {24, 0, 0},                                     // DeskPosition
        {25, 0, 0},                                     // DeskRecords_Teams_RuntimeField
        {26, 0, 0},                                     // CareerSession_FormatError_RuntimeField7
        {27, 0, 0},                                     // Cup_AdvanceCup_RuntimeField6
        {28, 0, 0},                                     // CurrentMatchday
        {29, 0, 0},                                     // ClubPlayerDetail_InvalidArgument_RuntimeField3
        {30, 0, 0},                                     // CoachActive
        {31, 0, 0},                                     // OriginalManagerCount
        {32, 0, 0},                                     // CareerSession_Invalid_RuntimeField2
        {33, 0, 0},                                     // CareerDuration
        {34, 0, 0},                                     // GameMode
        {35, 0, 0},                                     // CompetitionSelection
        {36, 0, 0},                                     // ManagerCount
        {37, 0, 0},                                     // SavedCompetition
        {38, 0, 0},                                     // Difficulty
        {39, 0, 0},                                     // StockExchange
        {40, 0, 0},                                     // FundBlock
        {41, 0, 0},                                     // Coupon
        {42, 0, 0},                                     // Savestate_Layout_ExecutableField2
        {43, 0, 0},                                     // Savestate_Layout_ExecutableField
        {44, 0, 0},                                     // AdGroupId
        {45, 0, 0},                                     // OfferSlots
        {46, 0, 0},                                     // ForeignClubStrength
        {47, 0, 0},                                     // Career_Put_ExecutableField3
        {48, 0, 0},                                     // ClubShortNameOffsets
        {49, 0, 0},                                     // ClubKitSelectors
        {50, 0, 0},                                     // Savestate_Layout_ExecutableField7
        {51, 0, 0},                                     // ClubInfo_FormatError_RuntimeField
        {52, 0, 0},                                     // Career_CareerManagersEnd_RuntimeField
        {53, 0, 0},                                     // ClubInfo_FormatError_RuntimeField2
        {54, 0, 0},                                     // DeskActions_Unsupported_RuntimeField
        {55, 0, 0},                                     // Career_RemoveJoinLoans_RuntimeField
        {56, 0, 0},                                     // ClubPlayerDetail_InvalidArgument_RuntimeField
        {57, 0, 0},                                     // ManagerRatings
        {58, 0, 0},                                     // DaysIntoSeason
        {-1, ::bmh::layout::EnglishFaCupResults, 0x86}, // fa_results
        {-1, ::bmh::layout::EnglishLeagueCupFirstRoundResults, 0x56},           // league_results
        {-1, ::bmh::layout::EnglishLeagueCupQuarterFinalOtherLegResults, 0x3c}, // league_return
        {-1, ::bmh::layout::EnglishLeagueCupField, 0xa4},                       // league_field
        {-1, ::bmh::layout::EnglishFaCupField, 0x134},                          // fa_field
        {61, 0, 0},                                                             // CalendarDay
        {62, 0, 0},                                                             // SeasonIndex
        {63, 0, 0},  // Savestate_Layout_ExecutableField5
        {64, 0, 0},  // CompetitionCalendar
        {65, 0, 0},  // Postponed
        {66, 0, 0},  // LeagueTableDisplayModes
        {67, 0, 0},  // CareerSession_FormatError_ExecutableField
        {68, 0, 0},  // ManagerSettings
        {69, 0, 0},  // CareerSession_DispatchOtherGames_RuntimeField
        {70, 0, 0},  // CareerSession_AnyOf_ExecutableField
        {71, 0, 0},  // Career_NewCareerMarketsAndMoods_RuntimeField2
        {72, 0, 0},  // ManagerMailbox
        {73, 0, 0},  // CareerSession_CloseLeagueFamily_ExecutableField
        {74, 0, 0},  // Savestate_Layout_ExecutableField3
        {75, 0, 0},  // Flags
        {76, 0, 0},  // Formations
        {77, 0, 0},  // PlayerDevelopmentBalance
        {78, 0, 0},  // PlayerBonuses
        {79, 0, 0},  // EditorList
        {80, 0, 0},  // Field0
        {81, 0, 0},  // Career_CompetitionPointersEnglish_RuntimeField13
        {82, 0, 0},  // Career_CompetitionPointersEnglish_ExecutableField25
        {83, 0, 0},  // Career_CompetitionPointersEnglish_ExecutableField22
        {84, 0, 0},  // Career_CompetitionPointersEnglish_ExecutableField23
        {85, 0, 0},  // Career_CompetitionPointersEnglish_ExecutableField19
        {86, 0, 0},  // Career_CompetitionPointersEnglish_ExecutableField26
        {87, 0, 0},  // Career_CompetitionPointersEnglish_ExecutableField21
        {88, 0, 0},  // Career_CompetitionPointersEnglish_RuntimeField14
        {89, 0, 0},  // Career_CompetitionPointersEnglish_RuntimeField17
        {90, 0, 0},  // Savestate_Layout_ExecutableField4
        {91, 0, 0},  // Career_CompetitionPointersEnglish_ExecutableField28
        {92, 0, 0},  // Career_CompetitionPointersEnglish_ExecutableField31
        {93, 0, 0},  // Teams
        {94, 0, 0},  // MatchdayGroupWorkspace
        {95, 0, 0},  // Savestate_Layout_ExecutableField6
        {96, 0, 0},  // EventPasses
        {97, 0, 0},  // LeagueResults
        {98, 0, 0},  // LeagueRoundCounters
        {99, 0, 0},  // TeamMorale
        {100, 0, 0}, // ClubBadgeSelectors
        {101, 0, 0}, // TeamFlags
        {102, 0, 0}, // ClubIdentities
        {103, 0, 0}, // PlayerNameOffsets
        {104, 0, 0}, // MasterDatabaseMetadata
        {105, 0, 0}, // ClubAuxiliaryWords
        {106, 0, 0}, // LeagueRankHistory
        {107, 0, 0}, // Titles
        {108, 0, 0}, // Cup_AdvanceCup_RuntimeField6
        {109, 0, 0}, // ClubAuxiliaryBytes
        {110, 0, 0}, // CupTable
        {111, 0, 0}, // SquadSlots
        {112, 0, 0}, // PlayerConditionTechniqueForm
        {113, 0, 0}, // PlayerCards
        {114, 0, 0}, // PlayerSkillWords
        {115, 0, 0}, // PlayerIdentityWords
        {116, 0, 0}, // TacticsBoards
        {117, 0, 0}, // TournamentName
        {118, 0, 0}, // Career_EuropeanClub_ExecutableField
        {119, 0, 0}, // Career_EuropeanClub_ExecutableField2
        {120, 0, 0}, // Career_EuropeanClub_ExecutableField3
        {121, 0, 0}, // Career_CompetitionPointersEnglish_ExecutableField24
        {122, 0, 0}, // Career_CompetitionPointersEnglish_ExecutableField29
        {123, 0, 0}, // ClubSeasonRecords
        {124, 0, 0}, // ClubSeasonRecords
    };
    std::map<int, Bytes> decoded;
    std::map<uint32_t, Bytes> native;
    for (const auto &[address, size] : kEnglishCompetitionBlocks)
        native[address] = Bytes(size, 0);
    size_t offset = 0;
    bool club_records = false;
    for (const auto &record : records) {
        if (record.target == 124 && !club_records)
            continue;
        const size_t size = record.target < 0 ? record.size : layout.at(size_t(record.target)).size;
        if (offset + size > source.stream.size())
            throw FormatError("V2.20 state is truncated");
        Bytes bytes(source.stream.begin() + offset, source.stream.begin() + offset + size);
        offset += size;
        if (record.target == 123) {
            if (size != 4)
                throw FormatError("Invalid club record pointer");
            club_records = le32(bytes, 0) != 0;
        }
        if (record.target >= 0)
            decoded[record.target] = bytes;
        uint32_t target = record.native_address;
        if (record.target == 93)
            target = ::bmh::layout::EnglishAdditionalCupField;
        if (record.target == 94)
            target = ::bmh::layout::EnglishAdditionalCupResults;
        if (target)
            for (auto &[at, block] : native)
                if (target >= at && uint64_t(target) + bytes.size() <= uint64_t(at) + block.size())
                    std::copy(bytes.begin(), bytes.end(), block.begin() + (target - at));
    }
    Bytes normalized;
    for (size_t i = 0; i < layout.size(); ++i) {
        if (i == 124 && !club_records)
            continue;
        if (const auto it = decoded.find(int(i)); it != decoded.end())
            normalized.insert(normalized.end(), it->second.begin(), it->second.end());
        else
            normalized.insert(normalized.end(), layout[i].size, 0);
    }
    // Four length-prefixed name pools, shared by both original editions.
    for (int i = 0; i < 4; ++i) {
        if (offset + 2 > source.stream.size())
            throw FormatError("V2.20 name pool missing");
        const size_t size = le16(source.stream, offset) + 2;
        if (offset + size > source.stream.size())
            throw FormatError("V2.20 name pool is truncated");
        normalized.insert(normalized.end(), source.stream.begin() + offset,
                          source.stream.begin() + offset + size);
        offset += size;
    }
    if (offset != source.stream.size())
        throw FormatError("V2.20 state has unexpected trailing bytes");
    std::vector<SaveState::ExtensionBlock> extension;
    for (auto &[at, bytes] : native)
        extension.push_back({at, std::move(bytes)});
    append_save_extension(normalized, extension);
    (void)SaveState(normalized, counts);
    return normalized;
}
} // namespace bmh::obw
