cmake_minimum_required(VERSION 3.5)

project(btrfs-assistant VERSION 1.5 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOUIC_SEARCH_PATHS src/ui)

find_package(QT NAMES Qt5 COMPONENTS Widgets LinguistTools REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets LinguistTools REQUIRED)

find_library(BTRFSUTIL_LIB btrfsutil)

file(GLOB TS_FILES ${PROJECT_SOURCE_DIR}/translations/*.ts)

configure_file(src/config.h.in config.h @ONLY)

set(PROJECT_SOURCES
        src/main.cpp
        src/ui/btrfs-assistant.ui
        data/icons.qrc
        src/Btrfs.h src/Btrfs.cpp
        src/BtrfsAssistant.h src/BtrfsAssistant.cpp
        src/BtrfsMaintenance.h src/BtrfsMaintenance.cpp
        src/Cli.h src/Cli.cpp
        src/DiffViewer.h src/DiffViewer.cpp src/ui/DiffViewer.ui
        src/FileBrowser.h src/FileBrowser.cpp src/ui/FileBrowser.ui
        src/Settings.h src/Settings.cpp
        src/Snapper.h src/Snapper.cpp
        src/models/SubvolModel.h src/models/SubvolModel.cpp
        src/System.h src/System.cpp
        widgets/FilterLineEdit.h widgets/FilterLineEdit.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

qt5_create_translation(FILES_TS ${PROJECT_SOURCES} ${TS_FILES})

add_executable(btrfs-assistant-bin
    ${PROJECT_SOURCES} ${FILES_TS}
)

install(FILES ${FILES_TS} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/btrfs-assistant/translations)
install(FILES data/btrfs-assistant.conf DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR})
install(FILES data/btrfs-assistant.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES data/assets/btrfs-assistant.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps/)
install(FILES data/org.btrfs-assistant.pkexec.policy DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/polkit-1/actions/)
install(PROGRAMS data/btrfs-assistant DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS btrfs-assistant-bin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

target_link_libraries(btrfs-assistant-bin PRIVATE Qt${QT_VERSION_MAJOR}::Widgets ${BTRFSUTIL_LIB})
