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
    , m_textureId(0)
    , m_textureValid(false)
    , m_texWidth(0)
    , m_texHeight(0)
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
}

void OverlayFactory::InvalidateTexture() {
    // Marque seulement la texture comme invalide — PAS de glDeleteTextures ici
    // car cette fonction est appelée hors contexte GL (chargement de données, etc.)
    m_textureValid = false;
}

void OverlayFactory::DestroyTexture() {
    // À appeler UNIQUEMENT depuis DeInit(), pendant que le contexte GL est actif
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_textureValid = false;
}

// ---------------------------------------------------------------------------
// Rendu principal OpenGL
// ---------------------------------------------------------------------------
bool OverlayFactory::RenderGL(PlugIn_ViewPort* vp) {
    if (!m_data || !m_data->isLoaded()) return false;
    if (m_stepIndex >= (int)m_data->scalarSteps.size()) return false;

    if (!m_textureValid) BuildTexture();
    if (!m_textureId)    return true;

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

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glPopAttrib();
    if (saved_program && s_glUseProgram)
        s_glUseProgram(saved_program);

    return true;
}

// ---------------------------------------------------------------------------
// Rendu DC fallback (non-OpenGL) — très simplifié
// ---------------------------------------------------------------------------
bool OverlayFactory::RenderDC(wxDC& dc, PlugIn_ViewPort* vp) {
    if (!m_data || !m_data->isLoaded()) return false;
    if (m_stepIndex >= (int)m_data->scalarSteps.size()) return false;

    const GridInfo&  g  = m_data->grid;
    const TimeStep&  ts = m_data->scalarSteps[m_stepIndex];
    double vmin = m_data->def.minValue;
    double vmax = m_data->def.maxValue;
    if (vmax <= vmin) vmax = vmin + 1.0;

    // Sous-échantillonnage pour performances
    int step = std::max(1, std::max(g.ni, g.nj) / 200);

    for (int j = 0; j < g.nj; j += step) {
        for (int i = 0; i < g.ni; i += step) {
            double v = ts.get(i, j, g);
            if (v >= TimeStep::MISSING_VALUE - 1.0) continue;

            float t = (float)std::max(0.0, std::min(1.0, (v - vmin)/(vmax - vmin)));
            unsigned char r, gv, b, a;
            ValueToRGBA(t, r, gv, b, a);

            wxPoint p;
            GetCanvasPixLL(vp, &p, g.lat(j), g.lon(i));
            dc.SetPen(wxPen(wxColour(r, gv, b), 1));
            dc.DrawPoint(p.x, p.y);
        }
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

    double vmin = m_data->def.minValue;
    double vmax = m_data->def.maxValue;
    if (vmax <= vmin) vmax = vmin + 1.0;

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
                float t = (float)std::max(0.0, std::min(1.0, (v - vmin)/(vmax - vmin)));
                unsigned char r, g_c, b, a;
                ValueToRGBA(t, r, g_c, b, a);
                buf[idx+0] = r;
                buf[idx+1] = g_c;
                buf[idx+2] = b;
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
// ---------------------------------------------------------------------------
void OverlayFactory::DrawTexture(PlugIn_ViewPort* vp) {
    const GridInfo& g = m_data->grid;

    // Coordonnées géographiques des 4 coins de la grille
    // Coin SW (bas-gauche), SE, NE, NW
    double latS = g.lat(0);
    double latN = g.lat(g.nj - 1);
    double lonW = g.lon(0);
    double lonE = g.lon(g.ni - 1);

    // Convertir en pixels écran
    wxPoint sw, se, ne, nw;
    GetCanvasPixLL(vp, &sw, latS, lonW);
    GetCanvasPixLL(vp, &se, latS, lonE);
    GetCanvasPixLL(vp, &ne, latN, lonE);
    GetCanvasPixLL(vp, &nw, latN, lonW);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glColor4f(1.0f, 1.0f, 1.0f, 0.75f);  // 75% d'opacité

    glBegin(GL_QUADS);
    // Coin bas-gauche (SW) → coord texture (0,0) en bas
    glTexCoord2f(0.0f, 0.0f);  glVertex2i(sw.x, sw.y);
    glTexCoord2f(1.0f, 0.0f);  glVertex2i(se.x, se.y);
    glTexCoord2f(1.0f, 1.0f);  glVertex2i(ne.x, ne.y);
    glTexCoord2f(0.0f, 1.0f);  glVertex2i(nw.x, nw.y);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// ---------------------------------------------------------------------------
// Dessin des flèches de direction
// ---------------------------------------------------------------------------
void OverlayFactory::DrawArrows(PlugIn_ViewPort* vp) {
    const GridInfo& g   = m_data->grid;
    const TimeStep& dir = m_data->directionSteps[m_stepIndex];

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
            const TimeStep& sc = m_data->scalarSteps[m_stepIndex];
            if (sc.isMissing(i, j, g)) continue;

            double dir_deg = dir.get(i, j, g);
            if (dir_deg >= TimeStep::MISSING_VALUE - 1.0) continue;

            wxPoint p;
            GetCanvasPixLL(vp, &p, g.lat(j), g.lon(i));
            DrawArrowGL((float)p.x, (float)p.y, (float)dir_deg, arrow_len);
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
// Palette de couleurs : bleu → cyan → vert → jaune → orange → rouge
// t ∈ [0, 1]
// ---------------------------------------------------------------------------
void OverlayFactory::ValueToRGBA(float t,
                                  unsigned char& r, unsigned char& g,
                                  unsigned char& b, unsigned char& a)
{
    t = std::max(0.0f, std::min(1.0f, t));

    float r_f, g_f, b_f;

    if (t < 0.25f) {
        // Bleu → Cyan
        float s = t / 0.25f;
        r_f = 0.0f;
        g_f = s;
        b_f = 1.0f;
    } else if (t < 0.50f) {
        // Cyan → Vert
        float s = (t - 0.25f) / 0.25f;
        r_f = 0.0f;
        g_f = 1.0f;
        b_f = 1.0f - s;
    } else if (t < 0.75f) {
        // Vert → Jaune
        float s = (t - 0.50f) / 0.25f;
        r_f = s;
        g_f = 1.0f;
        b_f = 0.0f;
    } else {
        // Jaune → Rouge
        float s = (t - 0.75f) / 0.25f;
        r_f = 1.0f;
        g_f = 1.0f - s;
        b_f = 0.0f;
    }

    r = (unsigned char)(r_f * 255);
    g = (unsigned char)(g_f * 255);
    b = (unsigned char)(b_f * 255);
    a = 200;  // semi-transparent
}
