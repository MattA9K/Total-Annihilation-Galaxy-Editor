<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Langues :** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · **Français** · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

Un éditeur de cartes pour [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation), pour macOS, Windows et Linux.

Peignez des tuiles, placez des arbres et du métal, définissez les départs des joueurs et façonnez la hauteur du terrain. Les cartes s'ouvrent et s'enregistrent sous forme de fichiers `.ufo` que le jeu peut charger.

## Télécharger

| Plateforme | Téléchargement |
| --- | --- |
| macOS | [GalaxyEditor-1.1.6-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.6/GalaxyEditor-1.1.6-macos.dmg) |
| Windows | [GalaxyEditor-1.1.6-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.6/GalaxyEditor-1.1.6-windows-x64.zip) |
| Linux (archive) | [GalaxyEditor-1.1.6-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.6/GalaxyEditor-1.1.6-linux-x64.tar.gz) |
| Debian / Ubuntu | [galaxy-editor_1.1.6_amd64.deb](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.6/galaxy-editor_1.1.6_amd64.deb) |

Sur macOS, ouvrez l'image disque et faites glisser le dossier Galaxy Editor dans votre dossier Applications. Sur Windows, décompressez l'archive et exécutez `GalaxyEditor.exe`. Sur Linux, vous pouvez utiliser le paquet `.deb` ou décompresser l'archive `.tar.gz`. Chaque téléchargement inclut `GE-Favorites-RandomMapGeneratorTiles.hpi` à côté de l'application. Le pinceau de tuiles lit cet ensemble de tuiles, alors gardez le fichier à côté de `GalaxyEditor.app`, `GalaxyEditor.exe` ou de `galaxy-editor` sous Linux. `GreenWorld.hpi` est inclus de la même manière. La première fois qu'il démarre, Galaxy Editor demande d'abord la langue, puis où Total Annihilation est installé. Chaque version est listée sur la [page des versions](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Installation Linux

**Option 1 : Installer via le paquet .deb (Debian/Ubuntu/Mint)**

Download `galaxy-editor_1.1.6_amd64.deb` and install it. `apt` installs the command `galaxy-editor` and pulls in zlib, OpenGL, X11, and Zenity. Launch it from the application menu or by running `galaxy-editor`. The `.deb` does not include `GE-Favorites-RandomMapGeneratorTiles.hpi` or `GreenWorld.hpi`. Those files are in the portable archive, beside `galaxy-editor`.

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.6/galaxy-editor_1.1.6_amd64.deb
sudo apt install ./galaxy-editor_1.1.6_amd64.deb
```

**Option 2 : Utiliser l'archive portable**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.6/GalaxyEditor-1.1.6-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.6-linux-x64.tar.gz
cd GalaxyEditor-1.1.6-linux-x64
./galaxy-editor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Ce que vous pouvez faire

**Tuiles et caractéristiques.** Recherchez les ensembles de tuiles livrés avec le jeu, cliquez sur une section et tamponnez-la sur la carte. Les arbres, rochers, plaques de métal et évents utilisent leurs graphismes du jeu. Les épingles de départ marquent où chaque joueur commence.

**Terrain.** Activez la carte des hauteurs, puis élevez, abaissez ou définissez la hauteur d'une sélection. Copiez le terrain et la hauteur ensemble, ou copiez uniquement la hauteur et laissez les tuiles en place. Blend Textures estompe les images là où une sélection rencontre les tuiles autour d'elle.

**La carte elle-même.** Les paramètres de la carte contiennent les valeurs que le jeu lit depuis la carte : vent, marées, force solaire, gravité, niveau de la mer, métal, profil AI et règles spéciales. Générer une carte construit un nouveau paysage à partir d'un ensemble de tuiles. Une minicarte se trouve au-dessus du coin de la vue, et annuler couvre les modifications de tuiles, de caractéristiques et de hauteur.

**Vue 3D.** Le bouton Vue 3D dans la barre d'outils (menu Affichage, Ctrl+3) affiche la carte comme un terrain, avec la carte des hauteurs soulevant le sol. Un plan translucide marque le niveau de la mer depuis les paramètres de la carte, et les options de l'éditeur définissent sa couleur et son opacité. La vue 3D sert à regarder autour, alors éditez en vue plate et alternez entre les deux comme vous le souhaitez.

## Compilation

Vous avez besoin de CMake 3.24 ou plus récent, d'un compilateur C++20, de zlib et d'OpenGL. La première configuration télécharge [GLFW](https://www.glfw.org/) 3.4 et [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8, donc cette étape nécessite une connexion réseau.

macOS construit `build/GalaxyEditor.app`. Ouvrez-le depuis le Finder, ou exécutez `open build/GalaxyEditor.app` :

```bash
cmake --preset default
cmake --build build
open build/GalaxyEditor.app
```

Linux :

```bash
cmake --preset default
cmake --build build
./build/galaxy-editor
```

Pour compiler le `.exe` Windows sur une machine Mac ou Linux, installez MinGW-w64 (`brew install mingw-w64`, ou `apt install g++-mingw-w64-x86-64-posix`), puis :

```bash
cmake --preset windows
cmake --build --preset windows
```

Cela écrit `build-windows/GalaxyEditor.exe`. C'est un fichier unique avec le runtime C++ intégré, et il n'a besoin que des DLL livrées avec Windows. La première configuration télécharge également zlib. Placez vos propres archives d'ensembles de tuiles à côté du `.exe`.

Sur Windows lui-même, choisissez un générateur que votre compilateur comprend :

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib doit être visible par CMake. Sur Debian et Ubuntu, c'est le paquet `zlib1g-dev`. Sur Fedora, c'est `zlib-devel`. Les dialogues d'ouverture et d'enregistrement de Linux utilisent Zenity.

Les tests de format se compilent avec l'éditeur :

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Ensembles de tuiles

Galaxy Editor charge les archives de jeu depuis votre dossier d'installation de Total Annihilation. La toute première fois qu'il démarre, avant l'écran de démarrage, il demande la langue, puis ce dossier. Choisissez Annuler pour démarrer sans lui. L'éditeur fonctionne toujours, mais les onglets Ensembles de tuiles et Caractéristiques restent vides, et il ne redemande pas au prochain lancement.

Changez le dossier à tout moment dans Options, sous Total Annihilation. Choisir le dossier recharge les archives immédiatement, et Effacer oublie le dossier, donc le prochain lancement le redemande, comme lors d'un premier lancement. Le dossier et la langue choisie sont enregistrés dans le `settings.json` par utilisateur. La langue se change ensuite dans Options, sous Langue, et la fenêtre bascule tout de suite.

Galaxy Editor lit les fichiers `.hpi`, `.ccx`, `.gp3` et `.gpf` directement dans ce dossier, puis dans le dossier contenant l'exécutable `GalaxyEditor`. Sur macOS, c'est le dossier contenant `GalaxyEditor.app`, donc un dossier nommé Galaxy Editor avec l'application et vos propres fichiers `.hpi` côte à côte fonctionne, comme Photoshop est disposé. Placez vos propres archives d'ensembles de tuiles à côté de l'exécutable (ou du `.app`). Si un fichier là-bas a le même nom qu'un fichier dans le dossier Total Annihilation, comme `worlds.hpi` ou `totala1.hpi`, il est ignoré, donc rien ne se charge deux fois. Une archive mondiale comme `worlds.hpi` remplit l'onglet Ensembles de tuiles. `totala1.hpi`, `rev31.gp3` ou `ccdata.ccx` fournissent la liste des caractéristiques et les images des caractéristiques.

Ces archives sont des données de jeu. Elles ne font pas partie de ce dépôt.

Ouvrir une carte lit `.ufo`, `.hpi` et `.tnt`. Enregistrer écrit un `.ufo`.

## Favoris

L'onglet Favoris, entre Ensembles de tuiles et Caractéristiques, contient les tuiles que vous enregistrez vous-même. En mode Sélectionner les tuiles, sélectionnez quelques tuiles et cliquez avec le bouton droit. Le menu propose Couper, Copier, Copier les hauteurs, Modifier la hauteur, Mélanger et Ajouter aux favoris. Dans l'onglet Ensembles de tuiles, pointer sur une image affiche un bouton étoile dans son coin qui ajoute cette tuile. Dans l'onglet Favoris, le même coin contient un bouton corbeille qui supprime la tuile après confirmation.

Ajouter aux favoris demande trois choses : une archive de favoris, une catégorie et une sous-catégorie. Tapez de nouveaux noms ou choisissez ceux existants. Les noms fonctionnent comme ils le font dans les ensembles de tuiles du jeu lui-même : dans `Acid_New_1.hpi`, la catégorie est Acid_New_1, et Rivers et Sm Lakes sont ses sous-catégories. Créer une archive de favoris demande un nom comme FirstOne et écrit `GE-Favorites-FirstOne.hpi` à côté de l'exécutable `GalaxyEditor` (à côté du `.app` sur macOS).

Seules les archives nommées `GE-Favorites-<nom>.hpi` apparaissent dans l'onglet Favoris, et elles sont écartées de l'onglet Ensembles de tuiles. À l'intérieur, une tuile est `sections/<catégorie>/<sous-catégorie>/<tuile>.sct`, donc l'archive est un ensemble de tuiles ordinaire que vous pouvez partager en copiant le fichier dans le dossier de quelqu'un d'autre. Une tuile enregistrée est un rectangle complet. Lorsque la sélection a des espaces, les tuiles entre celles sélectionnées sont également enregistrées, car un fichier de section n'a pas de place pour les trous. Une tuile peut avoir au maximum 512 tuiles de chaque côté.

## Éditeur HPI

Le bouton Éditeur HPI à l'extrémité droite de la barre d'outils (menu Fichier, Éditeur HPI) ouvre un navigateur de fichiers pour les archives : `.hpi`, `.ufo`, `.ccx`, `.gp3` et `.gpf`. Ouvrez-en une avec le bouton Ouvrir, ou démarrez une archive vide avec Nouveau. Lorsqu'une carte que vous avez ouverte est elle-même une archive, la fenêtre vide l'offre en un clic.

La fenêtre suit la disposition d'une fenêtre du Finder. Un arbre de dossiers descend à gauche, le contenu remplit le milieu sous forme de liste ou d'icônes (les deux boutons en haut à droite), et une barre de chemin et un compteur d'éléments longent le bas. Cliquez sur un dossier dans l'arbre ou un segment de la barre de chemin pour y aller, double-cliquez sur un dossier pour l'ouvrir, et utilisez Retour et Suivant pour retracer vos pas. Sélectionnez d'un clic, clic Command ou clic Ctrl pour en ajouter un, clic Shift pour une plage, ou Ctrl+A. La zone de recherche trouve les fichiers et dossiers par nom sous le dossier que vous consultez, et double-cliquer sur un résultat montre où il se trouve.

- **Exporter** copie les fichiers et dossiers sélectionnés vers un dossier que vous choisissez. Un dossier conserve son nom et tout ce qu'il contient. Sans rien de sélectionné, Exporter prend le dossier que vous consultez, et en haut de l'archive crée un dossier nommé d'après l'archive pour que son contenu ne soit pas dispersé. Si un fichier y est déjà, vous êtes demandé avant qu'il ne soit remplacé. Les noms qui pourraient atteindre l'extérieur du dossier que vous avez choisi sont ignorés, et les caractères qu'un ordinateur n'autorise pas dans les noms de fichiers sont remplacés par un trait de soulignement.
- **Importer** ajoute des fichiers, ou des dossiers entiers avec tout ce qu'ils contiennent, au dossier que vous consultez. Vous pouvez également déposer des fichiers et dossiers depuis le bureau sur la fenêtre. Importer demande avant de remplacer un fichier du même nom. Les noms de fichiers dans une archive correspondent sans tenir compte de la casse, comme ils le font dans le jeu.
- **Déplacer** en faisant glisser des éléments d'un dossier à un autre : sur un dossier dans la liste ou la vue des icônes, un dossier dans l'arbre, ou un segment de la barre de chemin. Faire glisser un dossier dans lui-même est refusé, et un dossier qui existe déjà à la destination est fusionné dedans.
- **Renommer** avec F2 ou le menu contextuel, **Nouveau dossier** avec le bouton de la barre d'outils ou Ctrl+Shift+N, et **Supprimer** avec la touche Suppr. Chaque modification peut être annulée et refaite.

Rien ne touche l'archive sur le disque jusqu'à ce que vous appuyiez sur Enregistrer. Enregistrer et Enregistrer sous écrivent une nouvelle archive et remplacent l'ancien fichier en une étape, donc un échec laisse l'original intact. Une archive ne peut pas stocker un dossier vide ni n'avoir aucun fichier du tout, donc les dossiers vides ne survivent pas à un enregistrement et l'éditeur n'enregistrera pas une archive sans rien dedans. Fermer la fenêtre, ouvrir une autre archive ou quitter demande d'abord les modifications non enregistrées.

## Raccourcis

Sur macOS, Command remplace Ctrl.

| Action | Raccourci |
| --- | --- |
| Nouvelle carte | Ctrl+N |
| Ouvrir une carte | Ctrl+O |
| Enregistrer | Ctrl+S |
| Enregistrer sous | Ctrl+Shift+S |
| Quitter | Ctrl+Q |
| Annuler | Ctrl+Z |
| Refaire | Ctrl+Shift+Z |
| Couper | Ctrl+X |
| Copier terrain + carte des hauteurs | Ctrl+C |
| Copier carte des hauteurs | Ctrl+Shift+C |
| Coller | Ctrl+V |
| Options de l'éditeur | Ctrl+, |
| Vue 3D activée ou désactivée | Ctrl+3 |

Dans la fenêtre Éditeur HPI, ceux-ci fonctionnent lorsqu'elle a le clavier au lieu des raccourcis de carte : Ctrl+O ouvre une archive, Ctrl+N en démarre une nouvelle, Ctrl+S et Ctrl+Shift+S enregistrent, Ctrl+Z et Ctrl+Shift+Z annulent et refont, Ctrl+A sélectionne tout, Ctrl+I importe, Ctrl+Shift+N crée un dossier, Ctrl+[ et Ctrl+] reculent et avancent, Ctrl+Up va au dossier englobant, Ctrl+Down ou Return ouvre un dossier, F2 renomme, Suppr ou Ctrl+Retour arrière supprime la sélection, et Retour arrière seul monte d'un dossier.

Les touches fléchées font glisser un tampon ou un collage depuis la tuile sous le curseur. Échap annule un collage, quitte le mode Supprimer ou efface la sélection.

Dans la vue 3D, faites glisser pour orbiter, faites glisser avec le bouton droit ou Shift-glisser pour panoramique, et faites défiler pour zoomer. W A S D (ou les touches fléchées) se déplacent sur le sol, Shift se déplace plus rapidement, Q et E tournent, et Home recommence.

Les options de l'éditeur sont mémorisées la prochaine fois que vous ouvrez l'éditeur. La taille de l'aperçu, l'espacement de la carte des hauteurs, la taille du texte de la barre latérale et l'enregistrement automatique y vivent tous. L'enregistrement automatique écrit une sauvegarde à côté de la carte sur un minuteur et laisse le fichier d'Enregistrer tranquille.

## Droits d'auteur

Droits d'auteur © 2026 Black Cat Software  
Développé par Matt Andrzejczuk
