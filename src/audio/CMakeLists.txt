# SPDX-License-Identifier: GPL-3.0-only
add_library(openbmh_audio STATIC container.cpp playback.cpp semantic_cues.cpp music_fallback.cpp mixer.cpp
    original.cpp third_party/nuked-opl3/opl3.c)
target_include_directories(openbmh_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openbmh_audio PUBLIC ${BMH_SDL_TARGET})

add_executable(openbmh_sound sound_driver.cpp)
target_link_libraries(openbmh_sound PRIVATE openbmh_audio bmhcore)

add_executable(openbmh_original_audio_validation original_validation.cpp)
target_link_libraries(openbmh_original_audio_validation PRIVATE openbmh_audio bmhcore)
bmh_add_data_test(NAME openbmh_original_audio_validation COMMAND openbmh_original_audio_validation "${BMH_DATA}")
set_tests_properties(openbmh_original_audio_validation PROPERTIES
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy")

add_executable(openbmh_audio_validation validation.cpp)
target_link_libraries(openbmh_audio_validation PRIVATE openbmh_audio)
add_test(NAME openbmh_audio_validation COMMAND openbmh_audio_validation)
set_tests_properties(openbmh_audio_validation PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy")

# SDL.h renames main to SDL_main on Windows: the two programs need SDL2main for the entry point
if(TARGET SDL2::SDL2main)
    target_link_libraries(openbmh_sound PRIVATE SDL2::SDL2main)
    target_link_libraries(openbmh_audio_validation PRIVATE SDL2::SDL2main)
    target_link_libraries(openbmh_original_audio_validation PRIVATE SDL2::SDL2main)
endif()
