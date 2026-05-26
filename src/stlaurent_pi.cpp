/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Implémentation de la classe principale
 ***************************************************************************/

#include "stlaurent_pi.h"
#include "ui_dialog.h"

#include <wx/wx.h>

// ---------------------------------------------------------------------------
// Points d'entrée OpenCPN
// ---------------------------------------------------------------------------
extern "C" {
    DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
        return new stlaurent_pi(ppimgr);
    }
    DECL_EXP void destroy_pi(opencpn_plugin* p) {
        delete p;
    }
}

// ---------------------------------------------------------------------------
// Constructeur / destructeur
// ---------------------------------------------------------------------------
stlaurent_pi::stlaurent_pi(void* ppimgr)
    : opencpn_plugin_116(ppimgr)
    , m_dialog(nullptr)
    , m_toolbar_item_id(-1)
    , m_parent_window(nullptr)
    , m_currentIndex(0)
    , m_currentStep(0)
    , m_overlayFactory(std::make_unique<OverlayFactory>())
{}

stlaurent_pi::~stlaurent_pi() {
    // m_dialog est détruit par wxWidgets (parent window)
}

// ---------------------------------------------------------------------------
// Init — appelé au démarrage d'OpenCPN ou à l'activation du plugin
// ---------------------------------------------------------------------------
int stlaurent_pi::Init() {
    m_parent_window = GetOCPNCanvasWindow();

    // Ajouter un bouton dans la toolbar OpenCPN
    // TODO: remplacer par une vraie icône (fichier .svg ou .png)
    wxBitmap icon = wxBitmap(16, 16);
    {
        wxMemoryDC dc(icon);
        dc.SetBackground(wxBrush(wxColour(0, 128, 128)));
        dc.Clear();
        dc.SetTextForeground(*wxWHITE);
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                          wxFONTWEIGHT_BOLD));
        dc.DrawText("SL", 1, 1);
    }

    m_toolbar_item_id = InsertPlugInTool(
        "",           // label
        &icon,        // bitmap normal
        &icon,        // bitmap rollover
        wxITEM_CHECK, // toggle button
        _("Indices Saint-Laurent"),
        "",           // tooltip longue
        nullptr,      // bitmap disabled
        -1,           // position (-1 = fin)
        0,            // tool_id (0 = auto)
        this          // parent
    );

    return (WANTS_OVERLAY_CALLBACK         |
            WANTS_OPENGL_OVERLAY_CALLBACK  |
            WANTS_TOOLBAR_CALLBACK         |
            INSTALLS_TOOLBAR_TOOL);
}

// ---------------------------------------------------------------------------
// DeInit — appelé à la fermeture d'OpenCPN ou désactivation du plugin
// ---------------------------------------------------------------------------
bool stlaurent_pi::DeInit() {
    // Libérer la texture GL pendant que le contexte est encore actif
    if (m_overlayFactory)
        m_overlayFactory->DestroyTexture();

    if (m_dialog) {
        m_dialog->Hide();
        m_dialog->Destroy();
        m_dialog = nullptr;
    }
    RemovePlugInTool(m_toolbar_item_id);
    return true;
}

// ---------------------------------------------------------------------------
// Descriptions
// ---------------------------------------------------------------------------
wxString stlaurent_pi::GetCommonName() {
    return _("Indices Saint-Laurent");
}

wxString stlaurent_pi::GetShortDescription() {
    return _("Indices Saint-Laurent");
}

wxString stlaurent_pi::GetLongDescription() {
    return _("Affichage des indices météo-marins pour le Saint-Laurent :\n"
             "indice d'agitation, mer croisée, et autres indices\n"
             "produits par Environnement et Changement climatique Canada.");
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------
int stlaurent_pi::GetToolbarToolCount() {
    return 1;
}

void stlaurent_pi::OnToolbarToolCallback(int id) {
    if (id != m_toolbar_item_id) return;

    // Créer le dialog au premier clic
    if (!m_dialog) {
        m_dialog = new StLaurentDialog(m_parent_window, this);
    }

    // Toggle affichage
    if (m_dialog->IsShown()) {
        m_dialog->Hide();
        SetToolbarItemState(m_toolbar_item_id, false);
    } else {
        m_dialog->Show();
        SetToolbarItemState(m_toolbar_item_id, true);
        m_dialog->Raise();
    }
}

// ---------------------------------------------------------------------------
// Chargement d'une run
// ---------------------------------------------------------------------------
bool stlaurent_pi::LoadRun(const wxString& runDir, wxString& errMsg) {
    // Réinitialiser m_data AVANT clear() pour éviter un pointeur dangling :
    // m_overlayFactory->m_data pointe dans m_loadedData ; si un événement GL
    // se produit après clear() mais avant UpdateOverlay(), BuildTexture()
    // déréférencerait de la mémoire libérée → segfault.
    m_overlayFactory->SetData(nullptr, 0);

    m_loadedData.clear();
    m_currentIndex = 0;
    m_currentStep  = 0;

    std::string dir = std::string(runDir.ToUTF8());
    if (!dir.empty() && dir.back() != '/') dir += '/';

    // Charger tous les indices du catalogue
    auto catalogue = IndicesCatalogue::all();
    bool anyLoaded = false;

    std::string firstErr;
    for (const auto& def : catalogue) {
        IndexData data;
        std::string localErr;
        if (GribReader::LoadIndex(dir, def, data, localErr)) {
            m_loadedData.push_back(std::move(data));
            anyLoaded = true;
        } else if (firstErr.empty()) {
            firstErr = def.displayName + ": " + localErr;
        }
    }

    if (!anyLoaded) {
        errMsg = wxString::FromUTF8(
            "Aucun indice trouvé dans:\n" + dir +
            "\n\nDétail: " + firstErr +
            "\n\nVérifiez que vous avez sélectionné le dossier de la run\n"
            "(ex: .../data/2026052518/)  et non un sous-dossier.");
        return false;
    }

    UpdateOverlay();
    RequestRefresh();
    return true;
}

// ---------------------------------------------------------------------------
// Changement de sélection
// ---------------------------------------------------------------------------
void stlaurent_pi::SetDisplayStep(int stepIndex) {
    m_currentStep = stepIndex;
    UpdateOverlay();
    RequestRefresh();
}

void stlaurent_pi::SetDisplayIndex(int dataIndex) {
    if (dataIndex >= 0 && dataIndex < (int)m_loadedData.size()) {
        m_currentIndex = dataIndex;
        m_currentStep  = 0;
        UpdateOverlay();
        RequestRefresh();
    }
}

void stlaurent_pi::UpdateOverlay() {
    if (m_loadedData.empty()) {
        m_overlayFactory->SetData(nullptr, 0);
        return;
    }
    if (m_currentIndex >= (int)m_loadedData.size())
        m_currentIndex = 0;

    const IndexData& data = m_loadedData[m_currentIndex];
    int maxStep = (int)data.scalarSteps.size() - 1;
    if (m_currentStep > maxStep) m_currentStep = maxStep;

    m_overlayFactory->SetData(&data, m_currentStep);
}

void stlaurent_pi::RequestRefresh() {
    if (m_parent_window)
        ::RequestRefresh(m_parent_window);
}

// ---------------------------------------------------------------------------
// Rendu — délégation à l'overlay factory
// ---------------------------------------------------------------------------
bool stlaurent_pi::DoRender(PlugIn_ViewPort* vp, bool useGL, wxDC* dc) {
    if (m_loadedData.empty()) return false;
    if (useGL)
        return m_overlayFactory->RenderGL(vp);
    else if (dc)
        return m_overlayFactory->RenderDC(*dc, vp);
    return false;
}

bool stlaurent_pi::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) {
    return DoRender(vp, false, &dc);
}

bool stlaurent_pi::RenderGLOverlay(wxGLContext* /*pcontext*/, PlugIn_ViewPort* vp) {
    return DoRender(vp, true);
}

bool stlaurent_pi::RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp,
                                             int /*canvasIndex*/) {
    return DoRender(vp, false, &dc);
}

bool stlaurent_pi::RenderGLOverlayMultiCanvas(wxGLContext* /*pcontext*/,
                                               PlugIn_ViewPort* vp,
                                               int /*canvasIndex*/) {
    return DoRender(vp, true);
}
