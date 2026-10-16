# SPDX-License-Identifier: GPL-3.0-only
# SDL-independent UI; shared file formats and game logic come from bmhcore.
add_library(bmhui STATIC
    setup.cpp
    manager.cpp
    flow.cpp
    desk_state.cpp
    desk_drawers.cpp
    desktop.cpp
    preparation.cpp
    preparation_flow.cpp
    lineup.cpp
    training.cpp
    training_camp.cpp
    bank.cpp
    stadium.cpp
    toto.cpp
    transfer_market.cpp
    save_load.cpp
    tactics.cpp
    live_intervention.cpp
    live_matchday.cpp
    live_set_piece.cpp
    live_reality.cpp
    reality_reporter.cpp
    pitch.cpp
    player_card.cpp
    matchday_results.cpp
    matchday_results_screen.cpp
    league_table.cpp
    league_table_range_dialog.cpp
    league_pairings.cpp
    friendly_match.cpp
    national_friendly_ui.cpp
    calendar_state.cpp
    calendar.cpp
    indoor_standings.cpp
    strengths_state.cpp
    strengths.cpp
    top_scorers_state.cpp
    top_scorers.cpp
    desk_records.cpp
    opportunities.cpp
    statistics_rankings.cpp
    alltime_table.cpp
    world_cup_statistics.cpp
    finance_totals.cpp
    series_records.cpp
    analytics.cpp
    minute_statistics.cpp
    transfer_history.cpp
    other_games_state.cpp
    other_games.cpp
    process_timer.cpp
    contracts.cpp
    incoming_transfer.cpp
    outgoing_transfer.cpp
    balance_plan.cpp
    pending_offer.cpp
    settings.cpp
    sprite_font.cpp
    advertising.cpp
    environment.cpp
    achievements.cpp
    mailbox_state.cpp
    mailbox_screen.cpp
    season_entrants.cpp
    season_promotion.cpp
    cup_draw.cpp
    club_info.cpp
    team_statistics.cpp
    club_player_detail.cpp
    game_end.cpp
    game_end_alternate.cpp
    game_end_tax_animation.cpp
    sportschau.cpp)
target_link_libraries(bmhui PUBLIC bmhcore)
add_executable(openbmh_ui main.cpp)
target_link_libraries(openbmh_ui PRIVATE bmhui ${BMH_SDL_TARGET})
add_executable(ui_setup_tests tests.cpp)
target_link_libraries(ui_setup_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_setup_tests COMMAND ui_setup_tests "${BMH_DATA}")
add_executable(ui_national_friendly_tests ../../tests/national_friendly_ui_tests.cpp)
target_link_libraries(ui_national_friendly_tests PRIVATE bmhui)
add_test(NAME ui_national_friendly_tests COMMAND ui_national_friendly_tests)
add_executable(ui_manager_tests manager_tests.cpp)
target_link_libraries(ui_manager_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_manager_tests COMMAND ui_manager_tests "${BMH_DATA}")
set(BMH_UI_REFERENCE_SAVE "" CACHE FILEPATH "Optional original LABA0012 desktop reference; never bundled")
add_executable(ui_desktop_tests desktop_tests.cpp)
target_link_libraries(ui_desktop_tests PRIVATE bmhui)
set(BMH_TEST_UI_DESKTOP_TESTS_INPUT_1 "" CACHE PATH "External input 1 for ui_desktop_tests; not distributed")
bmh_add_data_test(NAME ui_desktop_tests REQUIRED_OPTIONS BMH_TEST_UI_DESKTOP_TESTS_INPUT_1 COMMAND ui_desktop_tests "${BMH_DATA}" "${BMH_UI_REFERENCE_SAVE}"
    "${BMH_TEST_UI_DESKTOP_TESTS_INPUT_1}")
set(BMH_UI_PREPARATION_SAVE "" CACHE FILEPATH "Optional original LABA0020 reference after observed menu entry effects; never bundled")
foreach(menu lineup training tactics preparation pitch player_card)
    add_executable(ui_${menu}_tests ${menu}_tests.cpp)
    target_link_libraries(ui_${menu}_tests PRIVATE bmhui)
    bmh_add_data_test(NAME ui_${menu}_tests COMMAND ui_${menu}_tests "${BMH_DATA}" "${BMH_UI_PREPARATION_SAVE}")
endforeach()
add_executable(ui_training_camp_tests training_camp_tests.cpp)
target_link_libraries(ui_training_camp_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_training_camp_tests COMMAND ui_training_camp_tests "${BMH_DATA}")
add_executable(ui_bank_tests bank_tests.cpp)
target_link_libraries(ui_bank_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_bank_tests COMMAND ui_bank_tests "${BMH_DATA}")
add_executable(ui_matchday_results_tests matchday_results_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(ui_matchday_results_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_matchday_results_tests COMMAND ui_matchday_results_tests "${BMH_DATA}")
add_executable(ui_league_table_tests league_table_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(ui_league_table_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_league_table_tests COMMAND ui_league_table_tests "${BMH_DATA}")
add_executable(ui_friendly_match_tests friendly_match_tests.cpp)
target_link_libraries(ui_friendly_match_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_friendly_match_tests COMMAND ui_friendly_match_tests "${BMH_DATA}")
add_executable(ui_contracts_tests contracts_tests.cpp)
target_link_libraries(ui_contracts_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_contracts_tests COMMAND ui_contracts_tests "${BMH_DATA}")
add_executable(ui_transfer_market_tests transfer_market_tests.cpp)
target_link_libraries(ui_transfer_market_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_transfer_market_tests COMMAND ui_transfer_market_tests "${BMH_DATA}")
add_executable(ui_incoming_transfer_tests incoming_transfer_tests.cpp)
target_link_libraries(ui_incoming_transfer_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_incoming_transfer_tests COMMAND ui_incoming_transfer_tests "${BMH_DATA}")
add_executable(ui_outgoing_transfer_tests outgoing_transfer_tests.cpp)
target_link_libraries(ui_outgoing_transfer_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_outgoing_transfer_tests COMMAND ui_outgoing_transfer_tests "${BMH_DATA}")
add_executable(ui_balance_plan_tests balance_plan_tests.cpp)
target_link_libraries(ui_balance_plan_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_balance_plan_tests COMMAND ui_balance_plan_tests "${BMH_DATA}")
add_executable(ui_live_intervention_tests live_intervention_tests.cpp)
target_link_libraries(ui_live_intervention_tests PRIVATE bmhui)
add_test(NAME ui_live_intervention_tests COMMAND ui_live_intervention_tests)
add_executable(ui_mailbox_state_tests mailbox_state_tests.cpp)
target_link_libraries(ui_mailbox_state_tests PRIVATE bmhui)
add_test(NAME ui_mailbox_state_tests COMMAND ui_mailbox_state_tests)
add_executable(ui_mailbox_screen_tests mailbox_screen_tests.cpp)
target_link_libraries(ui_mailbox_screen_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_mailbox_screen_tests COMMAND ui_mailbox_screen_tests "${BMH_DATA}")
add_executable(ui_settings_tests settings_tests.cpp)
target_link_libraries(ui_settings_tests PRIVATE bmhui)
set(BMH_UI_SETTINGS_SAVE "" CACHE FILEPATH "Optional Original settings capture save; never bundled")
bmh_add_data_test(NAME ui_settings_tests COMMAND ui_settings_tests "${BMH_DATA}" "${BMH_UI_SETTINGS_SAVE}")
add_executable(ui_advertising_tests advertising_tests.cpp ${BMH_APP_SESSION_SOURCE})
target_link_libraries(ui_advertising_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_advertising_tests COMMAND ui_advertising_tests "${BMH_DATA}")
add_executable(ui_environment_tests environment_tests.cpp)
target_link_libraries(ui_environment_tests PRIVATE bmhui)
set(BMH_TEST_UI_ENVIRONMENT_TESTS_INPUT_1 "" CACHE PATH "External input 1 for ui_environment_tests; not distributed")
bmh_add_data_test(NAME ui_environment_tests REQUIRED_OPTIONS BMH_TEST_UI_ENVIRONMENT_TESTS_INPUT_1 COMMAND ui_environment_tests "${BMH_DATA}"
    "${BMH_TEST_UI_ENVIRONMENT_TESTS_INPUT_1}")
add_executable(ui_achievements_tests achievements_tests.cpp)
target_link_libraries(ui_achievements_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_achievements_tests COMMAND ui_achievements_tests "${BMH_DATA}")
add_executable(ui_sprite_font_tests sprite_font_tests.cpp)
target_link_libraries(ui_sprite_font_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_sprite_font_tests COMMAND ui_sprite_font_tests "${BMH_DATA}")
if(MSVC)
    target_compile_options(bmhui PRIVATE /W4 /utf-8)
    target_compile_options(openbmh_ui PRIVATE /utf-8)
else()
    target_compile_options(bmhui PRIVATE -Wall -Wextra)
    target_compile_options(openbmh_ui PRIVATE -Wall -Wextra)
endif()

add_executable(ui_season_entrants_tests season_entrants_tests.cpp)
target_link_libraries(ui_season_entrants_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_season_entrants_tests COMMAND ui_season_entrants_tests "${BMH_DATA}")

add_executable(ui_game_end_alternate_tests ../../tests/game_end_alternate_tests.cpp)
target_link_libraries(ui_game_end_alternate_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_game_end_alternate_tests COMMAND ui_game_end_alternate_tests "${BMH_DATA}")

add_executable(ui_game_end_tax_animation_tests ../../tests/game_end_tax_animation_tests.cpp)
target_link_libraries(ui_game_end_tax_animation_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_game_end_tax_animation_tests REQUIRED_OPTIONS BMH_TEST_INPUTS REQUIRED_FILES tax-animation-script.bin COMMAND ui_game_end_tax_animation_tests "${BMH_DATA}")

add_executable(ui_game_end_tests game_end_tests.cpp)
target_link_libraries(ui_game_end_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_game_end_tests COMMAND ui_game_end_tests "${BMH_DATA}")

add_executable(ui_club_info_tests ../../tests/club_info_ui_tests.cpp)
target_link_libraries(ui_club_info_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_club_info_tests COMMAND ui_club_info_tests "${BMH_DATA}")

add_executable(ui_club_player_detail_tests ../../tests/club_player_detail_ui_tests.cpp)
target_link_libraries(ui_club_player_detail_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_club_player_detail_tests COMMAND ui_club_player_detail_tests "${BMH_DATA}")

add_executable(ui_team_statistics_tests ../../tests/team_statistics_tests.cpp)
target_link_libraries(ui_team_statistics_tests PRIVATE bmhui)
set(BMH_TEST_UI_TEAM_STATISTICS_TESTS_INPUT_1 "" CACHE PATH "External input 1 for ui_team_statistics_tests; not distributed")
bmh_add_data_test(NAME ui_team_statistics_tests REQUIRED_OPTIONS BMH_TEST_UI_TEAM_STATISTICS_TESTS_INPUT_1 COMMAND ui_team_statistics_tests "${BMH_DATA}" "${BMH_TEST_UI_TEAM_STATISTICS_TESTS_INPUT_1}")

add_executable(ui_team_statistics_source_oracle_tests ../../tests/team_statistics_source_oracle_tests.cpp)
target_link_libraries(ui_team_statistics_source_oracle_tests PRIVATE bmhui)
set(BMH_TEST_UI_TEAM_STATISTICS_SOURCE_ORACLE_TESTS_INPUT_1 "" CACHE PATH "External input 1 for ui_team_statistics_source_oracle_tests; not distributed")
bmh_add_data_test(NAME ui_team_statistics_source_oracle_tests REQUIRED_OPTIONS BMH_TEST_UI_TEAM_STATISTICS_SOURCE_ORACLE_TESTS_INPUT_1 COMMAND ui_team_statistics_source_oracle_tests "${BMH_DATA}" "${BMH_TEST_UI_TEAM_STATISTICS_SOURCE_ORACLE_TESTS_INPUT_1}")

add_executable(ui_pending_offer_tests ../../tests/pending_offer_ui_tests.cpp)
target_link_libraries(ui_pending_offer_tests PRIVATE bmhui)
set(BMH_TEST_UI_PENDING_OFFER_TESTS_INPUT_1 "" CACHE PATH "External input 1 for ui_pending_offer_tests; not distributed")
bmh_add_data_test(NAME ui_pending_offer_tests REQUIRED_OPTIONS BMH_TEST_UI_PENDING_OFFER_TESTS_INPUT_1 COMMAND ui_pending_offer_tests "${BMH_DATA}"
    "${BMH_TEST_UI_PENDING_OFFER_TESTS_INPUT_1}")

add_executable(ui_top_scorers_tests top_scorers_tests.cpp)
target_link_libraries(ui_top_scorers_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_top_scorers_tests COMMAND ui_top_scorers_tests "${BMH_DATA}")
add_executable(ui_opportunities_tests opportunities_tests.cpp)
target_link_libraries(ui_opportunities_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_opportunities_tests COMMAND ui_opportunities_tests "${BMH_DATA}")
add_executable(ui_statistics_rankings_tests statistics_rankings_tests.cpp)
target_link_libraries(ui_statistics_rankings_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_statistics_rankings_tests COMMAND ui_statistics_rankings_tests "${BMH_DATA}")
add_executable(ui_alltime_table_tests alltime_table_tests.cpp)
target_link_libraries(ui_alltime_table_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_alltime_table_tests COMMAND ui_alltime_table_tests "${BMH_DATA}")
add_executable(ui_world_cup_statistics_tests world_cup_statistics_tests.cpp)
target_link_libraries(ui_world_cup_statistics_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_world_cup_statistics_tests COMMAND ui_world_cup_statistics_tests "${BMH_DATA}")
add_executable(ui_finance_totals_tests finance_totals_tests.cpp)
target_link_libraries(ui_finance_totals_tests PRIVATE bmhui)
set(BMH_STATISTICS_FINANCE_TEST_SAVE "" CACHE FILEPATH "External save for finance totals tests")
bmh_add_data_test(NAME ui_finance_totals_tests REQUIRED_OPTIONS BMH_STATISTICS_FINANCE_TEST_SAVE
    COMMAND ui_finance_totals_tests "${BMH_DATA}" "${BMH_STATISTICS_FINANCE_TEST_SAVE}")
add_executable(ui_series_records_tests series_records_tests.cpp)
target_link_libraries(ui_series_records_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_series_records_tests COMMAND ui_series_records_tests "${BMH_DATA}")
add_executable(ui_analytics_tests analytics_tests.cpp)
target_link_libraries(ui_analytics_tests PRIVATE bmhui)
bmh_add_data_test(NAME ui_analytics_tests COMMAND ui_analytics_tests "${BMH_DATA}")
add_executable(minute_statistics_tests minute_statistics_tests.cpp)
target_link_libraries(minute_statistics_tests PRIVATE bmhui)
bmh_add_data_test(NAME minute_statistics_tests COMMAND minute_statistics_tests "${BMH_DATA}")
add_executable(transfer_history_tests transfer_history_tests.cpp)
target_link_libraries(transfer_history_tests PRIVATE bmhui)
bmh_add_data_test(NAME transfer_history_tests COMMAND transfer_history_tests "${BMH_DATA}")

add_executable(sportschau_ui_tests ../../tests/sportschau_ui_tests.cpp)
target_link_libraries(sportschau_ui_tests PRIVATE bmhui)
bmh_add_data_test(NAME sportschau_ui_tests COMMAND sportschau_ui_tests "${BMH_DATA}")
