<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**언어:** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · [Français](README.fr.md) · **한국어** · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

macOS, Windows, Linux용 [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation) 맵 에디터입니다.

타일을 칠하고, 나무와 금속을 배치하고, 플레이어 시작 위치를 설정하고, 지형의 높이를 만드세요. 맵은 게임에서 로드할 수 있는 `.ufo` 파일로 열리고 저장됩니다.

## 다운로드

| 플랫폼 | 다운로드 |
| --- | --- |
| macOS | [GalaxyEditor-1.1.2-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-macos.dmg) |
| Windows | [GalaxyEditor-1.1.2-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-windows-x64.zip) |
| Linux (아카이브) | [GalaxyEditor-1.1.2-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz) |

macOS에서는 디스크 이미지를 열고 Galaxy Editor 폴더를 응용 프로그램 폴더로 드래그하세요. Windows에서는 아카이브의 압축을 풀고 `GalaxyEditor.exe`를 실행하세요. Linux에서는 `.deb` 패키지를 사용하거나 `.tar.gz` 아카이브의 압축을 풀 수 있습니다. 각 다운로드에는 앱 옆에 `GE-Favorites-RandomMapGeneratorTiles.hpi`가 포함되어 있습니다. 타일 브러시는 해당 타일셋을 읽으므로 `GalaxyEditor.app`, `GalaxyEditor.exe` 또는 Linux `GalaxyEditor` 옆에 파일을 보관하세요. `GreenWorld.hpi`도 같은 방식으로 포함되어 있습니다. 처음 시작할 때 Galaxy Editor는 Total Annihilation이 설치된 위치를 묻습니다. 모든 릴리스는 [릴리스 페이지](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases)에 나열되어 있습니다.

### Linux 설치

**옵션 1: .deb 패키지로 설치 (Debian/Ubuntu/Mint)**

The `.deb` is not on the v1.1.2 release, so there is no download URL for it yet. The package file name is `galaxy-editor_<version>_amd64.deb`. For version 1.1.2 that name is `galaxy-editor_1.1.2_amd64.deb`. When a later release includes the file, install the downloaded package with:

```bash
sudo apt install ./galaxy-editor_1.1.2_amd64.deb
```

Until then, use the `.tar.gz` below. That archive is what works today.

**옵션 2: 휴대용 아카이브 사용**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.2-linux-x64.tar.gz
cd GalaxyEditor-1.1.2-linux-x64
./GalaxyEditor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## 할 수 있는 일

**타일과 특징.** 게임과 함께 제공되는 타일셋을 검색하고, 섹션을 클릭하고, 맵에 스탬프를 찍으세요. 나무, 바위, 금속 조각 및 통풍구는 게임 내 그래픽을 사용합니다. 시작 핀은 각 플레이어가 시작하는 위치를 표시합니다.

**지형.** 높이 맵을 켜고 선택 영역의 높이를 올리거나 내리거나 설정하세요. 지형과 높이를 함께 복사하거나 높이만 복사하고 타일은 그대로 두세요. Blend Textures는 선택 영역이 주변 타일과 만나는 곳에서 이미지를 페이드합니다.

**맵 자체.** 맵 설정은 게임이 맵에서 읽는 값을 담고 있습니다: 바람, 조수, 태양 강도, 중력, 해수면, 금속, AI 프로필 및 특수 규칙. 맵 생성은 타일셋에서 새로운 풍경을 구축합니다. 미니맵은 뷰의 모서리에 있고, 실행 취소는 타일, 특징 및 높이 편집을 다룹니다.

**3D 뷰.** 툴바의 3D 뷰 버튼(뷰 메뉴, Ctrl+3)은 맵을 지형으로 표시하며, 높이 맵이 지면을 들어 올립니다. 반투명 평면은 맵 설정에서 해수면을 표시하고, 에디터 옵션은 색상과 불투명도를 설정합니다. 3D 뷰는 둘러보기용이므로 평면 뷰에서 편집하고 원하는 대로 전환하세요.

## 빌드

CMake 3.24 이상, C++20 컴파일러, zlib 및 OpenGL이 필요합니다. 첫 번째 구성은 [GLFW](https://www.glfw.org/) 3.4와 [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8을 다운로드하므로 이 단계에는 네트워크 연결이 필요합니다.

macOS는 `build/GalaxyEditor.app`을 빌드합니다. Finder에서 열거나 `open build/GalaxyEditor.app`을 실행하세요:

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

Mac 또는 Linux 컴퓨터에서 Windows `.exe`를 빌드하려면 MinGW-w64(`brew install mingw-w64` 또는 `apt install g++-mingw-w64-x86-64-posix`)를 설치한 다음:

```bash
cmake --preset windows
cmake --build --preset windows
```

이것은 `build-windows/GalaxyEditor.exe`를 작성합니다. C++ 런타임이 내장된 하나의 파일이며 Windows와 함께 제공되는 DLL만 필요합니다. 첫 번째 구성도 zlib를 다운로드합니다. `.exe` 옆에 자체 타일셋 아카이브를 배치하세요.

Windows 자체에서는 컴파일러가 이해하는 생성기를 선택하세요:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib는 CMake에 표시되어야 합니다. Debian과 Ubuntu에서는 `zlib1g-dev` 패키지입니다. Fedora에서는 `zlib-devel`입니다. Linux 열기 및 저장 대화 상자는 Zenity를 사용합니다.

형식 테스트는 에디터와 함께 빌드됩니다:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## 타일셋

Galaxy Editor는 Total Annihilation 설치 폴더에서 게임 아카이브를 로드합니다. 처음 시작할 때 시작 화면이 나타나기 전에 해당 폴더를 묻습니다. 취소를 선택하면 없이 시작합니다. 에디터는 여전히 실행되지만 타일셋과 특징 탭이 비어 있고 다음 실행 시 다시 묻지 않습니다.

옵션의 Total Annihilation에서 언제든지 폴더를 변경하세요. 폴더 선택은 즉시 아카이브를 다시 로드하고, 지우기는 폴더를 잊어버리므로 다음 실행 시 첫 실행처럼 다시 묻습니다. 폴더는 사용자별 `settings.json`에 저장됩니다.

Galaxy Editor는 해당 폴더 내의 `.hpi`, `.ccx`, `.gp3` 및 `.gpf` 파일을 직접 읽은 다음 `GalaxyEditor` 실행 파일이 포함된 폴더에서 읽습니다. macOS에서는 `GalaxyEditor.app`이 포함된 폴더이므로 Galaxy Editor라는 폴더에 앱과 자체 `.hpi` 파일이 나란히 있는 것이 Photoshop이 배치된 방식처럼 작동합니다. 실행 파일(또는 `.app`) 옆에 자체 타일셋 아카이브를 배치하세요. 거기에 있는 파일이 Total Annihilation 폴더에 있는 파일과 같은 이름(예: `worlds.hpi` 또는 `totala1.hpi`)을 가지면 건너뛰므로 아무것도 두 번 로드되지 않습니다. `worlds.hpi`와 같은 월드 아카이브는 타일셋 탭을 채웁니다. `totala1.hpi`, `rev31.gp3` 또는 `ccdata.ccx`는 특징 목록과 특징 이미지를 제공합니다.

이러한 아카이브는 게임 데이터입니다. 이 저장소의 일부가 아닙니다.

맵 열기는 `.ufo`, `.hpi` 및 `.tnt`를 읽습니다. 저장은 `.ufo`를 작성합니다.

## 즐겨찾기

타일셋과 특징 사이의 즐겨찾기 탭은 직접 저장한 타일을 담고 있습니다. 타일 선택 모드에서 일부 타일을 선택하고 마우스 오른쪽 버튼을 클릭하세요. 메뉴는 잘라내기, 복사, 높이 복사, 높이 수정, 블렌드 및 즐겨찾기에 추가를 제공합니다. 타일셋 탭에서 이미지를 가리키면 모서리에 해당 타일을 추가하는 별 버튼이 표시됩니다. 즐겨찾기 탭에서는 같은 모서리에 확인 후 타일을 삭제하는 휴지통 버튼이 있습니다.

즐겨찾기에 추가는 세 가지를 묻습니다: 즐겨찾기 아카이브, 카테고리 및 하위 카테고리. 새 이름을 입력하거나 기존 이름을 선택하세요. 이름은 게임 자체의 타일셋에서 작동하는 방식대로 작동합니다: `Acid_New_1.hpi`에서 카테고리는 Acid_New_1이고 Rivers와 Sm Lakes는 하위 카테고리입니다. 즐겨찾기 아카이브 생성은 FirstOne과 같은 이름을 묻고 `GalaxyEditor` 실행 파일(macOS의 `.app`) 옆에 `GE-Favorites-FirstOne.hpi`를 작성합니다.

`GE-Favorites-<이름>.hpi`라는 이름의 아카이브만 즐겨찾기 탭에 나타나며 타일셋 탭에서 제외됩니다. 내부에서 타일은 `sections/<카테고리>/<하위카테고리>/<타일>.sct`이므로 아카이브는 파일을 다른 사람의 폴더에 복사하여 공유할 수 있는 일반 타일셋입니다. 저장된 타일은 완전한 직사각형입니다. 선택 영역에 간격이 있으면 선택된 타일 사이의 타일도 저장됩니다. 섹션 파일에는 구멍을 위한 공간이 없기 때문입니다. 타일은 각 면에서 최대 512개의 타일이 될 수 있습니다.

## HPI 에디터

툴바의 오른쪽 끝에 있는 HPI 에디터 버튼(파일 메뉴, HPI 에디터)은 아카이브용 파일 브라우저를 엽니다: `.hpi`, `.ufo`, `.ccx`, `.gp3` 및 `.gpf`. 열기 버튼으로 하나를 열거나 새로 만들기로 빈 아카이브를 시작하세요. 열려 있는 맵이 그 자체로 아카이브인 경우 빈 창이 한 번의 클릭으로 제공합니다.

창은 Finder 창의 레이아웃을 따릅니다. 폴더 트리가 왼쪽으로 내려가고, 내용이 목록 또는 아이콘(오른쪽 상단의 두 버튼)으로 중간을 채우고, 경로 표시줄과 항목 수가 하단을 따라 실행됩니다. 트리의 폴더 또는 경로 표시줄의 세그먼트를 클릭하여 그곳으로 이동하고, 폴더를 두 번 클릭하여 열고, 뒤로 및 앞으로를 사용하여 단계를 되돌아가세요. 클릭으로 선택하고, Command-클릭 또는 Ctrl-클릭으로 하나를 추가하고, Shift-클릭으로 범위를 선택하거나 Ctrl+A를 사용하세요. 검색 상자는 보고 있는 폴더 아래에서 이름으로 파일과 폴더를 찾고, 결과를 두 번 클릭하면 위치가 표시됩니다.

- **내보내기**는 선택한 파일과 폴더를 선택한 폴더로 복사합니다. 폴더는 이름과 내부의 모든 것을 유지합니다. 선택된 것이 없으면 내보내기는 보고 있는 폴더를 가져와 아카이브 상단에 아카이브의 이름을 딴 폴더를 만들어 내용이 흩어지지 않도록 합니다. 파일이 이미 거기에 있으면 교체하기 전에 묻습니다. 선택한 폴더 밖으로 도달할 수 있는 이름은 건너뛰고, 컴퓨터가 파일 이름에 허용하지 않는 문자는 밑줄로 대체됩니다.
- **가져오기**는 파일 또는 그 안에 있는 모든 것이 포함된 전체 폴더를 보고 있는 폴더에 추가합니다. 데스크탑에서 창으로 파일과 폴더를 드롭할 수도 있습니다. 가져오기는 같은 이름의 파일을 교체하기 전에 묻습니다. 아카이브의 파일 이름은 게임에서처럼 대소문자를 구분하지 않고 일치합니다.
- **이동**은 한 폴더에서 다른 폴더로 항목을 드래그합니다: 목록 또는 아이콘 뷰의 폴더, 트리의 폴더 또는 경로 표시줄의 세그먼트로. 폴더를 자신에게 드래그하면 거부되고, 대상에 이미 존재하는 폴더는 병합됩니다.
- **이름 바꾸기**는 F2 또는 오른쪽 클릭 메뉴로, **새 폴더**는 툴바 버튼 또는 Ctrl+Shift+N으로, **삭제**는 Delete 키로. 모든 변경 사항은 실행 취소 및 재실행할 수 있습니다.

저장을 누를 때까지 디스크의 아카이브를 건드리지 않습니다. 저장 및 다른 이름으로 저장은 새 아카이브를 작성하고 한 단계로 이전 파일을 대체하므로 실패하면 원본을 그대로 둡니다. 아카이브는 빈 폴더를 저장할 수 없으며 파일이 전혀 없을 수 없으므로 빈 폴더는 저장 후 살아남지 못하고 에디터는 안에 아무것도 없는 아카이브를 저장하지 않습니다. 창 닫기, 다른 아카이브 열기 또는 종료는 먼저 저장되지 않은 변경 사항을 묻습니다.

## 단축키

macOS에서는 Command가 Ctrl을 대신합니다.

| 동작 | 단축키 |
| --- | --- |
| 새 맵 | Ctrl+N |
| 맵 열기 | Ctrl+O |
| 저장 | Ctrl+S |
| 다른 이름으로 저장 | Ctrl+Shift+S |
| 종료 | Ctrl+Q |
| 실행 취소 | Ctrl+Z |
| 재실행 | Ctrl+Shift+Z |
| 잘라내기 | Ctrl+X |
| 지형 + 높이 맵 복사 | Ctrl+C |
| 높이 맵 복사 | Ctrl+Shift+C |
| 붙여넣기 | Ctrl+V |
| 에디터 옵션 | Ctrl+, |
| 3D 뷰 켜기 또는 끄기 | Ctrl+3 |

HPI 에디터 창에서 맵 단축키 대신 키보드가 있을 때 작동합니다: Ctrl+O는 아카이브를 열고, Ctrl+N은 새 것을 시작하고, Ctrl+S 및 Ctrl+Shift+S는 저장하고, Ctrl+Z 및 Ctrl+Shift+Z는 실행 취소 및 재실행하고, Ctrl+A는 모두 선택하고, Ctrl+I는 가져오고, Ctrl+Shift+N은 폴더를 만들고, Ctrl+[ 및 Ctrl+]는 뒤로 및 앞으로 가고, Ctrl+Up은 둘러싸는 폴더로 가고, Ctrl+Down 또는 Return은 폴더를 열고, F2는 이름을 바꾸고, Delete 또는 Ctrl+Backspace는 선택을 제거하고, Backspace만으로는 한 폴더 위로 갑니다.

화살표 키는 커서 아래의 타일에서 스탬프 또는 붙여넣기를 슬라이드합니다. Esc는 붙여넣기를 취소하고, 삭제 모드를 떠나거나 선택을 지웁니다.

3D 뷰에서 드래그하여 궤도를 돌고, 오른쪽 드래그 또는 Shift-드래그하여 팬하고, 스크롤하여 확대/축소합니다. W A S D(또는 화살표 키)는 지면 위로 이동하고, Shift는 더 빠르게 이동하고, Q와 E는 회전하고, Home은 다시 시작합니다.

에디터 옵션은 다음에 에디터를 열 때 기억됩니다. 미리보기 크기, 높이 맵 간격, 사이드바 텍스트 크기 및 자동 저장이 모두 거기에 있습니다. 자동 저장은 타이머에서 맵 옆에 백업을 작성하고 저장의 파일을 그대로 둡니다.

## 저작권

저작권 © 2026 Black Cat Software  
개발자: Matt Andrzejczuk
