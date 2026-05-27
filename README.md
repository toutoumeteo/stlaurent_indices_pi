# stlaurent_indices_pi

Plugin [OpenCPN](https://opencpn.org) pour l'affichage des indices météo-marins du Saint-Laurent produits par [Environnement et Changement climatique Canada (ECCC)](https://meteo.gc.ca).

---

## Fonctionnalités

- Lecture des fichiers GRIB2 issus du modèle **RDWPS** (Regional Deterministic Wave Prediction System)
- Affichage d'une **carte de couleurs** superposée à la carte nautique (palette bleu → vert → rouge)
- Affichage de **flèches de direction** proportionnelles à la résolution d'affichage
- Navigation entre les **48 pas de temps** d'une run (1h à 48h)
- Rendu OpenGL accéléré + fallback non-GL

### Indices supportés

| Indice | Paramètre GRIB2 | Description |
|--------|-----------------|-------------|
| Indice d'agitation | `parameterNumber=192` | Intensité de l'agitation marine `[-]` |
| Direction d'agitation | `parameterNumber=193` | Direction de propagation `[°]` |

> D'autres indices peuvent être ajoutés dans `src/indices_data.h` (section `IndicesCatalogue`).

---

## Structure des fichiers de données

Le plugin attend la structure de répertoires produite par ECCC :

```
<run>/                              ← ex: 2026052518/
├── Indice_agitation_RDWPS/
│   ├── CMC_rdwps_..._PT001H.grib2
│   ├── CMC_rdwps_..._PT002H.grib2
│   └── ...  (48 fichiers)
└── Direction_agitation_RDWPS/
    ├── CMC_rdwps_..._PT001H.grib2
    └── ...
```

---

## Compilation locale

### Prérequis

| Plateforme | Dépendances |
|------------|-------------|
| **Linux** | `cmake`, `libwxgtk3.2-dev`, `libeccodes-dev`, `libgl-dev` |
| **macOS** | `cmake`, `wxwidgets`, `eccodes` (via Homebrew) |
| **Windows** | CMake, [vcpkg](https://vcpkg.io) avec `wxwidgets:x64-windows` et `eccodes:x64-windows` |

### Linux (Ubuntu 24.04)

```bash
sudo apt-get install -y libwxgtk3.2-dev libeccodes-dev libgl-dev cmake

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### macOS

```bash
brew install wxwidgets eccodes cmake

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows

```powershell
vcpkg install wxwidgets:x64-windows eccodes:x64-windows

cmake -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build --config Release --parallel
```

### Résultat

| Plateforme | Fichier produit |
|------------|-----------------|
| Linux | `build/libstlaurent_indices_pi.so` |
| macOS | `build/libstlaurent_indices_pi.dylib` |
| Windows | `build/Release/stlaurent_indices_pi.dll` |

---

## Intégration continue (GitHub Actions)

Chaque `git push` déclenche automatiquement trois builds en parallèle sur l'infrastructure GitHub :

```
push
 ├── ubuntu-24.04  →  libstlaurent_indices_pi.so
 ├── macos-latest  →  libstlaurent_indices_pi.dylib
 └── windows-latest→  stlaurent_indices_pi.dll
```

Les artefacts compilés sont disponibles dans l'onglet **Actions** → run → **Artifacts** sur GitHub, sans avoir à compiler soi-même.

Le fichier de configuration se trouve dans `.github/workflows/build.yml`.

---

## Installation du plugin dans OpenCPN

### Linux

```bash
# Créer le répertoire si nécessaire, puis copier
mkdir -p ~/.local/lib/opencpn/
cp build/libstlaurent_indices_pi.so ~/.local/lib/opencpn/
```

Vérifier que le plugin est activé dans **Options → Plugins → Indices Saint-Laurent → Activer**.

### macOS

Le plugin doit être **compilé localement** (pas depuis l'artefact CI) pour lier contre
la wxWidgets 3.2 embarquée dans OpenCPN. Voir la section **Compilation locale** ci-dessus.

```bash
# Répertoire de plugins OpenCPN sur macOS
mkdir -p ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/

cp build/libstlaurent_indices_pi.dylib \
   ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/

# Supprimer l'attribut de quarantaine macOS (obligatoire)
xattr -d com.apple.quarantine \
   ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/libstlaurent_indices_pi.dylib
```

Ouvrir OpenCPN normalement, puis activer dans **Options → Plugins**.

### Windows

Copier le `.dll` dans le répertoire de plugins OpenCPN, puis activer dans **Options → Plugins**.

---

## Utilisation

1. Cliquer sur le bouton **SL** dans la barre d'outils OpenCPN
2. Dans la fenêtre qui s'ouvre, cliquer sur **Choisir dossier de run…**
3. Sélectionner le **dossier de la run** (ex: `2026052518/`) — pas un sous-dossier
4. Une fois les données chargées, la carte de couleurs s'affiche sur la carte nautique
5. Utiliser le sélecteur de **pas de temps** pour naviguer entre les 48 heures de prévision

---

## Dépendances

- [OpenCPN](https://opencpn.org) 5.x — API plugin `opencpn_plugin_116`
- [wxWidgets](https://wxwidgets.org) 3.2+
- [ecCodes](https://confluence.ecmwf.int/display/ECC) (ECMWF) — lecture GRIB2
- OpenGL 2.0+ (rendu accéléré)
