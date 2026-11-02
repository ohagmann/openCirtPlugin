# =============================================================================
# CMake-Toolchain: Windows-Build (.brx) unter Linux
# =============================================================================
# clang-cl als Compiler, lld-link als Linker, MSVC-Bibliotheken und Windows SDK
# im /winsysroot-Layout (z.B. von xwin: splat --use-winsysroot-style
# --preserve-ms-arch-notation). Benutzt von CROSS_BUILD_WINDOWS.sh.
#
# Vorgaben (CMake- oder Umgebungsvariable):
#   OC_LLVM_BIN     Ordner mit clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt
#   OC_WINSYSROOT   Wurzel mit VC/Tools/MSVC/<ver> und Windows Kits/10
# =============================================================================

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

foreach(_oc_var OC_LLVM_BIN OC_WINSYSROOT)
    if(NOT DEFINED ${_oc_var} AND DEFINED ENV{${_oc_var}})
        set(${_oc_var} "$ENV{${_oc_var}}")
    endif()
    if(NOT DEFINED ${_oc_var} OR "${${_oc_var}}" STREQUAL "")
        message(FATAL_ERROR "${_oc_var} ist nicht gesetzt (siehe CROSS_BUILD_WINDOWS.sh).")
    endif()
endforeach()
# Fuer try_compile weiterreichen
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES OC_LLVM_BIN OC_WINSYSROOT)

set(CMAKE_C_COMPILER   "${OC_LLVM_BIN}/clang-cl")
set(CMAKE_CXX_COMPILER "${OC_LLVM_BIN}/clang-cl")
set(CMAKE_LINKER       "${OC_LLVM_BIN}/lld-link")
set(CMAKE_AR           "${OC_LLVM_BIN}/llvm-lib")
set(CMAKE_RC_COMPILER  "${OC_LLVM_BIN}/llvm-rc")
set(CMAKE_MT           "${OC_LLVM_BIN}/llvm-mt")
set(CMAKE_C_COMPILER_TARGET   x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# Header und Bibliotheken von MSVC und Windows SDK
set(CMAKE_C_FLAGS_INIT   "/winsysroot \"${OC_WINSYSROOT}\"")
set(CMAKE_CXX_FLAGS_INIT "/winsysroot \"${OC_WINSYSROOT}\"")
# Optional: Ordner mit Verweisen fuer abweichende Schreibweisen von
# Windows-Headern (legt CROSS_BUILD_WINDOWS.sh an)
if(NOT DEFINED OC_CASE_ALIAS_DIR AND DEFINED ENV{OC_CASE_ALIAS_DIR})
    set(OC_CASE_ALIAS_DIR "$ENV{OC_CASE_ALIAS_DIR}")
endif()
if(OC_CASE_ALIAS_DIR)
    string(APPEND CMAKE_C_FLAGS_INIT   " /imsvc \"${OC_CASE_ALIAS_DIR}\"")
    string(APPEND CMAKE_CXX_FLAGS_INIT " /imsvc \"${OC_CASE_ALIAS_DIR}\"")
endif()
foreach(_oc_t EXE SHARED MODULE)
    set(CMAKE_${_oc_t}_LINKER_FLAGS_INIT "/winsysroot:\"${OC_WINSYSROOT}\"")
endforeach()

# Nur Release-Laufzeit (/MD, msvcrt.lib): die Debug-Bibliotheken (msvcrtd.lib)
# sind nicht weitergebbar und fehlen im Sysroot - auch fuer die Compiler-Tests
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)

# Bibliotheken und Pakete nur ueber die ausdruecklich angegebenen Pfade suchen,
# Programme (moc, uic, rcc) kommen vom Linux-Qt (QT_HOST_PATH)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
