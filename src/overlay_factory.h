#pragma once
/***************************************************************************
 * stlaurent_indices_pi — OpenCPN plugin
 * Rendu OpenGL : palette de couleurs + flèches de direction
 ***************************************************************************/

#include "indices_data.h"
#include "ocpn_plugin.h"

#ifdef __APPLE__
  #include <OpenGL/gl.h>     // macOS : framework OpenGL
#else
  #ifdef _WIN32
    #include <windows.h>     // Windows : requis avant gl.h
  #endif
  #include <GL/gl.h>
#endif

// Constantes GL 1.2+ absentes du gl.h Windows (qui ne couvre que GL 1.1)
#ifndef GL_CLAMP_TO_EDGE
  #define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_CURRENT_PROGRAM
  #define GL_CURRENT_PROGRAM 0x8B8D
#endif

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

    // Met à jour la position du curseur souris (géographique)
    // Appelé depuis stlaurent_pi::SetCursorLatLon()
    void UpdateCursorPosition(double lat, double lon);

    // Accès aux valeurs au curseur — pour mise à jour du dialog
    bool   GetCursorInGrid()  const { return m_cursorInGrid; }
    double GetCursorScalar()  const { return m_cursorScalar; }
    double GetCursorDir()     const { return m_cursorDir; }

    // Libère la ressource GL — à appeler depuis DeInit() seulement
    void DestroyTexture();

private:
    // --- Données courantes ---
    const IndexData* m_data;      // pointeur non-propriétaire
    int              m_stepIndex; // pas de temps courant

    // --- Cache texture OpenGL — champ scalaire ---
    GLuint m_textureId;
    bool   m_textureValid;
    int    m_texWidth;
    int    m_texHeight;

    // --- Cache texture OpenGL — légende ---
    GLuint m_legendTexId;
    bool   m_legendTexValid;
    int    m_legendTexW;
    int    m_legendTexH;

    // --- Curseur : position et valeurs interpolées ---
    double m_cursorLat;
    double m_cursorLon;
    bool   m_cursorInGrid;    // curseur sur la grille ET valeur non-manquante
    double m_cursorScalar;    // valeur scalaire au point curseur
    double m_cursorDir;       // direction au point curseur (-1 = indisponible)
    int    m_cursorGridI;     // dernière cellule grille (évite les rebuilds inutiles)
    int    m_cursorGridJ;


    // --- Construction des textures ---
    void BuildTexture();
    void BuildLegendTexture();   // crée la texture légende via wxBitmap

    // Convertit une valeur normalisée [0,1] en couleur RGBA
    // Palette : bleu → cyan → vert → jaune → orange → rouge
    static void ValueToRGBA(float t, unsigned char& r, unsigned char& g,
                            unsigned char& b, unsigned char& a);

    // --- Rendu de la texture sur la carte ---
    void DrawTexture(PlugIn_ViewPort* vp);

    // --- Rendu de la légende (coin bas-gauche) ---
    void DrawLegend(PlugIn_ViewPort* vp);


    // --- Rendu des flèches de direction ---
    void DrawArrows(PlugIn_ViewPort* vp);

    // Dessine une flèche à l'écran (OpenGL)
    // x,y : position pixel, dir_deg : direction en degrés (convention météo: vient de)
    // length : longueur en pixels
    static void DrawArrowGL(float x, float y, float dir_deg, float length);
};
