#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Fenêtre de contrôle utilisateur (wxWidgets)
 ***************************************************************************/

#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/stattext.h>

class stlaurent_pi;

class StLaurentDialog : public wxDialog {
public:
    StLaurentDialog(wxWindow* parent, stlaurent_pi* plugin);

    // Mise à jour de l'interface après chargement d'une run
    void RefreshAfterLoad();

private:
    stlaurent_pi* m_plugin;

    // Contrôles UI
    wxButton*   m_btnOpen;       // "Ouvrir run..."
    wxChoice*   m_choiceIndex;   // liste des indices disponibles
    wxSlider*   m_sliderTime;    // curseur de temps H+1 → H+48
    wxStaticText* m_lblTime;     // "H+24 — 2026-05-26T18:00Z"
    wxStaticText* m_lblStatus;   // barre de statut

    // Handlers
    void OnOpenRun(wxCommandEvent& evt);
    void OnIndexChanged(wxCommandEvent& evt);
    void OnTimeSlider(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    void UpdateTimeLabel();

    wxDECLARE_EVENT_TABLE();
};
