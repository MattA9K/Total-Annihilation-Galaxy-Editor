<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Jezici:** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · **Hrvatski** · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

Uređivač karti za [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation), za macOS, Windows i Linux.

Bojajte pločice, postavljajte stabla i metal, postavite početne pozicije igrača i oblikujte visinu terena. Karte se otvaraju i spremaju kao `.ufo` datoteke koje igra može učitati.

## Preuzimanje

| Platforma | Preuzimanje |
| --- | --- |
| macOS | [GalaxyEditor-1.1.7-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/GalaxyEditor-1.1.7-macos.dmg) |
| Windows | [GalaxyEditor-1.1.7-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/GalaxyEditor-1.1.7-windows-x64.zip) |
| Linux (arhiva) | [GalaxyEditor-1.1.7-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/GalaxyEditor-1.1.7-linux-x64.tar.gz) |
| Debian / Ubuntu | [galaxy-editor_1.1.7_amd64.deb](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/galaxy-editor_1.1.7_amd64.deb) |

Na macOS-u otvorite sliku diska i povucite mapu Galaxy Editor u mapu Aplikacije. Na Windowsima raspakirajte arhivu i pokrenite `GalaxyEditor.exe`. Na Linuxu možete koristiti `.deb` paket ili raspakirajte `.tar.gz` arhivu. Svako preuzimanje uključuje `GE-Favorites-RandomMapGeneratorTiles.hpi` pored aplikacije. Kist za pločice čita taj skup pločica, stoga držite datoteku pored `GalaxyEditor.app`, `GalaxyEditor.exe` ili Linux `galaxy-editor`. `GreenWorld.hpi` je uključen na isti način. Prvi put kad se pokrene, Galaxy Editor prvo pita jezik, a zatim gdje je instaliran Total Annihilation. Svako izdanje je navedeno na [stranici izdanja](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Linux instalacija

**Opcija 1: Instalacija putem .deb paketa (Debian/Ubuntu/Mint)**

Download `galaxy-editor_1.1.7_amd64.deb` and install it. `apt` installs the command `galaxy-editor` and pulls in zlib, OpenGL, X11, and Zenity. Launch it from the application menu or by running `galaxy-editor`. The `.deb` does not include `GE-Favorites-RandomMapGeneratorTiles.hpi` or `GreenWorld.hpi`. Those files are in the portable archive, beside `galaxy-editor`.

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/galaxy-editor_1.1.7_amd64.deb
sudo apt install ./galaxy-editor_1.1.7_amd64.deb
```

**Opcija 2: Korištenje prenosive arhive**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/GalaxyEditor-1.1.7-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.7-linux-x64.tar.gz
cd GalaxyEditor-1.1.7-linux-x64
./galaxy-editor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Što možete raditi

**Pločice i značajke.** Pretražite skupove pločica koji dolaze s igrom, kliknite na odjeljak i otisните ga na kartu. Stabla, stijene, metalne mrlje i otvori koriste svoju grafiku iz igre. Početne igle označavaju gdje svaki igrač počinje.

**Teren.** Uključite kartu visina, zatim podignite, spustite ili postavite visinu odabira. Kopirajte teren i visinu zajedno ili kopirajte samo visinu i ostavite pločice na mjestu. Blend Textures blijedi slike tamo gdje odabir susreće pločice oko njega.

**Sama karta.** Postavke karte sadrže vrijednosti koje igra čita s karte: vjetar, plima i oseka, snaga sunca, gravitacija, razina mora, metal, AI profil i posebna pravila. Generiraj kartu gradi novi krajolik iz skupa pločica. Mala karta sjedi preko kuta prikaza, a poništi pokriva uređivanje pločica, značajki i visina.

**3D prikaz.** Gumb 3D prikaz na alatnoj traci (izbornik Prikaz, Ctrl+3) prikazuje kartu kao teren, s kartom visina koja podiže tlo. Prozirna ravnina označava razinu mora iz Postavki karte, a Opcije uređivača postavljaju njezinu boju i neprozirnost. 3D prikaz je za razgledavanje, pa uređujte u ravnom prikazu i prebacujte se naprijed-natrag kako želite.

## Izgradnja

Potreban vam je CMake 3.24 ili noviji, C++20 kompajler, zlib i OpenGL. Prva konfiguracija preuzima [GLFW](https://www.glfw.org/) 3.4 i [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8, pa taj korak zahtijeva mrežnu vezu.

macOS gradi `build/GalaxyEditor.app`. Otvorite ga iz Findera ili pokrenite `open build/GalaxyEditor.app`:

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

Za izgradnju Windows `.exe` na Mac ili Linux stroju, instalirajte MinGW-w64 (`brew install mingw-w64`, ili `apt install g++-mingw-w64-x86-64-posix`), zatim:

```bash
cmake --preset windows
cmake --build --preset windows
```

Ovo piše `build-windows/GalaxyEditor.exe`. To je jedna datoteka s ugrađenim C++ runtime-om i treba joj samo DLL-ovi koji dolaze s Windowsima. Prva konfiguracija također preuzima zlib. Stavite vlastite arhive skupova pločica pored `.exe`.

Na samom Windowsima odaberite generator koji vaš kompajler razumije:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib mora biti vidljiv CMake-u. Na Debianu i Ubuntuu to je `zlib1g-dev` paket. Na Fedori to je `zlib-devel`. Linux dijalozi za otvaranje i spremanje koriste Zenity.

Testovi formata grade se s uređivačem:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Skupovi pločica

Galaxy Editor učitava arhive igre iz vaše mape instalacije Total Annihilation. Pri sasvim prvom pokretanju, prije ekrana za pokretanje, pita jezik, a zatim tu mapu. Odaberite Otkaži za pokretanje bez nje. Uređivač i dalje radi, ali kartice Skupovi pločica i Značajke ostaju prazne i ne pita ponovno pri sljedećem pokretanju.

Promijenite mapu bilo kada u Opcijama, pod Total Annihilation. Odaberi mapu ponovno učitava arhive odmah, a Očisti zaboravlja mapu, pa sljedeće pokretanje ponovno pita za nju, kao pri prvom pokretanju. Mapa i odabrani jezik spremaju se u korisnički `settings.json`. Jezik se kasnije mijenja u Opcijama, pod Jezik, i prozor se prebacuje odmah.

Galaxy Editor čita `.hpi`, `.ccx`, `.gp3` i `.gpf` datoteke izravno unutar te mape, a zatim u mapi koja sadrži izvršnu datoteku `GalaxyEditor`. Na macOS-u to je mapa koja sadrži `GalaxyEditor.app`, pa mapa pod nazivom Galaxy Editor s aplikacijom i vašim vlastitim `.hpi` datotekama jedna uz drugu radi, na način na koji je Photoshop postavljen. Stavite vlastite arhive skupova pločica pored izvršne datoteke (ili `.app`). Ako datoteka tamo ima isto ime kao ona u mapi Total Annihilation, poput `worlds.hpi` ili `totala1.hpi`, preskače se, pa se ništa ne učitava dvaput. Arhiva svijeta poput `worlds.hpi` ispunjava karticu Skupovi pločica. `totala1.hpi`, `rev31.gp3` ili `ccdata.ccx` pružaju popis značajki i slike značajki.

Ove arhive su podaci igre. Nisu dio ovog repozitorija.

Otvori kartu čita `.ufo`, `.hpi` i `.tnt`. Spremi piše `.ufo`.

## Favoriti

Kartica Favoriti, između Skupova pločica i Značajki, sadrži pločice koje sami spremite. U načinu rada Odaberi pločice odaberite neke pločice i kliknite desnom tipkom miša. Izbornik nudi Izreži, Kopiraj, Kopiraj visine, Izmijeni visinu, Stopi i Dodaj u favorite. U kartici Skupovi pločica pokazivanje na sliku prikazuje gumb zvijezde u njezinom kutu koji dodaje tu pločicu. U kartici Favoriti isti kut sadrži gumb smeća koji briše pločicu nakon što potvrdite.

Dodaj u favorite pita za tri stvari: arhivu favorita, kategoriju i potkategoriju. Upišite nova imena ili odaberite postojeća. Imena rade na način na koji rade u vlastitim skupovima pločica igre: u `Acid_New_1.hpi` kategorija je Acid_New_1, a Rivers i Sm Lakes su njezine potkategorije. Stvori arhivu favorita pita za ime poput FirstOne i piše `GE-Favorites-FirstOne.hpi` pored izvršne datoteke `GalaxyEditor` (pored `.app` na macOS-u).

Samo arhive pod nazivom `GE-Favorites-<ime>.hpi` pojavljuju se na kartici Favoriti i drže se dalje od kartice Skupovi pločica. Unutra pločica je `sections/<kategorija>/<potkategorija>/<pločica>.sct`, pa je arhiva obični skup pločica koji možete podijeliti kopiranjem datoteke u nečiju drugu mapu. Spremljena pločica je potpuni pravokutnik. Kada odabir ima praznine, pločice između odabranih također se spremaju, jer datoteka odjeljka nema mjesta za rupe. Pločica može biti najviše 512 pločica sa svake strane.

## HPI Editor

Gumb HPI Editor na desnom kraju alatne trake (izbornik Datoteka, HPI Editor) otvara preglednik datoteka za arhive: `.hpi`, `.ufo`, `.ccx`, `.gp3` i `.gpf`. Otvorite jednu s gumbom Otvori ili započnite praznu arhivu s Novo. Kada je karta koju imate otvorenu sama arhiva, prazan prozor nudi je jednim klikom.

Prozor slijedi raspored Finder prozora. Stablo mapa teče niz lijevo, sadržaj ispunjava sredinu kao popis ili kao ikone (dva gumba u gornjem desnom kutu), a traka puta i broj stavki teku duž dna. Kliknite na mapu u stablu ili segment trake puta da tamo odete, dvaput kliknite na mapu da je otvorite i upotrijebite Natrag i Naprijed da pratite svoje korake. Odaberite klikom, Command-klik ili Ctrl-klik za dodavanje jednog, Shift-klik za raspon ili Ctrl+A. Okvir za pretraživanje pronalazi datoteke i mape po imenu ispod mape koju gledate, a dvostruki klik na rezultat prikazuje gdje se nalazi.

- **Izvoz** kopira odabrane datoteke i mape u mapu koju odaberete. Mapa zadržava svoje ime i sve u njoj. Bez odabranog ničega, Izvoz uzima mapu koju gledate i na vrhu arhive stvara mapu nazvanu prema arhivi tako da njezin sadržaj nije raspršen. Ako datoteka već postoji tamo, pitat će vas prije nego što se zamijeni. Imena koja bi mogla doseći izvan mape koju ste odabrali preskaču se, a znakovi koje računalo ne dopušta u imenima datoteka zamjenjuju se donjom crtom.
- **Uvoz** dodaje datoteke ili cijele mape sa svime u njima u mapu koju gledate. Također možete ispustiti datoteke i mape s radne površine na prozor. Uvoz pita prije zamjene datoteke istog imena. Imena datoteka u arhivi odgovaraju bez obzira na veličinu slova, kao što to čine u igri.
- **Premjesti** povlačenjem stavki iz jedne mape u drugu: na mapu na popisu ili prikazu ikona, mapu u stablu ili segment trake puta. Povlačenje mape u samu sebe se odbija, a mapa koja već postoji na odredištu spaja se u nju.
- **Preimenuj** s F2 ili desnim klikom na izbornik, **Nova mapa** s gumbom alatne trake ili Ctrl+Shift+N, i **Izbriši** s tipkom Delete. Svaka promjena može se poništiti i ponoviti.

Ništa ne dodiruje arhivu na disku dok ne pritisnete Spremi. Spremi i Spremi kao pišu novu arhivu i zamjenjuju staru datoteku u jednom koraku, pa neuspjeh ostavlja original na miru. Arhiva ne može pohraniti praznu mapu niti nemati uopće datoteke, pa prazne mape ne prežive spremanje i uređivač neće spremiti arhivu bez ičega u njoj. Zatvaranje prozora, otvaranje druge arhive ili izlazak prvo pita o nespremljenim promjenama.

## Prečaci

Na macOS-u Command zauzima mjesto Ctrl.

| Radnja | Prečac |
| --- | --- |
| Nova karta | Ctrl+N |
| Otvori kartu | Ctrl+O |
| Spremi | Ctrl+S |
| Spremi kao | Ctrl+Shift+S |
| Izađi | Ctrl+Q |
| Poništi | Ctrl+Z |
| Ponovi | Ctrl+Shift+Z |
| Izreži | Ctrl+X |
| Kopiraj teren + karta visina | Ctrl+C |
| Kopiraj kartu visina | Ctrl+Shift+C |
| Zalijepi | Ctrl+V |
| Opcije uređivača | Ctrl+, |
| 3D prikaz uključen ili isključen | Ctrl+3 |

U prozoru HPI Editor ovi rade kada ima tipkovnicu umjesto prečaca karte: Ctrl+O otvara arhivu, Ctrl+N započinje novu, Ctrl+S i Ctrl+Shift+S spremaju, Ctrl+Z i Ctrl+Shift+Z poništavaju i ponavljaju, Ctrl+A odabire sve, Ctrl+I uvozi, Ctrl+Shift+N stvara mapu, Ctrl+[ i Ctrl+] idu natrag i naprijed, Ctrl+Up ide u obuhvatnu mapu, Ctrl+Down ili Return otvara mapu, F2 preimenuje, Delete ili Ctrl+Backspace uklanja odabir, a samo Backspace ide gore za jednu mapu.

Tipke sa strelicama klize pečat ili zaljepljavanje s pločice ispod pokazivača. Esc otkazuje zaljepljavanje, napušta način rada Izbriši ili čisti odabir.

U 3D prikazu povucite za orbitiranje, desno povucite ili Shift-povucite za pomicanje i pomaknite za uvećavanje. W A S D (ili tipke sa strelicama) kreću se po tlu, Shift se kreće brže, Q i E skreću, a Home počinje iznova.

Opcije uređivača se pamte sljedeći put kada otvorite uređivač. Veličina pregleda, razmak karte visina, veličina teksta bočne trake i automatsko spremanje svi žive tamo. Automatsko spremanje piše sigurnosnu kopiju pored karte na mjeraču vremena i ostavlja datoteku iz Spremi na miru.

## Autorska prava

Autorska prava © 2026 Black Cat Software  
Razvio: Matt Andrzejczuk
