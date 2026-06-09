/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Implémentation du rendu OpenGL
 ***************************************************************************/

#include "overlay_factory.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Chargement dynamique de glUseProgram (GL2+)
// OpenCPN 5.x a un shader actif → on doit le désactiver avant le rendu
// fixed-function, puis le restaurer.
// On charge glUseProgram dynamiquement pour ne pas lier explicitement
// contre une lib GL2 qui peut ne pas exister sur toutes les plateformes.
// ---------------------------------------------------------------------------
#ifdef _WIN32
  // Windows : wglGetProcAddress, avec fallback sur opengl32.dll
  static void* getGLProc(const char* name) {
      void* p = (void*)wglGetProcAddress(name);
      if (!p) {
          // Certains pilotes anciens exposent glUseProgram dans opengl32.dll
          HMODULE mod = GetModuleHandleA("opengl32.dll");
          if (mod) p = (void*)GetProcAddress(mod, name);
      }
      return p;
  }
#else
  // Linux / macOS : dlsym sur le processus courant
  #include <dlfcn.h>
  static void* getGLProc(const char* name) {
      return dlsym(RTLD_DEFAULT, name);
  }
#endif

typedef void (*PFNGLUSEPROGRAMPROC)(GLuint program);
static PFNGLUSEPROGRAMPROC s_glUseProgram = nullptr;
static bool s_glUseProgramLoaded = false;

static void ensureGLUseProgram() {
    if (!s_glUseProgramLoaded) {
        s_glUseProgram = (PFNGLUSEPROGRAMPROC)getGLProc("glUseProgram");
        s_glUseProgramLoaded = true;
    }
}

#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
OverlayFactory::OverlayFactory()
    : m_data(nullptr)
    , m_stepIndex(0)
    , m_showLegend(true)
    , m_textureId(0)
    , m_textureValid(false)
    , m_texWidth(0)
    , m_texHeight(0)
    , m_legendTexId(0)
    , m_legendTexValid(false)
    , m_legendTexW(0)
    , m_legendTexH(0)
    , m_legendPixelsReady(false)
    , m_cursorInGrid(false)
    , m_cursorScalar(0.0)
    , m_cursorDir(-1.0)
    , m_cursorGridI(-1)
    , m_cursorGridJ(-1)
{}

OverlayFactory::~OverlayFactory() {
    // NE PAS appeler glDeleteTextures ici : le contexte GL peut déjà être
    // détruit quand le destructeur s'exécute → crash.
    // La texture est libérée par le contexte GL lors de sa destruction.
    m_textureId = 0;
}

// ---------------------------------------------------------------------------
void OverlayFactory::SetData(const IndexData* data, int stepIndex) {
    m_data      = data;
    m_stepIndex = stepIndex;
    InvalidateTexture();
    m_legendTexValid    = false;
    m_legendPixelsReady = false;
    m_legendBitmap      = wxBitmap();
    // Construire les pixels de légende ICI, hors contexte GL.
    // Sur Windows, wxBitmap ne peut pas être créé pendant un callback WGL.
    if (m_data) BuildLegendPixels();
}

void OverlayFactory::InvalidateTexture() {
    // Marque seulement les textures comme invalides — PAS de glDeleteTextures ici
    // car cette fonction est appelée hors contexte GL (chargement de données, etc.)
    m_textureValid  = false;
    // Forcer le recalcul des valeurs au curseur au prochain mouvement
    m_cursorGridI = -1;
    m_cursorGridJ = -1;
}

void OverlayFactory::DestroyTexture() {
    // À appeler UNIQUEMENT depuis DeInit(), pendant que le contexte GL est actif
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_textureValid = false;

    if (m_legendTexId) {
        glDeleteTextures(1, &m_legendTexId);
        m_legendTexId = 0;
    }
    m_legendTexValid = false;

}

// ---------------------------------------------------------------------------
// Rendu principal OpenGL
// ---------------------------------------------------------------------------
bool OverlayFactory::RenderGL(PlugIn_ViewPort* vp) {
    if (!m_data || !m_data->isLoaded()) return false;
    if (m_stepIndex >= (int)m_data->scalarSteps.size()) return false;

    if (!m_textureValid)    BuildTexture();
    if (!m_textureId)       return true;
    // Phase 2 de la légende : upload en texture GL si les pixels sont prêts.
    // Les pixels ont été construits dans SetData() (hors GL), ce qui évite
    // le conflit GDI/WGL sur Windows.
    if (m_showLegend && m_legendPixelsReady && !m_legendTexValid)
        UploadLegendTexture();

    ensureGLUseProgram();
    GLint saved_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &saved_program);
    if (saved_program && s_glUseProgram)
        s_glUseProgram(0);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, vp->pix_width, vp->pix_height, 0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    DrawTexture(vp);

    if (m_data->hasDirection() &&
        m_stepIndex < (int)m_data->directionSteps.size()) {
        DrawArrows(vp);
    }

    if (m_showLegend) DrawLegend(vp);

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glPopAttrib();

    // Remettre la couleur courante à blanc opaque — glPopAttrib restaure
    // GL_CURRENT_BIT mais certaines implémentations (pilotes anciens) ne
    // le font pas fiablement, ce qui peut corrompre la palette d'autres
    // plugins (ex: GRIB) qui lisent la couleur courante sans la définir.
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    if (saved_program && s_glUseProgram)
        s_glUseProgram(saved_program);

    return true;
}

// ---------------------------------------------------------------------------
// Rendu DC fallback (non-OpenGL)
//
// Sur Windows, OpenCPN peut être configuré sans OpenGL (logiciel, VM sans GPU).
// Dans ce cas, seul RenderOverlay (DC) est appelé — jamais RenderGLOverlay.
// → Ce rendu doit produire un résultat acceptable : blocs pleins + flèches.
//
// Principe : calculer la taille pixel d'une maille à l'échelle courante,
// dessiner un rectangle plein centré sur chaque point de grille sélectionné.
// Les blocs se touchent exactement → remplissage solide à toute échelle.
// ---------------------------------------------------------------------------
bool OverlayFactory::RenderDC(wxDC& dc, PlugIn_ViewPort* vp) {
    if (!m_data || !m_data->isLoaded()) return false;
    if (m_stepIndex >= (int)m_data->scalarSteps.size()) return false;

    const GridInfo&  g  = m_data->grid;
    const TimeStep&  ts = m_data->scalarSteps[m_stepIndex];
    if (!ts.matchesGrid(g)) return false;

    // Taille pixel d'une maille (coin SW de la grille comme référence)
    wxPoint p00, p10, p01;
    GetCanvasPixLL(vp, &p00, g.lat(0), g.lon(0));
    GetCanvasPixLL(vp, &p10, g.lat(0), g.lon(g.ni > 1 ? 1 : 0));
    GetCanvasPixLL(vp, &p01, g.lat(g.nj > 1 ? 1 : 0), g.lon(0));
    int cellW = std::max(1, std::abs(p10.x - p00.x));
    int cellH = std::max(1, std::abs(p01.y - p00.y));

    // Sous-échantillonnage : viser des blocs d'au moins 2 px de large pour
    // qu'ils se touchent et forment un remplissage sans trous.
    int step  = std::max(1, (int)std::ceil(2.0 / cellW));
    int drawW = cellW * step;
    int drawH = cellH * step;

    dc.SetPen(*wxTRANSPARENT_PEN);

    for (int j = 0; j < g.nj; j += step) {
        for (int i = 0; i < g.ni; i += step) {
            double v = ts.get(i, j, g);
            if (v >= TimeStep::MISSING_VALUE - 1.0) continue;

            unsigned char r, gv, b, a;
            ValueToRGBA((float)v, m_data->def, r, gv, b, a);
            if (a == 0) continue;

            wxPoint p;
            GetCanvasPixLL(vp, &p, g.lat(j), g.lon(i));
            dc.SetBrush(wxBrush(wxColour(r, gv, b)));
            dc.DrawRectangle(p.x - drawW / 2, p.y - drawH / 2, drawW, drawH);
        }
    }

    if (m_data->hasDirection() &&
        m_stepIndex < (int)m_data->directionSteps.size()) {
        DrawArrowsDC(dc, vp);
    }

    if (m_showLegend && m_legendBitmap.IsOk()) {
        const int margin = 15;
        int x = (vp->pix_width  - m_legendBitmap.GetWidth())  / 2;
        int y =  vp->pix_height - margin - m_legendBitmap.GetHeight();
        dc.DrawBitmap(m_legendBitmap, x, y, false);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Construction de la texture OpenGL depuis les données scalaires
// ---------------------------------------------------------------------------
void OverlayFactory::BuildTexture() {
    if (!m_data || m_stepIndex >= (int)m_data->scalarSteps.size()) return;

    const GridInfo& g  = m_data->grid;
    if (!g.isValid()) return;

    const TimeStep& ts = m_data->scalarSteps[m_stepIndex];
    // Vérification de cohérence taille grille ↔ taille données
    if ((int)ts.values.size() != g.ni * g.nj) return;

    m_texWidth  = g.ni;
    m_texHeight = g.nj;

    // Remplir le buffer RGBA
    std::vector<unsigned char> buf(m_texWidth * m_texHeight * 4, 0);

    for (int j = 0; j < g.nj; ++j) {
        for (int i = 0; i < g.ni; ++i) {
            double v = ts.get(i, j, g);

            // Index dans le buffer : OpenGL origin = bas-gauche
            // Nos données j=0 = sud (bas) → correspondance directe
            int idx = (j * m_texWidth + i) * 4;

            if (v >= TimeStep::MISSING_VALUE - 1.0) {
                // Transparent pour terre/no-data
                buf[idx+0] = 0;
                buf[idx+1] = 0;
                buf[idx+2] = 0;
                buf[idx+3] = 0;
            } else {
                unsigned char r, g_c, b, a;
                ValueToRGBA((float)v, m_data->def, r, g_c, b, a);
                // Alpha prémultiplié : RGB stockés multipliés par alpha. Combiné
                // au blend GL_ONE/GL_ONE_MINUS_SRC_ALPHA dans DrawTexture, cela
                // élimine le liseré sombre aux côtes (interpolation GL_LINEAR
                // entre un texel coloré et un texel transparent noir).
                buf[idx+0] = (unsigned char)(r   * a / 255);
                buf[idx+1] = (unsigned char)(g_c * a / 255);
                buf[idx+2] = (unsigned char)(b   * a / 255);
                buf[idx+3] = a;
            }
        }
    }

    // Créer/mettre à jour la texture OpenGL
    if (!m_textureId) glGenTextures(1, &m_textureId);

    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_texWidth, m_texHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_textureValid = true;
}

// ---------------------------------------------------------------------------
// Dessin de la texture sur la carte
//
// IMPORTANT — pourquoi dessiner rangée par rangée (nj quads) ?
// ----------------------------------------------------------------
// OpenCPN utilise la projection Mercator : la latitude se convertit en pixels
// de façon NON-LINÉAIRE (y = ln(tan(π/4 + lat/2))). Un seul GL_QUAD pour
// toute la grille interpole linéairement entre les 4 coins, ce qui produit
// une erreur systématique de ~30 km au centre de la grille (≈48°N).
//
// La solution : découper la grille en nj bandes horizontales d'une rangée
// chacune. Pour chaque bande, GetCanvasPixLL calcule exactement les pixels
// des bords sud/nord → l'erreur résiduelle est sub-pixel (< 0.1 km).
// ---------------------------------------------------------------------------
void OverlayFactory::DrawTexture(PlugIn_ViewPort* vp) {
    const GridInfo& g = m_data->grid;

    // Bords ouest et est de la grille (centres ± une demi-maille)
    double lonW = g.lon(0)        - g.dlon / 2.0;
    double lonE = g.lon(g.ni - 1) + g.dlon / 2.0;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    // Ré-appliquer le filtre linéaire : les paramètres de la texture sont stockés
    // dans l'objet GL, pas dans le push/pop d'attributs. Sur certains pilotes
    // Windows (VM, logiciel), une autre partie du code pourrait les avoir modifiés.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // Forcer GL_MODULATE pour que glColor4f contrôle l'opacité correctement.
    // Certains plugins (ex: GRIB) laissent GL_TEXTURE_ENV_MODE à GL_REPLACE ou
    // GL_DECAL ce qui ignorerait notre couleur et afficherait la texture opaque.
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // Alpha prémultiplié (cf. BuildTexture) : la texture stocke RGB×alpha.
    // On module RGB ET alpha par l'opacité globale via glColor4f(A,A,A,A),
    // puis on additionne avec GL_ONE/GL_ONE_MINUS_SRC_ALPHA.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const float opacity = 0.75f;
    glColor4f(opacity, opacity, opacity, opacity);

    // Précalcul des nj+1 latitudes-frontières → pixels ouest/est.
    // Le bord nord de la rangée j EST le bord sud de la rangée j+1 : chaque
    // frontière n'est donc projetée qu'une seule fois (≈2×(nj+1) appels à
    // GetCanvasPixLL au lieu de 4×nj). N'assume aucune projection particulière
    // (la rotation de carte reste gérée correctement, coin par coin).
    const int nb = g.nj + 1;
    std::vector<wxPoint> wpt(nb), ept(nb);  // bords ouest / est de chaque frontière
    const double latBase = g.lat(0) - g.dlat / 2.0;
    for (int b = 0; b < nb; ++b) {
        double lat = latBase + b * g.dlat;
        GetCanvasPixLL(vp, &wpt[b], lat, lonW);
        GetCanvasPixLL(vp, &ept[b], lat, lonE);
    }

    glBegin(GL_QUADS);
    for (int j = 0; j < g.nj; ++j) {
        // Coordonnées texture verticales : j=0 → bas (t=0), j=nj-1 → haut (t=1)
        float tv0 = (float)j       / (float)g.nj;
        float tv1 = (float)(j + 1) / (float)g.nj;

        const wxPoint& sw = wpt[j];      // sud-ouest
        const wxPoint& se = ept[j];      // sud-est
        const wxPoint& nw = wpt[j + 1];  // nord-ouest
        const wxPoint& ne = ept[j + 1];  // nord-est

        glTexCoord2f(0.0f, tv0);  glVertex2i(sw.x, sw.y);
        glTexCoord2f(1.0f, tv0);  glVertex2i(se.x, se.y);
        glTexCoord2f(1.0f, tv1);  glVertex2i(ne.x, ne.y);
        glTexCoord2f(0.0f, tv1);  glVertex2i(nw.x, nw.y);
    }
    glEnd();

    // Restaurer le blend standard pour les flèches et la légende
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// ---------------------------------------------------------------------------
// Phase 1 — construction des pixels de légende via wxBitmap + wxMemoryDC.
// Appelée depuis SetData(), HORS de tout contexte GL.
// Sur Windows, un wxBitmap créé pendant un callback WGL (GDI et OpenGL
// simultanés) provoque un crash ; cette séparation l'évite sur toutes
// les plateformes.
// ---------------------------------------------------------------------------
void OverlayFactory::BuildLegendPixels() {
    if (!m_data) return;
    const IndexDefinition& def = m_data->def;

    double scale = 1.0;
    if (wxWindow* cw = GetOCPNCanvasWindow())
        scale = std::max(1.0, std::min(4.0, (double)cw->GetContentScaleFactor()));
    auto S = [scale](int v) { return (int)std::lround(v * scale); };

    const int W = S(210), H = S(65);
    m_legendTexW = W;
    m_legendTexH = H;

    wxBitmap bmp(W, H);
    {
        wxMemoryDC dc(bmp);

        dc.SetBackground(wxBrush(wxColour(25, 25, 25)));
        dc.Clear();

        const int fontPt = S(8);

        wxString title = wxString::FromUTF8(def.displayName.c_str())
                       + wxT(" [") + wxString::FromUTF8(def.units.c_str()) + wxT("]");
        dc.SetFont(wxFont(fontPt, wxFONTFAMILY_DEFAULT,
                          wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(wxColour(230, 230, 230));
        dc.DrawText(title, S(8), S(5));

        const int barX = S(8), barY = S(23), barW = W - 2 * barX, barH = S(18);
        if (!def.colorScale.empty()) {
            int n = (int)def.colorScale.size();
            for (int k = 0; k < barW; ++k) {
                int lvl = std::max(0, std::min(n - 1, (int)(k * n / barW)));
                auto& c = def.colorScale[lvl];
                dc.SetPen(wxPen(wxColour(c.r, c.g, c.b)));
                dc.DrawLine(barX + k, barY, barX + k, barY + barH);
            }
        } else {
            for (int x = 0; x < barW; ++x) {
                float t = (float)x / (float)(barW - 1);
                float v = (float)(def.minValue + t * (def.maxValue - def.minValue));
                unsigned char r, g, b, a;
                ValueToRGBA(v, def, r, g, b, a);
                dc.SetPen(wxPen(wxColour(r, g, b)));
                dc.DrawLine(barX + x, barY, barX + x, barY + barH);
            }
        }
        dc.SetPen(wxPen(wxColour(160, 160, 160)));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(barX, barY, barW, barH);

        dc.SetFont(wxFont(fontPt, wxFONTFAMILY_DEFAULT,
                          wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(wxColour(200, 200, 200));
        wxString minStr = wxString::Format(wxT("%.2f"), def.minValue);
        wxString maxStr = wxString::Format(wxT("%.1f"), def.colorScale.empty()
                            ? def.maxValue : def.colorScale.back().threshold);
        const int textY = barY + barH + S(3);
        dc.DrawText(minStr, barX, textY);
        wxSize maxSz = dc.GetTextExtent(maxStr);
        dc.DrawText(maxStr, barX + barW - maxSz.x, textY);

    }  // dc libéré ici → SelectObject(wxNullBitmap) implicite

    // Conserver le bitmap pour RenderDC (Windows sans OpenGL).
    m_legendBitmap = bmp;

    // Stocker les pixels RGB — wxImage ligne 0 = haut de l'image.
    // glTexImage2D interprète ligne 0 = bas de la texture.
    // DrawLegend() compense en assignant v=0 au coin haut de l'écran.
    wxImage img = bmp.ConvertToImage();
    const unsigned char* rgb = img.GetData();
    m_legendPixels.assign(rgb, rgb + (size_t)W * H * 3);
    m_legendPixelsReady = true;
}

// ---------------------------------------------------------------------------
// Phase 2 — upload des pixels pré-calculés vers une texture OpenGL.
// Appelée depuis RenderGL(), dans le contexte GL actif.
// ---------------------------------------------------------------------------
void OverlayFactory::UploadLegendTexture() {
    if (!m_legendPixelsReady || m_legendPixels.empty()) return;

    if (!m_legendTexId) glGenTextures(1, &m_legendTexId);
    glBindTexture(GL_TEXTURE_2D, m_legendTexId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_legendTexW, m_legendTexH,
                 0, GL_RGB, GL_UNSIGNED_BYTE, m_legendPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    m_legendTexValid = true;
}

// ---------------------------------------------------------------------------
// Dessin de la légende dans le coin bas-gauche du viewport
// ---------------------------------------------------------------------------
void OverlayFactory::DrawLegend(PlugIn_ViewPort* vp) {
    if (!m_legendTexId || !m_legendTexValid) return;

    // Position bas-centre, 15 px du bas
    // (bas-gauche = échelle de distance, bas-droit = boutons de navigation)
    const int margin = 15;
    int x = (vp->pix_width - m_legendTexW) / 2;
    int y = vp->pix_height - margin - m_legendTexH;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_legendTexId);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 0.90f);  // 90 % opaque

    // wxImage fournit les lignes du haut vers le bas, mais OpenGL place la
    // ligne 0 des données en bas de la texture (v=0).
    // → assigner v=0 au coin haut-écran et v=1 au coin bas-écran corrige l'inversion.
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);  glVertex2i(x,                y);               // haut-gauche
    glTexCoord2f(1.0f, 0.0f);  glVertex2i(x + m_legendTexW, y);               // haut-droit
    glTexCoord2f(1.0f, 1.0f);  glVertex2i(x + m_legendTexW, y + m_legendTexH);// bas-droit
    glTexCoord2f(0.0f, 1.0f);  glVertex2i(x,                y + m_legendTexH);// bas-gauche
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// ---------------------------------------------------------------------------
// Mise à jour de la position du curseur — appelé hors contexte GL
// ---------------------------------------------------------------------------
void OverlayFactory::UpdateCursorPosition(double lat, double lon) {
    if (!m_data || !m_data->isLoaded() ||
        m_stepIndex >= (int)m_data->scalarSteps.size()) {
        m_cursorInGrid = false;
        return;
    }

    const GridInfo& g = m_data->grid;
    const TimeStep& scalar = m_data->scalarSteps[m_stepIndex];

    // Garde-fou : tableau incohérent avec la grille → ne rien indexer
    if (!scalar.matchesGrid(g)) {
        m_cursorInGrid = false;
        return;
    }

    int i, j;
    if (!g.toIndex(lon, lat, i, j)) {
        m_cursorInGrid = false;
        return;
    }

    // Valeur scalaire — si manquante (terre/no-data), pas d'affichage
    double val = scalar.get(i, j, g);
    if (val >= TimeStep::MISSING_VALUE - 1.0) {
        m_cursorInGrid = false;
        return;
    }

    m_cursorInGrid = true;

    // Même cellule qu'avant → rien à recalculer
    if (i == m_cursorGridI && j == m_cursorGridJ)
        return;

    m_cursorGridI  = i;
    m_cursorGridJ  = j;
    m_cursorScalar = val;

    // Direction (si disponible et cohérente avec la grille)
    m_cursorDir = -1.0;
    if (m_data->hasDirection() &&
        m_stepIndex < (int)m_data->directionSteps.size()) {
        const TimeStep& dir = m_data->directionSteps[m_stepIndex];
        if (dir.matchesGrid(g)) {
            double d = dir.get(i, j, g);
            if (d < TimeStep::MISSING_VALUE - 1.0) m_cursorDir = d;
        }
    }
}

// ---------------------------------------------------------------------------
// Dessin des flèches de direction
// ---------------------------------------------------------------------------
void OverlayFactory::DrawArrows(PlugIn_ViewPort* vp) {
    const GridInfo& g   = m_data->grid;
    const TimeStep& dir = m_data->directionSteps[m_stepIndex];
    const TimeStep& sc  = m_data->scalarSteps[m_stepIndex];

    // Garde-fou : ne pas itérer si un tableau ne correspond pas à la grille
    if (!dir.matchesGrid(g) || !sc.matchesGrid(g)) return;

    // Espacement des flèches : environ 1 flèche tous les 30 pixels
    // On calcule l'espacement en points de grille
    wxPoint p0, p1;
    GetCanvasPixLL(vp, &p0, g.lat0, g.lon0);
    GetCanvasPixLL(vp, &p1, g.lat0, g.lon0 + g.dlon);
    float pix_per_cell = std::abs((float)(p1.x - p0.x));
    if (pix_per_cell < 0.1f) pix_per_cell = 0.1f;

    int grid_step = std::max(1, (int)(30.0f / pix_per_cell));
    float arrow_len = std::min(30.0f, grid_step * pix_per_cell * 0.8f);
    if (arrow_len < 5.0f) return;  // Trop zoomé dehors pour afficher des flèches

    glColor4f(0.1f, 0.1f, 0.1f, 0.9f);
    glLineWidth(1.5f);

    for (int j = grid_step/2; j < g.nj; j += grid_step) {
        for (int i = grid_step/2; i < g.ni; i += grid_step) {
            // Vérifier que la valeur scalaire n'est pas manquante
            if (sc.isMissing(i, j, g)) continue;

            double dir_deg = dir.get(i, j, g);
            if (dir_deg >= TimeStep::MISSING_VALUE - 1.0) continue;

            wxPoint p;
            GetCanvasPixLL(vp, &p, g.lat(j), g.lon(i));
            // Convention GRIB ECCC/RDWPS : direction d'où viennent les vagues
            // (convention météo, « from »). On ajoute 180° pour obtenir le sens
            // de propagation et pointer la flèche dans la bonne direction.
            DrawArrowGL((float)p.x, (float)p.y, (float)(dir_deg + 180.0), arrow_len);
        }
    }
}

// ---------------------------------------------------------------------------
// Dessin des flèches de direction — rendu DC (non-OpenGL)
// Même logique que DrawArrows() mais via wxDC::DrawLine.
// ---------------------------------------------------------------------------
void OverlayFactory::DrawArrowsDC(wxDC& dc, PlugIn_ViewPort* vp) {
    const GridInfo& g   = m_data->grid;
    const TimeStep& dir = m_data->directionSteps[m_stepIndex];
    const TimeStep& sc  = m_data->scalarSteps[m_stepIndex];
    if (!dir.matchesGrid(g) || !sc.matchesGrid(g)) return;

    wxPoint p0, p1;
    GetCanvasPixLL(vp, &p0, g.lat0, g.lon0);
    GetCanvasPixLL(vp, &p1, g.lat0, g.lon0 + g.dlon);
    float pix_per_cell = std::abs((float)(p1.x - p0.x));
    if (pix_per_cell < 0.1f) pix_per_cell = 0.1f;

    int   grid_step = std::max(1, (int)(30.0f / pix_per_cell));
    float arrow_len = std::min(30.0f, grid_step * pix_per_cell * 0.8f);
    if (arrow_len < 5.0f) return;

    dc.SetPen(wxPen(wxColour(25, 25, 25), 1));

    for (int j = grid_step / 2; j < g.nj; j += grid_step) {
        for (int i = grid_step / 2; i < g.ni; i += grid_step) {
            if (sc.isMissing(i, j, g)) continue;

            double dir_deg = dir.get(i, j, g);
            if (dir_deg >= TimeStep::MISSING_VALUE - 1.0) continue;

            wxPoint p;
            GetCanvasPixLL(vp, &p, g.lat(j), g.lon(i));

            // Convention GRIB « from » + 180° → sens de propagation
            float angle_rad = (float)((90.0 - (dir_deg + 180.0)) * M_PI / 180.0);
            float dx = arrow_len * std::cos(angle_rad);
            float dy = -arrow_len * std::sin(angle_rad);  // y écran inversé

            int x0 = p.x, y0 = p.y;
            int x1 = x0 + (int)dx, y1 = y0 + (int)dy;
            dc.DrawLine(x0, y0, x1, y1);

            float head = arrow_len * 0.35f;
            float al = angle_rad + (float)(150.0 * M_PI / 180.0);
            float ar = angle_rad - (float)(150.0 * M_PI / 180.0);
            dc.DrawLine(x1, y1,
                        x1 + (int)(head * std::cos(al)),
                        y1 - (int)(head * std::sin(al)));
            dc.DrawLine(x1, y1,
                        x1 + (int)(head * std::cos(ar)),
                        y1 - (int)(head * std::sin(ar)));
        }
    }
}

// ---------------------------------------------------------------------------
// Dessin d'une flèche OpenGL
// dir_deg : direction en degrés conventionnels (0=N, 90=E, 180=S, 270=W)
// La flèche POINTE vers cette direction (sens de propagation)
// ---------------------------------------------------------------------------
void OverlayFactory::DrawArrowGL(float x, float y, float dir_deg, float length) {
    // Convertir direction géographique → angle mathématique
    // Géo: 0=Nord, sens horaire. Math: 0=Est, sens anti-horaire
    float angle_rad = (float)(( 90.0 - dir_deg) * M_PI / 180.0);

    float dx = length * std::cos(angle_rad);
    float dy = -length * std::sin(angle_rad);  // y inversé en coordonnées écran

    // Corps de la flèche
    glBegin(GL_LINES);
    glVertex2f(x, y);
    glVertex2f(x + dx, y + dy);
    glEnd();

    // Pointe de la flèche (deux traits à ±30°)
    float head = length * 0.35f;
    float angle_left  = angle_rad + (float)(150.0 * M_PI / 180.0);
    float angle_right = angle_rad - (float)(150.0 * M_PI / 180.0);

    glBegin(GL_LINES);
    glVertex2f(x + dx, y + dy);
    glVertex2f(x + dx + head * std::cos(angle_left),
               y + dy - head * std::sin(angle_left));
    glVertex2f(x + dx, y + dy);
    glVertex2f(x + dx + head * std::cos(angle_right),
               y + dy - head * std::sin(angle_right));
    glEnd();
}

// ---------------------------------------------------------------------------
// Conversion valeur physique → couleur RGBA
//   • Palette discrète (colorScale non vide) : cherche le premier niveau
//     dont le seuil >= valeur et utilise sa couleur (step function).
//   • Gradient linéaire (colorScale vide) : bleu→cyan→vert→jaune→rouge
//     sur [def.minValue, def.maxValue].
// ---------------------------------------------------------------------------
void OverlayFactory::ValueToRGBA(float value,
                                  const IndexDefinition& def,
                                  unsigned char& r, unsigned char& g,
                                  unsigned char& b, unsigned char& a)
{
    a = 255;  // opaque — la transparence est pilotée par glColor4f

    if (!def.colorScale.empty()) {
        // Palette discrète
        for (const auto& lvl : def.colorScale) {
            if (value <= lvl.threshold) {
                r = lvl.r; g = lvl.g; b = lvl.b;
                return;
            }
        }
        // Au-delà du dernier niveau → couleur du dernier niveau
        r = def.colorScale.back().r;
        g = def.colorScale.back().g;
        b = def.colorScale.back().b;
        return;
    }

    // Gradient linéaire (fallback)
    double vmin = def.minValue, vmax = def.maxValue;
    if (vmax <= vmin) vmax = vmin + 1.0;
    float t = (float)std::max(0.0, std::min(1.0, (value - vmin) / (vmax - vmin)));

    float r_f, g_f, b_f;
    if (t < 0.25f) {
        float s = t / 0.25f;
        r_f = 0.0f; g_f = s;       b_f = 1.0f;
    } else if (t < 0.50f) {
        float s = (t - 0.25f) / 0.25f;
        r_f = 0.0f; g_f = 1.0f;    b_f = 1.0f - s;
    } else if (t < 0.75f) {
        float s = (t - 0.50f) / 0.25f;
        r_f = s;    g_f = 1.0f;    b_f = 0.0f;
    } else {
        float s = (t - 0.75f) / 0.25f;
        r_f = 1.0f; g_f = 1.0f - s; b_f = 0.0f;
    }
    r = (unsigned char)(r_f * 255.0f + 0.5f);
    g = (unsigned char)(g_f * 255.0f + 0.5f);
    b = (unsigned char)(b_f * 255.0f + 0.5f);
}
