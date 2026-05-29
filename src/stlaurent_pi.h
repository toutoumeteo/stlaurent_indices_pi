#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Classe principale du plugin
 ***************************************************************************/

#include "ocpn_plugin.h"
#include "indices_data.h"
#include "grib_reader.h"
#include "overlay_factory.h"

#include <wx/wx.h>
#include <memory>
#include <vector>

class StLaurentDialog;

// ---------------------------------------------------------------------------
class stlaurent_pi : public opencpn_plugin_116 {
public:
    explicit stlaurent_pi(void* ppimgr);
    ~stlaurent_pi() override;

    // --- Interface OpenCPN obligatoire ---
    int      Init() override;
    bool     DeInit() override;
    int      GetAPIVersionMajor() override { return MY_API_VERSION_MAJOR; }
    int      GetAPIVersionMinor() override { return MY_API_VERSION_MINOR; }
    int      GetPlugInVersionMajor() override { return PLUGIN_VERSION_MAJOR; }
    int      GetPlugInVersionMinor() override { return PLUGIN_VERSION_MINOR; }
    wxString GetCommonName() override;       // nom affiché dans le gestionnaire
    wxString GetShortDescription() override; // description courte
    wxString GetLongDescription() override;  // description longue

    // --- Rendu ---
    bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) override;
    bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp) override;
    bool RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp,
                                  int canvasIndex) override;
    bool RenderGLOverlayMultiCanvas(wxGLContext* pcontext, PlugIn_ViewPort* vp,
                                    int canvasIndex) override;

    // --- Toolbar ---
    int  GetToolbarToolCount() override;
    void OnToolbarToolCallback(int id) override;

    // --- Curseur ---
    void SetCursorLatLon(double lat, double lon) override;

    // --- API appelée par le dialog ---

    // Charge tous les indices disponibles pour une run
    // runDir: ex "/home/plante/grib_interpol/data/2026052518/"
    bool LoadRun(const wxString& runDir, wxString& errMsg);

    // Change le pas de temps affiché (index dans scalarSteps)
    void SetDisplayStep(int stepIndex);

    // Change l'indice affiché (index dans m_loadedData)
    void SetDisplayIndex(int dataIndex);

    // Active / désactive l'overlay indépendamment de la fenêtre de contrôle
    void SetOverlayVisible(bool show);
    bool GetOverlayVisible() const { return m_bOverlayVisible; }

    // --- Accès en lecture pour le dialog ---
    const std::vector<IndexData>& GetLoadedData()  const { return m_loadedData; }
    int  GetCurrentStep()  const { return m_currentStep; }
    int  GetCurrentIndex() const { return m_currentIndex; }

    void RequestRefresh();

    // Constantes de version
    static constexpr int MY_API_VERSION_MAJOR = 1;
    static constexpr int MY_API_VERSION_MINOR = 16;
    static constexpr int PLUGIN_VERSION_MAJOR = 1;
    static constexpr int PLUGIN_VERSION_MINOR = 0;

private:
    StLaurentDialog*           m_dialog;
    int                        m_toolbar_item_id;
    wxWindow*                  m_parent_window;

    std::vector<IndexData>     m_loadedData;    // indices chargés
    int                        m_currentIndex;  // indice courant (m_loadedData[m_currentIndex])
    int                        m_currentStep;   // pas de temps courant
    bool                       m_bOverlayVisible; // contrôle indépendant de la fenêtre

    std::unique_ptr<OverlayFactory> m_overlayFactory;

    // Délègue le rendu à l'overlay factory
    bool DoRender(PlugIn_ViewPort* vp, bool useGL, wxDC* dc = nullptr);

    // Synchronise l'overlay factory avec la sélection courante
    void UpdateOverlay();
};

// ---------------------------------------------------------------------------
// Points d'entrée requis par OpenCPN
// ---------------------------------------------------------------------------
extern "C" {
    DECL_EXP opencpn_plugin* create_pi(void* ppimgr);
    DECL_EXP void            destroy_pi(opencpn_plugin* p);
}
