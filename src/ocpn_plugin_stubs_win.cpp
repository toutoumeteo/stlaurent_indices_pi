/***************************************************************************
 * ocpn_plugin_stubs_win.cpp — Windows-only
 *
 * Sur Linux/macOS, le chargeur dynamique résout les symboles OpenCPN à
 * l'exécution depuis l'exécutable hôte (opencpn / OpenCPN.app).
 *
 * Sur Windows, l'éditeur de liens MSVC exige que TOUS les symboles soient
 * résolus à la compilation. Sans opencpn.lib (la bibliothèque d'import
 * d'OpenCPN), le linker ne peut pas trouver :
 *
 *   1. Les fonctions virtuelles des classes de base (opencpn_plugin → _116)
 *      dont stlaurent_pi hérite mais ne redéfinit pas.
 *
 *   2. Les fonctions C de l'API (GetCanvasPixLL, InsertPlugInTool, …)
 *      appelées par notre code.
 *
 * Ce fichier fournit des implémentations par défaut pour ces deux groupes.
 *
 * IMPORTANT RUNTIME : ces stubs sont liés statiquement dans notre DLL.
 *   - Les virtuelles de base → appelées si stlaurent_pi ne les redéfinit pas
 *     (comportement identique au comportement attendu : valeurs par défaut).
 *   - Les fonctions API → appellent les vraies implémentations d'OpenCPN
 *     UNIQUEMENT si vous liez contre opencpn.lib. Avec ces stubs, elles
 *     retournent des valeurs sûres/neutres (plugin chargé mais inactif).
 *     Pour un fonctionnement complet sur Windows, recompilez avec opencpn.lib.
 ***************************************************************************/

#ifdef _WIN32

#include "ocpn_plugin.h"

// ============================================================
// opencpn_plugin  (classe de base)
// ============================================================
opencpn_plugin::~opencpn_plugin() {}
int            opencpn_plugin::Init()                                    { return 0; }
bool           opencpn_plugin::DeInit()                                  { return true; }
int            opencpn_plugin::GetAPIVersionMajor()                      { return 0; }
int            opencpn_plugin::GetAPIVersionMinor()                      { return 0; }
int            opencpn_plugin::GetPlugInVersionMajor()                   { return 0; }
int            opencpn_plugin::GetPlugInVersionMinor()                   { return 0; }
wxBitmap*      opencpn_plugin::GetPlugInBitmap()                         { return nullptr; }
wxString       opencpn_plugin::GetCommonName()                           { return wxEmptyString; }
wxString       opencpn_plugin::GetShortDescription()                     { return wxEmptyString; }
wxString       opencpn_plugin::GetLongDescription()                      { return wxEmptyString; }
void           opencpn_plugin::SetDefaults()                             {}
int            opencpn_plugin::GetToolbarToolCount()                     { return 0; }
int            opencpn_plugin::GetToolboxPanelCount()                    { return 0; }
void           opencpn_plugin::SetupToolboxPanel(int, wxNotebook*)       {}
void           opencpn_plugin::OnCloseToolboxPanel(int, int)             {}
void           opencpn_plugin::ShowPreferencesDialog(wxWindow*)          {}
bool           opencpn_plugin::RenderOverlay(wxMemoryDC*, PlugIn_ViewPort*) { return false; }
void           opencpn_plugin::SetCursorLatLon(double, double)           {}
void           opencpn_plugin::SetCurrentViewPort(PlugIn_ViewPort&)      {}
void           opencpn_plugin::SetPositionFix(PlugIn_Position_Fix&)      {}
void           opencpn_plugin::SetNMEASentence(wxString&)                {}
void           opencpn_plugin::SetAISSentence(wxString&)                 {}
void           opencpn_plugin::ProcessParentResize(int, int)             {}
void           opencpn_plugin::SetColorScheme(PI_ColorScheme)            {}
void           opencpn_plugin::OnToolbarToolCallback(int)                {}
void           opencpn_plugin::OnContextMenuItemCallback(int)            {}
void           opencpn_plugin::UpdateAuiStatus()                         {}
wxArrayString  opencpn_plugin::GetDynamicChartClassNameArray()           { return wxArrayString(); }

// ============================================================
// opencpn_plugin_16  (branche parallèle — pas notre ancêtre,
//   mais MSVC peut en avoir besoin selon les unités de compilation)
// ============================================================
opencpn_plugin_16::opencpn_plugin_16(void* pmgr) : opencpn_plugin(pmgr) {}
opencpn_plugin_16::~opencpn_plugin_16() {}
bool opencpn_plugin_16::RenderOverlay(wxDC&, PlugIn_ViewPort*)           { return false; }
void opencpn_plugin_16::SetPluginMessage(wxString&, wxString&)           {}

// ============================================================
// opencpn_plugin_17  (branche parallèle)
// ============================================================
opencpn_plugin_17::opencpn_plugin_17(void* pmgr) : opencpn_plugin(pmgr) {}
opencpn_plugin_17::~opencpn_plugin_17() {}
bool opencpn_plugin_17::RenderOverlay(wxDC&, PlugIn_ViewPort*)           { return false; }
bool opencpn_plugin_17::RenderGLOverlay(wxGLContext*, PlugIn_ViewPort*)  { return false; }
void opencpn_plugin_17::SetPluginMessage(wxString&, wxString&)           {}

// ============================================================
// opencpn_plugin_18  (notre ancêtre via _19→_110→…→_116)
// ============================================================
opencpn_plugin_18::opencpn_plugin_18(void* pmgr) : opencpn_plugin(pmgr) {}
opencpn_plugin_18::~opencpn_plugin_18() {}
bool opencpn_plugin_18::RenderOverlay(wxDC&, PlugIn_ViewPort*)           { return false; }
bool opencpn_plugin_18::RenderGLOverlay(wxGLContext*, PlugIn_ViewPort*)  { return false; }
void opencpn_plugin_18::SetPluginMessage(wxString&, wxString&)           {}
void opencpn_plugin_18::SetPositionFixEx(PlugIn_Position_Fix_Ex&)        {}

// ============================================================
// opencpn_plugin_19
// ============================================================
opencpn_plugin_19::opencpn_plugin_19(void* pmgr) : opencpn_plugin_18(pmgr) {}
opencpn_plugin_19::~opencpn_plugin_19() {}
void opencpn_plugin_19::OnSetupOptions() {}

// ============================================================
// opencpn_plugin_110
// ============================================================
opencpn_plugin_110::opencpn_plugin_110(void* pmgr) : opencpn_plugin_19(pmgr) {}
opencpn_plugin_110::~opencpn_plugin_110() {}
void opencpn_plugin_110::LateInit() {}

// ============================================================
// opencpn_plugin_111
// ============================================================
opencpn_plugin_111::opencpn_plugin_111(void* pmgr) : opencpn_plugin_110(pmgr) {}
opencpn_plugin_111::~opencpn_plugin_111() {}

// ============================================================
// opencpn_plugin_112
// ============================================================
opencpn_plugin_112::opencpn_plugin_112(void* pmgr) : opencpn_plugin_111(pmgr) {}
opencpn_plugin_112::~opencpn_plugin_112() {}
bool opencpn_plugin_112::MouseEventHook(wxMouseEvent&)                   { return false; }
void opencpn_plugin_112::SendVectorChartObjectInfo(
    wxString&, wxString&, wxString&, double, double, double, int)        {}

// ============================================================
// opencpn_plugin_113
// ============================================================
opencpn_plugin_113::opencpn_plugin_113(void* pmgr) : opencpn_plugin_112(pmgr) {}
opencpn_plugin_113::~opencpn_plugin_113() {}
bool opencpn_plugin_113::KeyboardEventHook(wxKeyEvent&)                  { return false; }
void opencpn_plugin_113::OnToolbarToolDownCallback(int)                  {}
void opencpn_plugin_113::OnToolbarToolUpCallback(int)                    {}

// ============================================================
// opencpn_plugin_114
// ============================================================
opencpn_plugin_114::opencpn_plugin_114(void* pmgr) : opencpn_plugin_113(pmgr) {}
opencpn_plugin_114::~opencpn_plugin_114() {}

// ============================================================
// opencpn_plugin_115
// ============================================================
opencpn_plugin_115::opencpn_plugin_115(void* pmgr) : opencpn_plugin_114(pmgr) {}
opencpn_plugin_115::~opencpn_plugin_115() {}

// ============================================================
// opencpn_plugin_116  (classe directement héritée par stlaurent_pi)
// ============================================================
opencpn_plugin_116::opencpn_plugin_116(void* pmgr) : opencpn_plugin_115(pmgr) {}
opencpn_plugin_116::~opencpn_plugin_116() {}
bool opencpn_plugin_116::RenderGLOverlayMultiCanvas(wxGLContext*, PlugIn_ViewPort*, int) { return false; }
bool opencpn_plugin_116::RenderOverlayMultiCanvas(wxDC&, PlugIn_ViewPort*, int)          { return false; }
void opencpn_plugin_116::PrepareContextMenu(int)                         {}

// ============================================================
// Fonctions C de l'API OpenCPN utilisées par notre code
//
// Ces stubs permettent à la DLL de se lier et de charger.
// Sans opencpn.lib, ils remplacent les vraies implémentations.
// Les retours sont des valeurs sûres/neutres.
// ============================================================
extern "C" {

wxWindow* GetOCPNCanvasWindow() {
    return nullptr;
}

int InsertPlugInTool(wxString, wxBitmap*, wxBitmap*, wxItemKind,
                     wxString, wxString, wxObject*, int, int, opencpn_plugin*) {
    return -1;
}

void RemovePlugInTool(int) {}

void SetToolbarItemState(int, bool) {}

void RequestRefresh(wxWindow*) {}

void GetCanvasPixLL(PlugIn_ViewPort*, wxPoint* pp, double, double) {
    if (pp) { pp->x = 0; pp->y = 0; }
}

} // extern "C"

#endif  // _WIN32
