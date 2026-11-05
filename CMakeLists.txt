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
# BricsCAD-Version
# ============================================================================
# Gebaut wird fuer eine BricsCAD-Hauptversion: 26 oder 27 (Vorgabe). Ein
# Plugin laedt nur in der Version, gegen deren SDK es gebaut ist. Umschalten
# mit cmake -DBRICSCAD_VERSION=26 oder der Umgebungsvariable BRICSCAD_VERSION.
# Davon haengen ab: SDK-Ordner, Bibliotheksnamen (brx<NN>), die Vorgaben fuer
# Qt und die BricsCAD-Installation und der Dateiname (...-v<NN>).
if(NOT DEFINED BRICSCAD_VERSION AND DEFINED ENV{BRICSCAD_VERSION})
    set(BRICSCAD_VERSION "$ENV{BRICSCAD_VERSION}")
endif()
if(NOT DEFINED BRICSCAD_VERSION)
    set(BRICSCAD_VERSION 27)
endif()
# Qt muss zur Version passen, die BricsCAD mitbringt (V26: 6.8.2, V27: 6.10.2,
# unter Windows wie unter Linux); das Plugin benutzt zur Laufzeit die
# Qt-Bibliotheken von BricsCAD. Patch-Versionen derselben Reihe sind
# binaerkompatibel: unter Windows wird mit 6.8.3 bzw. 6.10.3 gebaut, weil der
# Qt-Installer die .2-Staende nicht mehr anbietet.
if(BRICSCAD_VERSION STREQUAL "26")
    set(_oc_qt_linux "6.8.2")
    set(_oc_qt_windows "6.8.3")
    set(_oc_bricscad_windows "C:/Program Files/Bricsys/BricsCAD V26 de_DE")
elseif(BRICSCAD_VERSION STREQUAL "27")
    set(_oc_qt_linux "6.10.2")
    set(_oc_qt_windows "6.10.3")
    # V27 gibt es bisher nur auf Englisch
    set(_oc_bricscad_windows "C:/Program Files/Bricsys/BricsCAD V27 en_US")
else()
    message(FATAL_ERROR "BRICSCAD_VERSION=${BRICSCAD_VERSION} wird nicht unterstuetzt (26 oder 27).")
endif()

# ============================================================================
# Plattform-Pfade definieren
# ============================================================================
# Die Vorgaben lassen sich ohne Aenderung dieser Datei ueberschreiben:
#   cmake -DQT6_DIR=... -DBRICSCAD_DIR=... -DOC_SYSROOT=...
# oder ueber gleichnamige Umgebungsvariablen (siehe CLEAN_BUILD.sh).
# BRX SDK: Vorgabe ist external/brx_sdk_v<NN> im Projekt, sonst
# external/brx_sdk. BRX_SDK_DIR (CMake-Variable oder Umgebungsvariable) zeigt
# auf ein SDK an anderer Stelle, z.B. ein gemeinsames fuer openCirt und
# batchTool. Ob das SDK zur gewaehlten Version passt, prueft brx_version.h.
if(NOT DEFINED BRX_SDK_DIR AND DEFINED ENV{BRX_SDK_DIR})
    set(BRX_SDK_DIR "$ENV{BRX_SDK_DIR}")
endif()
if(DEFINED BRX_SDK_DIR AND NOT "${BRX_SDK_DIR}" STREQUAL "")
    set(BRX_SDK_PATH "${BRX_SDK_DIR}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/brx_sdk_v${BRICSCAD_VERSION}/inc")
    set(BRX_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/brx_sdk_v${BRICSCAD_VERSION}")
else()
    set(BRX_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/brx_sdk")
endif()
set(BRX_INC_DIR "${BRX_SDK_PATH}/inc")

if(WIN32)
    set(_oc_qt6_default "C:/Qt/${_oc_qt_windows}/msvc2022_64")
    set(_oc_bricscad_default "${_oc_bricscad_windows}")
    # Import-Bibliotheken (brx<NN>.lib) liegen im SDK
    set(BRX_LIB_DIR "${BRX_SDK_PATH}/lib64")
else()
    # Das Plugin laeuft im BricsCAD-Prozess und benutzt dessen Qt-Bibliotheken.
    set(_oc_qt6_default "$ENV{HOME}/.local/opt/opencirt-toolchain/Qt/${_oc_qt_linux}/gcc_64")
    set(_oc_bricscad_default "/opt/bricsys/bricscad/v${BRICSCAD_VERSION}")
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
    # (libbrx<NN>.so, libdrx_entrypoint.a, libTD_*.so)
    set(BRX_LIB_DIR "${BRICSCAD_DIR}")
endif()

# Prüfungen
if(NOT EXISTS "${BRX_SDK_PATH}/inc")
    message(FATAL_ERROR "BRX SDK nicht gefunden: ${BRX_SDK_PATH}\nDas SDK fuer V${BRICSCAD_VERSION} (Ordner mit inc/ und lib64/) nach external/brx_sdk_v${BRICSCAD_VERSION} kopieren oder BRX_SDK_DIR setzen: cmake -DBRX_SDK_DIR=<Pfad> oder Umgebungsvariable BRX_SDK_DIR.")
endif()
file(STRINGS "${BRX_INC_DIR}/brx_version.h" _oc_brxver REGEX "define[ \t]+__BRXVER[ \t]+[0-9]+")
string(REGEX MATCH "__BRXVER[ \t]+([0-9]+)" _oc_unused "${_oc_brxver}")
if(NOT CMAKE_MATCH_1 STREQUAL BRICSCAD_VERSION)
    message(FATAL_ERROR "Das BRX SDK unter ${BRX_SDK_PATH} gehoert zu V${CMAKE_MATCH_1}, gebaut werden soll fuer V${BRICSCAD_VERSION}.\nDas SDK fuer V${BRICSCAD_VERSION} nach external/brx_sdk_v${BRICSCAD_VERSION} legen, BRX_SDK_DIR darauf setzen oder BRICSCAD_VERSION anpassen.")
endif()

if(NOT EXISTS ${QT6_DIR})
    message(FATAL_ERROR "Qt6 not found at ${QT6_DIR}")
endif()

# Unter Windows wird BricsCAD selbst nicht gebraucht (die Import-Bibliotheken
# liegen im SDK); beim Windows-Build unter Linux (CROSS_BUILD_WINDOWS.sh) gibt
# es den Ordner nicht.
if(WIN32 AND CMAKE_CROSSCOMPILING)
    set(_oc_need_bricscad FALSE)
else()
    set(_oc_need_bricscad TRUE)
endif()
if(_oc_need_bricscad AND NOT EXISTS ${BRICSCAD_DIR})
    message(FATAL_ERROR "BricsCAD V${BRICSCAD_VERSION} not found at ${BRICSCAD_DIR}")
endif()

message(STATUS "✅ Qt6: ${QT6_DIR}")
message(STATUS "✅ BRX SDK: ${BRX_SDK_PATH}")
if(_oc_need_bricscad)
    message(STATUS "✅ BricsCAD V${BRICSCAD_VERSION}: ${BRICSCAD_DIR}")
endif()

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
# Windows-Build unter Linux (CROSS_BUILD_WINDOWS.sh): das Windows-Qt bringt
# moc/uic/rcc als .exe mit; benutzt werden die Werkzeuge des Linux-Qt derselben
# Reihe aus QT_HOST_PATH (gleiche moc-Ausgabe innerhalb einer Qt-Reihe).
if(WIN32 AND CMAKE_CROSSCOMPILING)
    if(NOT DEFINED QT_HOST_PATH OR NOT EXISTS "${QT_HOST_PATH}/libexec/moc")
        message(FATAL_ERROR "QT_HOST_PATH muss auf ein Linux-Qt mit libexec/moc zeigen (CROSS_BUILD_WINDOWS.sh).")
    endif()
    foreach(_oc_tool moc uic rcc)
        if(TARGET Qt6::${_oc_tool})
            set_target_properties(Qt6::${_oc_tool} PROPERTIES
                IMPORTED_LOCATION "${QT_HOST_PATH}/libexec/${_oc_tool}"
                IMPORTED_LOCATION_RELEASE "${QT_HOST_PATH}/libexec/${_oc_tool}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${QT_HOST_PATH}/libexec/${_oc_tool}"
                IMPORTED_LOCATION_DEBUG "${QT_HOST_PATH}/libexec/${_oc_tool}")
        endif()
    endforeach()
    message(STATUS "✅ Qt-Werkzeuge (moc/uic/rcc): ${QT_HOST_PATH}/libexec")
endif()
# Linux: Qt-Version mit der von BricsCAD vergleichen (libQt6Core.so.6.x.y)
if(NOT WIN32)
    file(GLOB _oc_bcad_qt "${BRICSCAD_DIR}/libQt6Core.so.6.*.*")
    if(_oc_bcad_qt)
        list(GET _oc_bcad_qt 0 _oc_bcad_qt)
        string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+$" _oc_bcad_qt "${_oc_bcad_qt}")
        if(NOT _oc_bcad_qt STREQUAL Qt6_VERSION)
            message(WARNING "Qt ${Qt6_VERSION} aus ${QT6_DIR}, BricsCAD V${BRICSCAD_VERSION} bringt Qt ${_oc_bcad_qt} mit. Das Plugin muss gegen dieselbe Version gebaut werden (QT6_DIR).")
        endif()
    endif()
endif()

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
    src/core/Leistungsliste.cpp
    src/ui/OpenCirtWindow.cpp
    src/ui/OpenCirtTab.cpp
    src/ui/BasConfigDialog.cpp
    src/ui/SeitennamenConfigDialog.cpp
    src/ui/Theming.cpp
    src/utils/SensorKeywordLoader.cpp
    src/utils/CsvListWriter.cpp
)

set(PLUGIN_HEADERS
    src/plugin/OpenCirtPlugin.h
    src/core/ProjectBuilder.h
    src/core/OpenCirtEngine.h
    src/core/Leistungsliste.h
    src/core/TextAlignment.h
    src/ui/OpenCirtWindow.h
    src/ui/OpenCirtTab.h
    src/ui/BasConfigDialog.h
    src/ui/SeitennamenConfigDialog.h
    src/ui/Theming.h
    src/utils/SensorKeywordLoader.h
    src/utils/CsvListWriter.h
)

# ============================================================================
# BricsCAD Libraries
# ============================================================================
set(_v ${BRICSCAD_VERSION})
if(WIN32)
    find_library(BRX_LIB NAMES brx${_v}.lib PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(BRXHLRAPI_LIB NAMES BrxHlrApi${_v}.lib PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH)
    find_library(BRXATIL_LIB NAMES BrxATIL${_v}.lib PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH)
else()
    find_library(BRX_LIB NAMES brx${_v} PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(BRXHLRAPI_LIB NAMES brxhlrapi${_v} PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH)
    find_library(BRXATIL_LIB NAMES brxatil${_v} PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH)
    # Teigha-Basisbibliotheken (Speicherverwaltung, Laufzeitkern), siehe LinuxBuild.txt
    find_library(TD_ALLOC_LIB NAMES TD_Alloc PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(TD_ROOT_LIB NAMES TD_Root PATHS ${BRX_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    # Statischer Einstiegspunkt: muss vollstaendig in das Modul uebernommen werden
    set(DRX_ENTRYPOINT_LIB "${BRX_LIB_DIR}/libdrx_entrypoint.a")
    if(NOT EXISTS "${DRX_ENTRYPOINT_LIB}")
        message(FATAL_ERROR "libdrx_entrypoint.a not found at ${DRX_ENTRYPOINT_LIB}")
    endif()
endif()

message(STATUS "✅ BRX: ${BRX_LIB}")
if(BRXHLRAPI_LIB)
    message(STATUS "✅ BRXHLRAPI: ${BRXHLRAPI_LIB}")
endif()
if(BRXATIL_LIB)
    message(STATUS "✅ BRXATIL: ${BRXATIL_LIB}")
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
    BRICSCAD_VERSION_STR="${BRICSCAD_VERSION}"
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
    )
    if(CMAKE_CROSSCOMPILING)
        # clang-cl unter Linux (Ninja): CMake setzt die Anfuehrungszeichen
        # selbst, eigene kaemen als Teil des Dateinamens an
        target_compile_options(opencirt_plugin PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/windows_fix.h"
        )
    else()
        target_compile_options(opencirt_plugin PRIVATE
            /FI"${CMAKE_CURRENT_SOURCE_DIR}/src/windows_fix.h"
        )
    endif()

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
set(BRX_LIBRARIES ${BRX_LIB})
if(BRXHLRAPI_LIB)
    list(APPEND BRX_LIBRARIES ${BRXHLRAPI_LIB})
endif()
if(BRXATIL_LIB)
    list(APPEND BRX_LIBRARIES ${BRXATIL_LIB})
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
        # Abhaengigkeiten von libbrx<NN>.so in der Installation aufloesen
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
# Windows: opencirt-<Version>-v<NN>.brx, Linux: opencirt-<Version>-v<NN>.lrx
# (Linux Runtime eXtension, NN = BricsCAD-Version). Version und BricsCAD-
# Version stehen im Dateinamen, damit sich die Staende auseinanderhalten lassen.
if(WIN32)
    set(_oc_module_suffix ".brx")
    set(_oc_dest_dir "${CMAKE_CURRENT_SOURCE_DIR}/sample_project/00- BricsCAD Plugin/00- Windows Version")
else()
    set(_oc_module_suffix ".lrx")
    set(_oc_dest_dir "${CMAKE_CURRENT_SOURCE_DIR}/sample_project/00- BricsCAD Plugin/01- Linux Version")
endif()
set(_oc_module_name "opencirt-${PROJECT_VERSION}-v${BRICSCAD_VERSION}")

# Optional: fertige Fassung zusätzlich an ein zweites Ziel ablegen (z.B.
# Projektordner). OC_PLUGIN_DEST wird als CMake-Variable oder Umgebungsvariable
# gesetzt (CLEAN_BUILD.sh liest sie unter anderem aus der lokalen, nicht
# getrackten local_env.sh); leer oder fehlender Ordner = zweites Ziel entfaellt.
if(NOT DEFINED OC_PLUGIN_DEST AND DEFINED ENV{OC_PLUGIN_DEST})
    set(OC_PLUGIN_DEST "$ENV{OC_PLUGIN_DEST}")
endif()
if(NOT DEFINED OC_PLUGIN_DEST)
    set(OC_PLUGIN_DEST "")
endif()

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
# Staende fuer dieselbe BricsCAD-Version werden dort entfernt, damit je
# BricsCAD-Version genau ein Stand dort liegt.
add_custom_command(TARGET opencirt_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DOC_MODULE=$<TARGET_FILE:opencirt_plugin>
        -DOC_DEST_DIR=${_oc_dest_dir}
        -DOC_PREFIX=opencirt
        -DOC_TAG=-v${BRICSCAD_VERSION}
        -DOC_SUFFIX=${_oc_module_suffix}
        -DOC_DEST_DIR2=${OC_PLUGIN_DEST}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/copy_plugin.cmake"
    COMMAND ${CMAKE_COMMAND} -E echo "Build complete: ${_oc_module_name}${_oc_module_suffix}"
    COMMAND ${CMAKE_COMMAND} -E echo "Load with: APPLOAD in BricsCAD V${BRICSCAD_VERSION}, command OPENCIRT (OC)"
    VERBATIM
)
