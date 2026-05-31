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

Le plugin lit directement des fichiers GRIB2 (extension `.grib2`, `.grb2`,
`.grib` ou `.grb`). **Aucune structure de répertoires particulière n'est
requise** : l'utilisateur choisit les fichiers eux-mêmes.

Un fichier peut contenir :
- **un seul** record (une échéance d'un indice), ou
- **plusieurs** records mélangés (plusieurs échéances, plusieurs paramètres,
  voire des champs non pertinents comme le vent ou la température).

Les pas de temps sont reconstitués à partir du `validTime` de chaque record
retenu ; sélectionner plusieurs fichiers les regroupe automatiquement (les
doublons de `validTime` sont éliminés).

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

**Première installation seulement** — OpenCPN désactive les nouveaux plugins par défaut.
Aller dans **Options → Plugins**, trouver *Indices Saint-Laurent* et cliquer **Activer**.

> Si le plugin n'apparaît pas dans la liste, fermez OpenCPN et ajoutez manuellement
> dans `~/.opencpn/opencpn.conf` (section `[PlugIns]`) :
> ```ini
> [PlugIns/libstlaurent_indices_pi.so]
> bEnabled=1
> ```
> Puis relancez OpenCPN.

Les mises à jour ultérieures du `.so` ne nécessitent pas cette étape — le plugin
reste activé entre les sessions.

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
2. Dans la fenêtre qui s'ouvre, cliquer sur **Choisir fichier(s) GRIB…**
3. Sélectionner **un ou plusieurs fichiers GRIB2** (sélection multiple possible)
4. Une fois les données chargées, la carte de couleurs s'affiche sur la carte nautique
5. Utiliser le sélecteur de **pas de temps** pour naviguer entre les échéances

> Chaque fichier peut contenir plusieurs records (vent, température, indices…).
> Le plugin ne lit que les records dont la **discipline / catégorie / numéro de
> paramètre** correspondent aux indices du catalogue ; tous les autres sont
> ignorés, exactement comme le plugin GRIB natif.

---

## Débogage d'un plantage (Linux)

Si OpenCPN plante (fenêtre « OpenCPN crashed » / proposition d'envoyer un rapport),
le système conserve **localement** la trace du crash. Ne pas envoyer le rapport à
OpenCPN : le plantage vient du plugin, pas de leur code. On l'analyse soi-même.

### 1. Le fichier de crash (apport — Ubuntu/Debian)

Sur les systèmes Ubuntu/Debian, le service **apport** capture automatiquement le
crash (c'est lui qui propose « send report ») et écrit un fichier dans :

```
/var/crash/_usr_bin_opencpn.<uid>.crash
```

> `<uid>` = identifiant utilisateur (`1000` pour le premier utilisateur).
> Le format est `_chemin_de_l_exécutable.<uid>.crash`.

Ce fichier (souvent plusieurs dizaines de Mo) contient le **core dump** complet
plus des champs lisibles très utiles, extractibles directement avec `grep` :

```bash
F=/var/crash/_usr_bin_opencpn.1000.crash

# Signal, exécutable, date, paquet
grep -aE "^(Signal|ExecutablePath|Date|Package):" "$F"

# Pile d'appel (déjà symbolisée par apport)
grep -aA20 "^Stacktrace:" "$F"

# Cause du segfault + registres (rip, rax… au moment du crash)
grep -aA4  "^SegvAnalysis:" "$F"
grep -aA20 "^Registers:"    "$F"

# La memory-map indique où chaque .so est chargé — permet de savoir
# si une adresse (ex. rax) tombe dans le segment du plugin :
grep -an "libstlaurent_indices_pi.so" "$F"
```

**Lecture rapide** : `Signal: 11` = SIGSEGV ; `rip = 0x0` = appel d'un pointeur
NULL ; une adresse de registre tombant dans le segment *read-only* du plugin
(zone des vtables) désigne un objet d'une de nos classes.

Pour une pile complète non tronquée, on peut extraire le core et l'ouvrir dans gdb :

```bash
apport-unpack "$F" /tmp/crash && \
gdb /usr/bin/opencpn /tmp/crash/CoreDump -batch -ex "thread apply all bt"
```

### 2. Une fois le crash analysé : supprimer le fichier

Tant qu'il existe, apport propose le rapport à chaque démarrage. Le supprimer :

```bash
rm -f /var/crash/_usr_bin_opencpn.1000.crash
```

### 3. Cas du plugin qui « refuse de se charger »

Après un crash au chargement, OpenCPN marque le plugin en échec et refuse de le
recharger (`Refusing to load … failed at last attempt` dans `~/.opencpn/opencpn.log`).
Effacer le marqueur d'échec :

```bash
rm -f ~/.opencpn/load_stamps/libstlaurent_indices_pi
```

### 4. Cas « rien ne s'ouvre, pas d'erreur »

OpenCPN est en **instance unique**. Un arrêt brutal (`kill -9`, crash) peut laisser
un verrou orphelin qui fait ressortir tout nouveau lancement silencieusement :

```bash
pgrep opencpn || rm -f ~/.opencpn/_OpenCPN_SILock
```

### 5. AddressSanitizer (optionnel)

Pour traquer un débordement mémoire reproductible, compiler avec
`-DENABLE_ASAN=ON` (voir `CMakeLists.txt`). Sur certaines configurations, le
préchargement de `libasan` dans OpenCPN se bloque au démarrage ; l'analyse du
core dump apport (ci-dessus) est alors la méthode la plus fiable.

---

## Dépendances

- [OpenCPN](https://opencpn.org) 5.x — API plugin `opencpn_plugin_116`
- [wxWidgets](https://wxwidgets.org) 3.2+
- [ecCodes](https://confluence.ecmwf.int/display/ECC) (ECMWF) — lecture GRIB2
- OpenGL 2.0+ (rendu accéléré)
