#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Données de grille et structures partagées entre les composants
 ***************************************************************************/

#include <vector>
#include <string>
#include <ctime>
#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// Métadonnées d'une grille lat/lon régulière
// ---------------------------------------------------------------------------
struct GridInfo {
    int    ni   = 0;    // nombre de points longitude (668)
    int    nj   = 0;    // nombre de points latitude  (343)
    double lon0 = 0.0;  // longitude coin SW, convention -180/+180 (-75.02°)
    double lat0 = 0.0;  // latitude  coin SW (45.06°N)
    double dlon = 0.0;  // résolution longitude (0.03°)
    double dlat = 0.0;  // résolution latitude  (0.02°)

    // Conversion indice (i,j) → (lon, lat)
    // i=0 → west, j=0 → south
    double lon(int i) const { return lon0 + i * dlon; }
    double lat(int j) const { return lat0 + j * dlat; }

    // Conversion (lon, lat) → indice (i, j), retourne false si hors grille
    bool toIndex(double lon_deg, double lat_deg, int& i, int& j) const {
        i = (int)std::round((lon_deg - lon0) / dlon);
        j = (int)std::round((lat_deg - lat0) / dlat);
        return (i >= 0 && i < ni && j >= 0 && j < nj);
    }

    // Indice dans le tableau 1D (row-major, j varie le plus lentement)
    int idx(int i, int j) const { return j * ni + i; }

    bool isValid() const { return ni > 0 && nj > 0; }
};

// ---------------------------------------------------------------------------
// Un pas de temps pour un champ scalaire ou directionnel
// ---------------------------------------------------------------------------
struct TimeStep {
    time_t  refTime;      // heure de référence du modèle (UTC)
    int     stepHours;    // horizon de prévision en heures (1 à 48)
    time_t  validTime;    // refTime + stepHours*3600

    // Valeurs sur la grille, taille ni*nj
    // Convention: row-major, j (latitude) varie le plus lentement
    // i=0 → west, j=0 → south
    // MISSING_VALUE (9999.0) pour les points invalides (terre, hors zone)
    std::vector<double> values;

    static constexpr double MISSING_VALUE = 9999.0;

    // Vrai si le tableau de valeurs correspond exactement à la grille.
    // À vérifier avant toute boucle indexée (garde-fou anti-débordement).
    bool matchesGrid(const GridInfo& g) const {
        return (int)values.size() == g.ni * g.nj;
    }

    bool isMissing(int i, int j, const GridInfo& g) const {
        return values[g.idx(i, j)] >= MISSING_VALUE - 1.0;
    }

    double get(int i, int j, const GridInfo& g) const {
        return values[g.idx(i, j)];
    }
};

// ---------------------------------------------------------------------------
// Définition d'un indice (paramètres GRIB2 et affichage)
// ---------------------------------------------------------------------------
struct IndexDefinition {
    std::string shortName;        // "AGITIDX" — tag dans le nom de fichier GRIB2
    std::string dirSearchString;  // chaîne à chercher dans le nom du répertoire
                                  // ex: "Indice_agitation" → trouve "Indice_agitation_RDWPS/"
    std::string dirDirSearchString; // idem pour le répertoire de direction
                                  // ex: "Direction_agitation" → trouve "Direction_agitation_RDWPS/"
    std::string displayName;      // "Indice d'agitation" — affiché dans l'UI
    std::string units;            // "[-]", "deg", etc.

    // Identification GRIB2
    int discipline;              // 10 (Oceanographic_Products)
    int parameterCategory;       // 0
    int parameterNumber;         // 192, 193, 194...
    int directionParamNumber;    // parameterNumber du champ direction associé
                                 // -1 si cet indice n'a pas de direction

    // Palette de couleurs — gradient linéaire
    double minValue;   // valeur min (utilisée si colorScale est vide)
    double maxValue;   // valeur max (utilisée si colorScale est vide)

    // Palette discrète par niveaux (prioritaire sur minValue/maxValue si non vide).
    // Chaque entrée définit la valeur MAX du niveau et sa couleur RGB.
    // Triée par threshold croissant. Toute valeur > dernier threshold → couleur du dernier niveau.
    struct ColorLevel { float threshold; uint8_t r, g, b; };
    std::vector<ColorLevel> colorScale;
};

// ---------------------------------------------------------------------------
// Toutes les données chargées pour un indice (scalaire + direction)
// ---------------------------------------------------------------------------
struct IndexData {
    IndexDefinition def;

    GridInfo grid;

    // Pas de temps scalaires, triés par stepHours (1 → 48)
    std::vector<TimeStep> scalarSteps;

    // Pas de temps directionnels, triés par stepHours
    // Peut être vide si def.directionParamNumber == -1
    std::vector<TimeStep> directionSteps;

    bool isLoaded() const {
        return grid.isValid() && !scalarSteps.empty();
    }

    bool hasDirection() const {
        return !directionSteps.empty();
    }

    // Retourne le pas de temps le plus proche d'une heure donnée
    // validTime_t: timestamp Unix cible
    int findNearestStep(time_t validTime_t) const {
        if (scalarSteps.empty()) return -1;
        int best = 0;
        long bestDiff = std::abs((long)(scalarSteps[0].validTime - validTime_t));
        for (int i = 1; i < (int)scalarSteps.size(); ++i) {
            long diff = std::abs((long)(scalarSteps[i].validTime - validTime_t));
            if (diff < bestDiff) { bestDiff = diff; best = i; }
        }
        return best;
    }
};

// ---------------------------------------------------------------------------
// Catalogue des indices supportés par le plugin
// Les parameterNumber doivent correspondre aux fichiers GRIB2 produits
// ---------------------------------------------------------------------------
namespace IndicesCatalogue {
    // Indice d'agitation (scalaire + direction)
    inline IndexDefinition AGITATION() {
        IndexDefinition d;
        d.shortName            = "AGITIDX";
        d.dirSearchString      = "Indice_agitation";   // trouve "Indice_agitation_RDWPS/"
        d.dirDirSearchString   = "Direction_agitation"; // trouve "Direction_agitation_RDWPS/"
        d.displayName          = "Indice d'agitation";
        d.units                = "[-]";
        d.discipline           = 10;
        d.parameterCategory    = 0;
        d.parameterNumber      = 192;
        d.directionParamNumber = 193;
        d.minValue             = 0.0;
        d.maxValue             = 5.0;
        // Palette discrète calquée sur la barre de couleur ECCC/RDWPS.
        // Extraite de l'image de référence (barre_agitation.png).
        d.colorScale = {
            { 0.04f, 164, 203, 250 },  // bleu très clair
            { 0.08f, 127, 175, 249 },  // bleu clair
            { 0.12f,  92, 148, 247 },  // bleu moyen clair
            { 0.16f,  61, 121, 246 },  // bleu moyen
            { 0.20f,  49, 108, 233 },  // bleu
            { 0.25f,  97, 148, 171 },  // bleu-vert
            { 0.30f, 156, 189, 116 },  // vert clair
            { 0.35f, 217, 229,  84 },  // jaune-vert
            { 0.40f, 253, 244,  81 },  // jaune
            { 0.50f, 245, 193,  66 },  // jaune-orange
            { 0.60f, 242, 167,  59 },  // orange clair
            { 0.70f, 239, 135,  51 },  // orange
            { 0.80f, 236, 100,  43 },  // orange foncé
            { 0.90f, 235,  70,  38 },  // orange-rouge
            { 1.10f, 234,  53,  36 },  // rouge
            { 1.30f, 194,  41,  27 },  // rouge foncé
            { 1.50f, 144,  27,  18 },  // rouge très foncé
            { 1.70f,  95,  15,   8 },  // bordeaux
            { 2.00f,  46,   4,   2 },  // rouge presque noir
            { 5.00f,   3,   0,   0 },  // noir (>2 m)
        };
        return d;
    }

    // Direction d'agitation seule (usage interne — non affiché directement)
    inline IndexDefinition AGITATION_DIR() {
        IndexDefinition d;
        d.shortName            = "AGITDIR";
        d.dirSearchString      = "Direction_agitation";
        d.dirDirSearchString   = "";
        d.displayName          = "Direction d'agitation";
        d.units                = "deg";
        d.discipline           = 10;
        d.parameterCategory    = 0;
        d.parameterNumber      = 193;
        d.directionParamNumber = -1;
        d.minValue             = 0.0;
        d.maxValue             = 360.0;
        return d;
    }

    // TODO: ajouter les autres indices ici au fur et à mesure
    // inline IndexDefinition MON_DEUXIEME_INDICE() { ... }
    // inline IndexDefinition MON_TROISIEME_INDICE() { ... }

    // Liste complète des indices affichables dans l'UI
    inline std::vector<IndexDefinition> all() {
        return { AGITATION() };
        // Ajouter les suivants ici quand disponibles
    }
}
