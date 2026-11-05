<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Języki:** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · **Polski** · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

Edytor map dla gry [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation), dla macOS, Windows i Linux.

Maluj kafelki, umieszczaj drzewa i metal, ustawiaj punkty startowe graczy i kształtuj wysokość terenu. Mapy otwierają się i zapisują jako pliki `.ufo`, które gra może wczytać.

## Pobierz

| Platforma | Pobierz |
| --- | --- |
| macOS | [GalaxyEditor-1.1.2-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-macos.dmg) |
| Windows | [GalaxyEditor-1.1.2-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-windows-x64.zip) |
| Linux (archiwum) | [GalaxyEditor-1.1.2-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz) |

W macOS otwórz obraz dysku i przeciągnij folder Galaxy Editor do folderu Aplikacje. W Windows rozpakuj archiwum i uruchom `GalaxyEditor.exe`. W Linux możesz użyć pakietu `.deb` lub rozpakować archiwum `.tar.gz`. Każde pobranie zawiera `GE-Favorites-RandomMapGeneratorTiles.hpi` obok aplikacji. Pędzel kafelków odczytuje ten zestaw kafelków, więc trzymaj plik obok `GalaxyEditor.app`, `GalaxyEditor.exe` lub `GalaxyEditor` na Linuksie. `GreenWorld.hpi` jest dołączony w ten sam sposób. Przy pierwszym uruchomieniu Galaxy Editor najpierw pyta o język, a potem o to, gdzie zainstalowano Total Annihilation. Każde wydanie jest wymienione na [stronie wydań](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Instalacja Linux

**Opcja 1: Instalacja przez pakiet .deb (Debian/Ubuntu/Mint)**

The `.deb` is not on the v1.1.2 release, so there is no download URL for it yet. The package file name is `galaxy-editor_<version>_amd64.deb`. For version 1.1.2 that name is `galaxy-editor_1.1.2_amd64.deb`. When a later release includes the file, install the downloaded package with:

```bash
sudo apt install ./galaxy-editor_1.1.2_amd64.deb
```

Until then, use the `.tar.gz` below. That archive is what works today.

**Opcja 2: Użyj przenośnego archiwum**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.2-linux-x64.tar.gz
cd GalaxyEditor-1.1.2-linux-x64
./GalaxyEditor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Co możesz robić

**Kafelki i elementy.** Przeszukuj zestawy kafelków dołączone do gry, kliknij sekcję i ostempluj ją na mapie. Drzewa, skały, złoża metalu i otwory używają swojej grafiki z gry. Pinezki startowe oznaczają, gdzie każdy gracz zaczyna.

**Teren.** Włącz mapę wysokości, a następnie podnieś, obniż lub ustaw wysokość zaznaczenia. Skopiuj teren i wysokość razem lub skopiuj samą wysokość i zostaw kafelki na miejscu. Blend Textures rozmywa obrazy tam, gdzie zaznaczenie spotyka się z otaczającymi kafelkami.

**Sama mapa.** Ustawienia mapy zawierają wartości, które gra odczytuje z mapy: wiatr, pływy, siła słoneczna, grawitacja, poziom morza, metal, profil AI i specjalne zasady. Generuj mapę buduje nowy krajobraz z zestawu kafelków. Minimapa znajduje się w rogu widoku, a cofnij obejmuje edycje kafelków, elementów i wysokości.

**Widok 3D.** Przycisk Widok 3D na pasku narzędzi (menu Widok, Ctrl+3) pokazuje mapę jako teren z mapą wysokości podnoszącą grunt. Półprzezroczysta płaszczyzna oznacza poziom morza z ustawień mapy, a opcje edytora ustawiają jej kolor i przezroczystość. Widok 3D służy do rozglądania się, więc edytuj w płaskim widoku i przełączaj się tam i z powrotem według potrzeb.

## Kompilacja

Potrzebujesz CMake 3.24 lub nowszego, kompilatora C++20, zlib i OpenGL. Pierwsza konfiguracja pobiera [GLFW](https://www.glfw.org/) 3.4 i [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8, więc ten krok wymaga połączenia sieciowego.

macOS buduje `build/GalaxyEditor.app`. Otwórz go z Findera lub uruchom `open build/GalaxyEditor.app`:

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

Aby zbudować Windows `.exe` na komputerze Mac lub Linux, zainstaluj MinGW-w64 (`brew install mingw-w64` lub `apt install g++-mingw-w64-x86-64-posix`), następnie:

```bash
cmake --preset windows
cmake --build --preset windows
```

To zapisuje `build-windows/GalaxyEditor.exe`. Jest to jeden plik z wbudowanym środowiskiem uruchomieniowym C++ i potrzebuje tylko bibliotek DLL dostarczanych z Windows. Pierwsza konfiguracja pobiera również zlib. Umieść własne archiwa zestawów kafelków obok `.exe`.

W samym Windows wybierz generator, który rozumie twój kompilator:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib musi być widoczny dla CMake. W Debianie i Ubuntu to pakiet `zlib1g-dev`. W Fedorze to `zlib-devel`. Okna dialogowe otwierania i zapisywania w Linuksie używają Zenity.

Testy formatu kompilują się z edytorem:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Zestawy kafelków

Galaxy Editor ładuje archiwa gry z folderu instalacyjnego Total Annihilation. Przy zupełnie pierwszym uruchomieniu, przed ekranem powitalnym, pyta o język, a potem o ten folder. Wybierz Anuluj, aby uruchomić bez niego. Edytor nadal działa, ale zakładki Zestawy kafelków i Elementy pozostają puste i nie pyta ponownie przy następnym uruchomieniu.

Zmień folder w dowolnym momencie w Opcjach, w Total Annihilation. Wybierz Folder natychmiast przeładowuje archiwa, a Wyczyść zapomina folder, więc następne uruchomienie pyta o niego ponownie, jak przy pierwszym uruchomieniu. Folder i wybrany język są zapisywane w osobnym dla użytkownika `settings.json`. Język zmienia się później w Opcjach, w sekcji Język, a okno przełącza się od razu.

Galaxy Editor odczytuje pliki `.hpi`, `.ccx`, `.gp3` i `.gpf` bezpośrednio wewnątrz tego folderu, a następnie w folderze zawierającym plik wykonywalny `GalaxyEditor`. W macOS jest to folder zawierający `GalaxyEditor.app`, więc folder o nazwie Galaxy Editor z aplikacją i własnymi plikami `.hpi` obok siebie działa tak, jak jest rozplanowany Photoshop. Umieść własne archiwa zestawów kafelków obok pliku wykonywalnego (lub `.app`). Jeśli plik tam ma taką samą nazwę jak plik w folderze Total Annihilation, na przykład `worlds.hpi` lub `totala1.hpi`, jest pomijany, więc nic nie ładuje się dwukrotnie. Archiwum świata, takie jak `worlds.hpi`, wypełnia zakładkę Zestawy kafelków. `totala1.hpi`, `rev31.gp3` lub `ccdata.ccx` dostarczają listę elementów i obrazy elementów.

Te archiwa to dane gry. Nie są częścią tego repozytorium.

Otwórz mapę odczytuje `.ufo`, `.hpi` i `.tnt`. Zapisz zapisuje `.ufo`.

## Ulubione

Zakładka Ulubione, między Zestawami kafelków a Elementami, przechowuje kafelki, które sam zapisujesz. W trybie Wybierz kafelki zaznacz niektóre kafelki i kliknij prawym przyciskiem myszy. Menu oferuje Wytnij, Kopiuj, Kopiuj wysokości, Modyfikuj wysokość, Mieszaj i Dodaj do ulubionych. W zakładce Zestawy kafelków wskazanie na obraz pokazuje przycisk gwiazdki w jego rogu, który dodaje ten kafelek. W zakładce Ulubione ten sam róg zawiera przycisk kosza, który usuwa kafelek po potwierdzeniu.

Dodaj do ulubionych pyta o trzy rzeczy: archiwum ulubionych, kategorię i podkategorię. Wpisz nowe nazwy lub wybierz istniejące. Nazwy działają tak, jak w zestawach kafelków samej gry: w `Acid_New_1.hpi` kategoria to Acid_New_1, a Rivers i Sm Lakes to jej podkategorie. Utwórz archiwum ulubionych pyta o nazwę, taką jak FirstOne, i zapisuje `GE-Favorites-FirstOne.hpi` obok pliku wykonywalnego `GalaxyEditor` (obok `.app` w macOS).

Tylko archiwa o nazwie `GE-Favorites-<nazwa>.hpi` pojawiają się w zakładce Ulubione i są trzymane z dala od zakładki Zestawy kafelków. Wewnątrz kafelek to `sections/<kategoria>/<podkategoria>/<kafelek>.sct`, więc archiwum to zwykły zestaw kafelków, którym możesz się podzielić, kopiując plik do folderu kogoś innego. Zapisany kafelek to pełny prostokąt. Gdy zaznaczenie ma luki, kafelki między zaznaczonymi są również zapisywane, ponieważ plik sekcji nie ma miejsca na dziury. Kafelek może mieć maksymalnie 512 kafelków na każdym boku.

## Edytor HPI

Przycisk Edytor HPI na prawym końcu paska narzędzi (menu Plik, Edytor HPI) otwiera przeglądarkę plików dla archiwów: `.hpi`, `.ufo`, `.ccx`, `.gp3` i `.gpf`. Otwórz jedno przyciskiem Otwórz lub rozpocznij puste archiwum przyciskiem Nowy. Gdy mapa, którą masz otwartą, jest sama w sobie archiwum, puste okno oferuje je jednym kliknięciem.

Okno ma układ okna Findera. Drzewo folderów biegnie w dół po lewej, zawartość wypełnia środek jako lista lub ikony (dwa przyciski w prawym górnym rogu), a pasek ścieżki i licznik elementów biegną wzdłuż dołu. Kliknij folder w drzewie lub segment paska ścieżki, aby tam przejść, kliknij dwukrotnie folder, aby go otworzyć, i użyj Wstecz i Do przodu, aby cofnąć swoje kroki. Zaznacz kliknięciem, Command-kliknięcie lub Ctrl-kliknięcie, aby dodać jeden, Shift-kliknięcie dla zakresu lub Ctrl+A. Pole wyszukiwania znajduje pliki i foldery według nazwy poniżej folderu, który przeglądasz, a dwukrotne kliknięcie wyniku pokazuje, gdzie się znajduje.

- **Eksportuj** kopiuje wybrane pliki i foldery do wybranego przez ciebie folderu. Folder zachowuje swoją nazwę i wszystko w nim. Gdy nic nie jest zaznaczone, Eksportuj pobiera folder, który przeglądasz, i na górze archiwum tworzy folder nazwany według archiwum, aby jego zawartość nie była rozproszona. Jeśli plik już tam jest, zostaniesz zapytany przed jego zastąpieniem. Nazwy, które mogłyby sięgać poza wybrany folder, są pomijane, a znaki, których komputer nie zezwala w nazwach plików, są zastępowane podkreśleniem.
- **Importuj** dodaje pliki lub całe foldery ze wszystkim w nich do folderu, który przeglądasz. Możesz również upuszczać pliki i foldery z pulpitu do okna. Import pyta przed zastąpieniem pliku o tej samej nazwie. Nazwy plików w archiwum są dopasowywane bez względu na wielkość liter, tak jak w grze.
- **Przenieś** przeciągając elementy z jednego folderu do drugiego: na folder na liście lub w widoku ikon, folder w drzewie lub segment paska ścieżki. Przeciąganie folderu do samego siebie jest odrzucane, a folder, który już istnieje w miejscu docelowym, jest scalany.
- **Zmień nazwę** za pomocą F2 lub menu kontekstowego, **Nowy folder** za pomocą przycisku paska narzędzi lub Ctrl+Shift+N, a **Usuń** za pomocą klawisza Delete. Każdą zmianę można cofnąć i powtórzyć.

Nic nie dotyka archiwum na dysku, dopóki nie naciśniesz Zapisz. Zapisz i Zapisz jako zapisują nowe archiwum i zastępują stary plik w jednym kroku, więc niepowodzenie pozostawia oryginał w spokoju. Archiwum nie może przechowywać pustego folderu ani nie mieć w ogóle plików, więc puste foldery nie przetrwają zapisu, a edytor nie zapisze archiwum bez niczego w nim. Zamykanie okna, otwieranie innego archiwum lub zamykanie pyta najpierw o niezapisane zmiany.

## Skróty

W macOS Command zastępuje Ctrl.

| Akcja | Skrót |
| --- | --- |
| Nowa mapa | Ctrl+N |
| Otwórz mapę | Ctrl+O |
| Zapisz | Ctrl+S |
| Zapisz jako | Ctrl+Shift+S |
| Zakończ | Ctrl+Q |
| Cofnij | Ctrl+Z |
| Ponów | Ctrl+Shift+Z |
| Wytnij | Ctrl+X |
| Kopiuj teren + mapę wysokości | Ctrl+C |
| Kopiuj mapę wysokości | Ctrl+Shift+C |
| Wklej | Ctrl+V |
| Opcje edytora | Ctrl+, |
| Widok 3D włączony lub wyłączony | Ctrl+3 |

W oknie Edytor HPI działają one, gdy ma klawiaturę zamiast skrótów mapy: Ctrl+O otwiera archiwum, Ctrl+N rozpoczyna nowe, Ctrl+S i Ctrl+Shift+S zapisują, Ctrl+Z i Ctrl+Shift+Z cofają i ponawiają, Ctrl+A zaznacza wszystko, Ctrl+I importuje, Ctrl+Shift+N tworzy folder, Ctrl+[ i Ctrl+] cofają i przechodzą do przodu, Ctrl+Up przechodzi do folderu otaczającego, Ctrl+Down lub Return otwiera folder, F2 zmienia nazwę, Delete lub Ctrl+Backspace usuwa zaznaczenie, a sam Backspace przechodzi w górę o folder.

Klawisze strzałek przesuwają stempel lub wklejenie z kafelka pod kursorem. Esc anuluje wklejenie, opuszcza tryb usuwania lub czyści zaznaczenie.

W widoku 3D przeciągnij, aby orbitować, przeciągnij prawym lub Shift-przeciągnij, aby panoramować, i przewiń, aby powiększyć. W A S D (lub klawisze strzałek) poruszają się po ziemi, Shift porusza się szybciej, Q i E obracają, a Home zaczyna od nowa.

Opcje edytora są zapamiętywane przy następnym otwarciu edytora. Rozmiar podglądu, odstępy mapy wysokości, rozmiar tekstu paska bocznego i autozapis wszystkie tam żyją. Autozapis zapisuje kopię zapasową obok mapy na timerze i pozostawia plik z Zapisz w spokoju.

## Prawa autorskie

Prawa autorskie © 2026 Black Cat Software  
Opracował: Matt Andrzejczuk
