<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Kielet:** [English](README.md) · [中文](README.zh-CN.md) · **Suomi** · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

Karttaeditori [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation) -pelille macOS:lle, Windowsille ja Linuxille.

Maalaa laattoja, sijoita puita ja metallia, aseta pelaajien aloituskohdat ja muokkaa maaston korkeutta. Kartat avautuvat ja tallentuvat `.ufo`-tiedostoina, jotka peli voi ladata.

## Lataa

| Alusta | Lataus |
| --- | --- |
| macOS | [GalaxyEditor-1.1.2-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-macos.dmg) |
| Windows | [GalaxyEditor-1.1.2-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-windows-x64.zip) |
| Linux (arkisto) | [GalaxyEditor-1.1.2-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz) |
| Linux (Debian/Ubuntu) | [Latest .deb package](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/latest) |

MacOS:ssä avaa levykuva ja vedä Galaxy Editor -kansio Applications-kansioon. Windowsissa pura arkisto ja suorita `GalaxyEditor.exe`. Linuxissa voit käyttää `.deb`-pakettia tai purkaa `.tar.gz`-arkiston. Jokainen lataus sisältää `GE-Favorites-RandomMapGeneratorTiles.hpi` -tiedoston sovelluksen vieressä. Laattojen sivellin lukee kyseisen laattavalikoiman, joten pidä tiedosto `GalaxyEditor.app`:n, `GalaxyEditor.exe`:n tai Linux-version `GalaxyEditor`:n vieressä. `GreenWorld.hpi` on mukana samalla tavalla. Ensimmäisellä käynnistyskerralla Galaxy Editor kysyy, missä Total Annihilation on asennettu. Jokainen julkaisu on lueteltu [Julkaisut-sivulla](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Linux-asennus

**Vaihtoehto 1: Asenna .deb-paketilla (Debian/Ubuntu/Mint)**

```bash
# Replace VERSION with the actual version number (e.g., 1.1.3)
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/vVERSION/galaxy-editor_VERSION_amd64.deb
sudo apt install ./galaxy-editor_VERSION_amd64.deb
```

**Vaihtoehto 2: Käytä siirrettävää arkistoa**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.2-linux-x64.tar.gz
cd GalaxyEditor-1.1.2-linux-x64
./GalaxyEditor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Mitä voit tehdä

**Laatat ja ominaisuudet.** Etsi pelin mukana tulevia laattavalikoimia, napsauta osiota ja leimaa se kartalle. Puut, kivet, metallilaikut ja aukot käyttävät pelin sisäisiä grafiikkoja. Aloitusneulat merkitsevät, mistä kukin pelaaja aloittaa.

**Maasto.** Ota korkeuskartta käyttöön ja nosta, laske tai aseta valinnan korkeus. Kopioi maasto ja korkeus yhdessä tai kopioi vain korkeus ja jätä laatat paikoilleen. Blend Textures -toiminto häivyttää kuvat kohdassa, jossa valinta kohtaa ympäröivät laatat.

**Itse kartta.** Kartan asetukset sisältävät arvot, jotka peli lukee kartalta: tuuli, vuorovesi, auringon voimakkuus, painovoima, merenpinta, metalli, AI-profiili ja erityissäännöt. Luo kartta rakentaa uuden maiseman laattojen valikoimasta. Minikartta on näkymän kulmassa, ja kumoa kattaa laattojen, ominaisuuksien ja korkeuksien muokkaukset.

**3D-näkymä.** Työkalurivin 3D-näkymä-painike (Näytä-valikko, Ctrl+3) näyttää kartan maastona, jossa korkeuskartta nostaa maata. Läpinäkyvä taso merkitsee kartan asetusten merenpintaa, ja editorin asetukset määrittävät sen värin ja läpinäkyvyyden. 3D-näkymä on tarkoitettu katselemiseen, joten muokkaa tasaisessa näkymässä ja vaihda edestakaisin haluamallasi tavalla.

## Rakenna

Tarvitset CMake 3.24:n tai uudemman, C++20-kääntäjän, zlibin ja OpenGL:n. Ensimmäinen konfigurointi lataa [GLFW](https://www.glfw.org/) 3.4:n ja [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8:n, joten tämä vaihe vaatii verkkoyhteyden.

macOS rakentaa `build/GalaxyEditor.app`:n. Avaa se Finderista tai suorita `open build/GalaxyEditor.app`:

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

Rakentaaksesi Windows `.exe`:n Mac- tai Linux-koneella, asenna MinGW-w64 (`brew install mingw-w64` tai `apt install g++-mingw-w64-x86-64-posix`), sitten:

```bash
cmake --preset windows
cmake --build --preset windows
```

Tämä kirjoittaa `build-windows/GalaxyEditor.exe`:n. Se on yksi tiedosto, jossa on sisäänrakennettu C++-ajoympäristö, ja se tarvitsee vain DLL-tiedostot, jotka toimitetaan Windowsin mukana. Ensimmäinen konfigurointi lataa myös zlibin. Aseta omat laattavalikoima-arkistosi `.exe`:n viereen.

Itse Windowsissa valitse generaattori, jonka kääntäjäsi ymmärtää:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlibin on oltava näkyvissä CMakelle. Debianissa ja Ubuntussa tämä on `zlib1g-dev`-paketti. Fedorassa se on `zlib-devel`. Linuxin avaus- ja tallennusikkunat käyttävät Zenityä.

Muototestit rakennetaan editorin kanssa:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Laattavalikoimat

Galaxy Editor lataa peliarkistot Total Annihilation -asennuskansiostasi. Ensimmäisellä käynnistyskerralla se kysyy kyseistä kansiota ennen aloitusnäyttöä. Valitse Peruuta käynnistääksesi ilman sitä. Editori toimii edelleen, mutta Tilesets- ja Features-välilehdet pysyvät tyhjinä, eikä se kysy uudelleen seuraavalla käynnistyskerralla.

Vaihda kansiota milloin tahansa Asetuksissa, Total Annihilation -kohdassa. Valitse Kansio lataa arkistot uudelleen heti, ja Tyhjennä unohtaa kansion, joten seuraava käynnistys kysyy sitä uudelleen, kuten ensimmäisellä käynnistyskerralla. Kansio tallennetaan käyttäjäkohtaiseen `settings.json`-tiedostoon.

Galaxy Editor lukee `.hpi`-, `.ccx`-, `.gp3`- ja `.gpf`-tiedostot suoraan kyseisestä kansiosta ja sitten kansiosta, joka sisältää `GalaxyEditor`-suoritettavan tiedoston. macOS:ssä tämä on kansio, joka sisältää `GalaxyEditor.app`:n, joten Galaxy Editor -niminen kansio, jossa sovellus ja omat `.hpi`-tiedostosi ovat vierekkäin, toimii kuten Photoshop on aseteltu. Aseta omat laattavalikoima-arkistosi suoritettavan tiedoston (tai `.app`:n) viereen. Jos siellä oleva tiedosto on samanniminen kuin Total Annihilation -kansiossa oleva tiedosto, kuten `worlds.hpi` tai `totala1.hpi`, se ohitetaan, joten mikään ei lataudu kahdesti. Maailma-arkisto, kuten `worlds.hpi`, täyttää Tilesets-välilehden. `totala1.hpi`, `rev31.gp3` tai `ccdata.ccx` toimittavat ominaisuusluettelon ja ominaisuuskuvat.

Nämä arkistot ovat pelidataa. Ne eivät ole osa tätä repositoriota.

Avaa kartta lukee `.ufo`-, `.hpi`- ja `.tnt`-tiedostot. Tallenna kirjoittaa `.ufo`-tiedoston.

## Suosikit

Suosikit-välilehti, Tilesets- ja Features-välilehden välissä, sisältää itse tallentamasi laatat. Valitse laatat -tilassa valitse joitakin laattoja ja napsauta hiiren oikealla painikkeella. Valikko tarjoaa Leikkaa, Kopioi, Kopioi korkeudet, Muokkaa korkeutta, Sekoita ja Lisää suosikkeihin. Tilesets-välilehdessä kuvan osoittaminen näyttää tähden sen kulmassa, joka lisää kyseisen laatan. Suosikit-välilehdessä samassa kulmassa on roskakori-painike, joka poistaa laatan vahvistuksen jälkeen.

Lisää suosikkeihin kysyy kolmea asiaa: suosikkiarkisto, kategoria ja alakategoria. Kirjoita uudet nimet tai valitse olemassa olevat. Nimet toimivat samalla tavalla kuin pelin omissa laattavalikoimissa: `Acid_New_1.hpi`:ssä kategoria on Acid_New_1 ja Rivers ja Sm Lakes ovat sen alakategorioita. Luo suosikkiarkisto kysyy nimeä, kuten FirstOne, ja kirjoittaa `GE-Favorites-FirstOne.hpi`:n `GalaxyEditor`-suoritettavan tiedoston (macOS:ssä `.app`) viereen.

Vain `GE-Favorites-<name>.hpi` -nimiset arkistot näkyvät Suosikit-välilehdellä, ja ne pidetään pois Tilesets-välilehdeltä. Sisällä laatta on `sections/<category>/<subcategory>/<tile>.sct`, joten arkisto on tavallinen laattavalikoima, jonka voit jakaa kopioimalla tiedoston jonkun toisen kansioon. Tallennettu laatta on täysi suorakulmio. Kun valinnassa on aukkoja, valittujen laattojen väliset laatat tallennetaan myös, koska osiotiedostossa ei ole tilaa rei'ille. Laatta voi olla enintään 512 laattaa kummallakin sivulla.

## HPI Editor

Työkalurivin oikeassa päässä oleva HPI Editor -painike (Tiedosto-valikko, HPI Editor) avaa tiedostoselain arkistoille: `.hpi`, `.ufo`, `.ccx`, `.gp3` ja `.gpf`. Avaa yksi Avaa-painikkeella tai aloita tyhjä arkisto painikkeella Uusi. Kun avoinna oleva kartta on itsessään arkisto, tyhjä ikkuna tarjoaa sen yhdellä napsautuksella.

Ikkuna noudattaa Finder-ikkunan asettelua. Kansiopuu kulkee vasemmalla, sisältö täyttää keskiosan luettelona tai kuvakkeina (kaksi painiketta oikeassa yläkulmassa), ja polkupalkki ja kohdelaskuri kulkevat alareunassa. Napsauta puun kansiota tai polkupalkin segmenttiä mennäksesi sinne, kaksoisnapsauta kansiota avataksesi sen ja käytä Takaisin ja Eteenpäin palataksesi askeleesi. Valitse napsauttamalla, Command-napsautus tai Ctrl-napsautus lisää yhden, Shift-napsautus alueelle tai Ctrl+A. Hakukenttä löytää tiedostot ja kansiot nimen perusteella katselemasi kansion alta, ja tuloksen kaksoisnapsautus näyttää, missä se sijaitsee.

- **Vie** kopioi valitut tiedostot ja kansiot valitsemaasi kansioon. Kansio pitää nimensä ja kaiken sen sisällä. Jos mitään ei ole valittu, Vie ottaa katselemasi kansion, ja arkiston yläosassa se luo kansion, joka on nimetty arkiston mukaan, jotta sen sisältö ei hajoa. Jos tiedosto on jo olemassa, sinulta kysytään ennen sen korvaamista. Nimet, jotka voisivat ulottua valitsemasi kansion ulkopuolelle, ohitetaan, ja merkit, joita tietokone ei salli tiedostonimissä, korvataan alaviivalla.
- **Tuo** lisää tiedostoja tai kokonaisia kansioita kaikkine sisältöineen katselemasi kansioon. Voit myös pudottaa tiedostoja ja kansioita työpöydältä ikkunaan. Tuonti kysyy ennen samannimisen tiedoston korvaamista. Arkiston tiedostonimet täsmäävät kirjainkoosta riippumatta, kuten pelissä.
- **Siirrä** vetämällä kohteita kansiosta toiseen: luettelo- tai kuvakenäkymän kansioon, puun kansioon tai polkupalkin segmenttiin. Kansion vetäminen itseensä hylätään, ja kohteessa jo oleva kansio yhdistetään siihen.
- **Nimeä uudelleen** F2:lla tai hiiren oikealla painikkeella, **Uusi kansio** työkalurivin painikkeella tai Ctrl+Shift+N:llä ja **Poista** Delete-näppäimellä. Jokainen muutos voidaan kumota ja tehdä uudelleen.

Mikään ei koske levyllä olevaa arkistoa ennen kuin painat Tallenna. Tallenna ja Tallenna nimellä kirjoittavat uuden arkiston ja korvaavat vanhan tiedoston yhdessä vaiheessa, joten epäonnistuminen jättää alkuperäisen ennalleen. Arkisto ei voi tallentaa tyhjää kansiota tai olla täysin ilman tiedostoja, joten tyhjät kansiot eivät säily tallennuksen jälkeen, eikä editori tallenna arkistoa, jossa ei ole mitään. Ikkunan sulkeminen, toisen arkiston avaaminen tai lopettaminen kysyy tallentamattomista muutoksista ensin.

## Pikanäppäimet

macOS:ssä Command korvaa Ctrl:n.

| Toiminto | Pikanäppäin |
| --- | --- |
| Uusi kartta | Ctrl+N |
| Avaa kartta | Ctrl+O |
| Tallenna | Ctrl+S |
| Tallenna nimellä | Ctrl+Shift+S |
| Lopeta | Ctrl+Q |
| Kumoa | Ctrl+Z |
| Tee uudelleen | Ctrl+Shift+Z |
| Leikkaa | Ctrl+X |
| Kopioi maasto + korkeuskartta | Ctrl+C |
| Kopioi korkeuskartta | Ctrl+Shift+C |
| Liitä | Ctrl+V |
| Editorin asetukset | Ctrl+, |
| 3D-näkymä päälle tai pois | Ctrl+3 |

HPI Editor -ikkunassa nämä toimivat, kun sillä on näppäimistö karttojen pikanäppäinten sijaan: Ctrl+O avaa arkiston, Ctrl+N aloittaa uuden, Ctrl+S ja Ctrl+Shift+S tallentavat, Ctrl+Z ja Ctrl+Shift+Z kumoavat ja tekevät uudelleen, Ctrl+A valitsee kaiken, Ctrl+I tuo, Ctrl+Shift+N luo kansion, Ctrl+[ ja Ctrl+] menevät taaksepäin ja eteenpäin, Ctrl+Up menee sulkevaan kansioon, Ctrl+Down tai Return avaa kansion, F2 nimeää uudelleen, Delete tai Ctrl+Backspace poistaa valinnan, ja pelkkä Backspace menee yhden kansion ylös.

Nuolinäppäimet liu'uttavat leimaa tai liitosta kursorin alla olevan laatan päältä. Esc peruuttaa liitoksen, poistuu Poista-tilasta tai tyhjentää valinnan.

3D-näkymässä vedä kiertääksesi, oikealla vedä tai Shift-vedä panoroidaksesi ja vieritä zoomataksesi. W A S D (tai nuolinäppäimet) liikkuvat maassa, Shift liikkuu nopeammin, Q ja E kääntyvät, ja Home aloittaa alusta.

Editorin asetukset muistetaan seuraavan kerran, kun avaat editorin. Esikatselukoko, korkeuskartan välistys, sivupalkin tekstin koko ja automaattitallennus ovat kaikki siellä. Automaattitallennus kirjoittaa varmuuskopion kartan viereen ajastimella ja jättää Tallenna-tiedoston rauhaan.

## Tekijänoikeus

Tekijänoikeus © 2026 Black Cat Software  
Kehittäjä: Matt Andrzejczuk
