cmake_minimum_required(VERSION 3.15)
project(stlaurent_indices_pi VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SOURCES
    src/grib_reader.cpp
    src/overlay_factory.cpp
    src/stlaurent_pi.cpp
    src/ui_dialog.cpp
)

# ---------------------------------------------------------------------------
# wxWidgets
# ---------------------------------------------------------------------------
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# ---------------------------------------------------------------------------
# ecCodes
#   Priorité 1 : find_package CONFIG (vcpkg sur Windows, certaines installs Linux)
#   Priorité 2 : pkg-config (Linux apt, Homebrew)
# ---------------------------------------------------------------------------
find_package(eccodes QUIET CONFIG)
if(NOT eccodes_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ECCODES REQUIRED eccodes)
endif()

# ---------------------------------------------------------------------------
# OpenGL
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Cible : bibliothèque partagée
# ---------------------------------------------------------------------------
add_library(stlaurent_indices_pi SHARED ${SOURCES})

target_include_directories(stlaurent_indices_pi PRIVATE src/)

# ecCodes — deux chemins selon la méthode de détection
if(eccodes_FOUND)
    target_link_libraries(stlaurent_indices_pi PRIVATE eccodes)
else()
    target_include_directories(stlaurent_indices_pi PRIVATE ${ECCODES_INCLUDE_DIRS})
    target_link_libraries(stlaurent_indices_pi PRIVATE ${ECCODES_LIBRARIES})
    target_compile_options(stlaurent_indices_pi PRIVATE ${ECCODES_CFLAGS_OTHER})
endif()

# OpenGL
target_link_libraries(stlaurent_indices_pi PRIVATE OpenGL::GL)

# dl (dlsym) — Linux seulement ; macOS l'inclut dans libSystem, Windows n'en a pas besoin
if(UNIX AND NOT APPLE)
    target_link_libraries(stlaurent_indices_pi PRIVATE dl)
endif()

# wxWidgets
target_link_libraries(stlaurent_indices_pi PRIVATE ${wxWidgets_LIBRARIES})

# ---------------------------------------------------------------------------
# Nom du fichier de sortie
#   Linux  → libstlaurent_indices_pi.so   (préfixe lib par défaut)
#   macOS  → libstlaurent_indices_pi.dylib
#   Windows→ stlaurent_indices_pi.dll     (pas de préfixe lib)
# ---------------------------------------------------------------------------
if(WIN32)
    set_target_properties(stlaurent_indices_pi PROPERTIES PREFIX "")
endif()
