# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.24)
project(OpenBMH VERSION 1.1.0 LANGUAGES CXX C)
if(APPLE)
    # Enable native languages before dependencies/subdirectories inherit rules.
    enable_language(OBJC OBJCXX)
endif()
set(OPENBMH_PRERELEASE "poc.2" CACHE STRING "Prerelease identifier; empty for final release")
set(OPENBMH_PRODUCT_VERSION "${PROJECT_VERSION}")
if(OPENBMH_PRERELEASE)
    string(APPEND OPENBMH_PRODUCT_VERSION "-${OPENBMH_PRERELEASE}")
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/product-version.txt" CONTENT "${OPENBMH_PRODUCT_VERSION}\n")
# the one place of the version: --version of both programs and the window title (src/version.hpp)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/generated/openbmh_build_version.hpp"
    CONTENT "// SPDX-License-Identifier: GPL-3.0-only\n#pragma once\n#define OPENBMH_VERSION \"${OPENBMH_PRODUCT_VERSION}\"\n")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BMH_DATA "" CACHE PATH "Optional directory containing externally supplied game data")
set(BMH_REFERENCE "" CACHE PATH "Optional directory containing externally supplied reference inputs")
set(BMH_TEST_INPUTS "" CACHE PATH "Optional directory containing external integration-test inputs")
if(BMH_DATA AND NOT IS_DIRECTORY "${BMH_DATA}")
    message(FATAL_ERROR "BMH_DATA was set but is not an existing directory: ${BMH_DATA}")
endif()
if(BMH_DATA)
    file(GLOB data_entries "${BMH_DATA}/*")
    set(has_game_executable FALSE)
    foreach(entry IN LISTS data_entries)
        get_filename_component(filename "${entry}" NAME)
        string(TOLOWER "${filename}" filename)
        if(filename STREQUAL "bmh.exe" OR filename STREQUAL "fltd.exe")
            set(has_game_executable TRUE)
        endif()
    endforeach()
    if(NOT has_game_executable)
        message(FATAL_ERROR "BMH_DATA does not contain a game executable: ${BMH_DATA}")
    endif()
endif()
add_executable(openbmh_test_skip tests/skip_test.cpp)

# Keep data-dependent tests visible without distributing their inputs. Missing
# optional inputs are reported as skipped, never as successful assertions.
function(bmh_add_data_test)
    cmake_parse_arguments(T "NO_GAME_DATA" "NAME;DATA_DIR" "COMMAND;REQUIRED_OPTIONS;REQUIRED_FILES" ${ARGN})
    if("DATA_DIR" IN_LIST T_KEYWORDS_MISSING_VALUES)
        set(T_DATA_DIR "")
    elseif(NOT DEFINED T_DATA_DIR)
        set(T_DATA_DIR "${BMH_DATA}")
    endif()
    set(reason "")
    set(external_text_tests season_transition_tests cli_console_tests
        ui_desktop_tests ui_lineup_tests format_tests app_controller_tests bank_app_tests
        cli_soak ui_cup_draw_tests league_pairings_tests ui_pending_offer_tests
        reality_reporter_layout_tests season_cup_draw_app_tests
        ui_team_statistics_source_oracle_tests transfer_bid_qualification_tests
        ui_achievements_tests ui_analytics_tests ui_opportunities_tests
        ui_outgoing_transfer_tests ui_season_entrants_tests ui_statistics_rankings_tests)
    if(T_NAME IN_LIST external_text_tests)
        list(APPEND T_REQUIRED_OPTIONS BMH_TEST_INPUTS)
        list(APPEND T_REQUIRED_FILES expected-text.tsv)
    endif()
    if(T_NAME STREQUAL "format_tests" AND BMH_REFERENCE)
        list(APPEND T_REQUIRED_OPTIONS BMH_REFERENCE)
        list(APPEND T_REQUIRED_FILES format-matches.tsv format-team-values.tsv)
    endif()
    if(T_NO_GAME_DATA)
        # This test needs only its explicit reference file, not an installation.
    elseif(NOT T_DATA_DIR)
        set(reason "External game data not configured")
    elseif(NOT IS_DIRECTORY "${T_DATA_DIR}")
        message(FATAL_ERROR "Invalid game data directory for ${T_NAME}: ${T_DATA_DIR}")
    endif()
    foreach(option IN LISTS T_REQUIRED_OPTIONS)
        if("${${option}}" STREQUAL "" OR NOT EXISTS "${${option}}")
            set(reason "External test input not supplied: ${option}")
        endif()
    endforeach()
    foreach(file IN LISTS T_REQUIRED_FILES)
        if(NOT BMH_TEST_INPUTS OR NOT EXISTS "${BMH_TEST_INPUTS}/${file}")
            set(reason "External integration input missing: ${file}")
        endif()
    endforeach()
    if(reason)
        add_test(NAME ${T_NAME} COMMAND openbmh_test_skip "${reason}")
    else()
        add_test(NAME ${T_NAME} COMMAND ${T_COMMAND})
        if(BMH_TEST_INPUTS)
            set_property(TEST ${T_NAME} APPEND PROPERTY ENVIRONMENT "OPENBMH_TEST_INPUTS=${BMH_TEST_INPUTS}")
        endif()
    endif()
    set_tests_properties(${T_NAME} PROPERTIES SKIP_RETURN_CODE 77 LABELS "external-data")
endfunction()

find_package(Python3 COMPONENTS Interpreter QUIET)

# Windows gives a thread 1 MB of stack (Linux 8 MB). Release builds inline the many test
# functions of an app test into main(), whose frame then holds several CareerSession
# objects and GameState checkpoints at once - over 1 MB, a stack overflow (0xC00000FD) at
# entry. 16 MB for every program built here, as on the other platforms.
if(MSVC)
    add_link_options(/STACK:16777216)
endif()

option(BMH_USE_SYSTEM_SDL2 "Use an installed SDL2 instead of fetching the pinned release" OFF)
if(BMH_USE_SYSTEM_SDL2)
    find_package(SDL2 REQUIRED CONFIG)
    set(BMH_SDL_TARGET SDL2::SDL2)
else()
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL2)
set(BMH_SDL_TARGET SDL2::SDL2-static)
endif()

# SDL2 renames main when SDL.h is included. Link SDL2main before SDL2 for
# every executable that consumes SDL, when the selected SDL package provides it.
if(TARGET SDL2::SDL2main)
    list(PREPEND BMH_SDL_TARGET SDL2::SDL2main)
endif()

# App sources share objects only within identical compile environments.
# MSVC consumers currently differ in target/source /utf-8 flags; keep that path.
set(BMH_APP_SESSION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/career_session.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/window_actions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/desktop_actions.cpp")
set(BMH_APP_SESSION_SDL_SOURCE "${BMH_APP_SESSION_SOURCE}")
set(BMH_APP_CONTROLLER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/controller.cpp")
if(NOT MSVC)
    add_library(bmh_app_session OBJECT src/app/career_session.cpp src/app/window_actions.cpp src/app/desktop_actions.cpp)
    target_link_libraries(bmh_app_session PRIVATE bmhui)
    add_library(bmh_app_session_sdl OBJECT src/app/career_session.cpp src/app/window_actions.cpp src/app/desktop_actions.cpp)
    target_link_libraries(bmh_app_session_sdl PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
    add_library(bmh_app_controller_sdl OBJECT src/app/controller.cpp)
    target_link_libraries(bmh_app_controller_sdl PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
    set(BMH_APP_SESSION_SOURCE "$<TARGET_OBJECTS:bmh_app_session>")
    set(BMH_APP_SESSION_SDL_SOURCE "$<TARGET_OBJECTS:bmh_app_session_sdl>")
    set(BMH_APP_CONTROLLER_SOURCE "$<TARGET_OBJECTS:bmh_app_controller_sdl>")
endif()

# (before the first subdirectory: tests added there are only registered once testing is on)
enable_testing()
if(Python3_Interpreter_FOUND)
    add_test(NAME public_source_guard COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/public_release/check_public_tree.py" --root "${CMAKE_SOURCE_DIR}")
    add_test(NAME public_source_guard_tests COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/public_release/test_public_tree.py")
    add_test(NAME package_allowlist_tests COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/package_tests.py")
    add_test(NAME cli_concept_tests COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/cli_concept/check_concept.py" --root "${CMAKE_SOURCE_DIR}")
    add_test(NAME documented_differences_tests COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/public_release/collect_differences.py" --root "${CMAKE_SOURCE_DIR}" --check)
endif()
add_subdirectory(src/audio)

add_executable(season_end_desk_tests tests/season_end_desk_tests.cpp ${BMH_APP_SESSION_SOURCE})
add_executable(national_pending_match_tests tests/national_pending_match_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(national_pending_match_tests PRIVATE bmhui)
set(BMH_TEST_NATIONAL_PENDING_MATCH_TESTS_INPUT_1 "" CACHE PATH "External input 1 for national_pending_match_tests; not distributed")
set(BMH_TEST_NATIONAL_PENDING_MATCH_TESTS_INPUT_2 "" CACHE PATH "External input 2 for national_pending_match_tests; not distributed")
bmh_add_data_test(NAME national_pending_match_tests REQUIRED_OPTIONS BMH_TEST_NATIONAL_PENDING_MATCH_TESTS_INPUT_1 BMH_TEST_NATIONAL_PENDING_MATCH_TESTS_INPUT_2 COMMAND national_pending_match_tests "${BMH_DATA}"
    "${BMH_TEST_NATIONAL_PENDING_MATCH_TESTS_INPUT_1}"
    "${BMH_TEST_NATIONAL_PENDING_MATCH_TESTS_INPUT_2}")
target_link_libraries(season_end_desk_tests PRIVATE bmhui)
set(BMH_TEST_SEASON_END_DESK_TESTS_INPUT_1 "" CACHE PATH "External input 1 for season_end_desk_tests; not distributed")
bmh_add_data_test(NAME season_end_desk_tests REQUIRED_OPTIONS BMH_TEST_SEASON_END_DESK_TESTS_INPUT_1 COMMAND season_end_desk_tests "${BMH_DATA}"
    "${BMH_TEST_SEASON_END_DESK_TESTS_INPUT_1}")

add_executable(season_transition_tests tests/season_transition_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(season_transition_tests PRIVATE bmhui)
set(BMH_TEST_SEASON_TRANSITION_TESTS_INPUT_1 "" CACHE PATH "External input 1 for season_transition_tests; not distributed")
bmh_add_data_test(NAME season_transition_tests REQUIRED_OPTIONS BMH_TEST_SEASON_TRANSITION_TESTS_INPUT_1 COMMAND season_transition_tests "${BMH_DATA}"
    "${BMH_TEST_SEASON_TRANSITION_TESTS_INPUT_1}")

add_executable(indoor_masters_transition_tests tests/indoor_masters_transition_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(indoor_masters_transition_tests PRIVATE bmhui)
set(BMH_TEST_INDOOR_MASTERS_TRANSITION_TESTS_INPUT_1 "" CACHE PATH "External input 1 for indoor_masters_transition_tests; not distributed")
bmh_add_data_test(NAME indoor_masters_transition_tests REQUIRED_OPTIONS BMH_TEST_INDOOR_MASTERS_TRANSITION_TESTS_INPUT_1 COMMAND indoor_masters_transition_tests "${BMH_DATA}"
    "${BMH_TEST_INDOOR_MASTERS_TRANSITION_TESTS_INPUT_1}")

add_executable(national_cycle_transition_tests tests/national_cycle_transition_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(national_cycle_transition_tests PRIVATE bmhui)
set(BMH_TEST_NATIONAL_CYCLE_TRANSITION_TESTS_INPUT_1 "" CACHE PATH "External input 1 for national_cycle_transition_tests; not distributed")
bmh_add_data_test(NAME national_cycle_transition_tests REQUIRED_OPTIONS BMH_TEST_NATIONAL_CYCLE_TRANSITION_TESTS_INPUT_1 COMMAND national_cycle_transition_tests "${BMH_DATA}"
    "${BMH_TEST_NATIONAL_CYCLE_TRANSITION_TESTS_INPUT_1}")

add_executable(season_tax_tests tests/season_tax_tests.cpp)
target_link_libraries(season_tax_tests PRIVATE bmhcore)
set(BMH_TEST_SEASON_TAX_TESTS_INPUT_1 "" CACHE PATH "External input 1 for season_tax_tests; not distributed")
bmh_add_data_test(NAME season_tax_tests REQUIRED_OPTIONS BMH_TEST_SEASON_TAX_TESTS_INPUT_1 COMMAND season_tax_tests "${BMH_DATA}"
    "${BMH_TEST_SEASON_TAX_TESTS_INPUT_1}")

add_executable(highscore_tests tests/highscore_tests.cpp)
target_link_libraries(highscore_tests PRIVATE bmhcore)
set(BMH_TEST_HIGHSCORE_TESTS_INPUT_1 "" CACHE PATH "External input 1 for highscore_tests; not distributed")
bmh_add_data_test(NAME highscore_tests REQUIRED_OPTIONS BMH_TEST_HIGHSCORE_TESTS_INPUT_1 COMMAND highscore_tests "${BMH_DATA}"
    "${BMH_TEST_HIGHSCORE_TESTS_INPUT_1}")

# Engine core: file formats and game logic, no platform dependencies
add_library(bmhcore STATIC
    src/formats/binary.cpp
    src/formats/resource_bundle.cpp
    src/formats/palette.cpp
    src/formats/vga.cpp
    src/formats/font.cpp
    src/formats/textd.cpp
    src/formats/fbm.cpp
    src/formats/le.cpp
    src/formats/data_profile.cpp
    src/formats/m3d.cpp
    src/formats/savestate.cpp
    src/game/schedule.cpp
    src/game/league_pairings.cpp
    src/game/source_tip.cpp
    src/game/table_forecast.cpp
    src/game/profile_runtime.cpp
    src/game/state.cpp
    src/game/rating.cpp
    src/game/match.cpp
    src/game/match_engine.cpp
    src/game/reality_motion.cpp
    src/game/reality_setup.cpp
    src/game/reality_space.cpp
    src/game/reality_movement.cpp
    src/game/reality_match.cpp
    src/game/reality_scene.cpp
    src/game/reality_ball.cpp
    src/game/reality_duel.cpp
    src/game/reality_reporter.cpp
    src/game/matchday.cpp
    src/game/sportschau_replay.cpp
    src/game/sportschau_rating.cpp
    src/game/sportschau_awards.cpp
    src/game/matchday_setup.cpp
    src/game/prediction.cpp
    src/game/month.cpp
    src/game/desk.cpp
    src/game/cup.cpp
    src/game/club_info.cpp
    src/game/club_player_detail.cpp
    src/game/driver.cpp
    src/game/season.cpp
    src/game/highscore.cpp
    src/game/weekly.cpp
    src/game/messages.cpp
    src/game/table.cpp
    src/game/calendar.cpp
    src/game/week.cpp
    src/game/lineup.cpp
    src/game/national.cpp
    src/game/desk_actions.cpp
    src/game/reschedule.cpp
    src/game/balance_plan.cpp
    src/game/top_scorers.cpp
    src/game/other_games.cpp
    src/game/stadium_blocks.cpp
    src/game/poach.cpp
    src/game/environment.cpp
    src/game/bank_credit.cpp
    src/game/career.cpp
    src/game/save.cpp
    src/game/turn.cpp
    src/game/turn_driver.cpp
)
target_include_directories(bmhcore PUBLIC src)

if(MSVC)
    target_compile_options(bmhcore PRIVATE /W4 /utf-8)
else()
    target_compile_options(bmhcore PRIVATE -Wall -Wextra)
endif()

add_executable(openbmh src/main.cpp)
target_link_libraries(openbmh PRIVATE bmhcore ${BMH_SDL_TARGET})
if(MSVC)
    target_compile_options(openbmh PRIVATE /utf-8)
endif()

# --- Game data launcher (src/launch): find, unpack and check the original game data ---------
# ZIP: miniz (MIT), 7z: the C decoder of the LZMA SDK (public domain), file dialog:
# nativefiledialog-extended (zlib). All pinned; fetched like SDL2.
include(FetchContent)
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5)
FetchContent_Declare(lzma_sdk
    GIT_REPOSITORY https://github.com/ip7z/7zip.git
    GIT_TAG 24.09
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake)  # (sources only: the repository has no CMake build)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.2.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(miniz lzma_sdk nfd)

add_library(bmh_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(bmh_miniz PUBLIC ${miniz_SOURCE_DIR})
target_link_libraries(bmhcore PRIVATE bmh_miniz)
set(BMH_LZMA_C ${lzma_sdk_SOURCE_DIR}/C)
add_library(bmh_7z STATIC
    ${BMH_LZMA_C}/7zAlloc.c ${BMH_LZMA_C}/7zArcIn.c ${BMH_LZMA_C}/7zBuf.c ${BMH_LZMA_C}/7zCrc.c
    ${BMH_LZMA_C}/7zCrcOpt.c ${BMH_LZMA_C}/7zDec.c ${BMH_LZMA_C}/7zStream.c ${BMH_LZMA_C}/Bcj2.c
    ${BMH_LZMA_C}/Bra.c ${BMH_LZMA_C}/Bra86.c ${BMH_LZMA_C}/BraIA64.c ${BMH_LZMA_C}/CpuArch.c
    ${BMH_LZMA_C}/Delta.c ${BMH_LZMA_C}/Lzma2Dec.c ${BMH_LZMA_C}/LzmaDec.c ${BMH_LZMA_C}/Ppmd7.c
    ${BMH_LZMA_C}/Ppmd7Dec.c)
target_include_directories(bmh_7z PUBLIC ${BMH_LZMA_C})
target_compile_definitions(bmh_7z PUBLIC Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)

add_library(bmhlaunch STATIC src/launch/archive.cpp src/launch/game_data.cpp src/launch/fltd.cpp)
target_include_directories(bmhlaunch PUBLIC src)
target_link_libraries(bmhlaunch PUBLIC bmhcore PRIVATE bmh_miniz bmh_7z)
if(WIN32)
    target_link_libraries(bmhlaunch PRIVATE shell32)  # CommandLineToArgvW
endif()
add_library(bmhlaunch_dialog STATIC src/launch/file_dialog.cpp)
target_link_libraries(bmhlaunch_dialog PUBLIC bmhlaunch PRIVATE nfd::nfd)
if(MSVC)
    target_compile_options(bmhlaunch PRIVATE /W4 /utf-8)
    target_compile_options(bmhlaunch_dialog PRIVATE /utf-8)
endif()

add_subdirectory(src/ui)
add_executable(driver_barrier_tests tests/driver_barrier_tests.cpp)
target_link_libraries(driver_barrier_tests PRIVATE bmhcore)
add_test(NAME driver_barrier_tests COMMAND driver_barrier_tests)
add_executable(format_tests tests/format_tests.cpp)
target_link_libraries(format_tests PRIVATE bmhcore)
if(MSVC)
    target_compile_options(format_tests PRIVATE /utf-8)
endif()
add_test(NAME format_unit_tests COMMAND format_tests)
bmh_add_data_test(NAME format_tests REQUIRED_OPTIONS BMH_TEST_INPUTS
    REQUIRED_FILES expected-text.tsv format-players.tsv format-start.fbm
    COMMAND format_tests "${BMH_DATA}" "${BMH_REFERENCE}")

add_executable(stand_block_actions_tests tests/stand_block_actions_tests.cpp)
target_link_libraries(stand_block_actions_tests PRIVATE bmhcore)
add_test(NAME stand_block_actions_tests COMMAND stand_block_actions_tests)

add_executable(toto_actions_tests tests/toto_actions_tests.cpp)
target_link_libraries(toto_actions_tests PRIVATE bmhcore)
add_test(NAME toto_actions_tests COMMAND toto_actions_tests)

add_executable(e13_turn_driver_tests tests/e13_turn_driver_tests.cpp)
target_link_libraries(e13_turn_driver_tests PRIVATE bmhcore)
set(BMH_TEST_E13_TURN_DRIVER_TESTS_INPUT_1 "" CACHE PATH "External input 1 for e13_turn_driver_tests; not distributed")
bmh_add_data_test(NAME e13_turn_driver_tests REQUIRED_OPTIONS BMH_TEST_E13_TURN_DRIVER_TESTS_INPUT_1 COMMAND e13_turn_driver_tests "${BMH_DATA}"
    "${BMH_TEST_E13_TURN_DRIVER_TESTS_INPUT_1}")

add_executable(balance_plan_tests tests/balance_plan_tests.cpp)
target_link_libraries(balance_plan_tests PRIVATE bmhcore)
set(BMH_TEST_BALANCE_PLAN_TESTS_INPUT_1 "" CACHE PATH "External input 1 for balance_plan_tests; not distributed")
bmh_add_data_test(NAME balance_plan_tests REQUIRED_OPTIONS BMH_TEST_BALANCE_PLAN_TESTS_INPUT_1 COMMAND balance_plan_tests "${BMH_DATA}"
    "${BMH_TEST_BALANCE_PLAN_TESTS_INPUT_1}")

add_executable(training_camp_actions_tests tests/training_camp_actions_tests.cpp)
target_link_libraries(training_camp_actions_tests PRIVATE bmhcore)
add_test(NAME training_camp_actions_tests COMMAND training_camp_actions_tests)

add_executable(lineup_assistant_detail_tests tests/lineup_assistant_detail_tests.cpp)
target_link_libraries(lineup_assistant_detail_tests PRIVATE bmhcore)
add_test(NAME lineup_assistant_detail_tests COMMAND lineup_assistant_detail_tests)

add_executable(reschedule_tests tests/reschedule_tests.cpp)
target_link_libraries(reschedule_tests PRIVATE bmhcore)
set(BMH_TEST_RESCHEDULE_TESTS_INPUT_1 "" CACHE PATH "External input 1 for reschedule_tests; not distributed")
bmh_add_data_test(NAME reschedule_tests REQUIRED_OPTIONS BMH_TEST_RESCHEDULE_TESTS_INPUT_1 COMMAND reschedule_tests "${BMH_DATA}"
    "${BMH_TEST_RESCHEDULE_TESTS_INPUT_1}")

add_executable(environment_tests tests/environment_tests.cpp)
target_link_libraries(environment_tests PRIVATE bmhcore)
add_test(NAME environment_tests COMMAND environment_tests)

add_executable(youth_academy_tests tests/youth_academy_tests.cpp)
target_link_libraries(youth_academy_tests PRIVATE bmhcore)
set(BMH_TEST_YOUTH_ACADEMY_TESTS_INPUT_1 "" CACHE PATH "External input 1 for youth_academy_tests; not distributed")
bmh_add_data_test(NAME youth_academy_tests REQUIRED_OPTIONS BMH_TEST_YOUTH_ACADEMY_TESTS_INPUT_1 COMMAND youth_academy_tests "${BMH_DATA}"
    "${BMH_TEST_YOUTH_ACADEMY_TESTS_INPUT_1}")

# Development helper: prints computed values of a savegame for comparison with the original
add_executable(probe tests/probe.cpp)
target_link_libraries(probe PRIVATE bmhcore)
if(MSVC)
    target_compile_options(probe PRIVATE /utf-8)
endif()

# Development helper: compares the ported engine with a matchday recorded from the original
add_executable(capture_check tests/capture_check.cpp)
target_link_libraries(capture_check PRIVATE bmhcore)
if(MSVC)
    target_compile_options(capture_check PRIVATE /utf-8)
endif()

# Development helper: compares the ported matchday processing with post-match snapshots
add_executable(post_check tests/post_check.cpp)
target_link_libraries(post_check PRIVATE bmhcore)
if(MSVC)
    target_compile_options(post_check PRIVATE /utf-8)
endif()

# Development helper: hex dump of one savegame block
add_executable(blockdump tests/blockdump.cpp)
target_link_libraries(blockdump PRIVATE bmhcore)
# Development helper: prints a club's squad from a savegame (player, age, words)
add_executable(squad_dump tests/squad_dump.cpp)
target_link_libraries(squad_dump PRIVATE bmhcore)

# Development helper: replays a recorded turn (matchday + week) section by section
add_executable(week_check tests/week_check.cpp)
target_link_libraries(week_check PRIVATE bmhcore)
if(MSVC)
    target_compile_options(week_check PRIVATE /utf-8)
endif()

# Development helper: the kick-off of a matchday with a human team, from the pre-match desk
add_executable(kickoff_check tests/kickoff_check.cpp)
target_link_libraries(kickoff_check PRIVATE bmhcore)

# Development helper: league table computed from a savegame
add_executable(table_print tests/table_print.cpp)
target_link_libraries(table_print PRIVATE bmhcore)
if(MSVC)
    target_compile_options(table_print PRIVATE /utf-8)
endif()

add_executable(textfind tests/textfind.cpp)
target_link_libraries(textfind PRIVATE bmhcore)
if(MSVC)
    target_compile_options(textfind PRIVATE /utf-8)
endif()

# Development helper: replays desk actions (training camp, ...) between two post snapshots
add_executable(desk_check tests/desk_check.cpp)
target_link_libraries(desk_check PRIVATE bmhcore)

# Development helper: the start of a new career against ref/2p/init_capture.jsonl
add_executable(init_check tests/init_check.cpp)
target_link_libraries(init_check PRIVATE bmhcore)

# The playable program: the shared UI library connected to the engine by src/app.
add_executable(openbmh_play
    src/app/main.cpp
    ${BMH_APP_CONTROLLER_SOURCE}
    ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(openbmh_play PRIVATE bmhui bmhlaunch_dialog openbmh_audio ${BMH_SDL_TARGET})
# a game without a console window on double-click (SDL2main provides WinMain); started from a
# console it attaches to that one for --version, --help and start errors (src/app/main.cpp)
set_target_properties(openbmh_play PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME OpenBMH)
target_include_directories(openbmh_play PRIVATE "${CMAKE_BINARY_DIR}/generated")
if(MSVC)
    target_compile_options(openbmh_play PRIVATE /utf-8)
    # App sources also compile in headless tests/helpers without target /utf-8.
    set_source_files_properties(src/app/career_session.cpp src/app/window_actions.cpp
        src/app/desktop_actions.cpp PROPERTIES COMPILE_OPTIONS /utf-8)
endif()

# Development helper: drives the career session like the UI, without a window
add_executable(play_check tests/play_check.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(play_check PRIVATE bmhui)
if(MSVC)
    target_compile_options(play_check PRIVATE /utf-8)
endif()

add_executable(training_camp_app_tests tests/training_camp_app_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(training_camp_app_tests PRIVATE bmhui)
bmh_add_data_test(NAME training_camp_app_tests COMMAND training_camp_app_tests "${BMH_DATA}")
add_executable(bank_app_tests tests/bank_app_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(bank_app_tests PRIVATE bmhui)
bmh_add_data_test(NAME bank_app_tests REQUIRED_OPTIONS BMH_TEST_INPUTS REQUIRED_FILES bank-deadline.fbm bank-timer.fbm bank-cup.fbm COMMAND bank_app_tests "${BMH_DATA}")

add_executable(shared_desktop_actions_tests tests/shared_desktop_actions_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(shared_desktop_actions_tests PRIVATE bmhui)
set(BMH_TEST_SHARED_DESKTOP_ACTIONS_TESTS_INPUT_1 "" CACHE PATH "External input 1 for shared_desktop_actions_tests; not distributed")
bmh_add_data_test(NAME shared_desktop_actions_tests REQUIRED_OPTIONS BMH_TEST_SHARED_DESKTOP_ACTIONS_TESTS_INPUT_1 COMMAND shared_desktop_actions_tests "${BMH_DATA}"
    "${BMH_TEST_SHARED_DESKTOP_ACTIONS_TESTS_INPUT_1}")

add_executable(credit_interest_weekly_tests tests/credit_interest_weekly_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(credit_interest_weekly_tests PRIVATE bmhui)
bmh_add_data_test(NAME credit_interest_weekly_tests COMMAND credit_interest_weekly_tests "${BMH_DATA}")

add_executable(app_controller_tests tests/app_controller_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(app_controller_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
if(MSVC)
    target_compile_options(app_controller_tests PRIVATE /utf-8)
endif()
bmh_add_data_test(NAME app_controller_tests COMMAND app_controller_tests "${BMH_DATA}")
set(BMH_TEST_APP_DELAY_SETTINGS_TESTS_INPUT_1 "" CACHE PATH "External input 1 for app_delay_settings_tests; not distributed")
bmh_add_data_test(NAME app_environment_tests COMMAND app_controller_tests "${BMH_DATA}" --environment-only)
bmh_add_data_test(NAME app_delay_settings_tests REQUIRED_OPTIONS BMH_TEST_APP_DELAY_SETTINGS_TESTS_INPUT_1 COMMAND app_controller_tests "${BMH_DATA}"
    --delay-settings-only "${BMH_TEST_APP_DELAY_SETTINGS_TESTS_INPUT_1}")

add_executable(sound_settings_app_tests tests/sound_settings_app_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(sound_settings_app_tests PRIVATE bmhui)
set(BMH_TEST_SOUND_SETTINGS_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for sound_settings_app_tests; not distributed")
bmh_add_data_test(NAME sound_settings_app_tests REQUIRED_OPTIONS BMH_TEST_SOUND_SETTINGS_APP_TESTS_INPUT_1 COMMAND sound_settings_app_tests "${BMH_DATA}"
    "${BMH_TEST_SOUND_SETTINGS_APP_TESTS_INPUT_1}")

add_executable(statistics_case2_app_tests tests/statistics_case2_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(statistics_case2_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_STATISTICS_CASE2_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for statistics_case2_app_tests; not distributed")
bmh_add_data_test(NAME statistics_case2_app_tests REQUIRED_OPTIONS BMH_TEST_STATISTICS_CASE2_APP_TESTS_INPUT_1 COMMAND statistics_case2_app_tests "${BMH_DATA}"
    "${BMH_TEST_STATISTICS_CASE2_APP_TESTS_INPUT_1}")

add_executable(national_friendly_actions_tests tests/national_friendly_actions_tests.cpp)
target_link_libraries(national_friendly_actions_tests PRIVATE bmhcore)
add_test(NAME national_friendly_actions_tests COMMAND national_friendly_actions_tests)

add_executable(manager_join_app_tests tests/manager_join_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(manager_join_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
if(MSVC)
    target_compile_options(manager_join_app_tests PRIVATE /utf-8)
endif()
set(BMH_TEST_MANAGER_JOIN_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for manager_join_app_tests; not distributed")
bmh_add_data_test(NAME manager_join_app_tests REQUIRED_OPTIONS BMH_TEST_MANAGER_JOIN_APP_TESTS_INPUT_1 COMMAND manager_join_app_tests "${BMH_DATA}"
    "${BMH_TEST_MANAGER_JOIN_APP_TESTS_INPUT_1}")

add_executable(live_matchday_events_tests tests/live_matchday_events_tests.cpp)
target_include_directories(live_matchday_events_tests PRIVATE src)
target_link_libraries(live_matchday_events_tests PRIVATE ${BMH_SDL_TARGET})
add_test(NAME live_matchday_events_tests COMMAND live_matchday_events_tests)

add_executable(reality_motion_tests tests/reality_motion_tests.cpp)
target_link_libraries(reality_motion_tests PRIVATE bmhui)
bmh_add_data_test(NAME reality_motion_tests COMMAND reality_motion_tests "${BMH_DATA}")

add_executable(reality_setup_tests tests/reality_setup_tests.cpp)
target_link_libraries(reality_setup_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_setup_tests COMMAND reality_setup_tests "${BMH_DATA}")

add_executable(reality_space_tests tests/reality_space_tests.cpp)
target_link_libraries(reality_space_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_space_tests COMMAND reality_space_tests "${BMH_DATA}")

add_executable(reality_movement_tests tests/reality_movement_tests.cpp)
target_link_libraries(reality_movement_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_movement_tests COMMAND reality_movement_tests "${BMH_DATA}")

add_executable(reality_ball_tests tests/reality_ball_tests.cpp)
target_link_libraries(reality_ball_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_ball_tests COMMAND reality_ball_tests "${BMH_DATA}")

add_executable(reality_duel_tests tests/reality_duel_tests.cpp)
target_link_libraries(reality_duel_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_duel_tests COMMAND reality_duel_tests "${BMH_DATA}")

add_executable(reality_forced_event_tests tests/reality_forced_event_tests.cpp)
target_link_libraries(reality_forced_event_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_forced_event_tests COMMAND reality_forced_event_tests "${BMH_DATA}")

add_executable(reality_reporter_tests tests/reality_reporter_tests.cpp)
target_link_libraries(reality_reporter_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_reporter_tests COMMAND reality_reporter_tests "${BMH_DATA}")

add_executable(reality_matchday_tests tests/reality_matchday_tests.cpp)
target_link_libraries(reality_matchday_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_matchday_tests COMMAND reality_matchday_tests "${BMH_DATA}")

add_executable(reality_career_special_tests tests/reality_career_special_tests.cpp ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(reality_career_special_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_REALITY_CAREER_SPECIAL_TESTS_INPUT_1 "" CACHE PATH "External input 1 for reality_career_special_tests; not distributed")
bmh_add_data_test(NAME reality_career_special_tests REQUIRED_OPTIONS BMH_TEST_REALITY_CAREER_SPECIAL_TESTS_INPUT_1 COMMAND reality_career_special_tests "${BMH_DATA}"
    "${BMH_TEST_REALITY_CAREER_SPECIAL_TESTS_INPUT_1}")

add_executable(reality_deferred_tests tests/reality_deferred_tests.cpp)
target_link_libraries(reality_deferred_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_deferred_tests COMMAND reality_deferred_tests "${BMH_DATA}")

add_executable(phase_results_career_tests tests/phase_results_career_tests.cpp ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(phase_results_career_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
bmh_add_data_test(NAME phase_results_career_tests COMMAND phase_results_career_tests "${BMH_DATA}")

add_executable(reality_scene_tests tests/reality_scene_tests.cpp)
target_link_libraries(reality_scene_tests PRIVATE bmhcore)
bmh_add_data_test(NAME reality_scene_tests COMMAND reality_scene_tests "${BMH_DATA}")

add_executable(reality_reporter_layout_tests tests/reality_reporter_layout_tests.cpp)
target_link_libraries(reality_reporter_layout_tests PRIVATE bmhui)
bmh_add_data_test(NAME reality_reporter_layout_tests COMMAND reality_reporter_layout_tests "${BMH_DATA}")

add_executable(training_dispatch_app_tests tests/training_dispatch_app_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(training_dispatch_app_tests PRIVATE bmhui)
if(MSVC)
    target_compile_options(training_dispatch_app_tests PRIVATE /utf-8)
endif()
bmh_add_data_test(NAME training_dispatch_app_tests COMMAND training_dispatch_app_tests "${BMH_DATA}")

# Development helper: savegames loaded and written again must be identical
add_executable(save_check tests/save_check.cpp)
target_link_libraries(save_check PRIVATE bmhcore)
if(UNIX)
    add_executable(save_close_fault_tests tests/save_close_fault_tests.cpp)
    target_link_libraries(save_close_fault_tests PRIVATE bmhcore)
    set(BMH_TEST_SAVE_CLOSE_FAULT_TESTS_INPUT_1 "" CACHE PATH "External input 1 for save_close_fault_tests; not distributed")
    bmh_add_data_test(NAME save_close_fault_tests NO_GAME_DATA REQUIRED_OPTIONS BMH_TEST_SAVE_CLOSE_FAULT_TESTS_INPUT_1 COMMAND save_close_fault_tests
        "${BMH_TEST_SAVE_CLOSE_FAULT_TESTS_INPUT_1}")
endif()

# Plausibility run: a new career played headless through the turn driver (E7)
add_executable(season_sim tests/season_sim.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(season_sim PRIVATE bmhui)
if(MSVC)
    target_compile_options(season_sim PRIVATE /utf-8)
endif()

# Headless terminal/protocol client; owns no game logic and has no SDL dependency.
add_executable(openbmh_cli
    src/cli/main.cpp
    src/cli/protocol.cpp
    src/cli/console/console.cpp
    src/cli/console/parser.cpp
    src/cli/console/windows_start.cpp
    src/cli/console/windows_desk.cpp
    src/cli/console/windows_tables.cpp
    src/cli/console/windows_club.cpp
    src/cli/console/windows_money.cpp
    src/cli/console/windows_office.cpp
    src/cli/console/windows_squad.cpp
    src/cli/console/windows_transfer.cpp
    src/cli/console/windows_info.cpp
    src/cli/console/windows_match.cpp
    src/cli/console/windows_friendly.cpp
    src/cli/console/windows_reschedule.cpp
    src/cli/console/windows_balance.cpp
    src/cli/console/windows_environment.cpp
    src/cli/console/windows_state.cpp
    src/cli/seat_hub.cpp
    src/cli/json.cpp
    ${BMH_APP_SESSION_SOURCE})
target_include_directories(openbmh_cli PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(openbmh_cli PRIVATE bmhui bmhlaunch)
if(WIN32)
    target_link_libraries(openbmh_cli PRIVATE ws2_32)  # --listen (Winsock)
endif()
if(MSVC)
    target_compile_options(openbmh_cli PRIVATE /utf-8)
endif()

add_executable(cli_protocol_tests tests/cli_protocol_tests.cpp src/cli/protocol.cpp src/cli/json.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(cli_protocol_tests PRIVATE bmhui)
if(MSVC)
    target_compile_options(cli_protocol_tests PRIVATE /utf-8)
endif()
bmh_add_data_test(NAME cli_protocol_tests COMMAND cli_protocol_tests "${BMH_DATA}")

# Console language of openbmh_cli (docs/CLI_CONCEPT): parser, golden transcripts, JSON,
# replay and the registry check against commands.csv
add_executable(cli_console_tests tests/cli_console_tests.cpp src/cli/console/console.cpp src/cli/console/parser.cpp
    src/cli/console/windows_start.cpp src/cli/console/windows_desk.cpp src/cli/console/windows_tables.cpp src/cli/console/windows_club.cpp src/cli/console/windows_money.cpp src/cli/console/windows_office.cpp src/cli/console/windows_squad.cpp src/cli/console/windows_transfer.cpp src/cli/console/windows_info.cpp src/cli/console/windows_match.cpp src/cli/console/windows_friendly.cpp src/cli/console/windows_reschedule.cpp src/cli/console/windows_balance.cpp src/cli/console/windows_environment.cpp src/cli/console/windows_state.cpp src/cli/json.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(cli_console_tests PRIVATE bmhui)
add_test(NAME cli_parser_unit_tests COMMAND cli_console_tests --parser-only)
if(MSVC)
    target_compile_options(cli_console_tests PRIVATE /utf-8)
endif()
set(BMH_TEST_CLI_CONSOLE_TESTS_INPUT_1 "" CACHE PATH "External input 1 for cli_console_tests; not distributed")
set(BMH_TEST_CLI_TRANSCRIPTS "" CACHE PATH "External console transcript expectations")
bmh_add_data_test(NAME cli_console_tests REQUIRED_OPTIONS BMH_TEST_INPUTS BMH_TEST_CLI_TRANSCRIPTS BMH_TEST_CLI_CONSOLE_TESTS_INPUT_1 REQUIRED_FILES loan-return.fbm human-transfer.fbm COMMAND cli_console_tests "${BMH_DATA}" "${BMH_TEST_CLI_TRANSCRIPTS}"
    "${BMH_TEST_CLI_CONSOLE_TESTS_INPUT_1}"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/CLI_CONCEPT/commands.csv")
# Soak test: random console play over several seasons with invariant checks (tests/cli_soak.cpp).
# CTest runs one short season with two managers; longer runs: cli_soak <data> <dir> <seasons> <managers> <seed>.
add_executable(cli_soak tests/cli_soak.cpp src/cli/console/console.cpp src/cli/console/parser.cpp
    src/cli/console/windows_start.cpp src/cli/console/windows_desk.cpp src/cli/console/windows_tables.cpp src/cli/console/windows_club.cpp src/cli/console/windows_money.cpp src/cli/console/windows_office.cpp src/cli/console/windows_squad.cpp src/cli/console/windows_transfer.cpp src/cli/console/windows_info.cpp src/cli/console/windows_match.cpp src/cli/console/windows_friendly.cpp src/cli/console/windows_reschedule.cpp src/cli/console/windows_balance.cpp src/cli/console/windows_environment.cpp src/cli/console/windows_state.cpp src/cli/json.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(cli_soak PRIVATE bmhui)
if(MSVC)
    target_compile_options(cli_soak PRIVATE /utf-8)
endif()
bmh_add_data_test(NAME cli_soak COMMAND cli_soak "${BMH_DATA}" "${CMAKE_CURRENT_BINARY_DIR}/soak" 1 2 7)

# Repository hygiene: no container login banner in committed files
add_test(NAME repo_hygiene COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/cmake/repo_hygiene.cmake)

add_executable(game_end_app_tests tests/game_end_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(game_end_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
if(MSVC)
    target_compile_options(game_end_app_tests PRIVATE /utf-8)
endif()
set(BMH_TEST_GAME_END_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for game_end_app_tests; not distributed")
bmh_add_data_test(NAME game_end_app_tests REQUIRED_OPTIONS BMH_TEST_GAME_END_APP_TESTS_INPUT_1 COMMAND game_end_app_tests "${BMH_DATA}"
    "${BMH_TEST_GAME_END_APP_TESTS_INPUT_1}")

add_executable(game_end_cli_tests tests/game_end_cli_tests.cpp src/cli/protocol.cpp src/cli/json.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(game_end_cli_tests PRIVATE bmhui)
if(MSVC)
    target_compile_options(game_end_cli_tests PRIVATE /utf-8)
endif()
set(BMH_TEST_GAME_END_CLI_TESTS_INPUT_1 "" CACHE PATH "External input 1 for game_end_cli_tests; not distributed")
bmh_add_data_test(NAME game_end_cli_tests REQUIRED_OPTIONS BMH_TEST_GAME_END_CLI_TESTS_INPUT_1 COMMAND game_end_cli_tests "${BMH_DATA}"
    "${BMH_TEST_GAME_END_CLI_TESTS_INPUT_1}")

add_executable(season_promotion_app_tests tests/season_promotion_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(season_promotion_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_SEASON_PROMOTION_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for season_promotion_app_tests; not distributed")
bmh_add_data_test(NAME season_promotion_app_tests REQUIRED_OPTIONS BMH_TEST_SEASON_PROMOTION_APP_TESTS_INPUT_1 COMMAND season_promotion_app_tests "${BMH_DATA}" "${BMH_TEST_SEASON_PROMOTION_APP_TESTS_INPUT_1}")

add_executable(season_promotion_tests tests/season_promotion_tests.cpp)
target_link_libraries(season_promotion_tests PRIVATE bmhcore)
set(BMH_TEST_SEASON_PROMOTION_TESTS_INPUT_1 "" CACHE PATH "External input 1 for season_promotion_tests; not distributed")
bmh_add_data_test(NAME season_promotion_tests REQUIRED_OPTIONS BMH_TEST_SEASON_PROMOTION_TESTS_INPUT_1 COMMAND season_promotion_tests "${BMH_DATA}"
    "${BMH_TEST_SEASON_PROMOTION_TESTS_INPUT_1}" 589388519 665367)
add_executable(ui_season_promotion_tests tests/ui_season_promotion_tests.cpp)
target_link_libraries(ui_season_promotion_tests PRIVATE bmhui)
set(BMH_TEST_UI_SEASON_PROMOTION_TESTS_INPUT_1 "" CACHE PATH "External input 1 for ui_season_promotion_tests; not distributed")
bmh_add_data_test(NAME ui_season_promotion_tests REQUIRED_OPTIONS BMH_TEST_UI_SEASON_PROMOTION_TESTS_INPUT_1 COMMAND ui_season_promotion_tests "${BMH_DATA}"
    "${BMH_TEST_UI_SEASON_PROMOTION_TESTS_INPUT_1}")

add_executable(national_nomination_tests tests/national_nomination_tests.cpp)
target_link_libraries(national_nomination_tests PRIVATE bmhcore)
set(BMH_TEST_NATIONAL_NOMINATION_TESTS_INPUT_1 "" CACHE PATH "External input 1 for national_nomination_tests; not distributed")
bmh_add_data_test(NAME national_nomination_tests REQUIRED_OPTIONS BMH_TEST_NATIONAL_NOMINATION_TESTS_INPUT_1 COMMAND national_nomination_tests "${BMH_DATA}"
    "${BMH_TEST_NATIONAL_NOMINATION_TESTS_INPUT_1}")

add_executable(season_cup_draw_app_tests tests/season_cup_draw_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(season_cup_draw_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_SEASON_CUP_DRAW_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for season_cup_draw_app_tests; not distributed")
bmh_add_data_test(NAME season_cup_draw_app_tests REQUIRED_OPTIONS BMH_TEST_SEASON_CUP_DRAW_APP_TESTS_INPUT_1 COMMAND season_cup_draw_app_tests "${BMH_DATA}"
    "${BMH_TEST_SEASON_CUP_DRAW_APP_TESTS_INPUT_1}")

add_executable(season_cup_draw_tests tests/season_cup_draw_tests.cpp)
target_link_libraries(season_cup_draw_tests PRIVATE bmhcore)
set(BMH_TEST_SEASON_CUP_DRAW_TESTS_INPUT_1 "" CACHE PATH "External input 1 for season_cup_draw_tests; not distributed")
bmh_add_data_test(NAME season_cup_draw_tests REQUIRED_OPTIONS BMH_TEST_SEASON_CUP_DRAW_TESTS_INPUT_1 COMMAND season_cup_draw_tests "${BMH_DATA}"
    "${BMH_TEST_SEASON_CUP_DRAW_TESTS_INPUT_1}" 589388519 665367)

add_executable(cup_followup_tests tests/cup_followup_tests.cpp)
target_link_libraries(cup_followup_tests PRIVATE bmhcore)
add_executable(cup_seeded_followup_tests tests/cup_seeded_followup_tests.cpp)
target_link_libraries(cup_seeded_followup_tests PRIVATE bmhcore)
add_test(NAME cup_seeded_followup_tests COMMAND cup_seeded_followup_tests)

add_executable(cup_calendar_boundary_tests tests/cup_calendar_boundary_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(cup_calendar_boundary_tests PRIVATE bmhui)
set(BMH_TEST_CUP_CALENDAR_BOUNDARY_TESTS_INPUT_1 "" CACHE PATH "External input 1 for cup_calendar_boundary_tests; not distributed")
bmh_add_data_test(NAME cup_calendar_boundary_tests REQUIRED_OPTIONS BMH_TEST_CUP_CALENDAR_BOUNDARY_TESTS_INPUT_1 COMMAND cup_calendar_boundary_tests "${BMH_DATA}"
    "${BMH_TEST_CUP_CALENDAR_BOUNDARY_TESTS_INPUT_1}")
set(BMH_TEST_CUP_FOLLOWUP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for cup_followup_tests; not distributed")
set(BMH_TEST_CUP_FOLLOWUP_TESTS_INPUT_2 "" CACHE PATH "External input 2 for cup_followup_tests; not distributed")
bmh_add_data_test(NAME cup_followup_tests REQUIRED_OPTIONS BMH_TEST_CUP_FOLLOWUP_TESTS_INPUT_1 BMH_TEST_CUP_FOLLOWUP_TESTS_INPUT_2 COMMAND cup_followup_tests "${BMH_DATA}"
    "${BMH_TEST_CUP_FOLLOWUP_TESTS_INPUT_1}"
    "${BMH_TEST_CUP_FOLLOWUP_TESTS_INPUT_2}")

add_executable(club_info_tests tests/club_info_tests.cpp)
target_link_libraries(club_info_tests PRIVATE bmhcore)
add_test(NAME club_info_tests COMMAND club_info_tests)

add_executable(cup_cursor_tests tests/cup_cursor_tests.cpp)
target_link_libraries(cup_cursor_tests PRIVATE bmhcore)
set(BMH_TEST_CUP_CURSOR_TESTS_INPUT_1 "" CACHE PATH "External input 1 for cup_cursor_tests; not distributed")
bmh_add_data_test(NAME cup_cursor_tests REQUIRED_OPTIONS BMH_TEST_CUP_CURSOR_TESTS_INPUT_1 COMMAND cup_cursor_tests "${BMH_DATA}"
    "${BMH_TEST_CUP_CURSOR_TESTS_INPUT_1}")
add_executable(club_info_category_tests tests/club_info_category_tests.cpp)
target_link_libraries(club_info_category_tests PRIVATE bmhcore)
add_test(NAME club_info_category_tests COMMAND club_info_category_tests)

add_executable(club_player_detail_tests tests/club_player_detail_tests.cpp)
target_link_libraries(club_player_detail_tests PRIVATE bmhcore)
bmh_add_data_test(NAME club_player_detail_tests COMMAND club_player_detail_tests "${BMH_DATA}")
add_executable(ui_cup_draw_tests tests/cup_draw_ui_tests.cpp)
target_link_libraries(ui_cup_draw_tests PRIVATE bmhui)
set(BMH_TEST_UI_CUP_DRAW_TESTS_INPUT_1 "" CACHE PATH "External input 1 for ui_cup_draw_tests; not distributed")
set(BMH_TEST_UI_CUP_DRAW_TESTS_INPUT_2 "" CACHE PATH "External input 2 for ui_cup_draw_tests; not distributed")
bmh_add_data_test(NAME ui_cup_draw_tests REQUIRED_OPTIONS BMH_TEST_UI_CUP_DRAW_TESTS_INPUT_1 BMH_TEST_UI_CUP_DRAW_TESTS_INPUT_2 COMMAND ui_cup_draw_tests "${BMH_DATA}"
    "${BMH_TEST_UI_CUP_DRAW_TESTS_INPUT_1}"
    "${BMH_TEST_UI_CUP_DRAW_TESTS_INPUT_2}")

add_executable(cup_script_oracle tests/cup_script_oracle.cpp)
target_link_libraries(cup_script_oracle PRIVATE bmhui)
bmh_add_data_test(NAME cup_script_oracle COMMAND cup_script_oracle "${BMH_DATA}")

add_executable(career_initial_offers_tests tests/career_initial_offers_tests.cpp)
target_link_libraries(career_initial_offers_tests PRIVATE bmhcore)
bmh_add_data_test(NAME career_initial_offers_tests COMMAND career_initial_offers_tests "${BMH_DATA}")

add_executable(pending_offer_tests tests/pending_offer_tests.cpp)
target_link_libraries(pending_offer_tests PRIVATE bmhcore)
set(BMH_TEST_PENDING_OFFER_TESTS_INPUT_1 "" CACHE PATH "External input 1 for pending_offer_tests; not distributed")
bmh_add_data_test(NAME pending_offer_tests REQUIRED_OPTIONS BMH_TEST_PENDING_OFFER_TESTS_INPUT_1 COMMAND pending_offer_tests "${BMH_DATA}"
    "${BMH_TEST_PENDING_OFFER_TESTS_INPUT_1}")

add_executable(pending_offer_app_tests tests/pending_offer_app_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(pending_offer_app_tests PRIVATE bmhui)
set(BMH_TEST_PENDING_OFFER_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for pending_offer_app_tests; not distributed")
bmh_add_data_test(NAME pending_offer_app_tests REQUIRED_OPTIONS BMH_TEST_PENDING_OFFER_APP_TESTS_INPUT_1 COMMAND pending_offer_app_tests "${BMH_DATA}"
    "${BMH_TEST_PENDING_OFFER_APP_TESTS_INPUT_1}")

add_executable(pending_offer_nested_app_tests tests/pending_offer_nested_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(pending_offer_nested_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_PENDING_OFFER_NESTED_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for pending_offer_nested_app_tests; not distributed")
set(BMH_TEST_PENDING_OFFER_NESTED_APP_TESTS_INPUT_2 "" CACHE PATH "External input 2 for pending_offer_nested_app_tests; not distributed")
bmh_add_data_test(NAME pending_offer_nested_app_tests REQUIRED_OPTIONS BMH_TEST_PENDING_OFFER_NESTED_APP_TESTS_INPUT_1 BMH_TEST_PENDING_OFFER_NESTED_APP_TESTS_INPUT_2 COMMAND pending_offer_nested_app_tests "${BMH_DATA}"
    "${BMH_TEST_PENDING_OFFER_NESTED_APP_TESTS_INPUT_1}"
    "${BMH_TEST_PENDING_OFFER_NESTED_APP_TESTS_INPUT_2}")

add_executable(pending_offer_desk_tests tests/pending_offer_desk_tests.cpp)
target_link_libraries(pending_offer_desk_tests PRIVATE bmhcore)
set(BMH_TEST_PENDING_OFFER_DESK_TESTS_INPUT_1 "" CACHE PATH "External input 1 for pending_offer_desk_tests; not distributed")
bmh_add_data_test(NAME pending_offer_desk_tests REQUIRED_OPTIONS BMH_TEST_PENDING_OFFER_DESK_TESTS_INPUT_1 COMMAND pending_offer_desk_tests "${BMH_DATA}"
    "${BMH_TEST_PENDING_OFFER_DESK_TESTS_INPUT_1}")

add_executable(human_buyer_pending_offer_tests tests/human_buyer_pending_offer_tests.cpp)
target_link_libraries(human_buyer_pending_offer_tests PRIVATE bmhcore)
set(BMH_TEST_HUMAN_BUYER_PENDING_OFFER_TESTS_INPUT_1 "" CACHE PATH "External input 1 for human_buyer_pending_offer_tests; not distributed")
bmh_add_data_test(NAME human_buyer_pending_offer_tests REQUIRED_OPTIONS BMH_TEST_HUMAN_BUYER_PENDING_OFFER_TESTS_INPUT_1 COMMAND human_buyer_pending_offer_tests "${BMH_DATA}"
    "${BMH_TEST_HUMAN_BUYER_PENDING_OFFER_TESTS_INPUT_1}")

add_executable(human_buyer_pending_app_tests tests/human_buyer_pending_app_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(human_buyer_pending_app_tests PRIVATE bmhui)
set(BMH_TEST_HUMAN_BUYER_PENDING_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for human_buyer_pending_app_tests; not distributed")
bmh_add_data_test(NAME human_buyer_pending_app_tests REQUIRED_OPTIONS BMH_TEST_HUMAN_BUYER_PENDING_APP_TESTS_INPUT_1 COMMAND human_buyer_pending_app_tests "${BMH_DATA}"
    "${BMH_TEST_HUMAN_BUYER_PENDING_APP_TESTS_INPUT_1}")

add_executable(human_buyer_market_tests tests/human_buyer_market_tests.cpp)
target_link_libraries(human_buyer_market_tests PRIVATE bmhcore)
set(BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_1 "" CACHE PATH "External input 1 for human_buyer_market_tests; not distributed")
set(BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_2 "" CACHE PATH "External input 2 for human_buyer_market_tests; not distributed")
set(BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_3 "" CACHE PATH "External input 3 for human_buyer_market_tests; not distributed")
bmh_add_data_test(NAME human_buyer_market_tests REQUIRED_OPTIONS BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_1 BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_2 BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_3 COMMAND human_buyer_market_tests "${BMH_DATA}"
    "${BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_1}"
    "${BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_2}"
    "${BMH_TEST_HUMAN_BUYER_MARKET_TESTS_INPUT_3}")

add_executable(weekly_human_buyer_loan_tests tests/weekly_human_buyer_loan_tests.cpp)
target_link_libraries(weekly_human_buyer_loan_tests PRIVATE bmhcore)
set(BMH_TEST_WEEKLY_HUMAN_BUYER_LOAN_TESTS_INPUT_1 "" CACHE PATH "External input 1 for weekly_human_buyer_loan_tests; not distributed")
bmh_add_data_test(NAME weekly_human_buyer_loan_tests REQUIRED_OPTIONS BMH_TEST_WEEKLY_HUMAN_BUYER_LOAN_TESTS_INPUT_1 COMMAND weekly_human_buyer_loan_tests "${BMH_DATA}"
    "${BMH_TEST_WEEKLY_HUMAN_BUYER_LOAN_TESTS_INPUT_1}")

add_executable(transfer_bid_qualification_tests tests/transfer_bid_qualification_tests.cpp)
target_link_libraries(transfer_bid_qualification_tests PRIVATE bmhcore)
set(BMH_TEST_TRANSFER_BID_QUALIFICATION_TESTS_INPUT_1 "" CACHE PATH "External input 1 for transfer_bid_qualification_tests; not distributed")
bmh_add_data_test(NAME transfer_bid_qualification_tests REQUIRED_OPTIONS BMH_TEST_TRANSFER_BID_QUALIFICATION_TESTS_INPUT_1 COMMAND transfer_bid_qualification_tests "${BMH_DATA}"
    "${BMH_TEST_TRANSFER_BID_QUALIFICATION_TESTS_INPUT_1}")

add_executable(transfer_market_preview_tests tests/transfer_market_preview_tests.cpp)
target_link_libraries(transfer_market_preview_tests PRIVATE bmhcore)
set(BMH_TEST_TRANSFER_MARKET_PREVIEW_TESTS_INPUT_1 "" CACHE PATH "External input 1 for transfer_market_preview_tests; not distributed")
bmh_add_data_test(NAME transfer_market_preview_tests REQUIRED_OPTIONS BMH_TEST_TRANSFER_MARKET_PREVIEW_TESTS_INPUT_1 COMMAND transfer_market_preview_tests "${BMH_DATA}"
    "${BMH_TEST_TRANSFER_MARKET_PREVIEW_TESTS_INPUT_1}")

add_executable(transfer_offer_editor_tests tests/transfer_offer_editor_tests.cpp)
target_link_libraries(transfer_offer_editor_tests PRIVATE bmhcore)
set(BMH_TEST_TRANSFER_OFFER_EDITOR_TESTS_INPUT_1 "" CACHE PATH "External input 1 for transfer_offer_editor_tests; not distributed")
bmh_add_data_test(NAME transfer_offer_editor_tests REQUIRED_OPTIONS BMH_TEST_TRANSFER_OFFER_EDITOR_TESTS_INPUT_1 COMMAND transfer_offer_editor_tests "${BMH_DATA}"
    "${BMH_TEST_TRANSFER_OFFER_EDITOR_TESTS_INPUT_1}")

add_executable(transfer_market_preview_app_tests tests/transfer_market_preview_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(transfer_market_preview_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for transfer_market_preview_app_tests; not distributed")
set(BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_2 "" CACHE PATH "External input 2 for transfer_market_preview_app_tests; not distributed")
set(BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_3 "" CACHE PATH "External input 3 for transfer_market_preview_app_tests; not distributed")
bmh_add_data_test(NAME transfer_market_preview_app_tests REQUIRED_OPTIONS BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_1 BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_2 BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_3 COMMAND transfer_market_preview_app_tests "${BMH_DATA}"
    "${BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_1}"
    "${BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_2}"
    "${BMH_TEST_TRANSFER_MARKET_PREVIEW_APP_TESTS_INPUT_3}")

add_executable(transfer_offer_class_app_tests tests/transfer_offer_class_app_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(transfer_offer_class_app_tests PRIVATE bmhui)
set(BMH_TEST_TRANSFER_OFFER_CLASS_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for transfer_offer_class_app_tests; not distributed")
bmh_add_data_test(NAME transfer_offer_class_app_tests REQUIRED_OPTIONS BMH_TEST_TRANSFER_OFFER_CLASS_APP_TESTS_INPUT_1 COMMAND transfer_offer_class_app_tests "${BMH_DATA}"
    "${BMH_TEST_TRANSFER_OFFER_CLASS_APP_TESTS_INPUT_1}")

add_executable(human_buyer_market_app_tests tests/human_buyer_market_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(human_buyer_market_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_HUMAN_BUYER_MARKET_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for human_buyer_market_app_tests; not distributed")
bmh_add_data_test(NAME human_buyer_market_app_tests REQUIRED_OPTIONS BMH_TEST_HUMAN_BUYER_MARKET_APP_TESTS_INPUT_1 COMMAND human_buyer_market_app_tests "${BMH_DATA}"
    "${BMH_TEST_HUMAN_BUYER_MARKET_APP_TESTS_INPUT_1}")

add_executable(pending_offer_desk_app_tests tests/pending_offer_desk_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(pending_offer_desk_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_PENDING_OFFER_DESK_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for pending_offer_desk_app_tests; not distributed")
bmh_add_data_test(NAME pending_offer_desk_app_tests REQUIRED_OPTIONS BMH_TEST_PENDING_OFFER_DESK_APP_TESTS_INPUT_1 COMMAND pending_offer_desk_app_tests "${BMH_DATA}"
    "${BMH_TEST_PENDING_OFFER_DESK_APP_TESTS_INPUT_1}")

add_executable(loaded_manager_loop_tests tests/loaded_manager_loop_tests.cpp)
target_link_libraries(loaded_manager_loop_tests PRIVATE bmhcore)
set(BMH_TEST_LOADED_MANAGER_LOOP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for loaded_manager_loop_tests; not distributed")
bmh_add_data_test(NAME loaded_manager_loop_tests REQUIRED_OPTIONS BMH_TEST_LOADED_MANAGER_LOOP_TESTS_INPUT_1 COMMAND loaded_manager_loop_tests "${BMH_DATA}"
    "${BMH_TEST_LOADED_MANAGER_LOOP_TESTS_INPUT_1}")

add_executable(loaded_manager_loop_app_tests tests/loaded_manager_loop_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(loaded_manager_loop_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_LOADED_MANAGER_LOOP_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for loaded_manager_loop_app_tests; not distributed")
bmh_add_data_test(NAME loaded_manager_loop_app_tests REQUIRED_OPTIONS BMH_TEST_LOADED_MANAGER_LOOP_APP_TESTS_INPUT_1 COMMAND loaded_manager_loop_app_tests "${BMH_DATA}"
    "${BMH_TEST_LOADED_MANAGER_LOOP_APP_TESTS_INPUT_1}")

add_executable(national_coach_desk_app_tests tests/national_coach_desk_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(national_coach_desk_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_NATIONAL_COACH_DESK_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for national_coach_desk_app_tests; not distributed")
set(BMH_TEST_NATIONAL_COACH_DESK_APP_TESTS_INPUT_2 "" CACHE PATH "External input 2 for national_coach_desk_app_tests; not distributed")
bmh_add_data_test(NAME national_coach_desk_app_tests REQUIRED_OPTIONS BMH_TEST_INPUTS BMH_TEST_NATIONAL_COACH_DESK_APP_TESTS_INPUT_1 BMH_TEST_NATIONAL_COACH_DESK_APP_TESTS_INPUT_2 REQUIRED_FILES national-pending.fbm COMMAND national_coach_desk_app_tests "${BMH_DATA}"
    "${BMH_TEST_NATIONAL_COACH_DESK_APP_TESTS_INPUT_1}"
    "${BMH_TEST_NATIONAL_COACH_DESK_APP_TESTS_INPUT_2}")

add_executable(national_coach_desk_tests tests/national_coach_desk_tests.cpp)
target_link_libraries(national_coach_desk_tests PRIVATE bmhcore)
set(BMH_TEST_NATIONAL_COACH_DESK_TESTS_INPUT_1 "" CACHE PATH "External input 1 for national_coach_desk_tests; not distributed")
bmh_add_data_test(NAME national_coach_desk_tests REQUIRED_OPTIONS BMH_TEST_NATIONAL_COACH_DESK_TESTS_INPUT_1 COMMAND national_coach_desk_tests "${BMH_DATA}"
    "${BMH_TEST_NATIONAL_COACH_DESK_TESTS_INPUT_1}")

add_executable(transfer_offer_team_info_tests tests/transfer_offer_team_info_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(transfer_offer_team_info_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_TRANSFER_OFFER_TEAM_INFO_TESTS_INPUT_1 "" CACHE PATH "External input 1 for transfer_offer_team_info_tests; not distributed")
bmh_add_data_test(NAME transfer_offer_team_info_tests REQUIRED_OPTIONS BMH_TEST_TRANSFER_OFFER_TEAM_INFO_TESTS_INPUT_1 COMMAND transfer_offer_team_info_tests "${BMH_DATA}"
    "${BMH_TEST_TRANSFER_OFFER_TEAM_INFO_TESTS_INPUT_1}")

add_executable(transfer_offer_player_info_tests tests/transfer_offer_player_info_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(transfer_offer_player_info_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_TRANSFER_OFFER_PLAYER_INFO_TESTS_INPUT_1 "" CACHE PATH "External input 1 for transfer_offer_player_info_tests; not distributed")
bmh_add_data_test(NAME transfer_offer_player_info_tests REQUIRED_OPTIONS BMH_TEST_TRANSFER_OFFER_PLAYER_INFO_TESTS_INPUT_1 COMMAND transfer_offer_player_info_tests "${BMH_DATA}"
    "${BMH_TEST_TRANSFER_OFFER_PLAYER_INFO_TESTS_INPUT_1}")

add_executable(league_pairings_tests tests/league_pairings_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(league_pairings_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_LEAGUE_PAIRINGS_TESTS_INPUT_1 "" CACHE PATH "External input 1 for league_pairings_tests; not distributed")
bmh_add_data_test(NAME league_pairings_tests REQUIRED_OPTIONS BMH_TEST_LEAGUE_PAIRINGS_TESTS_INPUT_1 COMMAND league_pairings_tests "${BMH_DATA}"
    "${BMH_TEST_LEAGUE_PAIRINGS_TESTS_INPUT_1}")

add_executable(league_pairings_data_tests tests/league_pairings_data_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(league_pairings_data_tests PRIVATE bmhui)
bmh_add_data_test(NAME league_pairings_data_tests COMMAND league_pairings_data_tests "${BMH_DATA}")

add_executable(desktop_data_tests tests/desktop_data_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(desktop_data_tests PRIVATE bmhui)
bmh_add_data_test(NAME desktop_data_tests COMMAND desktop_data_tests "${BMH_DATA}")

add_executable(league_views_data_tests tests/league_views_data_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(league_views_data_tests PRIVATE bmhui)
bmh_add_data_test(NAME league_views_data_tests COMMAND league_views_data_tests "${BMH_DATA}")

# FLTD is translated by launch_check/the launcher first. Do not point these
# tests at the raw DOS directory: the shared UI consumes the prepared image.
set(BMH_ENGLISH_DATA "" CACHE PATH "Prepared Football Limited game-data directory")
    bmh_add_data_test(NAME english_pairings_data_tests DATA_DIR "${BMH_ENGLISH_DATA}" COMMAND league_pairings_data_tests "${BMH_ENGLISH_DATA}")
    bmh_add_data_test(NAME english_desktop_data_tests DATA_DIR "${BMH_ENGLISH_DATA}" COMMAND desktop_data_tests "${BMH_ENGLISH_DATA}" --english)
    bmh_add_data_test(NAME english_league_views_data_tests DATA_DIR "${BMH_ENGLISH_DATA}" COMMAND league_views_data_tests "${BMH_ENGLISH_DATA}")
    bmh_add_data_test(NAME english_training_camp_ui_tests REQUIRED_OPTIONS BMH_TEST_INPUTS REQUIRED_FILES camp-label-colors.bin DATA_DIR "${BMH_ENGLISH_DATA}" COMMAND ui_training_camp_tests "${BMH_ENGLISH_DATA}")
    bmh_add_data_test(NAME english_training_camp_content_tests DATA_DIR "${BMH_ENGLISH_DATA}" COMMAND training_camp_app_tests "${BMH_ENGLISH_DATA}" --content-only)

add_executable(league_table_controls_app_tests tests/league_table_controls_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(league_table_controls_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
# This scenario binds the captured German manager/league/date in WEEK196.
set(BMH_TEST_LEAGUE_TABLE_CONTROLS_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for league_table_controls_app_tests; not distributed")
bmh_add_data_test(NAME league_table_controls_app_tests REQUIRED_OPTIONS BMH_TEST_LEAGUE_TABLE_CONTROLS_APP_TESTS_INPUT_1 COMMAND league_table_controls_app_tests "${BMH_DATA}"
    "${BMH_TEST_LEAGUE_TABLE_CONTROLS_APP_TESTS_INPUT_1}")

add_executable(manager_desks_tests tests/manager_desks_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(manager_desks_tests PRIVATE bmhui)
set(BMH_TEST_MANAGER_DESKS_TESTS_INPUT_1 "" CACHE PATH "External input 1 for manager_desks_tests; not distributed")
bmh_add_data_test(NAME manager_desks_tests REQUIRED_OPTIONS BMH_TEST_MANAGER_DESKS_TESTS_INPUT_1 COMMAND manager_desks_tests "${BMH_DATA}"
    "${BMH_TEST_MANAGER_DESKS_TESTS_INPUT_1}")

add_executable(league_table_controls_flow_tests tests/league_table_controls_flow_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(league_table_controls_flow_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_LEAGUE_TABLE_CONTROLS_FLOW_TESTS_INPUT_1 "" CACHE PATH "External input 1 for league_table_controls_flow_tests; not distributed")
bmh_add_data_test(NAME league_table_controls_flow_tests REQUIRED_OPTIONS BMH_TEST_LEAGUE_TABLE_CONTROLS_FLOW_TESTS_INPUT_1 COMMAND league_table_controls_flow_tests "${BMH_DATA}"
    "${BMH_TEST_LEAGUE_TABLE_CONTROLS_FLOW_TESTS_INPUT_1}")

add_executable(balance_plan_app_tests tests/balance_plan_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(balance_plan_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_BALANCE_PLAN_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for balance_plan_app_tests; not distributed")
bmh_add_data_test(NAME balance_plan_app_tests REQUIRED_OPTIONS BMH_TEST_BALANCE_PLAN_APP_TESTS_INPUT_1 COMMAND balance_plan_app_tests "${BMH_DATA}"
    "${BMH_TEST_BALANCE_PLAN_APP_TESTS_INPUT_1}")

add_executable(top_scorers_app_tests tests/top_scorers_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(top_scorers_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
bmh_add_data_test(NAME top_scorers_app_tests COMMAND top_scorers_app_tests "${BMH_DATA}")

add_executable(other_games_projection_tests tests/other_games_projection_tests.cpp)
target_link_libraries(other_games_projection_tests PRIVATE bmhcore)
add_test(NAME other_games_projection_tests COMMAND other_games_projection_tests)

add_executable(other_games_app_tests tests/other_games_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(other_games_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
bmh_add_data_test(NAME other_games_app_tests COMMAND other_games_app_tests "${BMH_DATA}")

add_executable(other_games_ui_tests tests/other_games_ui_tests.cpp)
target_link_libraries(other_games_ui_tests PRIVATE bmhui)
bmh_add_data_test(NAME other_games_ui_tests COMMAND other_games_ui_tests "${BMH_DATA}")

add_executable(process_timer_tests tests/process_timer_tests.cpp)
target_link_libraries(process_timer_tests PRIVATE bmhui)
bmh_add_data_test(NAME process_timer_tests COMMAND process_timer_tests "${BMH_DATA}")

add_executable(stadium_block_expansion_tests tests/stadium_block_expansion_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(stadium_block_expansion_tests PRIVATE bmhui)
set(BMH_TEST_STADIUM_BLOCK_EXPANSION_TESTS_INPUT_1 "" CACHE PATH "External input 1 for stadium_block_expansion_tests; not distributed")
bmh_add_data_test(NAME stadium_block_expansion_tests REQUIRED_OPTIONS BMH_TEST_STADIUM_BLOCK_EXPANSION_TESTS_INPUT_1 COMMAND stadium_block_expansion_tests "${BMH_DATA}" "${BMH_TEST_STADIUM_BLOCK_EXPANSION_TESTS_INPUT_1}")

add_executable(stadium_block_ui_tests tests/stadium_block_ui_tests.cpp)
target_link_libraries(stadium_block_ui_tests PRIVATE bmhui)
bmh_add_data_test(NAME stadium_block_ui_tests COMMAND stadium_block_ui_tests "${BMH_DATA}")

add_executable(stadium_block_app_tests tests/stadium_block_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(stadium_block_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
bmh_add_data_test(NAME stadium_block_app_tests REQUIRED_OPTIONS BMH_TEST_INPUTS REQUIRED_FILES stadium.fbm COMMAND stadium_block_app_tests "${BMH_DATA}")

# Launcher: check the game data like the program start (tests/launch_check.cpp is a helper,
# tests/launch_tests.cpp the CTest with generated archives).
add_executable(launch_check tests/launch_check.cpp)
target_link_libraries(launch_check PRIVATE bmhlaunch)
add_executable(launch_tests tests/launch_tests.cpp)
target_link_libraries(launch_tests PRIVATE bmhlaunch bmh_miniz)
bmh_add_data_test(NAME launch_tests COMMAND launch_tests "${BMH_DATA}")

add_executable(bank_lender_ui_tests tests/bank_lender_ui_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(bank_lender_ui_tests PRIVATE bmhui)
bmh_add_data_test(NAME bank_lender_ui_tests COMMAND bank_lender_ui_tests "${BMH_DATA}")

add_executable(bank_lender_app_tests tests/bank_lender_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
target_link_libraries(bank_lender_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
bmh_add_data_test(NAME bank_lender_app_tests COMMAND bank_lender_app_tests "${BMH_DATA}")

add_executable(sportschau_app_tests tests/sportschau_app_tests.cpp ${BMH_APP_CONTROLLER_SOURCE} ${BMH_APP_SESSION_SDL_SOURCE})
add_executable(sportschau_scene_tests tests/sportschau_scene_tests.cpp ${BMH_APP_SESSION_SOURCE})
add_executable(sportschau_rating_tests tests/sportschau_rating_tests.cpp ${BMH_APP_SESSION_SOURCE})
add_executable(sportschau_eleven_tests tests/sportschau_eleven_tests.cpp ${BMH_APP_SESSION_SOURCE})
add_executable(competition_winner_tests tests/competition_winner_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(competition_winner_tests PRIVATE bmhui)
set(BMH_TEST_COMPETITION_WINNER_TESTS_INPUT_1 "" CACHE PATH "External input 1 for competition_winner_tests; not distributed")
bmh_add_data_test(NAME competition_winner_tests REQUIRED_OPTIONS BMH_TEST_COMPETITION_WINNER_TESTS_INPUT_1 COMMAND competition_winner_tests "${BMH_DATA}"
    "${BMH_TEST_COMPETITION_WINNER_TESTS_INPUT_1}")
add_executable(matchday_intro_tests tests/matchday_intro_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(matchday_intro_tests PRIVATE bmhui)
set(BMH_TEST_MATCHDAY_INTRO_TESTS_INPUT_1 "" CACHE PATH "External input 1 for matchday_intro_tests; not distributed")
bmh_add_data_test(NAME matchday_intro_tests REQUIRED_OPTIONS BMH_TEST_MATCHDAY_INTRO_TESTS_INPUT_1 COMMAND matchday_intro_tests "${BMH_DATA}"
    "${BMH_TEST_MATCHDAY_INTRO_TESTS_INPUT_1}")
add_executable(sportschau_teaminfo_tests tests/sportschau_teaminfo_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(sportschau_teaminfo_tests PRIVATE bmhui)
set(BMH_TEST_SPORTSCHAU_TEAMINFO_TESTS_INPUT_1 "" CACHE PATH "External input 1 for sportschau_teaminfo_tests; not distributed")
bmh_add_data_test(NAME sportschau_teaminfo_tests REQUIRED_OPTIONS BMH_TEST_SPORTSCHAU_TEAMINFO_TESTS_INPUT_1 COMMAND sportschau_teaminfo_tests "${BMH_DATA}"
    "${BMH_TEST_SPORTSCHAU_TEAMINFO_TESTS_INPUT_1}")
target_link_libraries(sportschau_eleven_tests PRIVATE bmhui)
set(BMH_TEST_SPORTSCHAU_ELEVEN_TESTS_INPUT_1 "" CACHE PATH "External input 1 for sportschau_eleven_tests; not distributed")
bmh_add_data_test(NAME sportschau_eleven_tests REQUIRED_OPTIONS BMH_TEST_SPORTSCHAU_ELEVEN_TESTS_INPUT_1 COMMAND sportschau_eleven_tests "${BMH_DATA}"
    "${BMH_TEST_SPORTSCHAU_ELEVEN_TESTS_INPUT_1}")
target_link_libraries(sportschau_rating_tests PRIVATE bmhui)
set(BMH_TEST_SPORTSCHAU_RATING_TESTS_INPUT_1 "" CACHE PATH "External input 1 for sportschau_rating_tests; not distributed")
bmh_add_data_test(NAME sportschau_rating_tests REQUIRED_OPTIONS BMH_TEST_SPORTSCHAU_RATING_TESTS_INPUT_1 COMMAND sportschau_rating_tests "${BMH_DATA}"
    "${BMH_TEST_SPORTSCHAU_RATING_TESTS_INPUT_1}")
target_link_libraries(sportschau_scene_tests PRIVATE bmhui)
set(BMH_TEST_SPORTSCHAU_SCENE_TESTS_INPUT_1 "" CACHE PATH "External input 1 for sportschau_scene_tests; not distributed")
bmh_add_data_test(NAME sportschau_scene_tests REQUIRED_OPTIONS BMH_TEST_SPORTSCHAU_SCENE_TESTS_INPUT_1 COMMAND sportschau_scene_tests "${BMH_DATA}"
    "${BMH_TEST_SPORTSCHAU_SCENE_TESTS_INPUT_1}")
add_executable(autosave_tests tests/autosave_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(autosave_tests PRIVATE bmhui)
set(BMH_TEST_AUTOSAVE_TESTS_INPUT_1 "" CACHE PATH "External input 1 for autosave_tests; not distributed")
bmh_add_data_test(NAME autosave_tests REQUIRED_OPTIONS BMH_TEST_AUTOSAVE_TESTS_INPUT_1 COMMAND autosave_tests "${BMH_DATA}"
    "${BMH_TEST_AUTOSAVE_TESTS_INPUT_1}")
target_link_libraries(sportschau_app_tests PRIVATE bmhui openbmh_audio ${BMH_SDL_TARGET})
set(BMH_TEST_SPORTSCHAU_APP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for sportschau_app_tests; not distributed")
bmh_add_data_test(NAME sportschau_app_tests REQUIRED_OPTIONS BMH_TEST_SPORTSCHAU_APP_TESTS_INPUT_1 COMMAND sportschau_app_tests "${BMH_DATA}"
    "${BMH_TEST_SPORTSCHAU_APP_TESTS_INPUT_1}")

# The runtime component deliberately contains only the two products and notices.
install(TARGETS openbmh_play openbmh_cli RUNTIME DESTINATION . COMPONENT Runtime)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md ACKNOWLEDGEMENTS.md CONTRIBUTING.md
    DESTINATION . COMPONENT Runtime)
install(DIRECTORY docs/ DESTINATION docs COMPONENT Runtime)
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
endif()

add_executable(tax_animation_unit_tests tests/tax_animation_unit_tests.cpp)
target_link_libraries(tax_animation_unit_tests PRIVATE bmhui)
add_test(NAME tax_animation_unit_tests COMMAND tax_animation_unit_tests)
add_executable(json_unit_tests tests/json_unit_tests.cpp src/cli/json.cpp)
target_include_directories(json_unit_tests PRIVATE src)
add_test(NAME json_unit_tests COMMAND json_unit_tests)
add_executable(launch_unit_tests tests/launch_unit_tests.cpp)
target_link_libraries(launch_unit_tests PRIVATE bmhlaunch bmh_miniz)
if(MSVC)
    target_compile_options(launch_unit_tests PRIVATE /utf-8)
endif()
add_test(NAME launch_unit_tests COMMAND launch_unit_tests)

add_executable(fltd_translation_unit_tests tests/fltd_translation_unit_tests.cpp)
target_link_libraries(fltd_translation_unit_tests PRIVATE bmhlaunch)
add_test(NAME fltd_translation_unit_tests COMMAND fltd_translation_unit_tests)

add_executable(host_path_display_unit_tests tests/host_path_display_unit_tests.cpp)
target_include_directories(host_path_display_unit_tests PRIVATE src)
if(MSVC)
    target_compile_options(host_path_display_unit_tests PRIVATE /utf-8)
endif()
add_test(NAME host_path_display_unit_tests COMMAND host_path_display_unit_tests)

add_executable(quit_confirmation_unit_tests tests/quit_confirmation_unit_tests.cpp)
target_include_directories(quit_confirmation_unit_tests PRIVATE src)
target_link_libraries(quit_confirmation_unit_tests PRIVATE ${BMH_SDL_TARGET})
if(MSVC)
    target_compile_options(quit_confirmation_unit_tests PRIVATE /utf-8)
endif()
add_test(NAME quit_confirmation_unit_tests COMMAND quit_confirmation_unit_tests)

add_executable(fixture_table_tests tests/fixture_table_tests.cpp)
target_link_libraries(fixture_table_tests PRIVATE bmhcore)
add_test(NAME fixture_table_unit_tests COMMAND fixture_table_tests)
bmh_add_data_test(NAME fixture_table_data_tests COMMAND fixture_table_tests "${BMH_DATA}")
bmh_add_data_test(NAME english_fixture_table_data_tests DATA_DIR "${BMH_ENGLISH_DATA}"
    COMMAND fixture_table_tests "${BMH_ENGLISH_DATA}")

# Private build paths are inputs to the source-package tool, never shipped in its manifest.
set(package_dependencies "{}")
foreach(dependency IN ITEMS SDL2 miniz lzma_sdk nfd)
    string(REPLACE "\\" "/" dependency_path "${${dependency}_SOURCE_DIR}")
    if(dependency STREQUAL "SDL2" AND sdl2_SOURCE_DIR)
        set(dependency_path "${sdl2_SOURCE_DIR}")
    endif()
    string(REPLACE "\"" "\\\"" dependency_path "${dependency_path}")
    string(JSON package_dependencies SET "${package_dependencies}" "${dependency}" "\"${dependency_path}\"")
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/package-dependencies.json" CONTENT "${package_dependencies}\n")

add_executable(classic_rules_unit_tests tests/classic_rules_unit_tests.cpp)
target_link_libraries(classic_rules_unit_tests PRIVATE bmhcore)
add_test(NAME classic_rules_unit_tests COMMAND classic_rules_unit_tests)

add_executable(season_player_reset_tests tests/season_player_reset_tests.cpp)
target_link_libraries(season_player_reset_tests PRIVATE bmhcore)
add_test(NAME season_player_reset_tests COMMAND season_player_reset_tests)
set_tests_properties(season_player_reset_tests PROPERTIES TIMEOUT 10)

add_executable(lineup_foreign_limit_tests tests/lineup_foreign_limit_tests.cpp)
target_link_libraries(lineup_foreign_limit_tests PRIVATE bmhcore)
add_test(NAME lineup_foreign_limit_tests COMMAND lineup_foreign_limit_tests)

add_executable(calendar_epoch_tests tests/calendar_epoch_tests.cpp)
target_link_libraries(calendar_epoch_tests PRIVATE bmhui)
add_test(NAME calendar_epoch_tests COMMAND calendar_epoch_tests)

add_executable(video_session_tests tests/video_session_tests.cpp)
target_include_directories(video_session_tests PRIVATE src)
target_link_libraries(video_session_tests PRIVATE ${BMH_SDL_TARGET})
add_test(NAME video_session_tests COMMAND video_session_tests)
if(APPLE)
    add_executable(mac_video_picker_tests tests/mac_video_picker_tests.mm)
    set_target_properties(mac_video_picker_tests PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
    target_include_directories(mac_video_picker_tests PRIVATE src)
    target_link_libraries(mac_video_picker_tests PRIVATE ${BMH_SDL_TARGET} nfd::nfd "-framework Cocoa")
    add_test(NAME mac_video_picker_tests COMMAND mac_video_picker_tests)
endif()

add_executable(desktop_feedback_tests tests/desktop_feedback_tests.cpp)
target_link_libraries(desktop_feedback_tests PRIVATE bmhui)
bmh_add_data_test(NAME desktop_feedback_tests COMMAND desktop_feedback_tests "${BMH_DATA}")

add_executable(data_profile_tests tests/data_profile_tests.cpp)
target_link_libraries(data_profile_tests PRIVATE bmhcore)
add_test(NAME data_profile_tests COMMAND data_profile_tests)

# Offline private package preparation; never installs Original data.
add_executable(export_community_runtime tools/poc/export_runtime.cpp)
target_link_libraries(export_community_runtime PRIVATE bmhcore)
add_executable(resource_bundle_tests tests/resource_bundle_tests.cpp)
target_link_libraries(resource_bundle_tests PRIVATE bmhcore bmh_miniz)
add_test(NAME resource_bundle_tests COMMAND resource_bundle_tests)
