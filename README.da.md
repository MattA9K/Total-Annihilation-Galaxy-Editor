<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Sprog:** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · **Dansk** · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

En korteditor til [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation), til macOS, Windows og Linux.

Mal fliser, placer træer og metal, indstil spillerstarter og form terrænet højde. Kort åbnes og gemmes som `.ufo`-filer, som spillet kan indlæse.

## Download

| Platform | Download |
| --- | --- |
| macOS | [GalaxyEditor-1.1.9-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.9/GalaxyEditor-1.1.9-macos.dmg) |
| Windows | [GalaxyEditor-1.1.9-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.9/GalaxyEditor-1.1.9-windows-x64.zip) |
| Linux (arkiv) | [GalaxyEditor-1.1.9-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.9/GalaxyEditor-1.1.9-linux-x64.tar.gz) |
| Debian / Ubuntu | [galaxy-editor_1.1.9_amd64.deb](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.9/galaxy-editor_1.1.9_amd64.deb) |

På macOS skal du åbne diskbilledet og trække Galaxy Editor-mappen til din Programmer-mappe. På Windows skal du udpakke arkivet og køre `GalaxyEditor.exe`. På Linux kan du bruge `.deb`-pakken eller udpakke `.tar.gz`-arkivet. Hver download inkluderer `GE-Favorites-RandomMapGeneratorTiles.hpi` ved siden af appen. Flisepenslen læser det flisesæt, så hold filen ved siden af `GalaxyEditor.app`, `GalaxyEditor.exe` eller Linux `galaxy-editor`. `GreenWorld.hpi` er inkluderet på samme måde. Første gang den starter, spørger Galaxy Editor først om sproget og derefter, hvor Total Annihilation er installeret. Hver udgivelse er angivet på [Releases-siden](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Linux-installation

**Mulighed 1: Installer via .deb-pakke (Debian/Ubuntu/Mint)**

Download `galaxy-editor_1.1.9_amd64.deb` and install it. `apt` installs the command `galaxy-editor` and pulls in zlib, OpenGL, X11, and Zenity. Launch it from the application menu or by running `galaxy-editor`. The `.deb` does not include `GE-Favorites-RandomMapGeneratorTiles.hpi` or `GreenWorld.hpi`. Those files are in the portable archive, beside `galaxy-editor`.

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.9/galaxy-editor_1.1.9_amd64.deb
sudo apt install ./galaxy-editor_1.1.9_amd64.deb
```

**Mulighed 2: Brug det bærbare arkiv**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.9/GalaxyEditor-1.1.9-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.9-linux-x64.tar.gz
cd GalaxyEditor-1.1.9-linux-x64
./galaxy-editor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Hvad du kan gøre

**Fliser og funktioner.** Søg i flisesættene, der følger med spillet, klik på en sektion og stem den på kortet. Træer, sten, metalplamager og ventilationshuller bruger deres grafik i spillet. Startnåle markerer, hvor hver spiller starter.

**Terræn.** Slå højdekortet til, og hæv, sænk eller indstil højden af en markering. Kopier terrænet og højden sammen, eller kopier kun højden og lad fliserne være på plads. Blend Textures fader billederne, hvor en markering møder fliserne omkring den.

**Selve kortet.** Kortindstillinger indeholder de værdier, som spillet læser fra kortet: vind, tidevand, solkraft, tyngdekraft, havniveau, metal, AI-profil og særlige regler. Generer kort bygger et nyt landskab fra et flisesæt. Et minikort sidder over hjørnet af visningen, og fortryd dækker flise-, funktions- og højderedigeringer.

**3D-visning.** 3D-visningsknappen på værktøjslinjen (Vis-menu, Ctrl+3) viser kortet som terræn, med højdekortet, der løfter jorden. Et gennemsigtigt plan markerer havniveauet fra Kortindstillinger, og Editor-indstillinger indstiller dets farve og uigennemsigtighed. 3D-visningen er til at se sig omkring, så rediger i flad visning og skift frem og tilbage, som du vil.

## Byg

Du har brug for CMake 3.24 eller nyere, en C++20-compiler, zlib og OpenGL. Den første konfiguration downloader [GLFW](https://www.glfw.org/) 3.4 og [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8, så det trin kræver en netværksforbindelse.

macOS bygger `build/GalaxyEditor.app`. Åbn den fra Finder, eller kør `open build/GalaxyEditor.app`:

```bash
cmake --preset default
cmake --build build
open build/GalaxyEditor.app
```

Linux:

```bash
cmake --preset default
cmake --build build
./build/galaxy-editor
```

For at bygge Windows `.exe` på en Mac- eller Linux-maskine skal du installere MinGW-w64 (`brew install mingw-w64` eller `apt install g++-mingw-w64-x86-64-posix`) og derefter:

```bash
cmake --preset windows
cmake --build --preset windows
```

Dette skriver `build-windows/GalaxyEditor.exe`. Det er én fil med C++ runtime indbygget, og den har kun brug for DLL'erne, der følger med Windows. Den første konfiguration downloader også zlib. Placer dine egne flisesæt-arkiver ved siden af `.exe`.

På Windows selv skal du vælge en generator, som din compiler forstår:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib skal være synlig for CMake. På Debian og Ubuntu er det `zlib1g-dev`-pakken. På Fedora er det `zlib-devel`. Linux åbn- og gem-dialoger bruger Zenity.

Formattestene bygges med editoren:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Flisesæt

Galaxy Editor indlæser spilarkiver fra din Total Annihilation-installationsmappe. Allerførste gang den starter, før startskærmen, spørger den om sproget og derefter om den mappe. Vælg Annuller for at starte uden den. Editoren kører stadig, men fanebladene Flisesæt og Funktioner forbliver tomme, og den spørger ikke igen ved næste start.

Skift mappen til enhver tid i Indstillinger under Total Annihilation. Vælg Mappe genindlæser arkiverne med det samme, og Ryd glemmer mappen, så næste start spørger efter den igen, som ved en første start. Mappen og det valgte sprog gemmes i per-bruger `settings.json`. Sproget ændres senere i Indstillinger under Sprog, og vinduet skifter med det samme.

Galaxy Editor læser `.hpi`-, `.ccx`-, `.gp3`- og `.gpf`-filer direkte i den mappe og derefter i mappen, der indeholder den eksekverbare `GalaxyEditor`-fil. På macOS er det mappen, der indeholder `GalaxyEditor.app`, så en mappe ved navn Galaxy Editor med appen og dine egne `.hpi`-filer side om side virker, som Photoshop er lagt ud. Placer dine egne flisesæt-arkiver ved siden af den eksekverbare fil (eller `.app`). Hvis en fil der har samme navn som en fil i Total Annihilation-mappen, såsom `worlds.hpi` eller `totala1.hpi`, springes den over, så intet indlæses to gange. Et verdensarkiv som `worlds.hpi` udfylder fanen Flisesæt. `totala1.hpi`, `rev31.gp3` eller `ccdata.ccx` leverer funktionslisten og funktionsbillederne.

Disse arkiver er spildata. De er ikke en del af dette arkiv.

Åbn kort læser `.ufo`, `.hpi` og `.tnt`. Gem skriver en `.ufo`.

## Favoritter

Fanen Favoritter, mellem Flisesæt og Funktioner, indeholder fliser, som du gemmer selv. I tilstanden Vælg fliser skal du vælge nogle fliser og højreklikke. Menuen tilbyder Klip, Kopier, Kopier højder, Rediger højde, Bland og Tilføj til favoritter. I fanen Flisesæt viser peging på et billede en stjerneknap i dets hjørne, der tilføjer den flise. I fanen Favoritter indeholder samme hjørne en skraldespandsknap, der sletter flisen, efter at du har bekræftet.

Tilføj til favoritter spørger om tre ting: et favoritarkiv, en kategori og en underkategori. Indtast nye navne eller vælg eksisterende. Navnene fungerer, som de gør i spillets egne flisesæt: i `Acid_New_1.hpi` er kategorien Acid_New_1, og Rivers og Sm Lakes er dens underkategorier. Opret favoritarkiv spørger om et navn som FirstOne og skriver `GE-Favorites-FirstOne.hpi` ved siden af den eksekverbare `GalaxyEditor`-fil (ved siden af `.app` på macOS).

Kun arkiver med navnet `GE-Favorites-<navn>.hpi` vises på fanen Favoritter, og de holdes væk fra fanen Flisesæt. Inden i er en flise `sections/<kategori>/<underkategori>/<flise>.sct`, så arkivet er et almindeligt flisesæt, som du kan dele ved at kopiere filen til en andens mappe. En gemt flise er et fuldt rektangel. Når markeringen har huller, gemmes fliserne mellem de valgte også, fordi en sektionsfil ikke har plads til huller. En flise kan højst være 512 fliser på hver side.

## HPI-editor

HPI-editorknappen i den højre ende af værktøjslinjen (Fil-menu, HPI-editor) åbner en filbrowser til arkiver: `.hpi`, `.ufo`, `.ccx`, `.gp3` og `.gpf`. Åbn en med Åbn-knappen, eller start et tomt arkiv med Ny. Når et kort, du har åbent, selv er et arkiv, tilbyder det tomme vindue det med ét klik.

Vinduet følger layoutet af et Finder-vindue. Et mappetræ løber ned til venstre, indholdet fylder midten som en liste eller som ikoner (de to knapper øverst til højre), og en stibjælke og en vareoptælling løber langs bunden. Klik på en mappe i træet eller et segment af stibjælken for at gå derhen, dobbeltklik på en mappe for at åbne den og brug Tilbage og Fremad for at spore dine trin tilbage. Vælg med et klik, Command-klik eller Ctrl-klik for at tilføje en, Shift-klik for et område eller Ctrl+A. Søgefeltet finder filer og mapper efter navn under den mappe, du ser på, og dobbeltklik på et resultat viser, hvor det bor.

- **Eksporter** kopierer de valgte filer og mapper til en mappe, du vælger. En mappe beholder sit navn og alt inde i den. Uden noget valgt tager Eksporter den mappe, du ser på, og øverst i arkivet opretter den en mappe opkaldt efter arkivet, så dets indhold ikke spredes. Hvis en fil allerede er der, bliver du spurgt, før den erstattes. Navne, der kan nå uden for den mappe, du valgte, springes over, og tegn, som en computer ikke tillader i filnavne, erstattes med en understregning.
- **Importer** tilføjer filer eller hele mapper med alt inde i dem til den mappe, du ser på. Du kan også slippe filer og mapper fra skrivebordet på vinduet. Importer spørger, før den erstatter en fil med samme navn. Filnavne i et arkiv matcher uden hensyn til store og små bogstaver, som de gør i spillet.
- **Flyt** ved at trække elementer fra en mappe til en anden: på en mappe på listen eller ikonvisningen, en mappe i træet eller et segment af stibjælken. At trække en mappe ind i sig selv afvises, og en mappe, der allerede findes på destinationen, flettes ind i den.
- **Omdøb** med F2 eller højrekliksmenuen, **Ny mappe** med værktøjslinjeknappen eller Ctrl+Shift+N og **Slet** med Delete-tasten. Hver ændring kan fortrydes og omgøres.

Intet rører arkivet på disken, før du trykker på Gem. Gem og Gem som skriver et nyt arkiv og erstatter den gamle fil i ét trin, så en fejl efterlader originalen i fred. Et arkiv kan ikke gemme en tom mappe eller slet ingen filer, så tomme mapper overlever ikke en lagring, og editoren vil ikke gemme et arkiv uden noget i det. Lukning af vinduet, åbning af et andet arkiv eller afslutning spørger om ikke-gemte ændringer først.

## Genveje

På macOS tager Command pladsen for Ctrl.

| Handling | Genvej |
| --- | --- |
| Nyt kort | Ctrl+N |
| Åbn kort | Ctrl+O |
| Gem | Ctrl+S |
| Gem som | Ctrl+Shift+S |
| Afslut | Ctrl+Q |
| Fortryd | Ctrl+Z |
| Omgør | Ctrl+Shift+Z |
| Klip | Ctrl+X |
| Kopier terræn + højdekort | Ctrl+C |
| Kopier højdekort | Ctrl+Shift+C |
| Indsæt | Ctrl+V |
| Editor-indstillinger | Ctrl+, |
| 3D-visning til eller fra | Ctrl+3 |

I HPI-editorvinduet fungerer disse, når det har tastaturet i stedet for kortgenvejene: Ctrl+O åbner et arkiv, Ctrl+N starter et nyt, Ctrl+S og Ctrl+Shift+S gemmer, Ctrl+Z og Ctrl+Shift+Z fortryder og omgør, Ctrl+A vælger alt, Ctrl+I importerer, Ctrl+Shift+N opretter en mappe, Ctrl+[ og Ctrl+] går tilbage og fremad, Ctrl+Up går til den omsluttende mappe, Ctrl+Down eller Return åbner en mappe, F2 omdøber, Delete eller Ctrl+Backspace fjerner markeringen, og Backspace alene går en mappe op.

Piletasterne glider et stempel eller en indsættelse fra flisen under markøren. Esc annullerer en indsættelse, forlader Slet-tilstand eller rydder markeringen.

I 3D-visningen skal du trække for at kredse, højretrække eller Shift-trække for at panorere og rulle for at zoome. W A S D (eller piletasterne) bevæger sig over jorden, Shift bevæger sig hurtigere, Q og E drejer, og Home starter forfra.

Editor-indstillinger huskes næste gang, du åbner editoren. Forhåndsvisningsstørrelse, højdekortafstand, sidebjælketekststørrelse og autogem lever alle der. Autogem skriver en sikkerhedskopi ved siden af kortet på en timer og lader filen fra Gem være i fred.

## Ophavsret

Ophavsret © 2026 Black Cat Software  
Udviklet af Matt Andrzejczuk
