# SPDX-License-Identifier: GPL-3.0-only
# A shared visual layer for the launcher and future modern screens.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
foreach(feature IN ITEMS ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${feature} ON CACHE BOOL "Keep the bundled font engine minimal" FORCE)
endforeach()
set(SKIP_INSTALL_ALL ON)
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3 GIT_SHALLOW TRUE GIT_SUBMODULES "")
FetchContent_MakeAvailable(freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG 6.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(rmlui)
add_library(bmh_rmlui STATIC
    ${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp
    ${CMAKE_SOURCE_DIR}/src/launch/rml_video.cpp)
if(WIN32)
    target_sources(bmh_rmlui PRIVATE ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_DX11.cpp)
    target_link_libraries(bmh_rmlui PUBLIC d3d11 dxgi d3dcompiler)
    target_compile_definitions(bmh_rmlui PUBLIC NUM_MSAA_SAMPLES=1 NOMINMAX)
else()
    target_sources(bmh_rmlui PRIVATE ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp)
endif()
target_include_directories(bmh_rmlui PUBLIC ${rmlui_SOURCE_DIR}/Backends ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(bmh_rmlui PUBLIC RMLUI_SDL_VERSION_MAJOR=2 RMLUI_NUM_MSAA_SAMPLES=0)
target_link_libraries(bmh_rmlui PUBLIC RmlUi::Core ${BMH_SDL_TARGET} PRIVATE ${CMAKE_DL_LIBS})

# The background is project artwork, and the unmodified fonts come from the
# pinned dependency. All assets are located relative to the executable at runtime.
set(launcher_assets "${CMAKE_BINARY_DIR}/assets/launcher")
file(MAKE_DIRECTORY "${launcher_assets}")
foreach(asset IN ITEMS launcher.rml theme.rcss stadium.tga)
    configure_file("${CMAKE_SOURCE_DIR}/assets/launcher/${asset}" "${launcher_assets}/${asset}" COPYONLY)
endforeach()
foreach(weight IN ITEMS Regular Bold)
    configure_file("${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-${weight}.ttf"
                   "${launcher_assets}/LatoLatin-${weight}.ttf" COPYONLY)
endforeach()
install(DIRECTORY "${launcher_assets}/" DESTINATION assets/launcher COMPONENT Runtime)
