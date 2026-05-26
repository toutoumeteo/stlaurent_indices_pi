#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Rendu OpenGL : palette de couleurs + flèches de direction
 ***************************************************************************/

#include "indices_data.h"
#include "ocpn_plugin.h"

#ifdef __WXMSW__
  #include <windows.h>
#endif
#include <GL/gl.h>

class OverlayFactory {
public:
    OverlayFactory();
    ~OverlayFactory();

    // Charge les données à afficher
    // Appelé quand l'utilisateur change d'indice ou de pas de temps
    void SetData(const IndexData* data, int stepIndex);

    // Rendu OpenGL principal — appelé par RenderGLOverlay()
    // Retourne true si quelque chose a été dessiné
    bool RenderGL(PlugIn_ViewPort* vp);

    // Rendu DC fallback (non-OpenGL) — simplifié
    bool RenderDC(wxDC& dc, PlugIn_ViewPort* vp);

    // Invalide le cache texture (ex: changement de pas de temps)
    // Sûr à appeler hors contexte GL
    void InvalidateTexture();

    // Libère la ressource GL — à appeler depuis DeInit() seulement
    void DestroyTexture();

private:
    // --- Données courantes ---
    const IndexData* m_data;      // pointeur non-propriétaire
    int              m_stepIndex; // pas de temps courant

    // --- Cache texture OpenGL ---
    GLuint m_textureId;
    bool   m_textureValid;
    int    m_texWidth;
    int    m_texHeight;

    // --- Construction de la texture ---
    void BuildTexture();

    // Convertit une valeur normalisée [0,1] en couleur RGBA
    // Palette : bleu → cyan → vert → jaune → orange → rouge
    static void ValueToRGBA(float t, unsigned char& r, unsigned char& g,
                            unsigned char& b, unsigned char& a);

    // --- Rendu de la texture sur la carte ---
    void DrawTexture(PlugIn_ViewPort* vp);

    // --- Rendu des flèches de direction ---
    void DrawArrows(PlugIn_ViewPort* vp);

    // Dessine une flèche à l'écran (OpenGL)
    // x,y : position pixel, dir_deg : direction en degrés (convention météo: vient de)
    // length : longueur en pixels
    static void DrawArrowGL(float x, float y, float dir_deg, float length);
};
