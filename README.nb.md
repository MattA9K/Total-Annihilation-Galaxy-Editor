<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Språk:** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · **Norsk Bokmål** · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

En kartredigerer for [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation), for macOS, Windows og Linux.

Mal fliser, plasser trær og metall, angi spillerstarter og form høyden på bakken. Kart åpnes og lagres som `.ufo`-filer som spillet kan laste.

## Last ned

| Plattform | Last ned |
| --- | --- |
| macOS | [GalaxyEditor-1.1.2-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-macos.dmg) |
| Windows | [GalaxyEditor-1.1.2-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-windows-x64.zip) |
| Linux (arkiv) | [GalaxyEditor-1.1.2-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz) |

På macOS, åpne diskbildet og dra Galaxy Editor-mappen til Programmer-mappen din. På Windows, pakk ut arkivet og kjør `GalaxyEditor.exe`. På Linux kan du bruke `.deb`-pakken eller pakke ut `.tar.gz`-arkivet. Hver nedlasting inkluderer `GE-Favorites-RandomMapGeneratorTiles.hpi` ved siden av appen. Flispensel leser det flissettet, så hold filen ved siden av `GalaxyEditor.app`, `GalaxyEditor.exe` eller Linux `GalaxyEditor`. `GreenWorld.hpi` er inkludert på samme måte. Første gang den starter, spør Galaxy Editor hvor Total Annihilation er installert. Hver utgivelse er oppført på [Utgivelser-siden](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Linux-installasjon

**Alternativ 1: Installer via .deb-pakke (Debian/Ubuntu/Mint)**

The `.deb` is not on the v1.1.2 release, so there is no download URL for it yet. The package file name is `galaxy-editor_<version>_amd64.deb`. For version 1.1.2 that name is `galaxy-editor_1.1.2_amd64.deb`. When a later release includes the file, install the downloaded package with:

```bash
sudo apt install ./galaxy-editor_1.1.2_amd64.deb
```

Until then, use the `.tar.gz` below. That archive is what works today.

**Alternativ 2: Bruk portabelt arkiv**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.2-linux-x64.tar.gz
cd GalaxyEditor-1.1.2-linux-x64
./GalaxyEditor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Hva du kan gjøre

**Fliser og funksjoner.** Søk i flissettene som følger med spillet, klikk på en seksjon og stemple den på kartet. Trær, steiner, metallflekker og ventiler bruker grafikken sin i spillet. Startnåler markerer hvor hver spiller starter.

**Terreng.** Slå på høydekartet, og hev, senk eller angi høyden på et utvalg. Kopier terrenget og høyden sammen, eller kopier bare høyden og la flisene være på plass. Blend Textures fader bildene der et utvalg møter flisene rundt det.

**Selve kartet.** Kartinnstillinger inneholder verdiene som spillet leser fra kartet: vind, tidevannsstrømmer, solstyrke, tyngdekraft, havnivå, metall, AI-profil og spesielle regler. Generer kart bygger et nytt landskap fra et flissett. Et minikart sitter over hjørnet av visningen, og angre dekker flise-, funksjons- og høyderedigeringer.

**3D-visning.** 3D-visningsknappen på verktøylinjen (Vis-meny, Ctrl+3) viser kartet som terreng, med høydekartet som løfter bakken. Et gjennomsiktig plan markerer havnivået fra Kartinnstillinger, og Redigeringsalternativer angir fargen og ugjennomskinneligheten. 3D-visningen er for å se seg rundt, så rediger i flat visning og bytt frem og tilbake som du vil.

## Bygg

Du trenger CMake 3.24 eller nyere, en C++20-kompilator, zlib og OpenGL. Den første konfigurasjonen laster ned [GLFW](https://www.glfw.org/) 3.4 og [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8, så det trinnet trenger en nettverkstilkobling.

macOS bygger `build/GalaxyEditor.app`. Åpne den fra Finder, eller kjør `open build/GalaxyEditor.app`:

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

For å bygge Windows `.exe` på en Mac- eller Linux-maskin, installer MinGW-w64 (`brew install mingw-w64`, eller `apt install g++-mingw-w64-x86-64-posix`), deretter:

```bash
cmake --preset windows
cmake --build --preset windows
```

Dette skriver `build-windows/GalaxyEditor.exe`. Det er én fil med C++ runtime innebygd, og den trenger bare DLL-ene som følger med Windows. Den første konfigurasjonen laster også ned zlib. Plasser dine egne flissett-arkiver ved siden av `.exe`.

På Windows selv, velg en generator som kompilatoren din forstår:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib må være synlig for CMake. På Debian og Ubuntu er det `zlib1g-dev`-pakken. På Fedora er det `zlib-devel`. Linux åpne- og lagre-dialoger bruker Zenity.

Formattestene bygges med redigereren:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Flissett

Galaxy Editor laster spillarkiver fra Total Annihilation-installasjonsmappen din. Første gang den starter, spør den etter den mappen før oppstartsskjermen vises. Velg Avbryt for å starte uten den. Redigereren kjører fortsatt, men Flissett- og Funksjoner-fanene forblir tomme, og den spør ikke igjen ved neste oppstart.

Endre mappen når som helst i Alternativer, under Total Annihilation. Velg Mappe laster arkivene på nytt med en gang, og Tøm glemmer mappen, så neste oppstart spør etter den igjen, som ved en første oppstart. Mappen lagres i per-bruker `settings.json`.

Galaxy Editor leser `.hpi`-, `.ccx`-, `.gp3`- og `.gpf`-filer direkte inne i den mappen, og deretter i mappen som inneholder den kjørbare `GalaxyEditor`-filen. På macOS er det mappen som inneholder `GalaxyEditor.app`, så en mappe kalt Galaxy Editor med appen og dine egne `.hpi`-filer side om side fungerer, slik Photoshop er lagt ut. Plasser dine egne flissett-arkiver ved siden av den kjørbare filen (eller `.app`). Hvis en fil der har samme navn som en fil i Total Annihilation-mappen, for eksempel `worlds.hpi` eller `totala1.hpi`, hoppes den over, så ingenting lastes to ganger. Et verdensarkiv som `worlds.hpi` fyller Flissett-fanen. `totala1.hpi`, `rev31.gp3` eller `ccdata.ccx` leverer funksjonslisten og funksjonsbildene.

Disse arkivene er spilldata. De er ikke en del av dette arkivet.

Åpne kart leser `.ufo`, `.hpi` og `.tnt`. Lagre skriver en `.ufo`.

## Favoritter

Favoritter-fanen, mellom Flissett og Funksjoner, inneholder fliser du lagrer selv. I Velg fliser-modus, velg noen fliser og høyreklikk. Menyen tilbyr Klipp ut, Kopier, Kopier høyder, Endre høyde, Bland og Legg til i favoritter. I Flissett-fanen viser pekeren på et bilde en stjerneknapp i hjørnet som legger til den flisen. I Favoritter-fanen inneholder samme hjørne en søppelkasseknapp som sletter flisen etter at du har bekreftet.

Legg til i favoritter spør om tre ting: et favorittarkiv, en kategori og en underkategori. Skriv inn nye navn eller velg eksisterende. Navnene fungerer som de gjør i spillets egne flissett: i `Acid_New_1.hpi` er kategorien Acid_New_1, og Rivers og Sm Lakes er underkategoriene. Opprett favorittarkiv spør om et navn som FirstOne og skriver `GE-Favorites-FirstOne.hpi` ved siden av den kjørbare `GalaxyEditor`-filen (ved siden av `.app` på macOS).

Bare arkiver med navnet `GE-Favorites-<navn>.hpi` vises på Favoritter-fanen, og de holdes borte fra Flissett-fanen. Innvendig er en flise `sections/<kategori>/<underkategori>/<flise>.sct`, så arkivet er et vanlig flissett som du kan dele ved å kopiere filen til noens andre mappe. En lagret flise er et fullstendig rektangel. Når utvalget har hull, lagres flisene mellom de valgte også, fordi en seksjonsfil ikke har plass til hull. En flise kan være maksimalt 512 fliser på hver side.

## HPI-redigerer

HPI-redigeringsknappen i høyre ende av verktøylinjen (Fil-meny, HPI-redigerer) åpner en filnettleser for arkiver: `.hpi`, `.ufo`, `.ccx`, `.gp3` og `.gpf`. Åpne en med Åpne-knappen, eller start et tomt arkiv med Ny. Når et kart du har åpent selv er et arkiv, tilbyr det tomme vinduet det med ett klikk.

Vinduet følger oppsettet til et Finder-vindu. Et mappetrær løper ned til venstre, innholdet fyller midten som en liste eller som ikoner (de to knappene øverst til høyre), og en stibjælke og en varetellingen løper langs bunnen. Klikk på en mappe i treet eller et segment av stibjælken for å gå dit, dobbeltklikk på en mappe for å åpne den, og bruk Tilbake og Fremover for å spore trinnene dine tilbake. Velg med et klikk, Command-klikk eller Ctrl-klikk for å legge til en, Shift-klikk for et område, eller Ctrl+A. Søkeboksen finner filer og mapper etter navn under mappen du ser på, og dobbeltklikk på et resultat viser hvor den bor.

- **Eksporter** kopierer de valgte filene og mappene til en mappe du velger. En mappe beholder navnet sitt og alt i den. Uten noe valgt, tar Eksporter mappen du ser på, og øverst i arkivet oppretter den en mappe oppkalt etter arkivet slik at innholdet ikke spres. Hvis en fil allerede er der, blir du spurt før den erstattes. Navn som kan nå utenfor mappen du valgte hoppes over, og tegn som en datamaskin ikke tillater i filnavn erstattes med en understrek.
- **Importer** legger til filer, eller hele mapper med alt i dem, til mappen du ser på. Du kan også slippe filer og mapper fra skrivebordet på vinduet. Importer spør før den erstatter en fil med samme navn. Filnavn i et arkiv matcher uten hensyn til store og små bokstaver, som de gjør i spillet.
- **Flytt** ved å dra elementer fra en mappe til en annen: til en mappe i listen eller ikonvisningen, en mappe i treet eller et segment av stibjælken. Å dra en mappe inn i seg selv avvises, og en mappe som allerede finnes på destinasjonen slås sammen inn i den.
- **Gi nytt navn** med F2 eller høyreklikkmeny, **Ny mappe** med verktøylinjeknappen eller Ctrl+Shift+N, og **Slett** med Delete-tasten. Hver endring kan angres og gjøres om.

Ingenting berører arkivet på disken før du trykker på Lagre. Lagre og Lagre som skriver et nytt arkiv og erstatter den gamle filen i ett trinn, så en feil lar originalen være i fred. Et arkiv kan ikke lagre en tom mappe eller ikke ha noen filer i det hele tatt, så tomme mapper overlever ikke en lagring, og redigereren vil ikke lagre et arkiv uten noe i det. Lukking av vinduet, åpning av et annet arkiv eller avslutning spør om ulagrede endringer først.

## Snarveier

På macOS tar Command plassen til Ctrl.

| Handling | Snarvei |
| --- | --- |
| Nytt kart | Ctrl+N |
| Åpne kart | Ctrl+O |
| Lagre | Ctrl+S |
| Lagre som | Ctrl+Shift+S |
| Avslutt | Ctrl+Q |
| Angre | Ctrl+Z |
| Gjør om | Ctrl+Shift+Z |
| Klipp ut | Ctrl+X |
| Kopier terreng + høydekart | Ctrl+C |
| Kopier høydekart | Ctrl+Shift+C |
| Lim inn | Ctrl+V |
| Redigeringsalternativer | Ctrl+, |
| 3D-visning på eller av | Ctrl+3 |

I HPI-redigeringsvinduet fungerer disse når det har tastaturet i stedet for kartsnarveiene: Ctrl+O åpner et arkiv, Ctrl+N starter et nytt, Ctrl+S og Ctrl+Shift+S lagrer, Ctrl+Z og Ctrl+Shift+Z angrer og gjør om, Ctrl+A velger alt, Ctrl+I importerer, Ctrl+Shift+N oppretter en mappe, Ctrl+[ og Ctrl+] går tilbake og fremover, Ctrl+Up går til den omsluttende mappen, Ctrl+Down eller Return åpner en mappe, F2 gir nytt navn, Delete eller Ctrl+Backspace fjerner utvalget, og Backspace alene går opp en mappe.

Piltastene glir et stempel eller en innsetting fra flisen under markøren. Esc avbryter en innsetting, forlater Slett-modus eller tømmer utvalget.

I 3D-visningen, dra for å gå i bane, høyredra eller Shift-dra for å panorere og rull for å zoome. W A S D (eller piltastene) beveger seg over bakken, Shift beveger seg raskere, Q og E svinger, og Home starter på nytt.

Redigeringsalternativer huskes neste gang du åpner redigereren. Forhåndsvisningsstørrelse, høydekartavstand, sidefelttekststørrelse og autolagring lever alle der. Autolagring skriver en sikkerhetskopi ved siden av kartet på en tidtaker og lar filen fra Lagre være i fred.

## Opphavsrett

Opphavsrett © 2026 Black Cat Software  
Utviklet av Matt Andrzejczuk
