/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Implémentation du lecteur GRIB2 via ecCodes
 ***************************************************************************/

#include "grib_reader.h"
#include <eccodes.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <filesystem>   // C++17 — remplace dirent.h (cross-platform)
#include <stdexcept>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// timegm : interprète struct tm comme UTC et retourne time_t
//   POSIX/Linux : timegm() disponible
//   Windows     : _mkgmtime() (même sémantique)
//   macOS       : timegm() disponible (BSD extension)
// ---------------------------------------------------------------------------
#ifdef _WIN32
    static time_t portable_timegm(struct tm* t) { return _mkgmtime(t); }
#else
    static time_t portable_timegm(struct tm* t) { return timegm(t); }
#endif

// ---------------------------------------------------------------------------
// Interface principale
// ---------------------------------------------------------------------------
bool GribReader::LoadIndex(
    const std::string&     runDir,
    const IndexDefinition& def,
    IndexData&             outData,
    std::string&           errMsg
) {
    outData.def = def;

    // --- Charger les pas de temps scalaires ---
    std::string scalarDir = buildSubDir(runDir, def.dirSearchString);
    std::vector<std::string> scalarFiles = listGribFiles(scalarDir, errMsg);
    if (scalarFiles.empty()) {
        errMsg = "Aucun fichier GRIB2 dans: " + scalarDir;
        return false;
    }

    bool gridInitialized = false;
    for (const auto& path : scalarFiles) {
        TimeStep step;
        std::string localErr;
        if (ReadOneFile(path, def.parameterNumber, outData.grid, step, localErr)) {
            outData.scalarSteps.push_back(std::move(step));
            gridInitialized = true;
        } else {
            // Fichier illisible ou mauvais paramètre — on continue
        }
    }

    if (!gridInitialized || outData.scalarSteps.empty()) {
        errMsg = "Aucun pas de temps valide pour paramètre "
                 + std::to_string(def.parameterNumber)
                 + " dans " + scalarDir;
        return false;
    }

    // Tri par stepHours croissant
    std::sort(outData.scalarSteps.begin(), outData.scalarSteps.end(),
              [](const TimeStep& a, const TimeStep& b) {
                  return a.stepHours < b.stepHours;
              });

    // --- Charger les pas de temps directionnels (optionnel) ---
    if (def.directionParamNumber >= 0 && !def.dirDirSearchString.empty()) {
        std::string dirDir = buildSubDir(runDir, def.dirDirSearchString);
        std::vector<std::string> dirFiles = listGribFiles(dirDir, errMsg);

        GridInfo dirGrid;  // non utilisé (devrait être identique à outData.grid)
        for (const auto& path : dirFiles) {
            TimeStep step;
            std::string localErr;
            if (ReadOneFile(path, def.directionParamNumber, dirGrid, step, localErr)) {
                outData.directionSteps.push_back(std::move(step));
            }
        }

        std::sort(outData.directionSteps.begin(), outData.directionSteps.end(),
                  [](const TimeStep& a, const TimeStep& b) {
                      return a.stepHours < b.stepHours;
                  });
    }

    return true;
}

// ---------------------------------------------------------------------------
// Construction du sous-répertoire
// Cherche le premier répertoire dont le nom contient shortName dans runDir
// ex: runDir="data/2026052518/", shortName="Indice_agitation"
//     → "data/2026052518/Indice_agitation_RDWPS/"
// ---------------------------------------------------------------------------
std::string GribReader::buildSubDir(
    const std::string& runDir,
    const std::string& shortName
) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(runDir, ec)) {
        if (!entry.is_directory(ec)) continue;
        std::string name = entry.path().filename().string();
        if (name.find(shortName) != std::string::npos)
            return entry.path().string() + "/";
    }
    // Fallback: convention directe
    return runDir + "/" + shortName + "/";
}

// ---------------------------------------------------------------------------
// Liste des fichiers .grib2 / .grb2 d'un répertoire, triés par nom
// ---------------------------------------------------------------------------
std::vector<std::string> GribReader::listGribFiles(
    const std::string& dir,
    std::string&       errMsg
) {
    std::vector<std::string> result;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        if (ext == ".grib2" || ext == ".grb2")
            result.push_back(entry.path().string());
    }
    if (ec) {
        errMsg = "Répertoire introuvable: " + dir;
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// Lecture d'un fichier GRIB2 à message unique via ecCodes
// ---------------------------------------------------------------------------
bool GribReader::ReadOneFile(
    const std::string& filePath,
    int                expectedParamNumber,
    GridInfo&          outGrid,
    TimeStep&          outStep,
    std::string&       errMsg
) {
    FILE* f = fopen(filePath.c_str(), "rb");
    if (!f) {
        errMsg = "Impossible d'ouvrir: " + filePath;
        return false;
    }

    int err = 0;
    codes_handle* h = codes_handle_new_from_file(nullptr, f, PRODUCT_GRIB, &err);
    fclose(f);

    if (!h || err != 0) {
        errMsg = "Erreur ecCodes lecture: " + filePath;
        if (h) codes_handle_delete(h);
        return false;
    }

    // --- Vérifier discipline/category/parameterNumber ---
    long discipline, category, paramNum;
    codes_get_long(h, "discipline",        &discipline);
    codes_get_long(h, "parameterCategory", &category);
    codes_get_long(h, "parameterNumber",   &paramNum);

    if ((int)paramNum != expectedParamNumber) {
        errMsg = "Paramètre inattendu " + std::to_string(paramNum)
                 + " (attendu " + std::to_string(expectedParamNumber) + ")";
        codes_handle_delete(h);
        return false;
    }

    // --- Lire la grille (seulement si pas encore initialisée) ---
    if (!outGrid.isValid()) {
        long ni, nj;
        codes_get_long(h, "Ni", &ni);
        codes_get_long(h, "Nj", &nj);

        double lat0, lon0, latN, lonN, dlat, dlon;
        codes_get_double(h, "latitudeOfFirstGridPointInDegrees",  &lat0);
        codes_get_double(h, "longitudeOfFirstGridPointInDegrees", &lon0);
        codes_get_double(h, "latitudeOfLastGridPointInDegrees",   &latN);
        codes_get_double(h, "longitudeOfLastGridPointInDegrees",  &lonN);
        codes_get_double(h, "iDirectionIncrementInDegrees",       &dlon);
        codes_get_double(h, "jDirectionIncrementInDegrees",       &dlat);

        // jScansPositively=1 → lat0 est le coin SUD
        // Sinon (0) → lat0 est le coin NORD, on réordonne
        long jScansPositively = 1;
        codes_get_long(h, "jScansPositively", &jScansPositively);

        outGrid.ni   = (int)ni;
        outGrid.nj   = (int)nj;
        outGrid.dlon = dlon;
        outGrid.dlat = dlat;

        // Normaliser longitude en -180/+180
        double lon0_norm = normLon(lon0);

        if (jScansPositively) {
            // Premier point = coin SW : OK
            outGrid.lat0 = lat0;
            outGrid.lon0 = lon0_norm;
        } else {
            // Premier point = coin NW → ajuster lat0 au sud
            outGrid.lat0 = latN;
            outGrid.lon0 = lon0_norm;
        }
    }

    // --- Lire les temps ---
    long dataDate, dataTime, stepHours;
    codes_get_long(h, "dataDate", &dataDate);
    codes_get_long(h, "dataTime", &dataTime);

    // stepRange peut être un entier ou une chaîne selon le template PDS
    codes_get_long(h, "endStep", &stepHours);

    // Construire refTime (time_t) depuis dataDate (YYYYMMDD) et dataTime (HHMM)
    struct tm refTm = {};
    refTm.tm_year = (int)(dataDate / 10000) - 1900;
    refTm.tm_mon  = (int)(dataDate / 100 % 100) - 1;
    refTm.tm_mday = (int)(dataDate % 100);
    refTm.tm_hour = (int)(dataTime / 100);
    refTm.tm_min  = (int)(dataTime % 100);
    refTm.tm_sec  = 0;
    refTm.tm_isdst = 0;
    time_t refTime = portable_timegm(&refTm);

    outStep.refTime   = refTime;
    outStep.stepHours = (int)stepHours;
    outStep.validTime = refTime + (time_t)stepHours * 3600;

    // --- Lire les valeurs ---
    size_t count = 0;
    codes_get_size(h, "values", &count);

    // Rejeter tout message dont la taille ne correspond pas à la grille de
    // référence. Garantit que chaque pas de temps stocké a exactement ni*nj
    // valeurs → sécurise les accès indexés (rendu, flèches, curseur).
    if (outGrid.isValid() && (int)count != outGrid.ni * outGrid.nj) {
        errMsg = "Taille de grille incohérente (" + std::to_string(count)
               + " != " + std::to_string(outGrid.ni * outGrid.nj)
               + ") dans: " + filePath;
        codes_handle_delete(h);
        return false;
    }

    outStep.values.resize(count);
    codes_get_double_array(h, "values", outStep.values.data(), &count);

    // Note: si jScansPositively=0, les données sont stockées N→S.
    // On pourrait les réordonner ici, mais nos fichiers ont jScansPositively=1.
    // TODO: gérer le cas jScansPositively=0 si nécessaire.

    codes_handle_delete(h);
    return true;
}

// ---------------------------------------------------------------------------
// Extraction de stepHours depuis le nom de fichier
// ex: "...PT024H.grib2" → 24
// ---------------------------------------------------------------------------
int GribReader::stepFromFilename(const std::string& filename) {
    // Chercher le pattern "PT" suivi de chiffres suivi de "H"
    size_t pos = filename.rfind("_PT");
    if (pos == std::string::npos) return -1;
    pos += 3;  // sauter "_PT"
    size_t end = filename.find('H', pos);
    if (end == std::string::npos) return -1;
    try {
        return std::stoi(filename.substr(pos, end - pos));
    } catch (...) {
        return -1;
    }
}
