cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Version
# ============================================================================
# Einzige Quelle ist der oberste nummerierte Abschnitt in CHANGELOG.md.
# "## [Unreleased]" wird uebersprungen, weil das Muster Ziffern verlangt.
# CMAKE_CONFIGURE_DEPENDS sorgt dafuer, dass eine Aenderung am CHANGELOG die
# Konfiguration neu anstoesst - sonst bliebe die einkompilierte Version stehen.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md" _oc_changelog)
string(REGEX MATCH "\n## \\[([0-9]+\\.[0-9]+\\.[0-9]+)\\]" _oc_unused "${_oc_changelog}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Keine Version in CHANGELOG.md gefunden. Erwartet wird eine Zeile der Form '## [1.4.0] - JJJJ-MM-TT'.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md")

project(openCirt VERSION ${CMAKE_MATCH_1})
message(STATUS "openCirt Version (aus CHANGELOG.md): ${PROJECT_VERSION}")

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============================================================================
# Plattform-Pfade definieren
# ============================================================================
# Die Vorgaben lassen sich ohne Aenderung dieser Datei ueberschreiben:
#   cmake -DQT6_DIR=... -DBRICSCAD_DIR=... -DOC_SYSROOT=...
# oder ueber gleichnamige Umgebungsvariablen (siehe CLEAN_BUILD.sh).
set(BRX_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/brx_sdk")
set(BRX_INC_DIR "${BRX_SDK_PATH}/inc")

if(WIN32)
    set(_oc_qt6_default "C:/Qt/6.8.3/msvc2022_64")
    set(_oc_bricscad_default "C:/Program Files/Bricsys/BricsCAD V26 de_DE")
    # Import-Bibliotheken (brx26.lib) liegen im SDK
    set(BRX_LIB_DIR "${BRX_SDK_PATH}/lib64")
else()
    # Qt muss zur Version passen, die BricsCAD mitbringt (V26: Qt 6.8.2).
    # Das Plugin laeuft im BricsCAD-Prozess und benutzt dessen Qt-Bibliotheken.
    set(_oc_qt6_default "$ENV{HOME}/.local/opt/opencirt-toolchain/Qt/6.8.2/gcc_64")
    set(_oc_bricscad_default "/opt/bricsys/bricscad/v26")
    # Optional: Verzeichnis mit lokal entpackten Entwicklerpaketen
    # (OpenGL-/xkbcommon-Header), falls sie nicht systemweit installiert sind.
    set(_oc_sysroot_default "$ENV{HOME}/.local/opt/opencirt-toolchain/sysroot")
endif()

foreach(_oc_var QT6_DIR BRICSCAD_DIR OC_SYSROOT)
    if(NOT DEFINED ${_oc_var} AND DEFINED ENV{${_oc_var}})
        set(${_oc_var} "$ENV{${_oc_var}}")
    endif()
endforeach()
if(NOT DEFINED QT6_DIR)
    set(QT6_DIR "${_oc_qt6_default}")
endif()
if(NOT DEFINED BRICSCAD_DIR)
    set(BRICSCAD_DIR "${_oc_bricscad_default}")
endif()
if(NOT WIN32)
    if(NOT DEFINED OC_SYSROOT)
        set(OC_SYSROOT "${_oc_sysroot_default}")
    endif()
    # Unter Linux wird direkt gegen die Bibliotheken der Installation gelinkt
    # (libbrx26.so, libdrx_entrypoint.a, libTD_*.so)
    set(BRX_LIB_DIR "${BRICSCAD_DIR}")
endif()

# Prüfungen
if(NOT EXISTS ${BRX_SDK_PATH})
    message(FATAL_ERROR "BRX SDK not found at ${BRX_SDK_PATH}")
endif()

if(NOT EXISTS ${QT6_DIR})
    message(FATAL_ERROR "Qt6 not found at ${QT6_DIR}")
endif()

if(NOT EXISTS ${BRICSCAD_DIR})
    message(FATAL_ERROR "BricsCAD V26 not found at ${BRICSCAD_DIR}")
endif()

message(STATUS "✅ Qt6: ${QT6_DIR}")
message(STATUS "✅ BRX SDK: ${BRX_SDK_PATH}")
message(STATUS "✅ BricsCAD V26: ${BRICSCAD_DIR}")

# ============================================================================
# Linux: Grundeinstellungen
# ============================================================================
if(NOT WIN32)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # Nur der BRX-Einstiegspunkt wird exportiert. Alles andere bleibt intern,
    # damit gleichnamige Symbole des Hosts nicht mit den eigenen kollidieren.
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

    # Kein RPATH auf das Qt-SDK: zur Laufzeit gelten die Bibliotheken, die
    # BricsCAD bereits geladen hat.
    set(CMAKE_SKIP_RPATH ON)

    if(EXISTS "${OC_SYSROOT}/usr/include")
        message(STATUS "✅ Lokale Entwicklerpakete: ${OC_SYSROOT}")
        list(APPEND CMAKE_PREFIX_PATH "${OC_SYSROOT}/usr")
        list(APPEND CMAKE_INCLUDE_PATH "${OC_SYSROOT}/usr/include")
        list(APPEND CMAKE_LIBRARY_PATH "${OC_SYSROOT}/usr/lib/x86_64-linux-gnu")
    endif()
endif()

# ============================================================================
# Qt6 Setup
# ============================================================================
list(PREPEND CMAKE_PREFIX_PATH "${QT6_DIR}")
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ============================================================================
# Windows Fix Header - manually maintained for Qt 6.8+ compatibility
# ============================================================================
# Der Header ist auf allen Plattformen vorhanden; sein Inhalt ist vollstaendig
# von _WIN32 abhaengig und unter Linux leer.
set(WINDOWS_FIX_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/windows_fix.h")
if(NOT EXISTS "${WINDOWS_FIX_HEADER}")
    message(FATAL_ERROR "windows_fix.h not found at ${WINDOWS_FIX_HEADER}")
endif()
if(WIN32)
    message(STATUS "✅ Using manual windows_fix.h: ${WINDOWS_FIX_HEADER}")
endif()

# ============================================================================
# Include Directories
# ============================================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src  # For windows_fix.h
)
if(WIN32)
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/mfc_stubs  # Empty MFC/ATL stubs for Qt-based plugin (no MFC needed)
    )
endif()
include_directories(
    ${BRX_INC_DIR}
)
if(NOT WIN32)
    # CRITICAL: Platform/substitutes contains EMPTY stubs for windows.h, intrin.h, windef.h
    # These are ONLY for Linux/Mac builds and MUST NOT be used on Windows!
    # They shadow the real Windows SDK headers and cause all CALLBACK/HANDLE/intrinsics errors.
    include_directories(
        ${BRX_INC_DIR}/Platform/substitutes
    )
endif()
include_directories(
    ${BRX_INC_DIR}/Platform/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)

# ============================================================================
# Source Files
# ============================================================================
set(PLUGIN_SOURCES
    src/plugin/OpenCirtPlugin.cpp
    src/core/ProjectBuilder.cpp
    src/core/OpenCirtEngine.cpp
    src/ui/OpenCirtWindow.cpp
    src/ui/OpenCirtTab.cpp
    src/ui/BasConfigDialog.cpp
    src/ui/Theming.cpp
    src/utils/SensorKeywordLoader.cpp
    src/utils/CsvListWriter.cpp
)

set(PLUGIN_HEADERS
    src/plugin/OpenCirtPlugin.h
    src/core/ProjectBuilder.h
    src/core/OpenCirtEngine.h
    src/core/TextAlignment.h
    src/ui/OpenCirtWindow.h
    src/ui/OpenCirtTab.h
    src/ui/BasConfigDialog.h
    src/ui/Theming.h
    src/utils/SensorKeywordLoader.h
    src/utils/CsvListWriter.h
)

# ============================================================================
# BricsCAD Libraries
# ============================================================================
if(WIN32)
    find_library(BRX26_LIB NAMES brx26.lib PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(BRXHLRAPI26_LIB NAMES BrxHlrApi26.lib PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH)
    find_library(BRXATIL26_LIB NAMES BrxATIL26.lib PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH)
else()
    find_library(BRX26_LIB NAMES brx26 PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(BRXHLRAPI26_LIB NAMES brxhlrapi26 PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH)
    find_library(BRXATIL26_LIB NAMES brxatil26 PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH)
    # Teigha-Basisbibliotheken (Speicherverwaltung, Laufzeitkern), siehe LinuxBuild.txt
    find_library(TD_ALLOC_LIB NAMES TD_Alloc PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(TD_ROOT_LIB NAMES TD_Root PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    # Statischer Einstiegspunkt: muss vollstaendig in das Modul uebernommen werden
    set(DRX_ENTRYPOINT_LIB "${BRX_LIB_DIR}/libdrx_entrypoint.a")
    if(NOT EXISTS "${DRX_ENTRYPOINT_LIB}")
        message(FATAL_ERROR "libdrx_entrypoint.a not found at ${DRX_ENTRYPOINT_LIB}")
    endif()
endif()

message(STATUS "✅ BRX26: ${BRX26_LIB}")
if(BRXHLRAPI26_LIB)
    message(STATUS "✅ BRXHLRAPI26: ${BRXHLRAPI26_LIB}")
endif()
if(BRXATIL26_LIB)
    message(STATUS "✅ BRXATIL26: ${BRXATIL26_LIB}")
endif()

# ============================================================================
# Target
# ============================================================================
add_library(opencirt_plugin SHARED
    ${PLUGIN_SOURCES}
    ${PLUGIN_HEADERS}
)

# ============================================================================
# Compiler Definitions
# ============================================================================
target_compile_definitions(opencirt_plugin PRIVATE
    PLUGIN_VERSION="${PROJECT_VERSION}"
    _BRXEXPORT
    __BRXTARGET
    _UNICODE
    UNICODE
    BRXAPI
)
if(WIN32)
    target_compile_definitions(opencirt_plugin PRIVATE
        WINDOWS
        _WINDOWS
        WIN64
        _WIN64
        NOMINMAX
    )
else()
    target_compile_definitions(opencirt_plugin PRIVATE
        BRX_APP
        _ACRXAPP
        _FILE_OFFSET_BITS=64
        _LARGE_FILES
    )
endif()

# ============================================================================
# CRITICAL: Force-include windows_fix.h for ALL compilations (including MOC)
# ============================================================================
if(MSVC)
    target_compile_options(opencirt_plugin PRIVATE
        /W3
        /MP
        /EHsc
        /permissive-
        /Zc:__cplusplus
        /wd4251
        /wd4275
        /FI"${CMAKE_CURRENT_SOURCE_DIR}/src/windows_fix.h"
    )

    set_target_properties(opencirt_plugin PROPERTIES
        VS_PLATFORM_TOOLSET "v143"
    )

    message(STATUS "")
    message(STATUS "🔧 ===============================================")
    message(STATUS "🔧 Qt 6.8+ Windows Header Fix ACTIVE")
    message(STATUS "🔧 Method: /FI force-include on TARGET level")
    message(STATUS "🔧 Applies to: ALL files including MOC-generated")
    message(STATUS "🔧 ===============================================")
    message(STATUS "")
elseif(NOT WIN32)
    target_compile_options(opencirt_plugin PRIVATE
        -Wall
        # Die BRX-Header loesen diese Warnungen in grosser Zahl aus
        -Wno-unknown-pragmas
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-deprecated-declarations
    )
endif()

# ============================================================================
# Link Libraries
# ============================================================================
set(BRX_LIBRARIES ${BRX26_LIB})
if(BRXHLRAPI26_LIB)
    list(APPEND BRX_LIBRARIES ${BRXHLRAPI26_LIB})
endif()
if(BRXATIL26_LIB)
    list(APPEND BRX_LIBRARIES ${BRXATIL26_LIB})
endif()
if(NOT WIN32)
    list(APPEND BRX_LIBRARIES ${TD_ALLOC_LIB} ${TD_ROOT_LIB})
endif()

# Add BRX lib directory to linker search path (for #pragma comment(lib,...) in BRX headers)
target_link_directories(opencirt_plugin PRIVATE
    ${BRX_LIB_DIR}
)

if(NOT WIN32)
    target_link_options(opencirt_plugin PRIVATE
        # Exporte des Einstiegspunkts (odrxCreateModuleObject ...) vollstaendig uebernehmen
        "LINKER:--whole-archive,${DRX_ENTRYPOINT_LIB},--no-whole-archive"
        # Fehlende Symbole beim Linken melden, nicht erst beim Laden in BricsCAD
        "LINKER:--no-undefined"
        # Abhaengigkeiten von libbrx26.so in der Installation aufloesen
        "LINKER:-rpath-link,${BRX_LIB_DIR}"
    )
endif()

target_link_libraries(opencirt_plugin PRIVATE
    ${BRX_LIBRARIES}
    Qt6::Core
    Qt6::Widgets
)
if(NOT WIN32)
    target_link_libraries(opencirt_plugin PRIVATE ${CMAKE_DL_LIBS})
endif()

# ============================================================================
# Output Configuration
# ============================================================================
# Windows: opencirt-<Version>.brx, Linux: opencirt-<Version>.lrx (Linux
# Runtime eXtension). Die Version steht im Dateinamen, damit sich die
# Staende auseinanderhalten lassen.
if(WIN32)
    set(_oc_module_suffix ".brx")
    set(_oc_dest_dir "${CMAKE_CURRENT_SOURCE_DIR}/sample_project/00- BricsCAD Plugin/00- Windows Version")
else()
    set(_oc_module_suffix ".lrx")
    set(_oc_dest_dir "${CMAKE_CURRENT_SOURCE_DIR}/sample_project/00- BricsCAD Plugin/01- Linux Version")
endif()
set(_oc_module_name "opencirt-${PROJECT_VERSION}")

set_target_properties(opencirt_plugin PROPERTIES
    OUTPUT_NAME "${_oc_module_name}"
    SUFFIX "${_oc_module_suffix}"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)

# ============================================================================
# Status
# ============================================================================
message(STATUS "")
message(STATUS "🎯 ==========================================")
message(STATUS "🎯 openCirt Plugin - ${CMAKE_SYSTEM_NAME}")
message(STATUS "🎯 ==========================================")
message(STATUS "📦 Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "📁 Qt6: ${QT6_DIR}")
message(STATUS "📁 BRX SDK: ${BRX_SDK_PATH}")
message(STATUS "🎯 Output: ${_oc_module_name}${_oc_module_suffix}")
message(STATUS "🎯 ==========================================")
message(STATUS "")

# Nach dem Bauen: die fertige Datei ins Beispielprojekt legen. Aeltere
# Staende dort werden entfernt, damit immer genau eine Version dort liegt.
add_custom_command(TARGET opencirt_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DOC_MODULE=$<TARGET_FILE:opencirt_plugin>
        -DOC_DEST_DIR=${_oc_dest_dir}
        -DOC_PREFIX=opencirt
        -DOC_SUFFIX=${_oc_module_suffix}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/copy_plugin.cmake"
    COMMAND ${CMAKE_COMMAND} -E echo "Build complete: ${_oc_module_name}${_oc_module_suffix}"
    COMMAND ${CMAKE_COMMAND} -E echo "Load with: APPLOAD in BricsCAD V26, command OPENCIRT (OC)"
    VERBATIM
)
