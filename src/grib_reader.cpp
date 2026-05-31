/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Implémentation du lecteur GRIB2 via ecCodes
 ***************************************************************************/

#include "grib_reader.h"
#include <eccodes.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

// ---------------------------------------------------------------------------
// timegm : interprète struct tm comme UTC et retourne time_t
//   POSIX/Linux/macOS : timegm()   |   Windows : _mkgmtime()
// ---------------------------------------------------------------------------
#ifdef _WIN32
    static time_t portable_timegm(struct tm* t) { return _mkgmtime(t); }
#else
    static time_t portable_timegm(struct tm* t) { return timegm(t); }
#endif

// ---------------------------------------------------------------------------
// Lecture de la grille depuis un message
// ---------------------------------------------------------------------------
bool GribReader::readGridFromHandle(codes_handle* h, GridInfo& outGrid) {
    long ni, nj;
    if (codes_get_long(h, "Ni", &ni) != 0) return false;
    if (codes_get_long(h, "Nj", &nj) != 0) return false;

    double lat0, lon0, latN, lonN, dlat, dlon;
    codes_get_double(h, "latitudeOfFirstGridPointInDegrees",  &lat0);
    codes_get_double(h, "longitudeOfFirstGridPointInDegrees", &lon0);
    codes_get_double(h, "latitudeOfLastGridPointInDegrees",   &latN);
    codes_get_double(h, "longitudeOfLastGridPointInDegrees",  &lonN);
    codes_get_double(h, "iDirectionIncrementInDegrees",       &dlon);
    codes_get_double(h, "jDirectionIncrementInDegrees",       &dlat);

    // jScansPositively=1 → lat0 est le coin SUD ; sinon lat0 = coin NORD
    long jScansPositively = 1;
    codes_get_long(h, "jScansPositively", &jScansPositively);

    outGrid.ni   = (int)ni;
    outGrid.nj   = (int)nj;
    outGrid.dlon = dlon;
    outGrid.dlat = dlat;
    outGrid.lon0 = normLon(lon0);
    outGrid.lat0 = jScansPositively ? lat0 : latN;  // toujours le coin sud

    return outGrid.isValid();
}

// ---------------------------------------------------------------------------
// Lecture de l'horodatage et des valeurs d'un message
// ---------------------------------------------------------------------------
bool GribReader::readStepFromHandle(codes_handle* h, const GridInfo& grid,
                                    TimeStep& outStep) {
    long dataDate, dataTime, stepHours = 0;
    codes_get_long(h, "dataDate", &dataDate);
    codes_get_long(h, "dataTime", &dataTime);
    codes_get_long(h, "endStep",  &stepHours);

    struct tm refTm = {};
    refTm.tm_year  = (int)(dataDate / 10000) - 1900;
    refTm.tm_mon   = (int)(dataDate / 100 % 100) - 1;
    refTm.tm_mday  = (int)(dataDate % 100);
    refTm.tm_hour  = (int)(dataTime / 100);
    refTm.tm_min   = (int)(dataTime % 100);
    refTm.tm_sec   = 0;
    refTm.tm_isdst = 0;
    time_t refTime = portable_timegm(&refTm);

    outStep.refTime   = refTime;
    outStep.stepHours = (int)stepHours;
    outStep.validTime = refTime + (time_t)stepHours * 3600;

    size_t count = 0;
    codes_get_size(h, "values", &count);

    // Sécurité : la taille doit correspondre à la grille de référence,
    // sinon on ignore ce message (sécurise les accès indexés ailleurs).
    if ((int)count != grid.ni * grid.nj) return false;

    outStep.values.resize(count);
    if (codes_get_double_array(h, "values", outStep.values.data(), &count) != 0)
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Ajout avec déduplication par validTime
// ---------------------------------------------------------------------------
void GribReader::addStepDedup(std::vector<TimeStep>& steps, TimeStep&& step) {
    for (const auto& s : steps) {
        if (s.validTime == step.validTime) return;  // déjà présent
    }
    steps.push_back(std::move(step));
}

// ---------------------------------------------------------------------------
// Interface principale : lecture multi-fichiers / multi-messages
// ---------------------------------------------------------------------------
bool GribReader::LoadFiles(
    const std::vector<std::string>&     files,
    const std::vector<IndexDefinition>& catalogue,
    std::vector<IndexData>&             outData,
    std::string&                        errMsg
) {
    outData.clear();
    if (files.empty()) { errMsg = "Aucun fichier sélectionné."; return false; }
    if (catalogue.empty()) { errMsg = "Catalogue d'indices vide."; return false; }

    // Table de travail : un IndexData par définition du catalogue
    std::vector<IndexData> work(catalogue.size());
    for (size_t k = 0; k < catalogue.size(); ++k)
        work[k].def = catalogue[k];

    int filesOpened = 0;
    long messagesSeen = 0;

    for (const auto& path : files) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;  // fichier illisible — on passe au suivant
        ++filesOpened;

        int err = 0;
        codes_handle* h = nullptr;
        // Boucle sur TOUS les messages du fichier
        while ((h = codes_handle_new_from_file(nullptr, f, PRODUCT_GRIB, &err))
               != nullptr) {
            ++messagesSeen;

            long disc = -1, cat = -1, num = -1;
            codes_get_long(h, "discipline",        &disc);
            codes_get_long(h, "parameterCategory", &cat);
            codes_get_long(h, "parameterNumber",   &num);

            // Chercher une correspondance dans le catalogue.
            // Match sur les 3 clés → évite de confondre p.ex. le vent
            // (discipline 0) avec une vague (discipline 10).
            for (size_t k = 0; k < catalogue.size(); ++k) {
                const IndexDefinition& d = catalogue[k];
                bool sameParam = (disc == d.discipline &&
                                  cat  == d.parameterCategory);
                bool isScalar = sameParam && (num == d.parameterNumber);
                bool isDir    = sameParam && (d.directionParamNumber >= 0) &&
                                (num == d.directionParamNumber);
                if (!isScalar && !isDir) continue;

                // Grille : lue au premier message retenu pour cet indice
                if (!work[k].grid.isValid()) {
                    if (!readGridFromHandle(h, work[k].grid)) break;
                }

                TimeStep step;
                if (readStepFromHandle(h, work[k].grid, step)) {
                    if (isScalar) addStepDedup(work[k].scalarSteps, std::move(step));
                    else          addStepDedup(work[k].directionSteps, std::move(step));
                }
                break;  // un message ne correspond qu'à un seul indice
            }

            codes_handle_delete(h);
        }
        fclose(f);
    }

    // Tri par validTime + ne conserver que les indices avec données scalaires
    auto byValidTime = [](const TimeStep& a, const TimeStep& b) {
        return a.validTime < b.validTime;
    };
    for (auto& wdata : work) {
        if (wdata.scalarSteps.empty()) continue;  // indice absent des fichiers
        std::sort(wdata.scalarSteps.begin(),    wdata.scalarSteps.end(),    byValidTime);
        std::sort(wdata.directionSteps.begin(), wdata.directionSteps.end(), byValidTime);
        outData.push_back(std::move(wdata));
    }

    if (outData.empty()) {
        if (filesOpened == 0) {
            errMsg = "Aucun fichier lisible.";
        } else {
            // Cas particulier : des records de direction ont été reconnus mais
            // aucun champ scalaire associé. outData étant vide, aucune entrée
            // n'a été déplacée (move) → work est intact, on peut l'inspecter.
            bool anyDirection = false;
            for (const auto& wdata : work)
                if (!wdata.directionSteps.empty()) { anyDirection = true; break; }

            if (anyDirection)
                errMsg = "Des données de direction ont été trouvées, mais aucune "
                         "donnée scalaire (hauteur/indice) correspondante.\n"
                         "La direction seule ne peut pas être affichée — "
                         "ajoutez le fichier de l'indice.";
            else
                errMsg = "Aucun indice reconnu dans les " + std::to_string(messagesSeen)
                       + " message(s) GRIB lus. Vérifiez que les fichiers contiennent "
                         "bien les paramètres attendus (discipline/catégorie/numéro).";
        }
        return false;
    }
    return true;
}
