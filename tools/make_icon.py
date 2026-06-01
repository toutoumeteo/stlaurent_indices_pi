#!/usr/bin/env python3
"""
Génère l'icône de la rose des vents pour stlaurent_indices_pi.

Usage :
    python3 tools/make_icon.py

Produit :
    src/icons/stlaurent_icon.png   (32×32, PNG avec transparence — utilisé par le plugin)
    /tmp/stlaurent_icon_preview.png (128×128, pour visualiser les ajustements)
"""

from PIL import Image, ImageDraw
import math
import os

# ---------------------------------------------------------------------------
# Paramètres à ajuster
# ---------------------------------------------------------------------------
SIZE = 512          # résolution de travail (mis à l'échelle ensuite)
cx = cy = SIZE // 2

# Couleurs (R, G, B, A)
DARK  = (50,  50, 255, 255)   # bleu foncé  — branches cardinales (N S E O)
LIGHT = (130, 130, 240, 255)  # bleu lavande — branches intercardinales
BLACK = (0,   0,   0,  255)   # cercle central

# Rayons (fraction du demi-côté, soit de cx)
R_CARD_TIP   = 0.97   # pointe des branches cardinales (longues)
R_INTER_TIP  = 0.8   # pointe des branches intercardinales (courtes)

# Rayon du cercle noir central (fraction de cx)
R_CENTER_CIRCLE = 0.23

# ---------------------------------------------------------------------------
# Dessin
# ---------------------------------------------------------------------------
img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))  # fond transparent
draw = ImageDraw.Draw(img)

# Cercle blanc de fond (visible sur toolbar sombre)
R_BG = cx * 0.97
draw.ellipse([cx - R_BG, cy - R_BG, cx + R_BG, cy + R_BG], fill=(255, 255, 255, 255))


def draw_branch(angle_deg, r_tip_frac, color):
    """Dessine un triangle (branche) pointant dans la direction angle_deg.
    La base est définie par le vecteur perpendiculaire à la direction."""
    r_tip  = cx * r_tip_frac
    r_base = 0.25 * cx

    tip = (cx + r_tip  * math.cos(math.radians(angle_deg)),
           cy + r_tip  * math.sin(math.radians(angle_deg)))
    b1  = (cx + r_base * math.sin(math.radians(angle_deg)),
           cy - r_base * math.cos(math.radians(angle_deg)))
    b2  = (cx - r_base * math.sin(math.radians(angle_deg)),
           cy + r_base * math.cos(math.radians(angle_deg)))

    draw.polygon([tip, b1, b2], fill=color)


# Branches intercardinales en premier (derrière)
for i in range(8):
    if i % 2 != 0:   # 1, 3, 5, 7 → NE, SE, SO, NO
        draw_branch(i * 45 - 90, R_INTER_TIP, LIGHT)

# Branches cardinales par-dessus (devant)
for i in range(8):
    if i % 2 == 0:   # 0, 2, 4, 6 → N, E, S, O
        draw_branch(i * 45 - 90, R_CARD_TIP, DARK)

# Cercle noir central
cr = cx * R_CENTER_CIRCLE
draw.ellipse([cx - cr, cy - cr, cx + cr, cy + cr], fill=BLACK)

# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------
# Aperçu 128×128 dans /tmp
preview = img.resize((128, 128), Image.LANCZOS)
preview.save('/tmp/stlaurent_icon_preview.png')
print("Aperçu → /tmp/stlaurent_icon_preview.png")

# Icône finale 32×32 dans src/icons/
out_dir = os.path.join(os.path.dirname(__file__), '..', 'src', 'icons')
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, 'stlaurent_icon.png')
icon32 = img.resize((32, 32), Image.LANCZOS)
icon32.save(out_path)
print(f"Icône 32×32 → {os.path.abspath(out_path)}")
