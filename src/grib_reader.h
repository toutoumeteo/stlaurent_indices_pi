#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Lecteur de fichiers GRIB2 via l'API C d'ecCodes
 ***************************************************************************/

#include "indices_data.h"
#include <string>
#include <vector>

class GribReader {
public:
    // -----------------------------------------------------------------------
    // Interface principale
    //
    // Charge tous les pas de temps pour un indice.
    // runDir  : répertoire de la run, ex: "/path/to/data/2026052518/"
    // def     : définition de l'indice (paramètre GRIB2, direction, etc.)
    // outData : résultat rempli si succès
    // errMsg  : message d'erreur si échec
    // Retourne true si au moins un pas de temps scalaire a été chargé.
    // -----------------------------------------------------------------------
    static bool LoadIndex(
        const std::string&     runDir,
        const IndexDefinition& def,
        IndexData&             outData,
        std::string&           errMsg
    );

private:
    // Cherche dans runDir le premier sous-répertoire dont le nom contient searchString
    // ex: buildSubDir("/data/2026052518/", "Indice_agitation")
    //     → "/data/2026052518/Indice_agitation_RDWPS/"
    static std::string buildSubDir(
        const std::string& runDir,
        const std::string& searchString
    );

    // Liste les fichiers .grib2 d'un répertoire, triés par stepHours
    static std::vector<std::string> listGribFiles(
        const std::string& dir,
        std::string&       errMsg
    );

    // Lit un fichier GRIB2 à message unique
    // Vérifie discipline/category/parameterNumber
    // Remplit outGrid (à la première lecture) et outStep
    static bool ReadOneFile(
        const std::string& filePath,
        int                expectedParamNumber,
        GridInfo&          outGrid,       // rempli au premier appel
        TimeStep&          outStep,
        std::string&       errMsg
    );

    // Extrait le stepHours depuis le nom de fichier
    // ex: "...PT024H.grib2" → 24
    // Retourne -1 si non trouvable (lecture depuis le GRIB sera faite)
    static int stepFromFilename(const std::string& filename);

    // Convertit une longitude 0-360 en -180/+180
    static double normLon(double lon360) {
        return (lon360 > 180.0) ? lon360 - 360.0 : lon360;
    }
};
