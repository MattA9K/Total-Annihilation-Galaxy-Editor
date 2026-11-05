<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Språk:** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · [Polski](README.pl.md) · **Svenska** · [Hrvatski](README.hr.md) · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

En kartredigerare för [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation), för macOS, Windows och Linux.

Måla plattor, placera träd och metall, ställ in spelarstarter och forma markens höjd. Kartor öppnas och sparas som `.ufo`-filer som spelet kan ladda.

## Ladda ner

| Plattform | Ladda ner |
| --- | --- |
| macOS | [GalaxyEditor-1.1.2-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-macos.dmg) |
| Windows | [GalaxyEditor-1.1.2-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-windows-x64.zip) |
| Linux (arkiv) | [GalaxyEditor-1.1.2-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz) |

På macOS, öppna diskavbildningen och dra Galaxy Editor-mappen till din Program-mapp. På Windows, packa upp arkivet och kör `GalaxyEditor.exe`. På Linux kan du använda `.deb`-paketet eller packa upp `.tar.gz`-arkivet. Varje nedladdning inkluderar `GE-Favorites-RandomMapGeneratorTiles.hpi` bredvid appen. Plattborsten läser det plattuppsättningen, så behåll filen bredvid `GalaxyEditor.app`, `GalaxyEditor.exe` eller Linux `GalaxyEditor`. `GreenWorld.hpi` ingår på samma sätt. Första gången den startar frågar Galaxy Editor först om språket och sedan var Total Annihilation är installerat. Varje utgåva listas på [utgåvorsidan](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Linux-installation

**Alternativ 1: Installera via .deb-paket (Debian/Ubuntu/Mint)**

The `.deb` is not on the v1.1.2 release, so there is no download URL for it yet. The package file name is `galaxy-editor_<version>_amd64.deb`. For version 1.1.2 that name is `galaxy-editor_1.1.2_amd64.deb`. When a later release includes the file, install the downloaded package with:

```bash
sudo apt install ./galaxy-editor_1.1.2_amd64.deb
```

Until then, use the `.tar.gz` below. That archive is what works today.

**Alternativ 2: Använd portabelt arkiv**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.2-linux-x64.tar.gz
cd GalaxyEditor-1.1.2-linux-x64
./GalaxyEditor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Vad du kan göra

**Plattor och funktioner.** Sök i plattuppsättningarna som medföljer spelet, klicka på ett avsnitt och stämpla det på kartan. Träd, stenar, metallfläckar och ventiler använder sin spelinterna grafik. Startnålar markerar var varje spelare börjar.

**Terräng.** Aktivera höjdkartan och höj, sänk eller ställ in markeringens höjd. Kopiera terrängen och höjden tillsammans, eller kopiera bara höjden och lämna plattorna på plats. Blend Textures tonar bilderna där en markering möter plattorna runt den.

**Själva kartan.** Kartinställningar innehåller värdena som spelet läser från kartan: vind, tidvatten, solstyrka, gravitation, havsnivå, metall, AI-profil och specialregler. Generera karta bygger ett nytt landskap från en plattuppsättning. En minikarta sitter över hörnet av vyn, och ångra täcker platt-, funktions- och höjdredigeringar.

**3D-vy.** 3D-vyknappen i verktygsfältet (Visa-menyn, Ctrl+3) visar kartan som terräng, med höjdkartan som lyfter marken. Ett genomskinligt plan markerar havsnivån från Kartinställningar, och Redigeringsalternativ ställer in dess färg och opacitet. 3D-vyn är för att titta runt, så redigera i platt vy och växla fram och tillbaka som du vill.

## Bygg

Du behöver CMake 3.24 eller nyare, en C++20-kompilator, zlib och OpenGL. Den första konfigurationen laddar ner [GLFW](https://www.glfw.org/) 3.4 och [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8, så det steget behöver en nätverksanslutning.

macOS bygger `build/GalaxyEditor.app`. Öppna den från Finder, eller kör `open build/GalaxyEditor.app`:

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

För att bygga Windows `.exe` på en Mac- eller Linux-maskin, installera MinGW-w64 (`brew install mingw-w64`, eller `apt install g++-mingw-w64-x86-64-posix`), sedan:

```bash
cmake --preset windows
cmake --build --preset windows
```

Detta skriver `build-windows/GalaxyEditor.exe`. Det är en fil med inbyggd C++-runtime och den behöver bara DLL:er som medföljer Windows. Den första konfigurationen laddar också ner zlib. Placera dina egna plattuppsättningsarkiv bredvid `.exe`.

På Windows själv, välj en generator som din kompilator förstår:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib måste vara synlig för CMake. På Debian och Ubuntu är det `zlib1g-dev`-paketet. På Fedora är det `zlib-devel`. Linux öppna och spara-dialoger använder Zenity.

Formattesterna byggs med redigeraren:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Plattuppsättningar

Galaxy Editor laddar spelarkiv från din Total Annihilation-installationsmapp. Allra första gången den startar, före startskärmen, frågar den om språket och sedan efter den mappen. Välj Avbryt för att starta utan den. Redigeraren kör fortfarande, men flikarna Plattuppsättningar och Funktioner förblir tomma, och den frågar inte igen vid nästa start.

Ändra mappen när som helst i Alternativ, under Total Annihilation. Välj Mapp laddar om arkiven direkt, och Rensa glömmer mappen, så nästa start frågar efter den igen, som vid en första start. Mappen och det valda språket sparas i per-användar-`settings.json`. Språket ändras senare i Alternativ, under Språk, och fönstret byter direkt.

Galaxy Editor läser `.hpi`-, `.ccx`-, `.gp3`- och `.gpf`-filer direkt inuti den mappen, och sedan i mappen som innehåller den körbara `GalaxyEditor`-filen. På macOS är det mappen som innehåller `GalaxyEditor.app`, så en mapp som heter Galaxy Editor med appen och dina egna `.hpi`-filer sida vid sida fungerar, precis som Photoshop är upplagt. Placera dina egna plattuppsättningsarkiv bredvid den körbara filen (eller `.app`). Om en fil där har samma namn som en i Total Annihilation-mappen, såsom `worlds.hpi` eller `totala1.hpi`, hoppas den över, så ingenting laddas två gånger. Ett världsarkiv som `worlds.hpi` fyller fliken Plattuppsättningar. `totala1.hpi`, `rev31.gp3` eller `ccdata.ccx` tillhandahåller funktionslistan och funktionsbilderna.

Dessa arkiv är speldata. De är inte en del av detta arkiv.

Öppna karta läser `.ufo`, `.hpi` och `.tnt`. Spara skriver en `.ufo`.

## Favoriter

Fliken Favoriter, mellan Plattuppsättningar och Funktioner, innehåller plattor som du sparar själv. I Välj plattor-läge, välj några plattor och högerklicka. Menyn erbjuder Klipp ut, Kopiera, Kopiera höjder, Modifiera höjd, Blanda och Lägg till i favoriter. I fliken Plattuppsättningar visar pekning på en bild en stjärnknapp i dess hörn som lägger till den plattan. I fliken Favoriter har samma hörn en papperskorgknapp som tar bort plattan efter att du bekräftat.

Lägg till i favoriter frågar efter tre saker: ett favoritarkiv, en kategori och en underkategori. Skriv nya namn eller välj befintliga. Namnen fungerar som de gör i spelets egna plattuppsättningar: i `Acid_New_1.hpi` är kategorin Acid_New_1, och Rivers och Sm Lakes är dess underkategorier. Skapa favoritarkiv frågar efter ett namn som FirstOne och skriver `GE-Favorites-FirstOne.hpi` bredvid den körbara `GalaxyEditor`-filen (bredvid `.app` på macOS).

Endast arkiv som heter `GE-Favorites-<namn>.hpi` visas på fliken Favoriter, och de hålls borta från fliken Plattuppsättningar. Inuti är en platta `sections/<kategori>/<underkategori>/<platta>.sct`, så arkivet är en vanlig plattuppsättning som du kan dela genom att kopiera filen till någon annans mapp. En sparad platta är en fullständig rektangel. När markeringen har luckor sparas plattorna mellan de valda också, eftersom en sektionsfil inte har utrymme för hål. En platta kan vara högst 512 plattor på varje sida.

## HPI-redigerare

HPI-redigeringsknappen längst till höger i verktygsfältet (Arkiv-menyn, HPI-redigerare) öppnar en filbläddrare för arkiv: `.hpi`, `.ufo`, `.ccx`, `.gp3` och `.gpf`. Öppna en med Öppna-knappen, eller starta ett tomt arkiv med Ny. När en karta du har öppen själv är ett arkiv erbjuder det tomma fönstret den med ett klick.

Fönstret följer layouten för ett Finder-fönster. Ett mappträd löper ner till vänster, innehållet fyller mitten som en lista eller som ikoner (de två knapparna längst upp till höger), och en sökvägsrad och en objekträkning löper längs botten. Klicka på en mapp i trädet eller ett segment av sökvägsraden för att gå dit, dubbelklicka på en mapp för att öppna den och använd Tillbaka och Framåt för att spåra dina steg. Välj med ett klick, Command-klick eller Ctrl-klick för att lägga till en, Shift-klick för ett område eller Ctrl+A. Sökrutan hittar filer och mappar efter namn under mappen du tittar på, och dubbelklicka på ett resultat visar var det bor.

- **Exportera** kopierar de valda filerna och mapparna till en mapp du väljer. En mapp behåller sitt namn och allt inuti den. Med ingenting valt tar Exportera mappen du tittar på, och högst upp i arkivet skapar den en mapp uppkallad efter arkivet så dess innehåll inte sprids. Om en fil redan finns där tillfrågas du innan den ersätts. Namn som kan nå utanför mappen du valde hoppas över, och tecken som en dator inte tillåter i filnamn ersätts med ett understreck.
- **Importera** lägger till filer, eller hela mappar med allt inuti dem, till mappen du tittar på. Du kan också släppa filer och mappar från skrivbordet på fönstret. Import frågar innan den ersätter en fil med samma namn. Filnamn i ett arkiv matchar utan hänsyn till skiftläge, som de gör i spelet.
- **Flytta** genom att dra objekt från en mapp till en annan: till en mapp i listan eller ikonvyn, en mapp i trädet eller ett segment av sökvägsraden. Att dra en mapp till sig själv avvisas, och en mapp som redan finns på destinationen slås samman till.
- **Byt namn** med F2 eller högerklicksmenyn, **Ny mapp** med verktygsfältsknappen eller Ctrl+Shift+N, och **Ta bort** med Delete-tangenten. Varje ändring kan ångras och göras om.

Ingenting berör arkivet på disken tills du trycker på Spara. Spara och Spara som skriver ett nytt arkiv och ersätter den gamla filen i ett steg, så ett misslyckande lämnar originalet ifred. Ett arkiv kan inte lagra en tom mapp eller inte ha några filer alls, så tomma mappar överlever inte en sparning och redigeraren kommer inte att spara ett arkiv utan någonting i det. Att stänga fönstret, öppna ett annat arkiv eller avsluta frågar om osparade ändringar först.

## Genvägar

På macOS tar Command platsen för Ctrl.

| Åtgärd | Genväg |
| --- | --- |
| Ny karta | Ctrl+N |
| Öppna karta | Ctrl+O |
| Spara | Ctrl+S |
| Spara som | Ctrl+Shift+S |
| Avsluta | Ctrl+Q |
| Ångra | Ctrl+Z |
| Gör om | Ctrl+Shift+Z |
| Klipp ut | Ctrl+X |
| Kopiera terräng + höjdkarta | Ctrl+C |
| Kopiera höjdkarta | Ctrl+Shift+C |
| Klistra in | Ctrl+V |
| Redigeringsalternativ | Ctrl+, |
| 3D-vy på eller av | Ctrl+3 |

I HPI-redigeringsfönstret fungerar dessa när det har tangentbordet istället för kartgenvägarna: Ctrl+O öppnar ett arkiv, Ctrl+N startar ett nytt, Ctrl+S och Ctrl+Shift+S sparar, Ctrl+Z och Ctrl+Shift+Z ångrar och gör om, Ctrl+A väljer allt, Ctrl+I importerar, Ctrl+Shift+N skapar en mapp, Ctrl+[ och Ctrl+] går tillbaka och framåt, Ctrl+Up går till den omslutande mappen, Ctrl+Down eller Return öppnar en mapp, F2 byter namn, Delete eller Ctrl+Backspace tar bort markeringen, och Backspace ensam går upp en mapp.

Piltangenterna glider en stämpel eller en inklistring från plattan under markören. Esc avbryter en inklistring, lämnar Radera-läge eller rensar markeringen.

I 3D-vyn, dra för att kretsa, högerdra eller Shift-dra för att panorera och scrolla för att zooma. W A S D (eller piltangenterna) rör sig över marken, Shift rör sig snabbare, Q och E svänger, och Home börjar om.

Redigeringsalternativ kommer ihåg nästa gång du öppnar redigeraren. Förhandsgranskningsstorlek, höjdkartsavstånd, sidofältets textstorlek och autospara lever alla där. Autospara skriver en säkerhetskopia bredvid kartan på en timer och lämnar filen från Spara ifred.

## Upphovsrätt

Upphovsrätt © 2026 Black Cat Software  
Utvecklad av Matt Andrzejczuk
