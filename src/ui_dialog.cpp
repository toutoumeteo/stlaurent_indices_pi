/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Implémentation de la fenêtre de contrôle
 ***************************************************************************/

#include "ui_dialog.h"
#include "stlaurent_pi.h"
#include "indices_data.h"

#include <wx/dirdlg.h>
#include <wx/statline.h>
#include <ctime>
#include <iomanip>
#include <sstream>

// ---------------------------------------------------------------------------
// Table d'événements
// ---------------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(StLaurentDialog, wxDialog)
    EVT_BUTTON(wxID_OPEN,       StLaurentDialog::OnOpenRun)
    EVT_CHOICE(wxID_ANY,        StLaurentDialog::OnIndexChanged)
    EVT_SLIDER(wxID_ANY,        StLaurentDialog::OnTimeSlider)
    EVT_CLOSE(                  StLaurentDialog::OnClose)
wxEND_EVENT_TABLE()

// ---------------------------------------------------------------------------
// Constructeur
// ---------------------------------------------------------------------------
StLaurentDialog::StLaurentDialog(wxWindow* parent, stlaurent_pi* plugin)
    : wxDialog(parent, wxID_ANY, _("Indices Saint-Laurent"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP)
    , m_plugin(plugin)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* row;

    // --- Bouton ouvrir ---
    m_btnOpen = new wxButton(this, wxID_OPEN, _("Choisir dossier de run..."));
    m_btnOpen->SetToolTip(_("Sélectionner le dossier de la run\n(ex: .../data/2026052518/)"));
    mainSizer->Add(m_btnOpen, 0, wxALL | wxEXPAND, 6);

    // --- Séparateur ---
    mainSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // --- Choix de l'indice ---
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _("Indice :")),
             0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_choiceIndex = new wxChoice(this, wxID_ANY);
    m_choiceIndex->Enable(false);
    row->Add(m_choiceIndex, 1, wxEXPAND);
    mainSizer->Add(row, 0, wxALL | wxEXPAND, 6);

    // --- Curseur de temps ---
    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, _("Temps :")),
             0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_sliderTime = new wxSlider(this, wxID_ANY, 0, 0, 47,
                                 wxDefaultPosition, wxDefaultSize,
                                 wxSL_HORIZONTAL);
    m_sliderTime->Enable(false);
    row->Add(m_sliderTime, 1, wxEXPAND);
    mainSizer->Add(row, 0, wxALL | wxEXPAND, 6);

    // --- Label heure courante ---
    m_lblTime = new wxStaticText(this, wxID_ANY, _("—"),
                                  wxDefaultPosition, wxDefaultSize,
                                  wxALIGN_CENTRE_HORIZONTAL);
    mainSizer->Add(m_lblTime, 0, wxALL | wxEXPAND, 4);

    // --- Séparateur ---
    mainSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // --- Barre de statut ---
    m_lblStatus = new wxStaticText(this, wxID_ANY,
                                    _("Aucune donnée chargée."),
                                    wxDefaultPosition, wxDefaultSize,
                                    wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
    wxFont font = m_lblStatus->GetFont();
    font.SetPointSize(font.GetPointSize() - 1);
    m_lblStatus->SetFont(font);
    mainSizer->Add(m_lblStatus, 0, wxALL | wxEXPAND, 4);

    SetSizerAndFit(mainSizer);
    SetMinSize(wxSize(320, -1));
}

// ---------------------------------------------------------------------------
// Ouvrir un répertoire de run
// ---------------------------------------------------------------------------
void StLaurentDialog::OnOpenRun(wxCommandEvent& /*evt*/) {
    wxDirDialog dlg(this,
                    _("Choisir le dossier de la run\n"
                      "(ex: .../data/2026052518/  — sélectionnez le dossier, pas les fichiers)"),
                    wxEmptyString,
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    wxString runDir = dlg.GetPath();
    m_lblStatus->SetLabel(_("Chargement en cours..."));
    Update();

    wxString errMsg;
    bool ok = m_plugin->LoadRun(runDir, errMsg);

    if (!ok) {
        m_lblStatus->SetLabel(errMsg);
        wxMessageBox(errMsg, _("Erreur"), wxOK | wxICON_ERROR, this);
        return;
    }

    RefreshAfterLoad();
}

// ---------------------------------------------------------------------------
// Mise à jour de l'UI après chargement
// ---------------------------------------------------------------------------
void StLaurentDialog::RefreshAfterLoad() {
    const auto& data = m_plugin->GetLoadedData();
    if (data.empty()) return;

    // Remplir le choix des indices
    m_choiceIndex->Clear();
    for (const auto& d : data)
        m_choiceIndex->Append(wxString::FromUTF8(d.def.displayName));

    int cur = m_plugin->GetCurrentIndex();
    m_choiceIndex->SetSelection(cur < (int)data.size() ? cur : 0);
    m_choiceIndex->Enable(true);

    // Configurer le curseur de temps
    int nSteps = (int)data[cur].scalarSteps.size();
    if (nSteps > 0) {
        m_sliderTime->SetRange(0, nSteps - 1);
        m_sliderTime->SetValue(m_plugin->GetCurrentStep());
        m_sliderTime->Enable(true);
    }

    UpdateTimeLabel();

    wxString msg;
    msg.Printf(_("%d indice(s) chargé(s), %d pas de temps."),
               (int)data.size(), nSteps);
    m_lblStatus->SetLabel(msg);

    Layout();
    Fit();
}

// ---------------------------------------------------------------------------
// Changement d'indice
// ---------------------------------------------------------------------------
void StLaurentDialog::OnIndexChanged(wxCommandEvent& /*evt*/) {
    int sel = m_choiceIndex->GetSelection();
    if (sel < 0) return;

    m_plugin->SetDisplayIndex(sel);

    // Ajuster le curseur au nouveau nombre de pas de temps
    const auto& data = m_plugin->GetLoadedData();
    if (sel < (int)data.size()) {
        int nSteps = (int)data[sel].scalarSteps.size();
        m_sliderTime->SetRange(0, nSteps - 1);
        m_sliderTime->SetValue(0);
    }
    UpdateTimeLabel();
}

// ---------------------------------------------------------------------------
// Déplacement du curseur de temps
// ---------------------------------------------------------------------------
void StLaurentDialog::OnTimeSlider(wxCommandEvent& /*evt*/) {
    int step = m_sliderTime->GetValue();
    m_plugin->SetDisplayStep(step);
    UpdateTimeLabel();
}

// ---------------------------------------------------------------------------
// Mise à jour du label d'heure
// ---------------------------------------------------------------------------
void StLaurentDialog::UpdateTimeLabel() {
    const auto& data = m_plugin->GetLoadedData();
    int idx  = m_plugin->GetCurrentIndex();
    int step = m_plugin->GetCurrentStep();

    if (data.empty() || idx >= (int)data.size()) {
        m_lblTime->SetLabel(_("—"));
        return;
    }

    const auto& steps = data[idx].scalarSteps;
    if (step >= (int)steps.size()) {
        m_lblTime->SetLabel(_("—"));
        return;
    }

    const TimeStep& ts = steps[step];

    // Formater la date UTC
    char buf[32];
    struct tm* tm_utc = gmtime(&ts.validTime);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%MZ", tm_utc);

    wxString label;
    label.Printf("H+%02d  —  %s", ts.stepHours, buf);
    m_lblTime->SetLabel(label);
}

// ---------------------------------------------------------------------------
// Fermeture du dialog
// ---------------------------------------------------------------------------
void StLaurentDialog::OnClose(wxCloseEvent& /*evt*/) {
    Hide();  // Ne pas détruire — juste cacher
}
