set(CLIENT_MENUBAR_VERSION "0.1.0")

# Menu bar client. Objective-C++ talking to libmdr's C++ API directly (the C
# FFI intentionally lacks settings — same reason the TUI is C++). Reuses the
# TUI's UI-agnostic connection shim by compiling the same source file.
add_executable(SonyHeadphonesMenuBar MACOSX_BUNDLE
        main.mm
        MenuController.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/../client-tui/Connection.cpp
)

# App icon: copy the .icns into the bundle's Resources/ (Info.plist points at it
# via MACOSX_BUNDLE_ICON_FILE below).
set(MENUBAR_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns)
target_sources(SonyHeadphonesMenuBar PRIVATE ${MENUBAR_ICON})
set_source_files_properties(${MENUBAR_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

target_include_directories(SonyHeadphonesMenuBar PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../client-tui
)

set_source_files_properties(main.mm MenuController.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)

find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(SERVICEMANAGEMENT_FRAMEWORK ServiceManagement REQUIRED)

target_link_libraries(SonyHeadphonesMenuBar
        PRIVATE
        mdr
        ${APPKIT_FRAMEWORK}
        ${SERVICEMANAGEMENT_FRAMEWORK}
)

target_compile_definitions(SonyHeadphonesMenuBar PRIVATE
        -DCLIENT_MENUBAR_VERSION="${CLIENT_MENUBAR_VERSION}"
)

set_target_properties(SonyHeadphonesMenuBar PROPERTIES
        OUTPUT_NAME "Sony Headphones Bar"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "Sony Headphones Bar"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.sonyheadphones.menubar"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CLIENT_MENUBAR_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CLIENT_MENUBAR_VERSION}"
)
