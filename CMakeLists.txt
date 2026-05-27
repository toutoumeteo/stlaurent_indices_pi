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

# Sur macOS (Apple Silicon), Homebrew est dans /opt/homebrew
# qui n'est pas automatiquement dans les chemins de recherche CMake.
if(APPLE)
    list(PREPEND CMAKE_PREFIX_PATH /opt/homebrew)
endif()

if(WIN32)
    # Windows : on lie wxWidgets → REQUIRED
    find_package(wxWidgets REQUIRED COMPONENTS core base)
else()
    # macOS / Linux : on doit utiliser wxWidgets 3.2 (même ABI qu'OpenCPN).
    # OpenCPN embarque wx 3.2 ; compiler avec des headers 3.3 introduit des
    # symboles absents au chargement (ex. SetCursorBundle → dlopen échoue).
    #
    # Homebrew installe wx@3.2 avec wx-config-3.2 (pas wx-config) pour éviter
    # les conflits avec la formule wxwidgets (3.3+). On cherche wx-config-3.2
    # en priorité, puis wx-config comme fallback.
    if(NOT DEFINED wxWidgets_CONFIG_EXECUTABLE)
        find_program(wxWidgets_CONFIG_EXECUTABLE
            NAMES wx-config-3.2 wx-config
            HINTS /opt/homebrew/bin /usr/local/bin /usr/bin
            NO_DEFAULT_PATH)
    endif()

    if(wxWidgets_CONFIG_EXECUTABLE)
        message(STATUS "wxWidgets config : ${wxWidgets_CONFIG_EXECUTABLE}")
    else()
        message(FATAL_ERROR
            "wxWidgets 3.2 introuvable. Les headers 3.2 sont nécessaires (ABI OpenCPN).\n"
            "  macOS  : brew install wxwidgets@3.2\n"
            "  Ubuntu : sudo apt install libwxgtk3.2-dev wx-common\n"
            "Ou passez le chemin explicitement :\n"
            "  -DwxWidgets_CONFIG_EXECUTABLE=/opt/homebrew/bin/wx-config-3.2\n")
    endif()

    find_package(wxWidgets REQUIRED COMPONENTS core base)
endif()

# ---------------------------------------------------------------------------
# ecCodes
#   Windows (vcpkg) : find_package CONFIG
#   Linux / macOS   : find_path + find_library
#   (pkg-config évité : le .pc eccodes sur Ubuntu 22.04 contient un
#    includedir relatif cassé → IMPORTED_TARGET échoue à la validation.
#    find_library passe le chemin absolu à l'éditeur de liens → pas besoin
#    de -L séparé, fonctionne aussi bien sur macOS Homebrew que sur Linux.)
# ---------------------------------------------------------------------------
if(WIN32)
    find_package(eccodes REQUIRED CONFIG)
else()
    find_path(ECCODES_INCLUDE_DIR eccodes.h
        HINTS /opt/homebrew/include /usr/include /usr/local/include
        PATH_SUFFIXES eccodes x86_64-linux-gnu)
    find_library(ECCODES_LIBRARY NAMES eccodes
        HINTS /opt/homebrew/lib /usr/lib /usr/local/lib
              /usr/lib/x86_64-linux-gnu)
    if(NOT ECCODES_INCLUDE_DIR OR NOT ECCODES_LIBRARY)
        message(FATAL_ERROR
            "eccodes introuvable.\n"
            "  macOS  : brew install eccodes\n"
            "  Ubuntu : sudo apt install libeccodes-dev\n"
            "Passez le chemin manuellement si nécessaire :\n"
            "  -DECCODES_INCLUDE_DIR=<path> -DECCODES_LIBRARY=<path>\n")
    endif()
    message(STATUS "ecCodes headers : ${ECCODES_INCLUDE_DIR}")
    message(STATUS "ecCodes library : ${ECCODES_LIBRARY}")
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

# ecCodes — lien
if(WIN32)
    target_link_libraries(stlaurent_indices_pi PRIVATE eccodes)
else()
    target_include_directories(stlaurent_indices_pi PRIVATE ${ECCODES_INCLUDE_DIR})
    target_link_libraries(stlaurent_indices_pi PRIVATE ${ECCODES_LIBRARY})
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

# macOS
if(APPLE)
    # 1. Silence les warnings de dépréciation OpenGL
    target_compile_definitions(stlaurent_indices_pi PRIVATE GL_SILENCE_DEPRECATION)

    # 2. wxWidgets : lier contre le bundle OpenCPN installé
    #    Un plugin OpenCPN DOIT utiliser la MÊME wxWidgets qu'OpenCPN.
    #    OpenCPN embarque wx 3.2 dans Contents/Frameworks/ ; lier contre
    #    ces dylibs garantit que macOS réutilise la bibliothèque déjà chargée
    #    (même install-name → pas de double chargement, symboles disponibles).
    #
    #    Priorité :
    #      a) OPENCPN_APP cmake variable (override CI/custom install)
    #      b) /Applications/OpenCPN.app (installation standard)
    #      c) dynamic_lookup uniquement (CI sans OpenCPN — DLL non chargeable
    #         en standalone mais se charge correctement depuis OpenCPN)
    if(NOT DEFINED OPENCPN_APP)
        set(OPENCPN_APP "/Applications/OpenCPN.app")
    endif()
    set(_ocpn_fw "${OPENCPN_APP}/Contents/Frameworks")

    if(EXISTS "${_ocpn_fw}/libwx_osx_cocoau_core-3.2.dylib")
        message(STATUS "wxWidgets : bundle OpenCPN trouvé → ${_ocpn_fw}")
        target_link_libraries(stlaurent_indices_pi PRIVATE
            "${_ocpn_fw}/libwx_osx_cocoau_core-3.2.dylib"
            "${_ocpn_fw}/libwx_baseu-3.2.dylib"
        )
    else()
        message(WARNING
            "OpenCPN non trouvé dans ${OPENCPN_APP}.\n"
            "Lien wxWidgets ignoré — utilisez -DOPENCPN_APP=<chemin> si OpenCPN\n"
            "est installé ailleurs. Le plugin se chargera depuis OpenCPN mais\n"
            "ne peut pas être testé hors contexte OpenCPN.")
    endif()

    # 3. Symboles OpenCPN (GetCanvasPixLL, etc.) résolus depuis l'hôte au chargement
    target_link_options(stlaurent_indices_pi PRIVATE -undefined dynamic_lookup)
endif()
