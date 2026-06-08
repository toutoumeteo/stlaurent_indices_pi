cmake_minimum_required(VERSION 3.21)
project(stlaurent_indices_pi VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Option de diagnostic : AddressSanitizer + UndefinedBehaviorSanitizer
#   -DENABLE_ASAN=ON  → instrumente le plugin pour détecter débordements,
#   use-after-free, etc. au moment exact de l'accès fautif.
#
#   OpenCPN n'étant pas compilé avec ASan, on précharge la lib au lancement :
#     ASAN_OPTIONS=halt_on_error=0:log_path=/tmp/asan_stlaurent \
#     LD_PRELOAD=$(gcc -print-file-name=libasan.so) opencpn
#
#   Réservé au debug (Clang/GCC, Linux/macOS) — ne pas activer en production.
# ---------------------------------------------------------------------------
option(ENABLE_ASAN "Compiler avec AddressSanitizer + UBSan (debug)" OFF)

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
#   Linux  : on lie contre la version SYSTÈME (même que celle chargée par
#             OpenCPN). Deux raisons :
#             1. Le check de compatibilité OpenCPN 5.12+ (USE_LIBELF) scanne
#                les DT_NEEDED et cherche "libwx_gtk3u_core-3.2" ; si absent
#                → plugin rejeté ("Incompatible plugin detected").
#             2. C'est la même bibliothèque → aucun double chargement.
#   macOS  : on lie contre le bundle OpenCPN (voir bloc APPLE ci-dessous).
#             Lier contre la version Homebrew créerait deux runtimes wx.
#   Windows: on lie explicitement (MSVC n'a pas de résolution dynamique).
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
# GDAL — lecteur GRIB2
#   Partout : find_package(GDAL) crée la cible GDAL::GDAL (cmake >= 3.14).
#   Windows  : vcpkg fournit GDALConfig.cmake → trouvé via toolchain.
#   Linux    : libgdal-dev → trouvé via FindGDAL.cmake + gdal-config.
#   macOS    : brew install gdal → trouvé via /opt/homebrew dans PREFIX_PATH.
# ---------------------------------------------------------------------------
find_package(GDAL REQUIRED)
message(STATUS "GDAL version : ${GDAL_VERSION}")

# ---------------------------------------------------------------------------
# OpenGL
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Cible : bibliothèque partagée
# ---------------------------------------------------------------------------
add_library(stlaurent_indices_pi SHARED ${SOURCES})

target_include_directories(stlaurent_indices_pi PRIVATE src/)

# AddressSanitizer / UBSan (debug) — compile ET link
if(ENABLE_ASAN)
    if(MSVC)
        message(WARNING "ENABLE_ASAN non supporté avec MSVC — ignoré.")
    else()
        message(STATUS "AddressSanitizer + UBSan ACTIVÉS (build de debug)")
        target_compile_options(stlaurent_indices_pi PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(stlaurent_indices_pi PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

# wxWidgets — headers et définitions de compilation (sans lien sur Unix)
target_include_directories(stlaurent_indices_pi PRIVATE ${wxWidgets_INCLUDE_DIRS})
foreach(_def ${wxWidgets_DEFINITIONS})
    target_compile_definitions(stlaurent_indices_pi PRIVATE ${_def})
endforeach()
if(wxWidgets_CXX_FLAGS)
    separate_arguments(_wx_cxx_flags UNIX_COMMAND "${wxWidgets_CXX_FLAGS}")
    target_compile_options(stlaurent_indices_pi PRIVATE ${_wx_cxx_flags})
endif()

# GDAL
target_link_libraries(stlaurent_indices_pi PRIVATE GDAL::GDAL)

# OpenGL
target_link_libraries(stlaurent_indices_pi PRIVATE OpenGL::GL)

# dl (dlsym) — Linux seulement ; macOS l'inclut dans libSystem, Windows n'en a pas besoin
if(UNIX AND NOT APPLE)
    target_link_libraries(stlaurent_indices_pi PRIVATE dl)
endif()

# wxWidgets — lien sur Windows et Linux (pas macOS — bundle OpenCPN lié plus bas)
if(NOT APPLE)
    target_link_libraries(stlaurent_indices_pi PRIVATE ${wxWidgets_LIBRARIES})
endif()

# Linux : forcer hash-style sysv → EI_OSABI=0 (UNIX - System V) comme OpenCPN.
# Le défaut gcc/ld (--hash-style=gnu) produit EI_OSABI=3 (UNIX - GNU).
# Le check ELF de OpenCPN 5.12+ compare les type_magic (qui incluent EI_OSABI) ;
# une différence entraîne "Incompatible plugin detected".
if(UNIX AND NOT APPLE)
    target_link_options(stlaurent_indices_pi PRIVATE "-Wl,--hash-style=sysv")
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

# ---------------------------------------------------------------------------
# Installation et packaging
# ---------------------------------------------------------------------------

# Répertoire cible selon la plateforme (relatif au CMAKE_INSTALL_PREFIX)
if(WIN32)
    set(_plugin_dir "plugins")
elseif(APPLE)
    set(_plugin_dir "PlugIns")
else()
    set(_plugin_dir "lib/opencpn")
endif()

install(TARGETS stlaurent_indices_pi
    LIBRARY DESTINATION ${_plugin_dir}   # .so / .dylib
    RUNTIME DESTINATION ${_plugin_dir})  # .dll (Windows)

# ---------------------------------------------------------------------------
# CPack — génération du tarball redistribuable
# Utilisation :
#   cmake --install build --prefix /tmp/pkg
#   cd /tmp/pkg && tar czf stlaurent_indices_pi-X.Y.Z-<os>.tar.gz .
# (Le workflow CI fait ça automatiquement sur les tags.)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME             "stlaurent_indices_pi")
set(CPACK_PACKAGE_VENDOR           "toutoumeteo")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Indices Saint-Laurent — météo-marine ECCC/RDWPS pour OpenCPN")
set(CPACK_PACKAGE_VERSION          "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR    "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR    "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH    "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT          "https://github.com/toutoumeteo/stlaurent_indices_pi")
set(CPACK_RESOURCE_FILE_LICENSE    "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
