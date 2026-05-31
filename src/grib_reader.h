#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Lecteur de fichiers GRIB2 via l'API C d'ecCodes
 ***************************************************************************/

#include "indices_data.h"
#include <string>
#include <vector>

// Forward declaration (évite d'exposer <eccodes.h> dans le header)
struct grib_handle;
typedef struct grib_handle codes_handle;

class GribReader {
public:
    // -----------------------------------------------------------------------
    // Interface principale
    //
    // Charge tous les indices du catalogue à partir d'une liste de fichiers.
    // Chaque fichier peut contenir PLUSIEURS messages GRIB2 ; seuls les
    // messages dont (discipline, category, parameterNumber) correspondent à un
    // indice du catalogue (champ scalaire OU direction) sont retenus. Tous les
    // autres records (vent, température, etc.) sont ignorés — comme le fait le
    // plugin GRIB natif.
    //
    // files     : chemins de fichiers GRIB2 choisis par l'utilisateur
    // catalogue : indices recherchés (IndicesCatalogue::all())
    // outData   : un IndexData par indice effectivement trouvé (scalaire requis)
    // errMsg    : message d'erreur si aucun indice trouvé
    // Retourne true si au moins un indice avec données scalaires a été chargé.
    // -----------------------------------------------------------------------
    static bool LoadFiles(
        const std::vector<std::string>&      files,
        const std::vector<IndexDefinition>&  catalogue,
        std::vector<IndexData>&              outData,
        std::string&                         errMsg
    );

private:
    // Lit la grille (Ni, Nj, coins, incréments) depuis un message ecCodes.
    // Normalise lat0 au coin sud et lon0 en -180/+180.
    static bool readGridFromHandle(codes_handle* h, GridInfo& outGrid);

    // Lit l'horodatage et les valeurs d'un message. Vérifie que le nombre de
    // valeurs correspond à la grille (sinon false → message ignoré).
    static bool readStepFromHandle(codes_handle* h, const GridInfo& grid,
                                   TimeStep& outStep);

    // Ajoute un pas de temps en évitant les doublons de validTime
    // (si l'utilisateur sélectionne des fichiers qui se recouvrent).
    static void addStepDedup(std::vector<TimeStep>& steps, TimeStep&& step);

    // Convertit une longitude 0-360 en -180/+180
    static double normLon(double lon360) {
        return (lon360 > 180.0) ? lon360 - 360.0 : lon360;
    }
};
