<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**语言:** [English](README.md) · **中文** · [Suomi](README.fi.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · [Bahasa Melayu](README.ms.md) · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

适用于 macOS、Windows 和 Linux 的 [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation) 地图编辑器。

绘制地块、放置树木和金属、设置玩家起始位置，并塑造地形高度。地图以游戏可加载的 `.ufo` 文件格式打开和保存。

## 下载

| 平台 | 下载 |
| --- | --- |
| macOS | [GalaxyEditor-1.1.2-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-macos.dmg) |
| Windows | [GalaxyEditor-1.1.2-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-windows-x64.zip) |
| Linux (存档) | [GalaxyEditor-1.1.2-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz) |

在 macOS 上，打开磁盘映像并将 Galaxy Editor 文件夹拖到您的应用程序文件夹。在 Windows 上，解压缩存档并运行 `GalaxyEditor.exe`。在 Linux 上，您可以使用 `.deb` 软件包或解压 `.tar.gz` 存档。每个下载都包含应用程序旁边的 `GE-Favorites-RandomMapGeneratorTiles.hpi`。地块画笔会读取该图块集，因此请将文件保留在 `GalaxyEditor.app`、`GalaxyEditor.exe` 或 Linux `GalaxyEditor` 旁边。`GreenWorld.hpi` 也以同样的方式包含。首次启动时，Galaxy Editor 会询问 Total Annihilation 的安装位置。所有版本都列在[发布页面](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases)上。

### Linux 安装

**选项 1：通过 .deb 软件包安装 (Debian/Ubuntu/Mint)**

The `.deb` is not on the v1.1.2 release, so there is no download URL for it yet. The package file name is `galaxy-editor_<version>_amd64.deb`. For version 1.1.2 that name is `galaxy-editor_1.1.2_amd64.deb`. When a later release includes the file, install the downloaded package with:

```bash
sudo apt install ./galaxy-editor_1.1.2_amd64.deb
```

Until then, use the `.tar.gz` below. That archive is what works today.

**选项 2：使用便携式存档**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.2/GalaxyEditor-1.1.2-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.2-linux-x64.tar.gz
cd GalaxyEditor-1.1.2-linux-x64
./GalaxyEditor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## 功能

**地块和特征。** 搜索游戏附带的图块集，单击一个部分，并将其印在地图上。树木、岩石、金属斑块和喷口使用其游戏内图形。起始标记标明每个玩家的起始位置。

**地形。** 打开高度图，然后升高、降低或设置选区的高度。将地形和高度一起复制，或单独复制高度并保留地块。混合纹理会在选区与周围地块相接的位置淡化图片。

**地图本身。** 地图设置保存游戏从地图读取的值：风力、潮汐、太阳能强度、重力、海平面、金属、AI 配置文件和特殊规则。生成地图从图块集构建新景观。小地图位于视图的角落，撤消功能涵盖地块、特征和高度编辑。

**3D 视图。** 工具栏中的 3D 视图按钮（视图菜单，Ctrl+3）将地图显示为地形，高度图抬升地面。一个半透明平面标记地图设置中的海平面，编辑器选项设置其颜色和不透明度。3D 视图用于环顾四周，因此在平面视图中编辑并根据需要来回切换。

## 构建

您需要 CMake 3.24 或更高版本、C++20 编译器、zlib 和 OpenGL。首次配置会下载 [GLFW](https://www.glfw.org/) 3.4 和 [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8，因此该步骤需要网络连接。

macOS 构建 `build/GalaxyEditor.app`。从 Finder 打开它，或运行 `open build/GalaxyEditor.app`：

```bash
cmake --preset default
cmake --build build
open build/GalaxyEditor.app
```

Linux：

```bash
cmake --preset default
cmake --build build
./build/galaxy-editor
```

要在 Mac 或 Linux 机器上构建 Windows `.exe`，请安装 MinGW-w64（`brew install mingw-w64`，或 `apt install g++-mingw-w64-x86-64-posix`），然后：

```bash
cmake --preset windows
cmake --build --preset windows
```

这会写入 `build-windows/GalaxyEditor.exe`。这是一个内置 C++ 运行时的单文件，只需要 Windows 附带的 DLL。首次配置还会下载 zlib。将您自己的图块集存档放在 `.exe` 旁边。

在 Windows 本身上，选择您的编译器理解的生成器：

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib 必须对 CMake 可见。在 Debian 和 Ubuntu 上是 `zlib1g-dev` 包。在 Fedora 上是 `zlib-devel`。Linux 打开和保存对话框使用 Zenity。

格式测试与编辑器一起构建：

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## 图块集

Galaxy Editor 从您的 Total Annihilation 安装文件夹加载游戏存档。首次启动时，它会在启动画面出现之前询问该文件夹。选择取消以在没有它的情况下启动。编辑器仍然运行，但图块集和特征选项卡保持空白，并且它不会在下次启动时再次询问。

随时在选项的 Total Annihilation 下更改文件夹。选择文件夹会立即重新加载存档，清除会忘记文件夹，因此下次启动会再次询问，就像首次启动一样。该文件夹保存在每个用户的 `settings.json` 中。

Galaxy Editor 直接读取该文件夹内的 `.hpi`、`.ccx`、`.gp3` 和 `.gpf` 文件，然后读取包含 `GalaxyEditor` 可执行文件的文件夹。在 macOS 上，这是包含 `GalaxyEditor.app` 的文件夹，因此名为 Galaxy Editor 的文件夹可以包含应用程序和您自己的 `.hpi` 文件，就像 Photoshop 的布局一样。将您自己的图块集存档放在可执行文件（或 `.app`）旁边。如果那里的文件与 Total Annihilation 文件夹中的文件同名，例如 `worlds.hpi` 或 `totala1.hpi`，它会被跳过，因此不会加载两次。像 `worlds.hpi` 这样的世界存档会填充图块集选项卡。`totala1.hpi`、`rev31.gp3` 或 `ccdata.ccx` 提供特征列表和特征图片。

这些存档是游戏数据。它们不是此存储库的一部分。

打开地图读取 `.ufo`、`.hpi` 和 `.tnt`。保存写入 `.ufo`。

## 收藏夹

收藏夹选项卡位于图块集和特征之间，保存您自己保存的地块。在选择地块模式下，选择一些地块并右键单击。菜单提供剪切、复制、复制高度、修改高度、混合和添加到收藏夹。在图块集选项卡中，指向图片会在其角落显示一个星形按钮，该按钮会添加该地块。在收藏夹选项卡中，同一角落有一个垃圾桶按钮，在您确认后删除地块。

添加到收藏夹会询问三件事：收藏夹存档、类别和子类别。输入新名称或选择现有名称。这些名称的工作方式与游戏自己的图块集中的方式相同：在 `Acid_New_1.hpi` 中，类别是 Acid_New_1，Rivers 和 Sm Lakes 是其子类别。创建收藏夹存档会询问诸如 FirstOne 之类的名称，并在 `GalaxyEditor` 可执行文件（macOS 上为 `.app`）旁边写入 `GE-Favorites-FirstOne.hpi`。

只有名为 `GE-Favorites-<name>.hpi` 的存档出现在收藏夹选项卡上，并且它们不会出现在图块集选项卡上。在内部，地块是 `sections/<category>/<subcategory>/<tile>.sct`，因此存档是您可以通过将文件复制到其他人的文件夹中来共享的普通图块集。保存的地块是完整的矩形。当选区有间隙时，所选地块之间的地块也会被保存，因为部分文件没有空洞的空间。地块的每一边最多可以有 512 个地块。

## HPI 编辑器

工具栏右端的 HPI 编辑器按钮（文件菜单，HPI 编辑器）打开存档的文件浏览器：`.hpi`、`.ufo`、`.ccx`、`.gp3` 和 `.gpf`。使用打开按钮打开一个，或使用新建开始一个空存档。当您打开的地图本身是一个存档时，空窗口会一键提供它。

该窗口遵循 Finder 窗口的布局。文件夹树沿左侧向下运行，内容以列表或图标的形式填充中间（右上角的两个按钮），路径栏和项目计数沿底部运行。单击树中的文件夹或路径栏的一段以转到那里，双击文件夹以打开它，并使用后退和前进来回溯您的步骤。单击选择，Command-单击或 Ctrl-单击添加一个，Shift-单击选择范围，或 Ctrl+A。搜索框在您正在查看的文件夹下方按名称查找文件和文件夹，双击结果会显示它的位置。

- **导出** 将选定的文件和文件夹复制到您选择的文件夹。文件夹保留其名称及其内部的所有内容。如果未选择任何内容，导出会获取您正在查看的文件夹，并在存档顶部创建一个以存档命名的文件夹，以便其内容不会分散。如果文件已经存在，则在替换之前会询问您。可能到达您选择的文件夹之外的名称会被跳过，计算机不允许在文件名中使用的字符会替换为下划线。
- **导入** 将文件或包含所有内容的整个文件夹添加到您正在查看的文件夹。您还可以将文件和文件夹从桌面拖放到窗口上。导入在替换同名文件之前会询问。存档中的文件名不区分大小写匹配，就像游戏中一样。
- **移动** 通过将项目从一个文件夹拖到另一个文件夹：拖到列表或图标视图中的文件夹、树中的文件夹或路径栏的一段。将文件夹拖到自身中会被拒绝，目标中已经存在的文件夹会合并到其中。
- **重命名** 使用 F2 或右键单击菜单，**新建文件夹** 使用工具栏按钮或 Ctrl+Shift+N，**删除** 使用 Delete 键。每个更改都可以撤消和重做。

在您按保存之前，不会触及磁盘上的存档。保存和另存为会写入一个新存档并一步替换旧文件，因此失败会保留原始文件。存档不能存储空文件夹或根本没有文件，因此空文件夹不会在保存后保留，编辑器不会保存没有任何内容的存档。关闭窗口、打开另一个存档或退出会首先询问有关未保存的更改。

## 快捷键

在 macOS 上，Command 代替 Ctrl。

| 操作 | 快捷键 |
| --- | --- |
| 新建地图 | Ctrl+N |
| 打开地图 | Ctrl+O |
| 保存 | Ctrl+S |
| 另存为 | Ctrl+Shift+S |
| 退出 | Ctrl+Q |
| 撤消 | Ctrl+Z |
| 重做 | Ctrl+Shift+Z |
| 剪切 | Ctrl+X |
| 复制地形 + 高度图 | Ctrl+C |
| 复制高度图 | Ctrl+Shift+C |
| 粘贴 | Ctrl+V |
| 编辑器选项 | Ctrl+, |
| 打开或关闭 3D 视图 | Ctrl+3 |

在 HPI 编辑器窗口中，当它具有键盘而不是地图快捷键时，这些工作：Ctrl+O 打开存档，Ctrl+N 开始一个新存档，Ctrl+S 和 Ctrl+Shift+S 保存，Ctrl+Z 和 Ctrl+Shift+Z 撤消和重做，Ctrl+A 选择所有内容，Ctrl+I 导入，Ctrl+Shift+N 创建文件夹，Ctrl+[ 和 Ctrl+] 后退和前进，Ctrl+Up 转到封闭文件夹，Ctrl+Down 或 Return 打开文件夹，F2 重命名，Delete 或 Ctrl+Backspace 删除选择，单独的 Backspace 向上移动一个文件夹。

箭头键在光标下的地块上滑动印章或粘贴。Esc 取消粘贴、离开删除模式或清除选择。

在 3D 视图中，拖动以旋转，右键拖动或 Shift-拖动以平移，滚动以缩放。W A S D（或箭头键）在地面上移动，Shift 移动得更快，Q 和 E 转动，Home 重新开始。

编辑器选项会在您下次打开编辑器时被记住。预览大小、高度图间距、侧边栏文本大小和自动保存都在那里。自动保存会在计时器上的地图旁边写入备份，并将保存中的文件单独保留。

## 版权

版权所有 © 2026 Black Cat Software  
开发者：Matt Andrzejczuk
