#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Fenêtre de contrôle utilisateur (wxWidgets)
 ***************************************************************************/

#include <wx/wx.h>
#include <wx/checkbox.h>
#include <wx/slider.h>
#include <wx/stattext.h>

#include <vector>

class stlaurent_pi;

class StLaurentDialog : public wxDialog {
public:
    StLaurentDialog(wxWindow* parent, stlaurent_pi* plugin);

    // Mise à jour de l'interface après chargement d'une run
    void RefreshAfterLoad();

    // Mise à jour des valeurs au curseur dans le dialog
    // dataIndex : index de l'indice actif (m_currentIndex du plugin)
    // scalar    : valeur scalaire interpolée
    // dir       : direction en degrés (-1 = indisponible)
    // inGrid    : false → afficher "—"
    void UpdateCursorDisplay(int dataIndex, double scalar, double dir, bool inGrid);

private:
    stlaurent_pi* m_plugin;

    // Contrôles UI
    wxButton*           m_btnOpen;      // "Ouvrir run..."
    wxStaticBoxSizer*   m_checkSizer;   // conteneur des checkboxes (indices)
    wxCheckBox*         m_chkLegend;    // "Afficher la légende"
    wxSlider*      m_sliderTime;   // curseur de temps H+1 → H+48
    wxStaticText*  m_lblTime;      // "H+24 — 2026-05-26T18:00Z"
    wxStaticText*  m_lblStatus;    // barre de statut

    // Une checkbox par indice chargé (recréées à chaque chargement)
    std::vector<wxCheckBox*>   m_checkboxes;

    // Label de valeur à droite de chaque checkbox (valeur au curseur)
    std::vector<wxStaticText*> m_valueLabels;

    // Handlers
    void OnOpenRun(wxCommandEvent& evt);
    void OnCheckboxChanged(wxCommandEvent& evt);
    void OnLegendToggle(wxCommandEvent& evt);
    void OnTimeSlider(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    void UpdateTimeLabel();

    wxDECLARE_EVENT_TABLE();
};
