cmake_minimum_required(VERSION 3.16)
project(AnGscheidrDiffer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Test)

# ---------------------------------------------------------------------------
# Diff engine — UI-free, separately testable
# ---------------------------------------------------------------------------
add_library(diffcore STATIC
    code/diff/diffengine.h
    code/diff/diffengine.cpp
    code/diff/linenormalizer.h
    code/diff/linenormalizer.cpp
    code/diff/commentstripper.h
    code/diff/commentstripper.cpp
    code/diff/intralinediff.h
    code/diff/intralinediff.cpp
    code/diff/foldercompare.h
    code/diff/foldercompare.cpp
    code/diff/mergeengine.h
    code/diff/mergeengine.cpp
    code/diff/merge3engine.h
    code/diff/merge3engine.cpp
)
target_link_libraries(diffcore PUBLIC Qt6::Core)
target_include_directories(diffcore PUBLIC code)

# ---------------------------------------------------------------------------
# GUI application
# ---------------------------------------------------------------------------
add_executable(angscheidrdiffer
    code/main.cpp
    data/icons.qrc
    code/ui/iconloader.h
    code/ui/appwindow.h
    code/ui/appwindow.cpp
    code/ui/comparetab.h
    code/ui/filedifftab.h
    code/ui/filedifftab.cpp
    code/ui/folderdifftab.h
    code/ui/folderdifftab.cpp
    code/ui/mergetab.h
    code/ui/mergetab.cpp
    code/ui/diffview.h
    code/ui/diffview.cpp
    code/ui/difftextedit.h
    code/ui/difftextedit.cpp
    code/ui/connectorwidget.h
    code/ui/connectorwidget.cpp
    code/ui/overviewbar.h
    code/ui/overviewbar.cpp
)
target_link_libraries(angscheidrdiffer PRIVATE diffcore Qt6::Widgets Qt6::Concurrent)

# The version is kept here only. Everything else derives it from this line:
# the application reports it, and the package script reads it from this file.
target_compile_definitions(angscheidrdiffer PRIVATE
                           APP_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Optional: syntax highlighting via KSyntaxHighlighting (KF6)
# ---------------------------------------------------------------------------
option(WITH_KSYNTAXHIGHLIGHTING
       "Enable syntax highlighting via KF6SyntaxHighlighting" ON)
if(WITH_KSYNTAXHIGHLIGHTING)
    find_package(KF6SyntaxHighlighting QUIET)
    if(KF6SyntaxHighlighting_FOUND)
        target_link_libraries(angscheidrdiffer PRIVATE KF6::SyntaxHighlighting)
        target_compile_definitions(angscheidrdiffer PRIVATE HAVE_KSYNTAX)
        message(STATUS "Syntax highlighting: enabled (KF6SyntaxHighlighting)")
    else()
        message(STATUS "Syntax highlighting: disabled (KF6SyntaxHighlighting not found)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Install: binary, launcher, icon, Dolphin service menu
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS angscheidrdiffer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/angscheidrdiffer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# Application icon: the PNG set from graphics/logo/ into every hicolor size.
foreach(size 16 22 24 32 48 64 96 128 192 256 512)
    install(FILES graphics/logo/diff_${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME angscheidrdiffer.png)
endforeach()
install(FILES data/compare-angscheidrdiffer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus)
# AppStream: without this file, software centres such as Discover show
# neither author nor licence. They do not read DEBIAN/control.
install(FILES data/de.unifyzer.angscheidrdiffer.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# Debian policy: every package places its licence here.
install(FILES data/copyright
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/angscheidrdiffer)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(tst_diffengine tests/tst_diffengine.cpp)
target_link_libraries(tst_diffengine PRIVATE diffcore Qt6::Test)
add_test(NAME diffengine COMMAND tst_diffengine)

add_executable(tst_mergeengine tests/tst_mergeengine.cpp)
target_link_libraries(tst_mergeengine PRIVATE diffcore Qt6::Test)
add_test(NAME mergeengine COMMAND tst_mergeengine)

add_executable(tst_merge3engine tests/tst_merge3engine.cpp)
target_link_libraries(tst_merge3engine PRIVATE diffcore Qt6::Test)
add_test(NAME merge3engine COMMAND tst_merge3engine)
