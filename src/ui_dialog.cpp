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
// ID dédié pour la checkbox légende (évite la collision avec EVT_CHECKBOX(wxID_ANY))
static const int ID_CHK_LEGEND = wxID_HIGHEST + 1;

wxBEGIN_EVENT_TABLE(StLaurentDialog, wxDialog)
    EVT_BUTTON(wxID_OPEN,           StLaurentDialog::OnOpenRun)
    EVT_CHECKBOX(ID_CHK_LEGEND,     StLaurentDialog::OnLegendToggle)
    EVT_CHECKBOX(wxID_ANY,          StLaurentDialog::OnCheckboxChanged)
    EVT_SLIDER(wxID_ANY,            StLaurentDialog::OnTimeSlider)
    EVT_CLOSE(                      StLaurentDialog::OnClose)
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

    // --- Zone des indices (remplie dynamiquement par RefreshAfterLoad) ---
    // Chaque indice chargé apparaît comme une checkbox — cocher un indice
    // décoche le précédent (comportement radio, identique au plugin GRIB).
    // Fermer la fenêtre ne masque PAS l'overlay ; seul décocher le fait.
    wxStaticBoxSizer* indiceBox =
        new wxStaticBoxSizer(wxVERTICAL, this, _("Indice"));
    m_checkSizer = indiceBox;
    mainSizer->Add(indiceBox, 0, wxALL | wxEXPAND, 6);

    // --- Options d'affichage ---
    m_chkLegend = new wxCheckBox(this, ID_CHK_LEGEND, _("Afficher la légende"));
    m_chkLegend->SetValue(true);
    mainSizer->Add(m_chkLegend, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

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
// Mise à jour de l'UI après chargement d'une run
// Détruit les anciennes checkboxes et en crée une par indice chargé.
// Le premier indice est auto-sélectionné (comportement GRIB).
// ---------------------------------------------------------------------------
void StLaurentDialog::RefreshAfterLoad() {
    const auto& data = m_plugin->GetLoadedData();
    if (data.empty()) return;

    // --- Détruire les anciennes checkboxes et labels ---
    for (wxCheckBox* cb : m_checkboxes) cb->Destroy();
    for (wxStaticText* lbl : m_valueLabels) lbl->Destroy();
    m_checkboxes.clear();
    m_valueLabels.clear();
    // Vider le sizer (les widgets sont déjà détruits)
    m_checkSizer->Clear(false);

    // --- Créer une ligne par indice : [checkbox] [valeur au curseur] ---
    // Sur GTK (Linux) et macOS, les enfants d'un wxStaticBoxSizer DOIVENT
    // avoir le StaticBox comme parent — pas le dialog — sinon ils n'apparaissent pas.
    wxWindow* cbParent = m_checkSizer->GetStaticBox();
    for (const auto& d : data) {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

        wxCheckBox* cb = new wxCheckBox(cbParent, wxID_ANY,
                                        wxString::FromUTF8(d.def.displayName));
        row->Add(cb, 1, wxALIGN_CENTER_VERTICAL);

        // Label de valeur : largeur fixe, aligné à droite
        // Monospace pour que les chiffres ne bougent pas d'un rendu à l'autre
        wxStaticText* lbl = new wxStaticText(cbParent, wxID_ANY, wxT("—"),
                                              wxDefaultPosition, wxSize(130, -1),
                                              wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
        wxFont monoFont = lbl->GetFont();
        monoFont.SetFamily(wxFONTFAMILY_TELETYPE);
        lbl->SetFont(monoFont);
        row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

        m_checkSizer->Add(row, 0, wxALL | wxEXPAND, 4);
        m_checkboxes.push_back(cb);
        m_valueLabels.push_back(lbl);
    }

    // --- Auto-sélectionner le premier indice ---
    if (!m_checkboxes.empty()) {
        m_checkboxes[0]->SetValue(true);
        m_plugin->SetDisplayIndex(0);
        // m_bOverlayVisible est déjà true (mis par LoadRun)

        int nSteps = (int)data[0].scalarSteps.size();
        if (nSteps > 0) {
            m_sliderTime->SetRange(0, nSteps - 1);
            m_sliderTime->SetValue(m_plugin->GetCurrentStep());
            m_sliderTime->Enable(true);
        }
    }

    UpdateTimeLabel();

    wxString msg;
    msg.Printf(_("%d indice(s) chargé(s), %d pas de temps."),
               (int)data.size(), (int)data[0].scalarSteps.size());
    m_lblStatus->SetLabel(msg);

    Layout();
    Fit();
}

// ---------------------------------------------------------------------------
// Changement d'état d'une checkbox d'indice
//   • Cocher  → décocher toutes les autres, afficher cet indice
//   • Décocher → si aucune n'est cochée, masquer l'overlay
// ---------------------------------------------------------------------------
void StLaurentDialog::OnCheckboxChanged(wxCommandEvent& evt) {
    wxCheckBox* changed = wxDynamicCast(evt.GetEventObject(), wxCheckBox);
    if (!changed) return;

    if (changed->GetValue()) {
        // Trouver l'index de la checkbox cochée et décocher les autres
        int newIndex = -1;
        for (int i = 0; i < (int)m_checkboxes.size(); ++i) {
            if (m_checkboxes[i] == changed) {
                newIndex = i;
            } else {
                m_checkboxes[i]->SetValue(false);
            }
        }
        if (newIndex < 0) return;

        m_plugin->SetDisplayIndex(newIndex);
        m_plugin->SetOverlayVisible(true);

        // Adapter le curseur au nombre de pas de temps du nouvel indice
        const auto& data = m_plugin->GetLoadedData();
        if (newIndex < (int)data.size()) {
            int nSteps = (int)data[newIndex].scalarSteps.size();
            m_sliderTime->SetRange(0, nSteps - 1);
            m_sliderTime->SetValue(0);
            m_sliderTime->Enable(nSteps > 0);
        }

    } else {
        // Vérifier si au moins une checkbox reste cochée
        bool anyChecked = false;
        for (auto* cb : m_checkboxes)
            anyChecked = anyChecked || cb->GetValue();

        if (!anyChecked) {
            // Tout est décoché → masquer l'overlay, libère la place pour GRIB
            m_plugin->SetOverlayVisible(false);
            m_sliderTime->Enable(false);
        }
    }

    UpdateTimeLabel();
    m_plugin->RequestRefresh();
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
// Toggle légende
// ---------------------------------------------------------------------------
void StLaurentDialog::OnLegendToggle(wxCommandEvent& /*evt*/) {
    m_plugin->SetLegendVisible(m_chkLegend->GetValue());
}

// ---------------------------------------------------------------------------
// Mise à jour de la valeur au curseur dans le label à droite de la checkbox
// Appelé depuis stlaurent_pi::SetCursorLatLon() à chaque déplacement souris.
// ---------------------------------------------------------------------------
void StLaurentDialog::UpdateCursorDisplay(int dataIndex, double scalar,
                                           double dir, bool inGrid) {
    if (dataIndex < 0 || dataIndex >= (int)m_valueLabels.size()) return;
    wxStaticText* lbl = m_valueLabels[dataIndex];

    wxString text;
    if (!inGrid) {
        text = wxT("—");
    } else {
        const auto& data = m_plugin->GetLoadedData();
        wxString units = (dataIndex < (int)data.size())
            ? wxString::FromUTF8(data[dataIndex].def.units.c_str())
            : wxT("");

        text = wxString::Format(wxT("%.2f %s"), scalar, units);

        if (dir >= 0.0) {
            double dispDeg = fmod(dir + 360.0, 360.0);
            text += wxString::Format(wxT("  %.0f°"), dispDeg);
        }
    }

    // Éviter les SetLabel() redondants (appelé à chaque mouvement souris)
    if (lbl->GetLabel() != text)
        lbl->SetLabel(text);
}

// ---------------------------------------------------------------------------
// Fermeture du dialog — cache le panneau de contrôle sans toucher à l'overlay
// ---------------------------------------------------------------------------
void StLaurentDialog::OnClose(wxCloseEvent& /*evt*/) {
    Hide();  // Ne pas détruire — juste cacher le panneau de contrôle
             // L'overlay reste affiché si une checkbox était cochée
}
