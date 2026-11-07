<p align="center">
  <img src="src/SplashScreen.png" alt="Galaxy Editor splash screen" width="760">
</p>

**Bahasa:** [English](README.md) · [中文](README.zh-CN.md) · [Suomi](README.fi.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Hrvatski](README.hr.md) · [Français](README.fr.md) · [한국어](README.ko.md) · [Nederlands](README.nl.md) · **Bahasa Melayu** · [Dansk](README.da.md) · [Беларуская](README.be.md) · [Norsk Bokmål](README.nb.md) · [Русский](README.ru.md) · [Slovenčina](README.sk.md)

# Galaxy Editor

Editor peta untuk [Total Annihilation](https://en.wikipedia.org/wiki/Total_Annihilation), untuk macOS, Windows, dan Linux.

Cat jubin, letakkan pokok dan logam, tetapkan permulaan pemain, dan bentuk ketinggian tanah. Peta dibuka dan disimpan sebagai fail `.ufo` yang boleh dimuatkan oleh permainan.

## Muat Turun

| Platform | Muat Turun |
| --- | --- |
| macOS | [GalaxyEditor-1.1.7-macos.dmg](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/GalaxyEditor-1.1.7-macos.dmg) |
| Windows | [GalaxyEditor-1.1.7-windows-x64.zip](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/GalaxyEditor-1.1.7-windows-x64.zip) |
| Linux | [GalaxyEditor-1.1.7-linux-x64.tar.gz](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/GalaxyEditor-1.1.7-linux-x64.tar.gz) |
| Debian / Ubuntu | [galaxy-editor_1.1.7_amd64.deb](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/galaxy-editor_1.1.7_amd64.deb) |

Pada macOS, buka imej cakera dan seret folder Galaxy Editor ke folder Applications anda. Pada Windows, nyahzip arkib dan jalankan `GalaxyEditor.exe`. Pada Linux, anda boleh menggunakan pakej `.deb` atau nyahpak arkib `.tar.gz`. Setiap muat turun termasuk `GE-Favorites-RandomMapGeneratorTiles.hpi` di sebelah aplikasi. Berus jubin membaca set jubin tersebut, jadi simpan fail di sebelah `GalaxyEditor.app`, `GalaxyEditor.exe`, atau Linux `galaxy-editor`. `GreenWorld.hpi` disertakan dengan cara yang sama. Kali pertama ia bermula, Galaxy Editor bertanya bahasa dahulu, kemudian di mana Total Annihilation dipasang. Setiap keluaran disenaraikan di [halaman Keluaran](https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases).

### Pemasangan Linux

**Pilihan 1: Pasang melalui pakej .deb (Debian/Ubuntu/Mint)**

Download `galaxy-editor_1.1.7_amd64.deb` and install it. `apt` installs the command `galaxy-editor` and pulls in zlib, OpenGL, X11, and Zenity. Launch it from the application menu or by running `galaxy-editor`. The `.deb` does not include `GE-Favorites-RandomMapGeneratorTiles.hpi` or `GreenWorld.hpi`. Those files are in the portable archive, beside `galaxy-editor`.

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/galaxy-editor_1.1.7_amd64.deb
sudo apt install ./galaxy-editor_1.1.7_amd64.deb
```

**Pilihan 2: Gunakan arkib mudah alih**

```bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v1.1.7/GalaxyEditor-1.1.7-linux-x64.tar.gz
tar -xzf GalaxyEditor-1.1.7-linux-x64.tar.gz
cd GalaxyEditor-1.1.7-linux-x64
./galaxy-editor
```

<p align="center">
  <img src="Screenshot.png" alt="Galaxy Editor open on a large map, with the tileset sidebar, a height tooltip, and the minimap" width="920">
</p>

## Apa yang boleh anda lakukan

**Jubin dan ciri.** Cari set jubin yang dihantar bersama permainan, klik bahagian, dan capkan ia pada peta. Pokok, batu, tompok logam, dan lubang angin menggunakan grafik dalam permainan mereka. Pin permulaan menandakan di mana setiap pemain bermula.

**Rupa bumi.** Hidupkan peta ketinggian, kemudian naikkan, turunkan, atau tetapkan ketinggian pilihan. Salin rupa bumi dan ketinggian bersama, atau salin ketinggian sahaja dan biarkan jubin di tempatnya. Blend Textures memudarkan gambar di mana pilihan bertemu dengan jubin di sekelilingnya.

**Peta itu sendiri.** Tetapan Peta memegang nilai yang permainan baca dari peta: angin, pasang surut, kekuatan solar, graviti, paras laut, logam, profil AI, dan peraturan khas. Jana Peta membina landskap baru dari set jubin. Peta mini terletak di atas sudut paparan, dan buat asal meliputi suntingan jubin, ciri, dan ketinggian.

**Paparan 3D.** Butang Paparan 3D dalam bar alat (menu Paparan, Ctrl+3) menunjukkan peta sebagai rupa bumi, dengan peta ketinggian mengangkat tanah. Satah lut sinar menandakan paras laut dari Tetapan Peta, dan Pilihan Editor menetapkan warna dan kelegapannya. Paparan 3D adalah untuk melihat sekeliling, jadi edit dalam paparan rata dan tukar bolak-balik seperti yang anda suka.

## Bina

Anda memerlukan CMake 3.24 atau lebih baru, pengkompil C++20, zlib, dan OpenGL. Konfigurasi pertama memuat turun [GLFW](https://www.glfw.org/) 3.4 dan [Dear ImGui](https://github.com/ocornut/imgui) 1.91.8, jadi langkah itu memerlukan sambungan rangkaian.

macOS membina `build/GalaxyEditor.app`. Buka ia dari Finder, atau jalankan `open build/GalaxyEditor.app`:

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

Untuk membina Windows `.exe` pada mesin Mac atau Linux, pasang MinGW-w64 (`brew install mingw-w64`, atau `apt install g++-mingw-w64-x86-64-posix`), kemudian:

```bash
cmake --preset windows
cmake --build --preset windows
```

Ini menulis `build-windows/GalaxyEditor.exe`. Ia adalah satu fail dengan runtime C++ terbina dalam, dan ia hanya memerlukan DLL yang dihantar dengan Windows. Konfigurasi pertama juga memuat turun zlib. Letakkan arkib set jubin anda sendiri di sebelah `.exe`.

Pada Windows sendiri, pilih penjana yang pengkompil anda fahami:

```bat
cmake -S . -B build
cmake --build build --config Release
```

zlib mesti kelihatan kepada CMake. Pada Debian dan Ubuntu itu adalah pakej `zlib1g-dev`. Pada Fedora ia adalah `zlib-devel`. Dialog buka dan simpan Linux menggunakan Zenity.

Ujian format dibina dengan editor:

```bash
cmake --build build --target galaxy_tests
./build/galaxy_tests
```

## Set Jubin

Galaxy Editor memuatkan arkib permainan dari folder pemasangan Total Annihilation anda. Pada pelancaran yang paling pertama, sebelum skrin percikan, ia bertanya bahasa dahulu, kemudian folder tersebut. Pilih Batal untuk memulakan tanpanya. Editor masih berjalan, tetapi tab Set Jubin dan Ciri kekal kosong, dan ia tidak bertanya lagi pada pelancaran seterusnya.

Tukar folder pada bila-bila masa dalam Pilihan, di bawah Total Annihilation. Pilih Folder memuatkan semula arkib dengan segera, dan Kosongkan melupakan folder, jadi pelancaran seterusnya bertanya untuk ia lagi, seperti pada pelancaran pertama. Folder dan bahasa yang dipilih disimpan dalam `settings.json` setiap pengguna. Bahasa ditukar kemudian dalam Pilihan, di bawah Bahasa, dan tetingkap bertukar serta-merta.

Galaxy Editor membaca fail `.hpi`, `.ccx`, `.gp3`, dan `.gpf` terus di dalam folder tersebut, dan kemudian dalam folder yang mengandungi fail boleh laku `GalaxyEditor`. Pada macOS itu adalah folder yang mengandungi `GalaxyEditor.app`, jadi folder bernama Galaxy Editor dengan aplikasi dan fail `.hpi` anda sendiri bersebelahan berfungsi, cara Photoshop disusun. Letakkan arkib set jubin anda sendiri di sebelah fail boleh laku (atau `.app`). Jika fail di sana mempunyai nama yang sama dengan satu dalam folder Total Annihilation, seperti `worlds.hpi` atau `totala1.hpi`, ia dilangkau, jadi tiada apa yang dimuatkan dua kali. Arkib dunia seperti `worlds.hpi` mengisi tab Set Jubin. `totala1.hpi`, `rev31.gp3`, atau `ccdata.ccx` membekalkan senarai ciri dan gambar ciri.

Arkib ini adalah data permainan. Mereka bukan sebahagian daripada repositori ini.

Buka Peta membaca `.ufo`, `.hpi`, dan `.tnt`. Simpan menulis `.ufo`.

## Kegemaran

Tab Kegemaran, antara Set Jubin dan Ciri, memegang jubin yang anda simpan sendiri. Dalam mod Pilih Jubin, pilih beberapa jubin dan klik kanan. Menu menawarkan Potong, Salin, Salin Ketinggian, Ubah Ketinggian, Campuran, dan Tambah ke Kegemaran. Dalam tab Set Jubin, menuding pada gambar menunjukkan butang bintang di sudutnya yang menambah jubin tersebut. Dalam tab Kegemaran, sudut yang sama memegang butang sampah yang memadamkan jubin selepas anda mengesahkan.

Tambah ke Kegemaran bertanya untuk tiga perkara: arkib kegemaran, kategori, dan subkategori. Taip nama baru atau pilih yang sedia ada. Nama berfungsi seperti yang mereka lakukan dalam set jubin permainan sendiri: dalam `Acid_New_1.hpi` kategori adalah Acid_New_1, dan Rivers dan Sm Lakes adalah subkategorinya. Cipta Arkib Kegemaran bertanya untuk nama seperti FirstOne dan menulis `GE-Favorites-FirstOne.hpi` di sebelah fail boleh laku `GalaxyEditor` (di sebelah `.app` pada macOS).

Hanya arkib bernama `GE-Favorites-<nama>.hpi` muncul pada tab Kegemaran, dan mereka dijauhkan dari tab Set Jubin. Di dalam, jubin adalah `sections/<kategori>/<subkategori>/<jubin>.sct`, jadi arkib adalah set jubin biasa yang anda boleh kongsi dengan menyalin fail ke folder orang lain. Jubin yang disimpan adalah segi empat tepat penuh. Apabila pilihan mempunyai jurang, jubin antara yang dipilih juga disimpan, kerana fail bahagian tidak mempunyai ruang untuk lubang. Jubin boleh menjadi maksimum 512 jubin pada setiap sisi.

## Editor HPI

Butang Editor HPI di hujung kanan bar alat (menu Fail, Editor HPI) membuka pelayar fail untuk arkib: `.hpi`, `.ufo`, `.ccx`, `.gp3`, dan `.gpf`. Buka satu dengan butang Buka, atau mulakan arkib kosong dengan Baru. Apabila peta yang anda buka sendiri adalah arkib, tetingkap kosong menawarkannya dalam satu klik.

Tetingkap mengikuti susun atur tetingkap Finder. Pokok folder berjalan ke bawah di sebelah kiri, kandungan mengisi tengah sebagai senarai atau sebagai ikon (dua butang di kanan atas), dan bar laluan dan kiraan item berjalan di sepanjang bawah. Klik folder dalam pokok atau segmen bar laluan untuk pergi ke sana, klik dua kali folder untuk membukanya, dan gunakan Undur dan Maju untuk menjejak langkah anda. Pilih dengan klik, klik Command atau klik Ctrl untuk menambah satu, klik Shift untuk julat, atau Ctrl+A. Kotak carian mencari fail dan folder mengikut nama di bawah folder yang anda lihat, dan klik dua kali hasil menunjukkan di mana ia tinggal.

- **Eksport** menyalin fail dan folder yang dipilih ke folder yang anda pilih. Folder mengekalkan namanya dan segala yang ada di dalamnya. Dengan tiada apa yang dipilih, Eksport mengambil folder yang anda lihat, dan di bahagian atas arkib ia mencipta folder yang dinamakan sempena arkib supaya kandungannya tidak berselerak. Jika fail sudah ada di sana, anda ditanya sebelum ia digantikan. Nama yang boleh mencapai di luar folder yang anda pilih dilangkau, dan aksara yang komputer tidak membenarkan dalam nama fail digantikan dengan garis bawah.
- **Import** menambah fail, atau seluruh folder dengan segala yang ada di dalamnya, ke folder yang anda lihat. Anda juga boleh menurunkan fail dan folder dari desktop ke tetingkap. Import bertanya sebelum menggantikan fail dengan nama yang sama. Nama fail dalam arkib sepadan tanpa mengira huruf besar kecil, seperti yang mereka lakukan dalam permainan.
- **Pindah** dengan menyeret item dari satu folder ke folder lain: ke folder dalam senarai atau paparan ikon, folder dalam pokok, atau segmen bar laluan. Menyeret folder ke dalam dirinya sendiri ditolak, dan folder yang sudah wujud di destinasi digabungkan ke dalamnya.
- **Namakan semula** dengan F2 atau menu klik kanan, **Folder Baru** dengan butang bar alat atau Ctrl+Shift+N, dan **Padam** dengan kunci Delete. Setiap perubahan boleh dibuat asal dan dibuat semula.

Tiada apa yang menyentuh arkib pada cakera sehingga anda menekan Simpan. Simpan dan Simpan Sebagai menulis arkib baru dan menggantikan fail lama dalam satu langkah, jadi kegagalan meninggalkan yang asal sahaja. Arkib tidak boleh menyimpan folder kosong atau tidak mempunyai fail langsung, jadi folder kosong tidak bertahan selepas simpan dan editor tidak akan menyimpan arkib tanpa apa-apa di dalamnya. Menutup tetingkap, membuka arkib lain, atau berhenti bertanya tentang perubahan yang tidak disimpan dahulu.

## Pintasan

Pada macOS, Command mengambil tempat Ctrl.

| Tindakan | Pintasan |
| --- | --- |
| Peta Baru | Ctrl+N |
| Buka Peta | Ctrl+O |
| Simpan | Ctrl+S |
| Simpan Sebagai | Ctrl+Shift+S |
| Berhenti | Ctrl+Q |
| Buat Asal | Ctrl+Z |
| Buat Semula | Ctrl+Shift+Z |
| Potong | Ctrl+X |
| Salin Rupa Bumi + Peta Ketinggian | Ctrl+C |
| Salin Peta Ketinggian | Ctrl+Shift+C |
| Tampal | Ctrl+V |
| Pilihan Editor | Ctrl+, |
| Paparan 3D hidup atau mati | Ctrl+3 |

Dalam tetingkap Editor HPI, ini berfungsi apabila ia mempunyai papan kekunci bukannya pintasan peta: Ctrl+O membuka arkib, Ctrl+N memulakan yang baru, Ctrl+S dan Ctrl+Shift+S menyimpan, Ctrl+Z dan Ctrl+Shift+Z membuat asal dan membuat semula, Ctrl+A memilih semua, Ctrl+I mengimport, Ctrl+Shift+N mencipta folder, Ctrl+[ dan Ctrl+] pergi undur dan maju, Ctrl+Up pergi ke folder yang merangkumi, Ctrl+Down atau Return membuka folder, F2 menamakan semula, Delete atau Ctrl+Backspace membuang pilihan, dan Backspace sahaja naik satu folder.

Kunci anak panah meluncurkan cop atau tampal dari jubin di bawah kursor. Esc membatalkan tampal, meninggalkan mod Padam, atau mengosongkan pilihan.

Dalam paparan 3D, seret untuk mengorbit, seret kanan atau Shift-seret untuk pan, dan tatal untuk zum. W A S D (atau kunci anak panah) bergerak di atas tanah, Shift bergerak lebih cepat, Q dan E menoleh, dan Home bermula semula.

Pilihan Editor diingati pada kali seterusnya anda membuka editor. Saiz pratonton, jarak peta ketinggian, saiz teks bar sisi, dan autosimpan semuanya hidup di sana. Autosimpan menulis sandaran di sebelah peta pada pemasa dan meninggalkan fail dari Simpan sahaja.

## Hak Cipta

Hak Cipta © 2026 Black Cat Software  
Dibangunkan oleh Matt Andrzejczuk
