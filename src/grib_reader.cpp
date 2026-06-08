/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Lecteur GRIB2 via GDAL
 ***************************************************************************/

#include "grib_reader.h"
#include <gdal_priv.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers fichier-local
// ---------------------------------------------------------------------------

// Parse "1234567890 sec UTC" → time_t
static time_t parseGribTime(const char* s) {
    if (!s || !*s) return 0;
    return (time_t)strtoll(s, nullptr, 10);
}

// Extrait discipline / parameterCategory / parameterNumber depuis les
// métadonnées GDAL d'une bande GRIB2 (PDT 4.0).
//   GRIB_DISCIPLINE          → discipline (ex. 10 = oceanographic)
//   GRIB_PDS_TEMPLATE_NUMBERS → octets séparés par espaces ; pour PDT 4.0 :
//                               [0] = parameterCategory, [1] = parameterNumber
static bool parseBandParam(GDALRasterBand* band,
                           long& discipline, long& cat, long& num)
{
    const char* discStr = band->GetMetadataItem("GRIB_DISCIPLINE");
    const char* pdsStr  = band->GetMetadataItem("GRIB_PDS_TEMPLATE_NUMBERS");
    if (!discStr || !pdsStr) return false;

    discipline = strtol(discStr, nullptr, 10);

    char* end = nullptr;
    cat = strtol(pdsStr, &end, 10);
    if (!end || end == pdsStr) return false;
    num = strtol(end, nullptr, 10);

    return true;
}

// Lit la grille à partir du GeoTransform du dataset.
// Convention GDAL : gt[0]/gt[3] = coin supérieur-gauche (pas le centre).
// Notre GridInfo : lon0/lat0 = centre du coin SW, dlat positif vers le nord.
static bool readGrid(GDALDataset* ds, GridInfo& out,
                     double normLon(double))
{
    double gt[6];
    if (ds->GetGeoTransform(gt) != CE_None) return false;

    int ni = ds->GetRasterXSize();
    int nj = ds->GetRasterYSize();
    if (ni <= 0 || nj <= 0) return false;
    if (gt[1] <= 0.0 || gt[5] >= 0.0) return false;  // dlon>0, dlat<0 attendus

    out.ni   = ni;
    out.nj   = nj;
    out.dlon = gt[1];
    out.dlat = -gt[5];                         // stocké positif
    out.lon0 = normLon(gt[0] + 0.5 * gt[1]);  // centre colonne ouest
    out.lat0 = gt[3] + (nj - 0.5) * gt[5];    // centre ligne sud (gt[5]<0)

    return out.isValid();
}

// Lit un pas de temps depuis une bande.
// GDAL stocke les valeurs N→S (j=0 = nord) ; notre convention j=0 = sud.
static bool readBand(GDALRasterBand* band, const GridInfo& grid, TimeStep& out)
{
    const char* validStr = band->GetMetadataItem("GRIB_VALID_TIME");
    const char* refStr   = band->GetMetadataItem("GRIB_REF_TIME");

    out.validTime = parseGribTime(validStr);
    out.refTime   = parseGribTime(refStr);
    if (out.validTime == 0) return false;
    if (out.refTime == 0) out.refTime = out.validTime;
    out.stepHours = (int)((out.validTime - out.refTime) / 3600);

    int ni = grid.ni, nj = grid.nj;
    size_t total = (size_t)ni * nj;

    // Lire dans un tampon N→S temporaire
    std::vector<double> buf(total);
    if (band->RasterIO(GF_Read, 0, 0, ni, nj,
                       buf.data(), ni, nj, GDT_Float64, 0, 0) != CE_None)
        return false;

    int hasNodata = 0;
    double nodata = band->GetNoDataValue(&hasNodata);

    // Remplir out.values en inversant l'ordre des lignes (S→N)
    out.values.resize(total);
    for (int j = 0; j < nj; ++j) {
        int srcRow = nj - 1 - j;
        for (int i = 0; i < ni; ++i) {
            double v = buf[(size_t)srcRow * ni + i];
            if (hasNodata && v == nodata)
                v = TimeStep::MISSING_VALUE;
            out.values[(size_t)j * ni + i] = v;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Interface principale
// ---------------------------------------------------------------------------

void GribReader::addStepDedup(std::vector<TimeStep>& steps, TimeStep&& step) {
    for (const auto& s : steps)
        if (s.validTime == step.validTime) return;
    steps.push_back(std::move(step));
}

bool GribReader::LoadFiles(
    const std::vector<std::string>&     files,
    const std::vector<IndexDefinition>& catalogue,
    std::vector<IndexData>&             outData,
    std::string&                        errMsg
) {
    GDALAllRegister();

    outData.clear();
    if (files.empty())    { errMsg = "Aucun fichier sélectionné."; return false; }
    if (catalogue.empty()){ errMsg = "Catalogue d'indices vide.";  return false; }

    std::vector<IndexData> work(catalogue.size());
    for (size_t k = 0; k < catalogue.size(); ++k)
        work[k].def = catalogue[k];

    int  filesOpened = 0;
    long bandsSeen   = 0;

    for (const auto& path : files) {
        GDALDataset* ds = static_cast<GDALDataset*>(
            GDALOpenEx(path.c_str(),
                       GDAL_OF_RASTER | GDAL_OF_READONLY,
                       nullptr, nullptr, nullptr));
        if (!ds) continue;
        ++filesOpened;

        // Grille commune à toutes les bandes du fichier
        GridInfo fileGrid;
        bool gridOk = readGrid(ds, fileGrid, normLon);

        int nbands = ds->GetRasterCount();
        for (int b = 1; b <= nbands; ++b) {
            ++bandsSeen;
            GDALRasterBand* band = ds->GetRasterBand(b);
            if (!band) continue;

            long disc = -1, cat = -1, num = -1;
            if (!parseBandParam(band, disc, cat, num)) continue;

            for (size_t k = 0; k < catalogue.size(); ++k) {
                const IndexDefinition& d = catalogue[k];
                bool sameParam = (disc == d.discipline &&
                                  cat  == d.parameterCategory);
                bool isScalar  = sameParam && (num == d.parameterNumber);
                bool isDir     = sameParam && (d.directionParamNumber >= 0) &&
                                 (num == d.directionParamNumber);
                if (!isScalar && !isDir) continue;

                if (!work[k].grid.isValid()) {
                    if (!gridOk) break;
                    work[k].grid = fileGrid;
                }

                TimeStep step;
                if (!readBand(band, work[k].grid, step)) break;
                if ((int)step.values.size() != work[k].grid.ni * work[k].grid.nj) break;

                if (isScalar) addStepDedup(work[k].scalarSteps,    std::move(step));
                else          addStepDedup(work[k].directionSteps, std::move(step));
                break;
            }
        }

        GDALClose(ds);
    }

    auto byValidTime = [](const TimeStep& a, const TimeStep& b) {
        return a.validTime < b.validTime;
    };
    for (auto& wdata : work) {
        if (wdata.scalarSteps.empty()) continue;
        std::sort(wdata.scalarSteps.begin(),    wdata.scalarSteps.end(),    byValidTime);
        std::sort(wdata.directionSteps.begin(), wdata.directionSteps.end(), byValidTime);
        outData.push_back(std::move(wdata));
    }

    if (outData.empty()) {
        if (filesOpened == 0) {
            errMsg = "Aucun fichier lisible.";
        } else {
            bool anyDir = false;
            for (const auto& w : work)
                if (!w.directionSteps.empty()) { anyDir = true; break; }
            if (anyDir)
                errMsg = "Des données de direction trouvées, mais aucun champ "
                         "scalaire correspondant.\n"
                         "Ajoutez le fichier de l'indice scalaire.";
            else
                errMsg = "Aucun indice reconnu dans les " +
                         std::to_string(bandsSeen) +
                         " bande(s) GRIB lues. Vérifiez discipline/"
                         "parameterCategory/parameterNumber.";
        }
        return false;
    }
    return true;
}
