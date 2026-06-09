/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Implémentation de la classe principale
 ***************************************************************************/

#include "stlaurent_pi.h"
#include "ui_dialog.h"
#include "icons/stlaurent_icon.h"

#include <wx/wx.h>
#include <wx/mstream.h>

// ---------------------------------------------------------------------------
// Points d'entrée OpenCPN
// ---------------------------------------------------------------------------
extern "C" {
    DECL_EXP int GetABI_VersionMajor() { return stlaurent_pi::MY_API_VERSION_MAJOR; }
    DECL_EXP int GetABI_VersionMinor() { return stlaurent_pi::MY_API_VERSION_MINOR; }

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
    , m_bOverlayVisible(false)
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

    // Charger l'icône rose des vents depuis le tableau C embarqué
    wxMemoryInputStream mis(src_icons_stlaurent_icon_png,
                            src_icons_stlaurent_icon_png_len);
    wxImage img(mis, wxBITMAP_TYPE_PNG);
    wxBitmap icon(img.IsOk() ? img : wxImage(16, 16));

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
            INSTALLS_TOOLBAR_TOOL          |
            WANTS_CURSOR_LATLON);
}

// ---------------------------------------------------------------------------
// DeInit — appelé à la fermeture d'OpenCPN ou désactivation du plugin
// ---------------------------------------------------------------------------
bool stlaurent_pi::DeInit() {
    // Libérer la texture GL pendant que le contexte est encore actif
    if (m_overlayFactory)
        m_overlayFactory->DestroyTexture();

    if (m_dialog) {
        // Destruction SYNCHRONE (delete), pas Destroy() qui diffère la
        // destruction au prochain cycle idle. Après DeInit(), OpenCPN décharge
        // le .so (dlclose) : si le dialog survivait dans wxPendingDelete, wx lui
        // enverrait un événement idle via une vtable désormais dans du code
        // démappé → appel d'un pointeur NULL → SIGSEGV (crash observé).
        // delete exécute ~StLaurentDialog et retire le dialog de la hiérarchie
        // de fenêtres immédiatement, tant que le code du plugin est mappé.
        m_dialog->Hide();
        delete m_dialog;
        m_dialog = nullptr;
    }
    RemovePlugInTool(m_toolbar_item_id);
    return true;
}

// ---------------------------------------------------------------------------
// Icône plugin — évite le nullptr de la classe de base (crash dans OpenCPN
// quand il construit le gestionnaire de plugins)
// ---------------------------------------------------------------------------
wxBitmap* stlaurent_pi::GetPlugInBitmap() {
    static wxBitmap s_bm;
    if (!s_bm.IsOk()) {
        wxMemoryInputStream mis(src_icons_stlaurent_icon_png,
                                src_icons_stlaurent_icon_png_len);
        wxImage img(mis, wxBITMAP_TYPE_PNG);
        s_bm = wxBitmap(img.IsOk() ? img : wxImage(32, 32));
    }
    return &s_bm;
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
    return wxString::FromUTF8(
        "Affichage des indices m\xc3\xa9t\xc3\xa9o-marins pour le Saint-Laurent :\n"
        "indice d'agitation, mer crois\xc3\xa9" "e, et autres indices\n"
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
        // Pas de RequestRefresh ici : l'overlay reste visible quand on
        // ferme le panneau de contrôle (comportement identique à GRIB)
    } else {
        m_dialog->Show();
        SetToolbarItemState(m_toolbar_item_id, true);
        m_dialog->Raise();
    }
}

// ---------------------------------------------------------------------------
// Curseur — appelé par OpenCPN à chaque déplacement souris sur la carte
// ---------------------------------------------------------------------------
void stlaurent_pi::SetCursorLatLon(double lat, double lon) {
    if (!m_overlayFactory || !m_bOverlayVisible) return;

    m_overlayFactory->UpdateCursorPosition(lat, lon);

    // Mettre à jour le label de valeur dans le dialog (s'il est ouvert)
    if (m_dialog) {
        m_dialog->UpdateCursorDisplay(
            m_currentIndex,
            m_overlayFactory->GetCursorScalar(),
            m_overlayFactory->GetCursorDir(),
            m_overlayFactory->GetCursorInGrid()
        );
    }
}

// ---------------------------------------------------------------------------
// Chargement d'un ou plusieurs fichiers GRIB2
// ---------------------------------------------------------------------------
bool stlaurent_pi::LoadFiles(const wxArrayString& paths, wxString& errMsg) {
    // Réinitialiser m_data AVANT clear() pour éviter un pointeur dangling :
    // m_overlayFactory->m_data pointe dans m_loadedData ; si un événement GL
    // se produit après clear() mais avant UpdateOverlay(), BuildTexture()
    // déréférencerait de la mémoire libérée → segfault.
    m_overlayFactory->SetData(nullptr, 0);

    m_loadedData.clear();
    m_currentIndex = 0;
    m_currentStep  = 0;

    std::vector<std::string> files;
    files.reserve(paths.GetCount());
    for (const auto& p : paths)
        files.push_back(std::string(p.ToUTF8()));

    std::string localErr;
    if (!GribReader::LoadFiles(files, IndicesCatalogue::all(),
                               m_loadedData, localErr)) {
        errMsg = wxString::FromUTF8(("Échec du chargement.\n\n" + localErr).c_str());
        return false;
    }

    m_bOverlayVisible = true;
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

void stlaurent_pi::SetLegendVisible(bool show) {
    if (m_overlayFactory) m_overlayFactory->SetShowLegend(show);
    RequestRefresh();
}

bool stlaurent_pi::GetLegendVisible() const {
    return m_overlayFactory ? m_overlayFactory->GetShowLegend() : true;
}

void stlaurent_pi::SetOverlayVisible(bool show) {
    m_bOverlayVisible = show;
    if (!show) {
        // Déconnecter la factory immédiatement pour que le prochain
        // RenderGL ne dessine rien même avant le redraw
        m_overlayFactory->SetData(nullptr, 0);
    } else {
        UpdateOverlay();
    }
}

void stlaurent_pi::RequestRefresh() {
    if (m_parent_window)
        ::RequestRefresh(m_parent_window);
}

// ---------------------------------------------------------------------------
// Rendu — délégation à l'overlay factory
// ---------------------------------------------------------------------------
bool stlaurent_pi::DoRender(PlugIn_ViewPort* vp, bool useGL, wxDC* dc) {
    // Le flag m_bOverlayVisible est contrôlé par les checkboxes du dialog,
    // indépendamment de l'ouverture/fermeture du panneau de contrôle.
    if (!m_bOverlayVisible || m_loadedData.empty()) return false;
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
