/***************************************************************************
 * Test standalone du GribReader — sans OpenCPN
 * Compile avec:
 *   g++ -std=c++17 -I src/ test_reader.cpp src/grib_reader.cpp \
 *       -leccodes -o test_reader
 * Exécute:
 *   ./test_reader /home/plante/grib_interpol/data/2026052518/
 ***************************************************************************/

#include "src/indices_data.h"
#include "src/grib_reader.h"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <algorithm>

static std::string fmtTime(time_t t) {
    char buf[32];
    struct tm* tm = gmtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%MZ", tm);
    return buf;
}

static void printStats(const std::vector<TimeStep>& steps, const GridInfo& g) {
    if (steps.empty()) { std::cout << "  (vide)\n"; return; }
    for (const auto& s : steps) {
        // Calculer min/max en ignorant MISSING
        double vmin = 1e30, vmax = -1e30;
        int    nmissing = 0, nvalid = 0;
        for (double v : s.values) {
            if (v >= TimeStep::MISSING_VALUE - 1.0) { nmissing++; continue; }
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
            nvalid++;
        }
        std::cout << "  +H" << std::setw(3) << std::setfill('0') << s.stepHours
                  << "  valid=" << fmtTime(s.validTime)
                  << "  min=" << std::fixed << std::setprecision(4) << vmin
                  << "  max=" << vmax
                  << "  valides=" << nvalid << "  manquants=" << nmissing
                  << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <runDir>\n";
        std::cerr << "ex: " << argv[0]
                  << " /home/plante/grib_interpol/data/2026052518/\n";
        return 1;
    }

    std::string runDir = argv[1];
    if (runDir.back() != '/') runDir += '/';

    IndexDefinition def = IndicesCatalogue::AGITATION();
    IndexData data;
    std::string errMsg;

    std::cout << "=== Test GribReader ===\n";
    std::cout << "Run dir : " << runDir << "\n";
    std::cout << "Indice  : " << def.displayName
              << " (paramètre " << def.parameterNumber << ")\n\n";

    bool ok = GribReader::LoadIndex(runDir, def, data, errMsg);

    if (!ok) {
        std::cerr << "ERREUR: " << errMsg << "\n";
        return 1;
    }

    // --- Afficher info grille ---
    std::cout << "Grille chargée:\n";
    std::cout << "  Ni=" << data.grid.ni << "  Nj=" << data.grid.nj << "\n";
    std::cout << "  Coin SW : lon=" << data.grid.lon0
                               << "  lat=" << data.grid.lat0 << "\n";
    std::cout << "  Résolution : dlon=" << data.grid.dlon
                              << "  dlat=" << data.grid.dlat << "\n\n";

    // --- Pas de temps scalaires ---
    std::cout << "Pas de temps scalaires (" << data.scalarSteps.size() << "):\n";
    printStats(data.scalarSteps, data.grid);

    // --- Pas de temps directionnels ---
    std::cout << "\nPas de temps directionnels (" << data.directionSteps.size() << "):\n";
    printStats(data.directionSteps, data.grid);

    // --- Test d'accès à un point précis (Québec city ~46.8N, -71.2E) ---
    double testLat = 46.8, testLon = -71.2;
    int ti = 24;  // pas de temps H+24
    if (ti < (int)data.scalarSteps.size()) {
        const TimeStep& ts = data.scalarSteps[ti];
        int gi, gj;
        if (data.grid.toIndex(testLon, testLat, gi, gj)) {
            double val = ts.get(gi, gj, data.grid);
            std::cout << "\nTest point Québec (" << testLat << "N, " << testLon << "E)"
                      << " à H+" << ts.stepHours << " : ";
            if (val >= TimeStep::MISSING_VALUE - 1.0)
                std::cout << "MANQUANT (terre)\n";
            else
                std::cout << val << " " << def.units << "\n";
        }
    }

    std::cout << "\nOK — Lecteur fonctionnel.\n";
    return 0;
}
