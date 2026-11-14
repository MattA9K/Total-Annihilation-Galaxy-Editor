# Galaxy Editor website

Static multilingual site for GitHub Pages. English is served at the project
root. The other languages are separate pages. The HTML is the page: there is
no language redirect and no analytics. Pictures, captions, and download
links do not need JavaScript. With JavaScript, the screenshot gallery can
open a picture across the viewport.

The intended public URL, after GitHub Pages is turned on, is:

`https://matta9k.github.io/Total-Annihilation-Galaxy-Editor/`

That URL is not live until Pages is enabled. See "After merge" below.

## Preview

From the repository root:

```bash
python3 site/build_site.py
python3 site/preview.py
```

Open `http://127.0.0.1:8765/Total-Annihilation-Galaxy-Editor/`.

The build defaults to the project Pages base path
`/Total-Annihilation-Galaxy-Editor`. To preview at the server root instead:

```bash
python3 site/build_site.py --base / --out /tmp/galaxy-site
python3 site/preview.py --base / --directory /tmp/galaxy-site
```

Check the generated files, including the four download URLs for the version in `VERSION`:

```bash
python3 site/test_site.py
python3 site/test_site.py --check-downloads
```

`python3` from the standard library is enough. There is no package install.

## Where the words come from

- The short description, feature headings, feature text, and copyright lines
  are read from `README.md` and `README.<locale>.md` at build time.
- Navigation, install instructions, image descriptions, and the language
  names that are not already in a README live in `site/i18n.json`.
- Download URLs are built from `VERSION`. For 1.1.11 they are the four files
  actually attached to the `v1.1.11` release:
  - `GalaxyEditor-1.1.11-macos.dmg`
  - `GalaxyEditor-1.1.11-windows-x64.zip`
  - `GalaxyEditor-1.1.11-linux-x64.tar.gz`
  - `galaxy-editor_1.1.11_amd64.deb`
- The opening screenshot and icons are copied from `src/SplashScreen.png`,
  `Screenshot.png`, and `assets/icons/`. That screenshot is the editor on a
  map. Its window title still says v1.0.0, so the site does not claim it is
  a picture of the current release.
- The gallery pictures live in `site/screenshots/`. `full/` holds
  byte-for-byte copies of eight editor screenshots. `preview/` and `thumb/`
  are smaller JPEG derivatives. Do not recompress the files in `full/`.
  `manifest.json` records their hashes. A ninth screenshot, the dark
  Favorites view, is omitted because the Favorites list is already shown
  and the placed trees are shown in another picture.

The Linux commands install a downloaded `.deb` from the current directory:

```bash
sudo apt install ./galaxy-editor_<version>_amd64.deb
```

They do not add an apt repository. The `.deb` does not include
`GE-Favorites-RandomMapGeneratorTiles.hpi` or `GreenWorld.hpi`. Those two
files are in the disk image, the Windows zip, and the Linux `.tar.gz`.

`site/i18n.json` follows `README.md` and the v1.1.3 release notes for that
fact. Several translated READMEs still say every download includes the
`.hpi` files, and their `.deb` paragraph is still English. Do not copy that
paragraph back into the site.

The macOS build is for Apple silicon, which is what the release notes and
the macOS release workflow say.

## Update a release

1. Publish the GitHub release with the same four filenames, with the new
   version number in place of `1.1.11`.
2. Make sure `VERSION` in the repository is that version.
3. Update the download sections of the READMEs so the documentation matches.
4. If the install steps changed, edit `site/i18n.json` in every language.
5. Run `python3 site/test_site.py --check-downloads`.
6. Merge to `main`. The Pages workflow rebuilds the site from `VERSION`.

Changing only C++ code does not change the site text. The workflow still
rebuilds on every push to `main`, using whatever `VERSION` and READMEs are
in that commit.

## Add or edit a language

The locale list in `site/build_site.py` matches `src/core/I18n.cpp`:
English, 简体中文, Suomi, Polski, Svenska, Hrvatski, Français, 한국어,
Nederlands, Bahasa Melayu, Dansk, Беларуская, Norsk bokmål, Русский,
Slovenčina.

To add another language later:

1. Add `README.<id>.md` with the same opening shape: a `# Galaxy Editor`
   title, two intro paragraphs, then the download, features, and copyright
   sections.
2. Add the locale to `LOCALES` in `site/build_site.py`, using the native
   name shown in the editor.
3. Add every string key for that id in `site/i18n.json`. Translate the
   install text from the English entry, including the local `.deb` commands'
   explanation. Do not leave the install paragraph in English.
4. Run `python3 site/test_site.py`.

Editing a feature in a README updates the site on the next build. Editing a
button, heading that is not in the README, or install paragraph means
editing `site/i18n.json`.

## After merge

This repository does not turn Pages on by itself. Someone with admin access
needs to:

1. Open Settings, then Pages.
2. Under Build and deployment, set Source to GitHub Actions.
3. If the "Deploy GitHub Pages" job on the merge commit failed before that
   setting existed, open that workflow run and run it again. `workflow_dispatch`
   on `main` also deploys.

No custom domain, analytics, or paid host is required. Leave the site on the
project Pages URL above.

Pull requests run the same build and `site/test_site.py`. They do not deploy.

The macOS, Windows, Linux, and release workflows are unchanged.
