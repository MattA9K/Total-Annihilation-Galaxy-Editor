#!/usr/bin/env python3
"""Build the static multilingual Galaxy Editor site.

English is the root page. Every other locale is a real directory with its
own index.html. Nothing in the output redirects, and the pages do not depend
on JavaScript for their text, links, or downloads.

The feature copy and the short description come from the README in that
language. Download and install wording comes from site/i18n.json, which
follows the English README and the v1.1.3 release notes. Several translated
READMEs still say every download includes the .hpi files; the .deb does not.
"""

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

REPO = "https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor"
DEFAULT_ORIGIN = "https://matta9k.github.io"
DEFAULT_BASE = "/Total-Annihilation-Galaxy-Editor"

# Same order and native names as src/core/I18n.cpp and the README language line.
LOCALES = (
    {"id": "en", "html_lang": "en", "og_locale": "en_US", "native": "English", "readme": "README.md"},
    {"id": "zh-CN", "html_lang": "zh-CN", "og_locale": "zh_CN", "native": "简体中文", "readme": "README.zh-CN.md"},
    {"id": "fi", "html_lang": "fi", "og_locale": "fi_FI", "native": "Suomi", "readme": "README.fi.md"},
    {"id": "pl", "html_lang": "pl", "og_locale": "pl_PL", "native": "Polski", "readme": "README.pl.md"},
    {"id": "sv", "html_lang": "sv", "og_locale": "sv_SE", "native": "Svenska", "readme": "README.sv.md"},
    {"id": "hr", "html_lang": "hr", "og_locale": "hr_HR", "native": "Hrvatski", "readme": "README.hr.md"},
    {"id": "fr", "html_lang": "fr", "og_locale": "fr_FR", "native": "Français", "readme": "README.fr.md"},
    {"id": "ko", "html_lang": "ko", "og_locale": "ko_KR", "native": "한국어", "readme": "README.ko.md"},
    {"id": "nl", "html_lang": "nl", "og_locale": "nl_NL", "native": "Nederlands", "readme": "README.nl.md"},
    {"id": "ms", "html_lang": "ms", "og_locale": "ms_MY", "native": "Bahasa Melayu", "readme": "README.ms.md"},
    {"id": "da", "html_lang": "da", "og_locale": "da_DK", "native": "Dansk", "readme": "README.da.md"},
    {"id": "be", "html_lang": "be", "og_locale": "be_BY", "native": "Беларуская", "readme": "README.be.md"},
    {"id": "nb", "html_lang": "nb", "og_locale": "nb_NO", "native": "Norsk bokmål", "readme": "README.nb.md"},
    {"id": "ru", "html_lang": "ru", "og_locale": "ru_RU", "native": "Русский", "readme": "README.ru.md"},
    {"id": "sk", "html_lang": "sk", "og_locale": "sk_SK", "native": "Slovenčina", "readme": "README.sk.md"},
)

STRING_KEYS = (
    "skip",
    "nav_label",
    "source",
    "docs",
    "language",
    "version_label",
    "splash_alt",
    "screenshot_alt",
    "install_intro",
    "releases_sentence",
    "linux_title",
    "linux_requirements",
    "deb_title",
    "deb_body",
    "local_deb_note",
    "archive_title",
    "archive_body",
    "download_word",
    "card_mac",
    "card_win",
    "card_linux",
    "card_deb",
    "docs_blurb",
    "commands_label",
    "not_found",
    "not_found_body",
    "release_notes",
)

VOID_TAGS = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "source", "track", "wbr",
}


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def normalize_base(base: str) -> str:
    cleaned = base.strip()
    if cleaned in ("", "/"):
        return ""
    return "/" + cleaned.strip("/")


def site_path(base: str, *parts: str) -> str:
    tail = "/".join(part.strip("/") for part in parts if part and part.strip("/"))
    if not tail:
        return (base or "") + "/"
    return (base + "/" + tail) if base else ("/" + tail)


def locale_href(base: str, locale_id: str) -> str:
    if locale_id == "en":
        return site_path(base)
    return site_path(base, locale_id) + "/"


def absolute(origin: str, path: str) -> str:
    return origin.rstrip("/") + path


def read_version(root: Path) -> str:
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit(f"VERSION must look like 1.2.3, found {version!r}")
    return version


def release_assets(version: str) -> dict[str, str]:
    prefix = f"{REPO}/releases/download/v{version}"
    return {
        "macos": f"{prefix}/GalaxyEditor-{version}-macos.dmg",
        "windows": f"{prefix}/GalaxyEditor-{version}-windows-x64.zip",
        "linux": f"{prefix}/GalaxyEditor-{version}-linux-x64.tar.gz",
        "deb": f"{prefix}/galaxy-editor_{version}_amd64.deb",
    }


def asset_names(version: str) -> dict[str, str]:
    return {
        "macos": f"GalaxyEditor-{version}-macos.dmg",
        "windows": f"GalaxyEditor-{version}-windows-x64.zip",
        "linux": f"GalaxyEditor-{version}-linux-x64.tar.gz",
        "deb": f"galaxy-editor_{version}_amd64.deb",
    }


def deb_commands(version: str, assets: dict[str, str]) -> str:
    return (
        f"wget {assets['deb']}\n"
        f"sudo apt install ./{asset_names(version)['deb']}"
    )


def archive_commands(version: str, assets: dict[str, str]) -> str:
    folder = f"GalaxyEditor-{version}-linux-x64"
    return (
        f"wget {assets['linux']}\n"
        f"tar -xzf {folder}.tar.gz\n"
        f"cd {folder}\n"
        "./galaxy-editor"
    )


def load_strings(site_dir: Path) -> dict[str, dict[str, str]]:
    data = json.loads((site_dir / "i18n.json").read_text(encoding="utf-8"))
    if set(data) != {locale["id"] for locale in LOCALES}:
        missing = [locale["id"] for locale in LOCALES if locale["id"] not in data]
        extra = sorted(set(data) - {locale["id"] for locale in LOCALES})
        raise SystemExit(f"i18n.json locales do not match the site list. missing={missing} extra={extra}")
    for locale_id, strings in data.items():
        if not isinstance(strings, dict):
            raise SystemExit(f"{locale_id} strings must be an object")
        missing = [key for key in STRING_KEYS if not str(strings.get(key, "")).strip()]
        extra = sorted(set(strings) - set(STRING_KEYS))
        if missing or extra:
            raise SystemExit(f"{locale_id} string keys missing={missing} extra={extra}")
        if locale_id != "en" and strings["install_intro"].startswith("On macOS"):
            raise SystemExit(f"{locale_id} install_intro is still English")
    return data


def split_h2(text: str) -> tuple[str, list[tuple[str, str]]]:
    lines = text.splitlines()
    indexes = [index for index, line in enumerate(lines) if line.startswith("## ")]
    if len(indexes) < 3:
        raise ValueError("expected Download, features, and Copyright headings")
    preamble = "\n".join(lines[: indexes[0]])
    sections: list[tuple[str, str]] = []
    for position, index in enumerate(indexes):
        heading = lines[index][3:].strip()
        end = indexes[position + 1] if position + 1 < len(indexes) else len(lines)
        sections.append((heading, "\n".join(lines[index + 1 : end])))
    return preamble, sections


def parse_intro(preamble: str) -> list[str]:
    lines = preamble.splitlines()
    try:
        h1 = next(index for index, line in enumerate(lines) if line.startswith("# "))
    except StopIteration as exc:
        raise ValueError("README is missing its title") from exc
    if lines[h1].strip() != "# Galaxy Editor":
        raise ValueError(f"unexpected title {lines[h1]!r}")
    paragraphs = [
        paragraph.strip()
        for paragraph in re.split(r"\n\s*\n", "\n".join(lines[h1 + 1 :]))
        if paragraph.strip()
    ]
    if len(paragraphs) < 2:
        raise ValueError("README intro needs two paragraphs")
    return paragraphs[:2]


def parse_features(body: str) -> list[tuple[str, str]]:
    blocks: list[tuple[str, str]] = []
    title: str | None = None
    collected: list[str] = []

    def flush() -> None:
        nonlocal title, collected
        if title is None:
            return
        text = " ".join(part.strip() for part in collected if part.strip())
        blocks.append((title, text))
        title = None
        collected = []

    for line in body.splitlines():
        match = re.match(r"^\*\*(.+?)\*\*\s*(.*)$", line.strip())
        if match:
            flush()
            title = match.group(1).strip().rstrip("。．.")
            if match.group(2).strip():
                collected.append(match.group(2).strip())
        elif title is not None and line.strip():
            collected.append(line.strip())
    flush()
    if len(blocks) < 4:
        raise ValueError(f"expected at least four features, found {len(blocks)}")
    return blocks


def parse_readme(path: Path) -> dict:
    preamble, sections = split_h2(path.read_text(encoding="utf-8"))
    intro = parse_intro(preamble)
    features = parse_features(sections[1][1])
    copyright_lines = [line.strip() for line in sections[-1][1].splitlines() if line.strip()]
    if len(copyright_lines) < 2:
        raise ValueError("copyright section needs two lines")
    return {
        "intro": intro,
        "download_heading": sections[0][0],
        "features_heading": sections[1][0],
        "features": features,
        "copyright_heading": sections[-1][0],
        "copyright": copyright_lines[:2],
    }


def discover_readmes(root: Path) -> None:
    found = {path.name for path in root.glob("README*.md")}
    expected = {locale["readme"] for locale in LOCALES}
    if found != expected:
        raise SystemExit(
            "README language set does not match the site. "
            f"missing={sorted(expected - found)} unexpected={sorted(found - expected)}"
        )


def inline(markdown: str) -> str:
    """Escape text, then keep code spans and http(s) links."""
    output: list[str] = []
    index = 0
    while index < len(markdown):
        if markdown[index] == "`":
            end = markdown.find("`", index + 1)
            if end == -1:
                output.append(html.escape(markdown[index:], quote=False))
                break
            output.append("<code>" + html.escape(markdown[index + 1 : end], quote=False) + "</code>")
            index = end + 1
            continue
        if markdown[index] == "[":
            match = re.match(r"\[([^\]]+)\]\((https?://[^)\s]+)\)", markdown[index:])
            if match:
                text = html.escape(match.group(1), quote=False)
                url = html.escape(match.group(2), quote=True)
                output.append(f'<a href="{url}">{text}</a>')
                index += match.end()
                continue
        output.append(html.escape(markdown[index], quote=False))
        index += 1
    return "".join(output)


def plain(markdown: str) -> str:
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", markdown)
    text = text.replace("`", "")
    return " ".join(text.split())


def markdown_link_label(markdown: str) -> str:
    match = re.search(r"\[([^\]]+)\]\(", markdown)
    if not match:
        raise ValueError("expected a markdown link")
    return match.group(1)


def png_size(path: Path) -> tuple[int, int]:
    header = path.read_bytes()[:24]
    if header[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path} is not a PNG")
    import struct

    return struct.unpack(">II", header[16:24])


def language_items(base: str, current: str) -> str:
    items = []
    for locale in LOCALES:
        href = html.escape(locale_href(base, locale["id"]), quote=True)
        name = html.escape(locale["native"])
        current_attr = ' aria-current="page"' if locale["id"] == current else ""
        items.append(
            f'<li><a href="{href}" hreflang="{html.escape(locale["html_lang"])}" '
            f'lang="{html.escape(locale["html_lang"])}"{current_attr}>{name}</a></li>'
        )
    return "\n".join(items)


def hreflang_links(origin: str, base: str) -> str:
    links = []
    for locale in LOCALES:
        href = html.escape(absolute(origin, locale_href(base, locale["id"])), quote=True)
        links.append(
            f'<link rel="alternate" hreflang="{html.escape(locale["html_lang"])}" href="{href}">'
        )
    default = html.escape(absolute(origin, locale_href(base, "en")), quote=True)
    links.append(f'<link rel="alternate" hreflang="x-default" href="{default}">')
    return "\n".join(links)


def download_button(label: str, platform: str, filename: str, url: str) -> str:
    return (
        f'<a class="button" href="{html.escape(url, quote=True)}">'
        f'<span class="dl">{html.escape(label)}</span>'
        f'<span class="os">{html.escape(platform)}</span>'
        f'<span class="file">{html.escape(filename)}</span>'
        "</a>"
    )


def render_page(context: dict) -> str:
    locale = context["locale"]
    strings = context["strings"]
    readme = context["readme"]
    base = context["base"]
    origin = context["origin"]
    version = context["version"]
    assets = context["assets"]
    names = context["names"]
    page = locale_href(base, locale["id"])
    canonical = absolute(origin, page)
    style = site_path(base, "assets", "style.css")
    splash = site_path(base, "assets", "splash.png")
    screenshot = site_path(base, "assets", "screenshot.png")
    icon = site_path(base, "assets", "icon.png")
    favicon = site_path(base, "assets", "favicon.png")
    touch = site_path(base, "assets", "apple-touch-icon.png")
    title = "Galaxy Editor — " + plain(readme["intro"][0]).rstrip(".")
    description = plain(readme["intro"][0]) + " " + plain(readme["intro"][1])
    readme_url = f"{REPO}/blob/main/{locale['readme']}"
    releases_url = f"{REPO}/releases"
    tag_url = f"{REPO}/releases/tag/v{version}"
    releases_html = inline(strings["releases_sentence"].replace("{releases}", releases_url))
    features = []
    for feature_title, feature_body in readme["features"]:
        features.append(
            "<article class=\"feature\">"
            f"<h3>{html.escape(feature_title)}</h3>"
            f"<p>{inline(feature_body)}</p>"
            "</article>"
        )
    platforms = [
        ("macOS", strings["card_mac"], names["macos"], assets["macos"]),
        ("Windows", strings["card_win"], names["windows"], assets["windows"]),
        ("Linux", strings["card_linux"], names["linux"], assets["linux"]),
        ("Debian / Ubuntu", strings["card_deb"], names["deb"], assets["deb"]),
    ]
    hero_buttons = "\n".join(
        download_button(strings["download_word"], platform, filename, url)
        for platform, _detail, filename, url in platforms
    )
    cards = []
    for platform, detail, filename, url in platforms:
        cards.append(
            "<article class=\"card\">"
            f"<h3>{html.escape(platform)}</h3>"
            f"<p>{html.escape(detail)}</p>"
            f"{download_button(strings['download_word'], platform, filename, url)}"
            "</article>"
        )
    alternates = []
    for other in LOCALES:
        if other["id"] == locale["id"]:
            continue
        alternates.append(
            f'<meta property="og:locale:alternate" content="{html.escape(other["og_locale"])}">'
        )
    copyright_html = "\n".join(f"<p>{inline(line)}</p>" for line in readme["copyright"])
    schema = {
        "@context": "https://schema.org",
        "@type": "SoftwareApplication",
        "name": "Galaxy Editor",
        "description": description,
        "applicationCategory": "DesignApplication",
        "operatingSystem": "macOS, Windows, Linux",
        "softwareVersion": version,
        "inLanguage": locale["html_lang"],
        "url": canonical,
        "image": absolute(origin, splash),
        "author": {"@type": "Person", "name": "Matt Andrzejczuk"},
        "copyrightHolder": {"@type": "Organization", "name": "Black Cat Software"},
        "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"},
        "downloadUrl": [assets["macos"], assets["windows"], assets["linux"], assets["deb"]],
        "isPartOf": {"@type": "WebSite", "name": "Galaxy Editor", "url": absolute(origin, locale_href(base, "en"))},
    }
    schema_json = json.dumps(schema, ensure_ascii=False).replace("<", "\\u003c")
    splash_w, splash_h = context["splash_size"]
    shot_w, shot_h = context["shot_size"]
    return f"""<!DOCTYPE html>
<html lang="{html.escape(locale["html_lang"])}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)}</title>
<meta name="description" content="{html.escape(description, quote=True)}">
<link rel="canonical" href="{html.escape(canonical, quote=True)}">
{hreflang_links(origin, base)}
<meta name="robots" content="index,follow">
<meta name="theme-color" content="#080a09">
<meta property="og:type" content="website">
<meta property="og:site_name" content="Galaxy Editor">
<meta property="og:title" content="{html.escape(title, quote=True)}">
<meta property="og:description" content="{html.escape(description, quote=True)}">
<meta property="og:url" content="{html.escape(canonical, quote=True)}">
<meta property="og:image" content="{html.escape(absolute(origin, splash), quote=True)}">
<meta property="og:locale" content="{html.escape(locale["og_locale"])}">
{chr(10).join(alternates)}
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="{html.escape(title, quote=True)}">
<meta name="twitter:description" content="{html.escape(description, quote=True)}">
<meta name="twitter:image" content="{html.escape(absolute(origin, splash), quote=True)}">
<link rel="icon" href="{html.escape(favicon, quote=True)}" type="image/png">
<link rel="apple-touch-icon" href="{html.escape(touch, quote=True)}">
<link rel="stylesheet" href="{html.escape(style, quote=True)}">
<script type="application/ld+json">{schema_json}</script>
</head>
<body>
<!-- Static page. The language is the URL. This page does not redirect. -->
<a class="skip" href="#content">{html.escape(strings["skip"])}</a>
<header class="site-header">
<div class="header-inner">
<a class="brand" href="{html.escape(page, quote=True)}">
<img src="{html.escape(icon, quote=True)}" alt="" width="256" height="256">
<span>Galaxy Editor</span>
</a>
<nav class="site-nav" aria-label="{html.escape(strings["nav_label"], quote=True)}">
<ul>
<li><a href="{html.escape(page, quote=True)}#features">{html.escape(readme["features_heading"])}</a></li>
<li><a href="{html.escape(page, quote=True)}#download">{html.escape(readme["download_heading"])}</a></li>
<li><a href="{html.escape(page, quote=True)}#linux">{html.escape(strings["linux_title"])}</a></li>
<li><a href="{html.escape(REPO, quote=True)}">{html.escape(strings["source"])}</a></li>
<li><a href="{html.escape(readme_url, quote=True)}">{html.escape(strings["docs"])}</a></li>
</ul>
</nav>
<details class="language">
<summary>{html.escape(strings["language"])}: <span lang="{html.escape(locale["html_lang"])}">{html.escape(locale["native"])}</span></summary>
<nav aria-label="{html.escape(strings["language"], quote=True)}">
<ul class="lang-list">
{language_items(base, locale["id"])}
</ul>
</nav>
</details>
</div>
</header>
<main id="content">
<div class="wrap">
<section class="hero" aria-labelledby="page-title">
<p class="version">{html.escape(strings["version_label"])} {html.escape(version)}</p>
<h1 id="page-title">Galaxy Editor</h1>
<p class="tagline">{inline(readme["intro"][0])}</p>
<p>{inline(readme["intro"][1])}</p>
<div class="hero-actions">
{hero_buttons}
</div>
<figure class="splash">
<img src="{html.escape(splash, quote=True)}" alt="{html.escape(strings["splash_alt"], quote=True)}" width="{splash_w}" height="{splash_h}">
</figure>
</section>
<figure class="shot">
<img src="{html.escape(screenshot, quote=True)}" alt="{html.escape(strings["screenshot_alt"], quote=True)}" width="{shot_w}" height="{shot_h}">
<figcaption aria-hidden="true">{html.escape(strings["screenshot_alt"])}</figcaption>
</figure>
<section class="section" id="features" aria-labelledby="features-title">
<h2 id="features-title">{html.escape(readme["features_heading"])}</h2>
<div class="features">
{"".join(features)}
</div>
</section>
<section class="section" id="download" aria-labelledby="download-title">
<h2 id="download-title">{html.escape(readme["download_heading"])}</h2>
<p>{inline(strings["install_intro"])}</p>
<p>{releases_html}</p>
<div class="cards">
{"".join(cards)}
</div>
</section>
<section class="section" id="linux" aria-labelledby="linux-title">
<h2 id="linux-title">{html.escape(strings["linux_title"])}</h2>
<p>{inline(strings["linux_requirements"])}</p>
<article class="install-block">
<h3>{html.escape(strings["deb_title"])}</h3>
<p>{inline(strings["deb_body"])}</p>
<p>{inline(strings["local_deb_note"])}</p>
<p class="commands-label">{html.escape(strings["commands_label"])}</p>
<pre><code>{html.escape(deb_commands(version, assets))}</code></pre>
</article>
<article class="install-block">
<h3>{html.escape(strings["archive_title"])}</h3>
<p>{inline(strings["archive_body"])}</p>
<p class="commands-label">{html.escape(strings["commands_label"])}</p>
<pre><code>{html.escape(archive_commands(version, assets))}</code></pre>
</article>
</section>
<section class="section" id="documentation" aria-labelledby="docs-title">
<h2 id="docs-title">{html.escape(strings["docs"])}</h2>
<p>{html.escape(strings["docs_blurb"])}</p>
<p><a href="{html.escape(readme_url, quote=True)}">{html.escape(locale["readme"])}</a></p>
</section>
</div>
</main>
<footer class="site-footer">
<div class="wrap">
{copyright_html}
<ul class="footer-links">
<li><a href="{html.escape(REPO, quote=True)}">{html.escape(strings["source"])}</a></li>
<li><a href="{html.escape(readme_url, quote=True)}">{html.escape(strings["docs"])}</a></li>
<li><a href="{html.escape(releases_url, quote=True)}">{html.escape(markdown_link_label(strings["releases_sentence"]))}</a></li>
<li><a href="{html.escape(tag_url, quote=True)}">{html.escape(strings["release_notes"])} {html.escape(version)}</a></li>
</ul>
<nav class="footer-languages" aria-label="{html.escape(strings["language"], quote=True)}">
<ul class="lang-list">
{language_items(base, locale["id"])}
</ul>
</nav>
</div>
</footer>
</body>
</html>
"""


def render_not_found(context: dict) -> str:
    base = context["base"]
    strings_by_id = context["strings_by_id"]
    english = strings_by_id["en"]
    style = site_path(base, "assets", "style.css")
    icon = site_path(base, "assets", "icon.png")
    favicon = site_path(base, "assets", "favicon.png")
    lines = []
    for locale in LOCALES:
        strings = strings_by_id[locale["id"]]
        lines.append(
            f'<li lang="{html.escape(locale["html_lang"])}">'
            f'<strong>{html.escape(strings["not_found"])}</strong> '
            f'{html.escape(strings["not_found_body"])}</li>'
        )
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Galaxy Editor — {html.escape(english["not_found"])}</title>
<meta name="robots" content="noindex">
<link rel="icon" href="{html.escape(favicon, quote=True)}" type="image/png">
<link rel="stylesheet" href="{html.escape(style, quote=True)}">
</head>
<body>
<a class="skip" href="#content">{html.escape(english["skip"])}</a>
<header class="site-header">
<div class="header-inner">
<a class="brand" href="{html.escape(locale_href(base, "en"), quote=True)}">
<img src="{html.escape(icon, quote=True)}" alt="" width="256" height="256">
<span>Galaxy Editor</span>
</a>
</div>
</header>
<main id="content">
<div class="wrap">
<section class="hero">
<h1>{html.escape(english["not_found"])}</h1>
<ul class="missing-list">
{chr(10).join(lines)}
</ul>
<nav aria-label="{html.escape(english["language"], quote=True)}">
<ul class="lang-list">
{language_items(base, "")}
</ul>
</nav>
</section>
</div>
</main>
</body>
</html>
"""


def render_sitemap(origin: str, base: str) -> str:
    urls = []
    alternate_rows = []
    for locale in LOCALES:
        href = absolute(origin, locale_href(base, locale["id"]))
        alternate_rows.append(
            f'    <xhtml:link rel="alternate" hreflang="{html.escape(locale["html_lang"])}" href="{html.escape(href)}"/>'
        )
    default_href = absolute(origin, locale_href(base, "en"))
    alternate_rows.append(
        f'    <xhtml:link rel="alternate" hreflang="x-default" href="{html.escape(default_href)}"/>'
    )
    alternates = "\n".join(alternate_rows)
    for locale in LOCALES:
        href = absolute(origin, locale_href(base, locale["id"]))
        urls.append(
            "  <url>\n"
            f"    <loc>{html.escape(href)}</loc>\n"
            f"{alternates}\n"
            "  </url>"
        )
    body = "\n".join(urls)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"\n'
        '        xmlns:xhtml="http://www.w3.org/1999/xhtml">\n'
        f"{body}\n"
        "</urlset>\n"
    )


def render_robots(origin: str, base: str) -> str:
    sitemap = absolute(origin, site_path(base, "sitemap.xml"))
    return f"User-agent: *\nAllow: /\n\nSitemap: {sitemap}\n"


def assert_safe_output(root: Path, out: Path) -> None:
    """The build deletes its output directory before writing."""
    temp_root = Path(tempfile.gettempdir()).resolve()
    in_repo = root in out.parents and out.name == "_site"
    in_temp = temp_root in out.parents
    if out in {root, Path("/"), Path.home()} or len(out.parts) < 3 or not (in_repo or in_temp):
        raise SystemExit(f"refusing to replace {out}; choose <repo>/_site or a temp directory")


def build(root: Path, out: Path, base: str, origin: str) -> None:
    root = root.resolve()
    out = out.resolve()
    assert_safe_output(root, out)
    discover_readmes(root)
    version = read_version(root)
    strings_by_id = load_strings(root / "site")
    parsed = {}
    for locale in LOCALES:
        try:
            parsed[locale["id"]] = parse_readme(root / locale["readme"])
        except ValueError as exc:
            raise SystemExit(f"{locale['readme']}: {exc}") from exc
    assets = release_assets(version)
    names = asset_names(version)
    splash_size = png_size(root / "src" / "SplashScreen.png")
    shot_size = png_size(root / "Screenshot.png")
    if out.exists():
        shutil.rmtree(out)
    (out / "assets").mkdir(parents=True)
    shutil.copyfile(root / "site" / "static" / "style.css", out / "assets" / "style.css")
    shutil.copyfile(root / "src" / "SplashScreen.png", out / "assets" / "splash.png")
    shutil.copyfile(root / "Screenshot.png", out / "assets" / "screenshot.png")
    shutil.copyfile(root / "assets" / "icons" / "AppIcon.png", out / "assets" / "icon.png")
    shutil.copyfile(root / "assets" / "icons" / "window-32.png", out / "assets" / "favicon.png")
    shutil.copyfile(root / "assets" / "icons" / "window-256.png", out / "assets" / "apple-touch-icon.png")
    shared = {
        "base": base,
        "origin": origin,
        "version": version,
        "assets": assets,
        "names": names,
        "strings_by_id": strings_by_id,
        "splash_size": splash_size,
        "shot_size": shot_size,
    }
    for locale in LOCALES:
        page = render_page({**shared, "locale": locale, "strings": strings_by_id[locale["id"]], "readme": parsed[locale["id"]]})
        if locale["id"] == "en":
            destination = out / "index.html"
        else:
            destination = out / locale["id"] / "index.html"
            destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(page, encoding="utf-8")
    (out / "404.html").write_text(render_not_found(shared), encoding="utf-8")
    (out / "sitemap.xml").write_text(render_sitemap(origin, base), encoding="utf-8")
    (out / "robots.txt").write_text(render_robots(origin, base), encoding="utf-8")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the Galaxy Editor static site.")
    parser.add_argument("--root", type=Path, default=repo_root())
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--base", default=DEFAULT_BASE)
    parser.add_argument("--origin", default=DEFAULT_ORIGIN)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = args.root.resolve()
    out = args.out if args.out is not None else root / "_site"
    base = normalize_base(args.base)
    origin = args.origin.rstrip("/")
    if not origin.startswith("https://"):
        raise SystemExit("--origin must be an https URL")
    build(root, out, base, origin)
    print(f"Built {len(LOCALES)} locales into {out}")
    print(f"Preview base {base or '/'} on {origin}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
