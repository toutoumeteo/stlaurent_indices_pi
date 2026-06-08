#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Lecteur de fichiers GRIB2 via GDAL
 ***************************************************************************/

#include "indices_data.h"
#include <string>
#include <vector>

class GribReader {
public:
    // Charge tous les indices du catalogue à partir d'une liste de fichiers.
    // Chaque fichier peut contenir plusieurs bandes (pas de temps, paramètres).
    // Seules les bandes dont (discipline, parameterCategory, parameterNumber)
    // correspondent au catalogue sont retenues.
    //
    // Retourne true si au moins un indice avec données scalaires a été chargé.
    static bool LoadFiles(
        const std::vector<std::string>&      files,
        const std::vector<IndexDefinition>&  catalogue,
        std::vector<IndexData>&              outData,
        std::string&                         errMsg
    );

private:
    static void addStepDedup(std::vector<TimeStep>& steps, TimeStep&& step);

    static double normLon(double lon360) {
        return (lon360 > 180.0) ? lon360 - 360.0 : lon360;
    }
};
