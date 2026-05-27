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

# Sur Windows, MSVC exige que tous les symboles soient résolus à la
# compilation. Sans opencpn.lib, on fournit :
#   - des stubs pour les virtuelles des classes de base (vtable)
#   - des stubs pour les fonctions C de l'API OpenCPN appelées par notre code
# Le fichier .def exporte create_pi/destroy_pi (DECL_EXP étant vidé).
if(WIN32)
    list(APPEND SOURCES src/ocpn_plugin_stubs_win.cpp)
endif()

# ---------------------------------------------------------------------------
# wxWidgets — find_package pour les headers ; le lien est géré plus bas.
#   On ne lie PAS wxWidgets sur macOS et Linux.
#   Sur ces plateformes, OpenCPN embarque déjà wxWidgets dans son processus ;
#   lier contre une version Homebrew/système DIFFÉRENTE créerait deux runtimes
#   wx simultanés → échec de dlopen ou crash.
#   Les symboles wx sont résolus automatiquement depuis OpenCPN à l'exécution
#   (Linux : comportement par défaut du chargeur ; macOS : -undefined dynamic_lookup).
#   Sur Windows : on lie explicitement (MSVC n'a pas de résolution dynamique).
# ---------------------------------------------------------------------------
find_package(wxWidgets REQUIRED COMPONENTS core base)

# ---------------------------------------------------------------------------
# ecCodes
#   Windows (vcpkg) : find_package CONFIG
#   Linux / macOS   : pkg-config
#   (Le cmake-config livré avec libeccodes-dev sur Ubuntu 22.04 est cassé —
#    il référence /usr/bin/codes_info absent → on évite find_package sur Unix)
# ---------------------------------------------------------------------------
if(WIN32)
    find_package(eccodes REQUIRED CONFIG)
else()
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

# wxWidgets — headers et définitions de compilation (sans lien sur Unix)
target_include_directories(stlaurent_indices_pi PRIVATE ${wxWidgets_INCLUDE_DIRS})
foreach(_def ${wxWidgets_DEFINITIONS})
    target_compile_definitions(stlaurent_indices_pi PRIVATE ${_def})
endforeach()
if(wxWidgets_CXX_FLAGS)
    separate_arguments(_wx_cxx_flags UNIX_COMMAND "${wxWidgets_CXX_FLAGS}")
    target_compile_options(stlaurent_indices_pi PRIVATE ${_wx_cxx_flags})
endif()

# ecCodes — deux chemins selon la méthode de détection
if(WIN32)
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

# wxWidgets — lien uniquement sur Windows (voir commentaire find_package ci-dessus)
if(WIN32)
    target_link_libraries(stlaurent_indices_pi PRIVATE ${wxWidgets_LIBRARIES})
endif()

# ---------------------------------------------------------------------------
# Nom du fichier de sortie
#   Linux  → libstlaurent_indices_pi.so   (préfixe lib par défaut)
#   macOS  → libstlaurent_indices_pi.dylib
#   Windows→ stlaurent_indices_pi.dll     (pas de préfixe lib)
# ---------------------------------------------------------------------------
if(WIN32)
    set_target_properties(stlaurent_indices_pi PROPERTIES PREFIX "")

    # Redéfinir DECL_EXP comme vide pour notre DLL :
    #   Dans ocpn_plugin.h, DECL_EXP vaut __declspec(dllexport) sur Windows,
    #   ce qui force MSVC à instancier et exporter les vtables de TOUTES les
    #   classes marquées (PlugInChartBase, OCPN_downloadEvent, …), même celles
    #   qu'on n'utilise pas → 127 LNK2019. En vidant DECL_EXP, seules les
    #   classes dont stlaurent_pi hérite réellement ont besoin d'implémentations
    #   (fournies par ocpn_plugin_stubs_win.cpp).
    #   create_pi et destroy_pi sont exportés via le fichier .def ci-dessous.
    target_compile_definitions(stlaurent_indices_pi PRIVATE "DECL_EXP=")

    # Fichier .def : liste explicite des exports du plugin
    target_sources(stlaurent_indices_pi PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/stlaurent_indices_pi.def)
endif()

# macOS : deux ajustements nécessaires
if(APPLE)
    # 1. Silence les warnings de dépréciation OpenGL (deprecated depuis 10.14,
    #    mais toujours fonctionnel — c'est OpenCPN qui gère le contexte GL)
    target_compile_definitions(stlaurent_indices_pi PRIVATE GL_SILENCE_DEPRECATION)

    # 2. Les symboles OpenCPN (GetCanvasPixLL, opencpn_plugin_116, …) sont
    #    fournis par l'exécutable hôte au chargement du plugin.
    #    Sur Linux c'est le comportement par défaut ; sur macOS il faut
    #    l'autoriser explicitement.
    target_link_options(stlaurent_indices_pi PRIVATE -undefined dynamic_lookup)
endif()
