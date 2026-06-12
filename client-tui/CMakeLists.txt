set(CLIENT_TUI_VERSION "0.1.0")

include(FetchContent)
FetchContent_Declare(
        ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
        GIT_TAG v5.0.0
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ftxui)

add_executable(SonyHeadphonesClientTUI
        main.cpp
        Connection.cpp
        Panels.cpp
        Controls.cpp
)

target_link_libraries(SonyHeadphonesClientTUI
        PRIVATE
        mdr
        ftxui::screen
        ftxui::dom
        ftxui::component
)

target_compile_definitions(SonyHeadphonesClientTUI PRIVATE
        -DCLIENT_TUI_VERSION="${CLIENT_TUI_VERSION}"
        -DMDR_PLATFORM_OS="${MDR_PLATFORM_OS}"
)

# macOS-only system-audio visualizer (CoreAudio process tap + vDSP FFT).
# .mm compiles under the CXX rule like libmdr's PlatformMacOS.mm — clang
# picks ObjC++ from the extension; no OBJCXX language needed.
if (MDR_PLATFORM_OS STREQUAL "MACOS")
    target_sources(SonyHeadphonesClientTUI PRIVATE AudioTap_macOS.mm)
    set_source_files_properties(AudioTap_macOS.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_compile_definitions(SonyHeadphonesClientTUI PRIVATE TUI_HAS_AUDIO_TAP)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(SonyHeadphonesClientTUI PRIVATE
            ${COREAUDIO_FRAMEWORK}
            ${ACCELERATE_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
    )
endif ()

# Typable binary name; the CMake target keeps its descriptive name.
set_target_properties(SonyHeadphonesClientTUI PROPERTIES OUTPUT_NAME sonytui)

install(TARGETS SonyHeadphonesClientTUI RUNTIME DESTINATION bin)
