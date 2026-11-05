<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Jazyky:** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · **Slovenčina**

# Galaxy Editor

Editor máp pre [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation), pre macOS, Windows a Linux.

Maľujte dlaždice, umiestňujte stromy a kov, nastavte štarty hráčov a tvarujte výšku terénu. Mapy sa otvárajú a ukladajú ako súbory `.ufo`, ktoré hra môže načítať.

## Stiahnuť

| Platforma | Stiahnuť |
| --- | --- |
| macOS | [GalaxyEditor-1.1.2-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-macos.dmg) |
| Windows | [GalaxyEditor-1.1.2-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-windows-x64.zip) |
| Linux (archív) | [GalaxyEditor-1.1.2-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz) |

V macOS otvorte obraz disku a presuňte priečinok Galaxy Editor do priečinka Aplikácie. Vo Windows rozbaľte archív a spustite `GalaxyEditor.exe`. V Linuxe môžete použiť balík `.deb` alebo rozbaliť archív `.tar.gz`. Každé stiahnutie obsahuje `GE-Favorites-RandomMapGeneratorTiles.hpi` vedľa aplikácie. Štetec dlaždíc číta túto sadu dlaždíc, takže súbor držte vedľa `GalaxyEditor.app`, `GalaxyEditor.exe` alebo Linux `GalaxyEditor`. `GreenWorld.hpi` je zahrnutý rovnakým způsobom. Pri prvom spustení Galaxy Editor opýta sa, kde je nainštalovaný Total Annihilation. Každé vydanie je uvedené na [stránke vydaní](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Inštalácia Linuxu

**Možnosť 1: Inštalácia cez balík .deb (Debian/Ubuntu/Mint)**

The `.deb` is not on the v1.1.2 release, so there is no download URL for it yet. The package file name is `galaxy-editor_<version>_amd64.deb`. For version 1.1.2 that name is `galaxy-editor_1.1.2_amd64.deb`. When a later release includes the file, install the downloaded package with:

```bash
sudo apt install ./galaxy-editor_1.1.2_amd64.deb
```

Until then, use the `.tar.gz` below. That archive is what works today.

**Možnosť 2: Použitie prenosného archívu**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.2-linux-x64.tar.gz
cd GalaxyEditor-1.1.2-linux-x64
./GalaxyEditor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Čo môžete robiť

**Dlaždice a funkcie.** Prehľadávajte sady dlaždíc, ktoré sa dodávajú s hrou, kliknite na sekciu a odtlačte ju na mapu. Stromy, kamene, kovové škvrny a otvory používajú svoju grafiku v hre. Štartovacie špendlíky označujú, kde každý hráč začína.

**Terén.** Zapnite výškovú mapu a potom zvýšte, znížte alebo nastavte výšku výberu. Skopírujte terén a výšku spolu alebo skopírujte iba výšku a nechajte dlaždice na mieste. Blend Textures zosvetľuje obrázky tam, kde sa výber stretáva s dlaždice okolo neho.

**Samotná mapa.** Nastavenia mapy obsahujú hodnoty, ktoré hra číta z mapy: vietor, príliv a odliv, solárna sila, gravitácia, hladina mora, kov, AI profil a špeciálne pravidlá. Generovať mapu buduje novú krajinu zo sady dlaždíc. Minimapa sedí nad rohom pohľadu a späť zahŕňa úpravy dlaždíc, funkcií a výšky.

**3D zobrazenie.** Tlačidlo 3D zobrazenie na paneli nástrojov (ponuka Zobraziť, Ctrl+3) zobrazuje mapu ako terén s výškovou mapou, ktorá dvíha zem. Polopriehľadná rovina označuje hladinu mora z Nastavení mapy a Možnosti editora nastavujú jej farbu a nepriehľadnosť. 3D zobrazenie je na rozhliadnutie sa, takže upravujte v plochom zobrazení a prepínajte tam a späť, ako chcete.

## Zostavenie

Potrebujete CMake 3.24 alebo novší, kompilátora C++20, zlib a OpenGL. Prvá konfigurácia stiahne [GLFW](https://www.glfw.org/) 3.4 a [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8, takže tento krok potrebuje sieťové pripojenie.

macOS zostavuje `build/GalaxyEditor.app`. Otvorte ho z Finderu alebo spustite `open build/GalaxyEditor.app`:

```bash
cmake --preset default
cmake --build build
open build/GalaxyEditor.app
```

Linux:

```bash
cmake --preset default
cmake --build build
./build/GalaxyEditor
```

Na zostavenie Windows `.exe` na Mac alebo Linux stroji nainštalujte MinGW-w64 (`brew install mingw-w64` alebo `apt install g++-mingw-w64-x86-64-posix`), potom:

```bash
cmake --preset windows
cmake --build --preset windows
```

Toto zapíše `build-windows/GalaxyEditor.exe`. Je to jeden súbor s vbudovaným C++ runtime a potrebuje len DLL, ktoré sa dodávajú s Windows. Prvá konfigurácia tiež stiahne zlib. Umiestnite svoje vlastné archívy sád dlaždíc vedľa `.exe`.

Vo Windows samotnom vyberte generátor, ktorému váš kompilátor rozumie:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib musí byť viditeľný pre CMake. V Debiane a Ubuntu je to balík `zlib1g-dev`. Vo Fedore je to `zlib-devel`. Dialógy na otváranie a ukladanie v Linuxe používajú Zenity.

Formátové testy sa zostavujú s editorom:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Sady dlaždíc

Galaxy Editor načítava herné archívy z vášho inštalačného priečinka Total Annihilation. Pri prvom spustení sa opýta na tento priečinok pred zobrazením úvodnej obrazovky. Vyberte Zrušiť na spustenie bez neho. Editor stále beží, ale záložky Sady dlaždíc a Funkcie zostávajú prázdne a nepýta sa znovu pri ďalšom spustení.

Zmeňte priečinok kedykoľvek v Možnostiach pod Total Annihilation. Vybrať priečinok znovu načíta archívy okamžite a Vymazať zabudne priečinok, takže ďalšie spustenie sa opýta na neho znovu, ako pri prvom spustení. Priečinok sa ukladá do `settings.json` pre každého používateľa.

Galaxy Editor číta súbory `.hpi`, `.ccx`, `.gp3` a `.gpf` priamo v tomto priečinku a potom v priečinku obsahujúcom spustiteľný súbor `GalaxyEditor`. V macOS je to priečinok obsahujúci `GalaxyEditor.app`, takže priečinok s názvom Galaxy Editor s aplikáciou a vašimi vlastnými súbormi `.hpi` vedľa seba funguje, ako je rozložený Photoshop. Umiestnite svoje vlastné archívy sád dlaždíc vedľa spustiteľného súboru (alebo `.app`). Ak má súbor tam rovnaký názov ako jeden v priečinku Total Annihilation, napríklad `worlds.hpi` alebo `totala1.hpi`, preskočí sa, takže sa nič nenačíta dvakrát. Svetový archív ako `worlds.hpi` vypĺňa záložku Sady dlaždíc. `totala1.hpi`, `rev31.gp3` alebo `ccdata.ccx` dodávajú zoznam funkcií a obrázky funkcií.

Tieto archívy sú herné dáta. Nie sú súčasťou tohto repozitára.

Otvoriť mapu číta `.ufo`, `.hpi` a `.tnt`. Uložiť zapíše `.ufo`.

## Obľúbené

Záložka Obľúbené, medzi Sady dlaždíc a Funkcie, obsahuje dlaždice, ktoré si uložíte sami. V režime Vybrať dlaždice vyberte niekoľko dlaždíc a kliknite pravým tlačidlom myši. Ponuka ponúka Vystrihnúť, Kopírovať, Kopírovať výšky, Upraviť výšku, Zmiešať a Pridať do obľúbených. V záložke Sady dlaždíc ukážete na obrázok, zobrazí sa tlačidlo hviezdičky v jeho rohu, ktoré pridá túto dlaždicu. V záložke Obľúbené má rovnaký roh tlačidlo koša, ktoré vymaže dlaždicu po potvrdení.

Pridať do obľúbených sa pýta na tri veci: archív obľúbených, kategóriu a podkategóriu. Zadajte nové názvy alebo vyberte existujúce. Názvy fungujú tak, ako fungujú vo vlastných sadách dlaždíc hry: v `Acid_New_1.hpi` je kategória Acid_New_1 a Rivers a Sm Lakes sú jej podkategórie. Vytvoriť archív obľúbených sa pýta na názov ako FirstOne a zapíše `GE-Favorites-FirstOne.hpi` vedľa spustiteľného súboru `GalaxyEditor` (vedľa `.app` v macOS).

Iba archívy s názvom `GE-Favorites-<názov>.hpi` sa zobrazujú na záložke Obľúbené a sú držané ďaleko od záložky Sady dlaždíc. Vo vnútri je dlaždica `sections/<kategória>/<podkategória>/<dlaždica>.sct`, takže archív je bežná sada dlaždíc, ktorú môžete zdieľať kopírovaním súboru do priečinka niekoho iného. Uložená dlaždica je úplný obdĺžnik. Keď má výber medzery, dlaždice medzi vybranými sa tiež uložia, pretože súbor sekcie nemá miesto pre diery. Dlaždica môže byť maximálne 512 dlaždíc na každej strane.

## HPI editor

Tlačidlo HPI editor na pravom konci panela nástrojov (ponuka Súbor, HPI editor) otvorí prehliadač súborov pre archívy: `.hpi`, `.ufo`, `.ccx`, `.gp3` a `.gpf`. Otvorte jeden tlačidlom Otvoriť alebo začnite prázdny archív s Nový. Keď je mapa, ktorú máte otvorenú, sama archívom, prázdne okno ho ponúka jedným kliknutím.

Okno sleduje rozloženie okna Finderu. Strom priečinkov beží dolu vľavo, obsah vypĺňa stred ako zoznam alebo ako ikony (dve tlačidlá vpravo hore) a panel s cestou a počet položiek bežia pozdĺž spodku. Kliknite na priečinok v strome alebo segment panela s cestou, aby ste tam išli, dvakrát kliknite na priečinok, aby ste ho otvorili, a použite Späť a Dopredu na sledovanie vašich krokov. Vyberte kliknutím, Command-kliknutím alebo Ctrl-kliknutím na pridanie jedného, Shift-kliknutím pre rozsah alebo Ctrl+A. Vyhľadávacie pole nájde súbory a priečinky podľa názvu pod priečinkom, ktorý prezeráte, a dvojklik na výsledok ukáže, kde sa nachádza.

- **Exportovať** kopíruje vybraté súbory a priečinky do priečinka, ktorý vyberiete. Priečinok si ponecháva svoj názov a všetko v ňom. Bez ničoho vybraného Export vezme priečinok, ktorý prezeráte, a navrchu archívu vytvorí priečinok pomenovaný podľa archívu, aby sa jeho obsah nerozptýlil. Ak súbor už tam je, opýta sa vás pred jeho nahradením. Názvy, ktoré by mohli dosahovať mimo priečinka, ktorý ste vybrali, sa preskočia a znaky, ktoré počítač nepovoľuje v názvoch súborov, sa nahradia podčiarkovníkom.
- **Importovať** pridáva súbory alebo celé priečinky so všetkým v nich do priečinka, ktorý prezeráte. Môžete tiež pustiť súbory a priečinky z pracovnej plochy na okno. Import sa opýta pred nahradením súboru s rovnakým názvom. Názvy súborov v archíve sa zhodujú bez ohľadu na veľkosť písmen, ako to robia v hre.
- **Presunúť** presunutím položiek z jedného priečinka do druhého: na priečinok v zozname alebo zobrazení ikon, priečinok v strome alebo segment panela s cestou. Presunutie priečinka do samého seba sa odmietne a priečinok, ktorý už v cieli existuje, sa zlúči do neho.
- **Premenovať** s F2 alebo ponukou pravým tlačidlom myši, **Nový priečinok** s tlačidlom panela nástrojov alebo Ctrl+Shift+N a **Vymazať** s klávesom Delete. Každú zmenu možno vrátiť a znovu vykonať.

Nič sa nedotýka archívu na disku, kým nestlačíte Uložiť. Uložiť a Uložiť ako zapíšu nový archív a nahradia starý súbor v jednom kroku, takže zlyhanie ponechá originál na pokoji. Archív nemôže uložiť prázdny priečinok ani nemať žiadne súbory vôbec, takže prázdne priečinky neprežijú uloženie a editor neuloží archív bez ničoho v ňom. Zatvorenie okna, otvorenie iného archívu alebo ukončenie sa najskôr opýta na neuložené zmeny.

## Skratky

V macOS Command zaujíma miesto Ctrl.

| Akcia | Skratka |
| --- | --- |
| Nová mapa | Ctrl+N |
| Otvoriť mapu | Ctrl+O |
| Uložiť | Ctrl+S |
| Uložiť ako | Ctrl+Shift+S |
| Ukončiť | Ctrl+Q |
| Vrátiť | Ctrl+Z |
| Zopakovať | Ctrl+Shift+Z |
| Vystrihnúť | Ctrl+X |
| Kopírovať terén + výšková mapa | Ctrl+C |
| Kopírovať výškovú mapu | Ctrl+Shift+C |
| Vložiť | Ctrl+V |
| Možnosti editora | Ctrl+, |
| 3D zobrazenie zapnuté alebo vypnuté | Ctrl+3 |

V okne HPI editora fungujú tieto, keď má klávesnicu namiesto skratiek mapy: Ctrl+O otvorí archív, Ctrl+N začne nový, Ctrl+S a Ctrl+Shift+S uložia, Ctrl+Z a Ctrl+Shift+Z vrátia a zopakujú, Ctrl+A vyberie všetko, Ctrl+I importuje, Ctrl+Shift+N vytvorí priečinok, Ctrl+[ a Ctrl+] idú späť a dopredu, Ctrl+Up ide na obaľujúci priečinok, Ctrl+Down alebo Return otvorí priečinok, F2 premenuje, Delete alebo Ctrl+Backspace odstráni výber a samotný Backspace ide o priečinok vyššie.

Klávesy so šípkami posunú pečiatku alebo vloženie z dlaždice pod kurzorom. Esc zruší vloženie, opustí režim Vymazať alebo vyčistí výber.

V 3D zobrazení potiahnite na obíhanie, pravým ťahom alebo Shift-ťahom na panorámovanie a posúvajte na zväčšenie. W A S D (alebo klávesy so šípkami) sa pohybujú po zemi, Shift sa pohybuje rýchlejšie, Q a E otáčajú a Home začína znova.

Možnosti editora sa zapamätajú nabudúce, keď otvoríte editor. Veľkosť náhľadu, rozstup výškovej mapy, veľkosť textu bočného panela a automatické ukladanie všetky tam žijú. Automatické ukladanie zapíše zálohu vedľa mapy na časovač a nechá súbor z Uložiť na pokoji.

## Autorské práva

Autorské práva © 2026 Black Cat Software  
Vyvinul: Matt Andrzejczuk
