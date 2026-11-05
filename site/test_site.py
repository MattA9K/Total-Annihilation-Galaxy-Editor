#!/usr/bin/env python3
"""Check the generated site: locales, SEO, base path, and download links."""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
import urllib.error
import urllib.request
from html.parser import HTMLParser
from pathlib import Path
from xml.etree import ElementTree

import build_site

VOID_TAGS = build_site.VOID_TAGS
BASE = build_site.DEFAULT_BASE
ORIGIN = build_site.DEFAULT_ORIGIN
TRACKERS = (
    "google-analytics",
    "googletagmanager",
    "googleadservices",
    "doubleclick",
    "facebook.net",
    "plausible",
    "mc.yandex",
    "hotjar",
    "segment.com",
    "mixpanel",
)


class Balancer(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.stack: list[str] = []
        self.ids: set[str] = set()
        self.errors: list[str] = []
        self.scripts: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attributes = {key: value or "" for key, value in attrs}
        if tag == "script":
            self.scripts.append(attributes.get("type", ""))
            if "src" in attributes:
                self.errors.append("script src is not allowed")
        element_id = attributes.get("id")
        if element_id:
            if element_id in self.ids:
                self.errors.append(f"duplicate id {element_id}")
            self.ids.add(element_id)
        if tag not in VOID_TAGS:
            self.stack.append(tag)

    def handle_endtag(self, tag: str) -> None:
        if tag in VOID_TAGS:
            self.errors.append(f"void tag closed: {tag}")
            return
        if not self.stack or self.stack[-1] != tag:
            self.errors.append(f"unexpected </{tag}> with stack {self.stack[-6:]}")
            return
        self.stack.pop()

    def close(self) -> None:
        super().close()
        if self.stack:
            self.errors.append(f"unclosed tags {self.stack}")


def page_path(out: Path, locale_id: str) -> Path:
    if locale_id == "en":
        return out / "index.html"
    return out / locale_id / "index.html"


def local_target(out: Path, base: str, href: str) -> Path | None:
    path = href.split("#", 1)[0]
    if href.startswith("#"):
        return None
    prefix = f"{base}/" if base else "/"
    if not (href.startswith(prefix) or path == prefix or path == base):
        return None
    relative = path[len(base):].lstrip("/") if base else path.lstrip("/")
    if relative == "" or relative.endswith("/"):
        return out / relative / "index.html"
    return out / relative


def check_structure(out: Path, base: str, origin: str, version: str) -> list[str]:
    errors: list[str] = []
    assets = build_site.release_assets(version)
    names = build_site.asset_names(version)
    deb_block = build_site.deb_commands(version, assets)
    archive_block = build_site.archive_commands(version, assets)
    descriptions: set[str] = set()
    titles: set[str] = set()
    expected_urls = {
        build_site.absolute(origin, build_site.locale_href(base, locale["id"]))
        for locale in build_site.LOCALES
    }
    if (out / "en").exists():
        errors.append("English must stay at the site root, not /en/")
    html_files = sorted(path.relative_to(out).as_posix() for path in out.rglob("*.html"))
    expected_html = ["404.html", "index.html"] + [
        f"{locale['id']}/index.html" for locale in build_site.LOCALES if locale["id"] != "en"
    ]
    if html_files != sorted(expected_html):
        errors.append(f"unexpected html files: {html_files}")

    for filename in (
        "assets/style.css",
        "assets/splash.png",
        "assets/screenshot.png",
        "assets/icon.png",
        "assets/favicon.png",
        "assets/apple-touch-icon.png",
    ):
        file_path = out / filename
        if not file_path.is_file() or file_path.stat().st_size == 0:
            errors.append(f"missing asset {filename}")

    css = (out / "assets" / "style.css").read_text(encoding="utf-8")
    if "http://" in css or "https://" in css:
        errors.append("stylesheet requests a remote URL")

    for locale in build_site.LOCALES:
        text = page_path(out, locale["id"]).read_text(encoding="utf-8")
        readme = build_site.parse_readme(
            build_site.repo_root() / locale["readme"]
        )
        lowered = text.lower()
        for tracker in TRACKERS:
            if tracker in lowered:
                errors.append(f"{locale['id']} references {tracker}")
        if re.search(r"http-equiv\s*=\s*[\"']refresh", text, re.I):
            errors.append(f"{locale['id']} has a refresh redirect")
        if re.search(r"location\.(href|replace|assign)", text):
            errors.append(f"{locale['id']} has a script redirect")
        if "This page does not redirect." not in text:
            errors.append(f"{locale['id']} is missing the static-page note")
        lang = re.search(r'<html lang="([^"]+)">', text)
        if not lang or lang.group(1) != locale["html_lang"]:
            errors.append(f"{locale['id']} html lang is {None if not lang else lang.group(1)}")
        title = re.search(r"<title>(.*?)</title>", text, re.S)
        description = re.search(r'<meta name="description" content="(.*?)">', text)
        canonical = re.search(r'<link rel="canonical" href="(.*?)">', text)
        if not title or "Galaxy Editor" not in title.group(1):
            errors.append(f"{locale['id']} title is missing Galaxy Editor")
        else:
            titles.add(title.group(1))
        if not description or len(description.group(1)) < 40:
            errors.append(f"{locale['id']} description is missing")
        else:
            descriptions.add(description.group(1))
        expected_canonical = build_site.absolute(origin, build_site.locale_href(base, locale["id"]))
        if not canonical or canonical.group(1) != expected_canonical:
            errors.append(f"{locale['id']} canonical is wrong")
        alternates = re.findall(r'<link rel="alternate" hreflang="([^"]+)" href="([^"]+)">', text)
        found = {code: href for code, href in alternates}
        expected_codes = {item["html_lang"] for item in build_site.LOCALES} | {"x-default"}
        if set(found) != expected_codes:
            errors.append(f"{locale['id']} hreflang set is {sorted(found)}")
        if found.get("x-default") != build_site.absolute(origin, build_site.locale_href(base, "en")):
            errors.append(f"{locale['id']} x-default is not English")
        if found.get(locale["html_lang"]) != expected_canonical:
            errors.append(f"{locale['id']} hreflang does not point at itself")
        for other in build_site.LOCALES:
            if found.get(other["html_lang"]) != build_site.absolute(origin, build_site.locale_href(base, other["id"])):
                errors.append(f"{locale['id']} hreflang for {other['id']} is wrong")
                break
        if locale["native"] not in text:
            errors.append(f"{locale['id']} is missing its native name")
        for other in build_site.LOCALES:
            if other["native"] not in text:
                errors.append(f"{locale['id']} language list is missing {other['native']}")
                break
        feature_title, feature_body = readme["features"][0]
        visible_body = feature_body.replace("`", "")
        visible_page = text.replace("<code>", "").replace("</code>", "")
        if feature_title not in text or visible_body[:40] not in visible_page:
            errors.append(f"{locale['id']} is missing README feature text")
        if readme["download_heading"] not in text or readme["features_heading"] not in text:
            errors.append(f"{locale['id']} is missing a README heading")
        if readme["copyright"][0] not in text:
            errors.append(f"{locale['id']} is missing the copyright line")
        for url in assets.values():
            if text.count(url) < 1:
                errors.append(f"{locale['id']} is missing {url}")
        if deb_block not in text or archive_block not in text:
            errors.append(f"{locale['id']} install commands do not match v{version}")
        if re.search(r"apt install (?!\./)", text):
            errors.append(f"{locale['id']} has an apt install that is not a local file")
        if "1.1.2" in text or "Mint" in text:
            errors.append(f"{locale['id']} still mentions 1.1.2 or Mint")
        parser = Balancer()
        parser.feed(text)
        parser.close()
        errors.extend(f"{locale['id']}: {item}" for item in parser.errors)
        if parser.scripts != ["application/ld+json"]:
            errors.append(f"{locale['id']} scripts are {parser.scripts}")
        schema = json.loads(re.search(r'<script type="application/ld\+json">(.*?)</script>', text).group(1))
        if schema["softwareVersion"] != version or schema["inLanguage"] != locale["html_lang"]:
            errors.append(f"{locale['id']} schema version or language is wrong")
        if schema["downloadUrl"] != [assets["macos"], assets["windows"], assets["linux"], assets["deb"]]:
            errors.append(f"{locale['id']} schema downloads are wrong")
        for href in re.findall(r'(?:href|src)="([^"]+)"', text):
            if href.startswith(("http://", "https://", "mailto:")):
                continue
            if not (href.startswith(f"{base}/") or href.startswith("#")):
                errors.append(f"{locale['id']} has a link outside the base path: {href}")
                continue
            target = local_target(out, base, href)
            if target is not None and not target.is_file():
                errors.append(f"{locale['id']} link does not resolve: {href}")
            fragment = href.split("#", 1)[1] if "#" in href else ""
            if fragment and fragment not in parser.ids:
                errors.append(f"{locale['id']} fragment does not exist: {fragment}")
        for external in re.findall(r'href="(https://[^"]+)"', text):
            allowed = (
                external in assets.values()
                or external.startswith(f"{build_site.REPO}/blob/main/README")
                or external in {build_site.REPO, f"{build_site.REPO}/releases", f"{build_site.REPO}/releases/tag/v{version}"}
                or external.startswith("https://en.wikipedia.org/wiki/Total_Annihilation")
            )
            if external.startswith(f"{origin}{base}/") or external == f"{origin}{base}/":
                allowed = True
            if not allowed:
                errors.append(f"{locale['id']} unexpected external link {external}")
        if names["deb"] not in text or names["macos"] not in text:
            errors.append(f"{locale['id']} is missing a download filename")

    if len(descriptions) != len(build_site.LOCALES) or len(titles) != len(build_site.LOCALES):
        errors.append("titles or descriptions are not localized")

    not_found = (out / "404.html").read_text(encoding="utf-8")
    if '<html lang="en">' not in not_found or 'content="noindex"' not in not_found:
        errors.append("404 page is missing lang or noindex")
    if re.search(r"http-equiv\s*=\s*[\"']refresh", not_found, re.I):
        errors.append("404 page redirects")
    for locale in build_site.LOCALES:
        if locale["native"] not in not_found or build_site.locale_href(base, locale["id"]) not in not_found:
            errors.append(f"404 page is missing {locale['id']}")
            break

    robots = (out / "robots.txt").read_text(encoding="utf-8")
    sitemap_url = build_site.absolute(origin, build_site.site_path(base, "sitemap.xml"))
    if f"Sitemap: {sitemap_url}" not in robots or "Allow: /" not in robots:
        errors.append("robots.txt is missing the project sitemap")
    root = ElementTree.parse(out / "sitemap.xml").getroot()
    namespace = {"s": "http://www.sitemaps.org/schemas/sitemap/0.9", "x": "http://www.w3.org/1999/xhtml"}
    locs = [node.text for node in root.findall("s:url/s:loc", namespace)]
    if locs != [build_site.absolute(origin, build_site.locale_href(base, locale["id"])) for locale in build_site.LOCALES]:
        errors.append(f"sitemap URLs are {locs}")
    if set(locs) != expected_urls:
        errors.append("sitemap is missing a locale")
    for url_node in root.findall("s:url", namespace):
        alternates = {
            link.get("hreflang"): link.get("href")
            for link in url_node.findall("x:link", namespace)
        }
        if alternates.get("x-default") != build_site.absolute(origin, build_site.locale_href(base, "en")):
            errors.append("sitemap x-default is wrong")
        if set(alternates) != {locale["html_lang"] for locale in build_site.LOCALES} | {"x-default"}:
            errors.append("sitemap hreflang set is incomplete")
            break
    return errors


def check_downloads(version: str) -> list[str]:
    errors = []
    for name, url in build_site.release_assets(version).items():
        request = urllib.request.Request(
            url,
            method="GET",
            headers={"User-Agent": "galaxy-editor-site-test", "Range": "bytes=0-0"},
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                status = response.status
        except urllib.error.HTTPError as exc:
            status = exc.code
        if status not in (200, 206):
            errors.append(f"{name} download returned {status}: {url}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Test the generated Galaxy Editor site.")
    parser.add_argument("--check-downloads", action="store_true")
    parser.add_argument("--base", default=BASE)
    parser.add_argument("--origin", default=ORIGIN)
    args = parser.parse_args(argv)
    root = build_site.repo_root()
    version = build_site.read_version(root)
    base = build_site.normalize_base(args.base)
    with tempfile.TemporaryDirectory(prefix="galaxy-site-") as temporary:
        out = Path(temporary) / "_site"
        build_site.build(root, out, base, args.origin.rstrip("/"))
        errors = check_structure(out, base, args.origin.rstrip("/"), version)
    if args.check_downloads:
        errors.extend(check_downloads(version))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        print(f"{len(errors)} site check(s) failed", file=sys.stderr)
        return 1
    print(f"Site checks passed for {len(build_site.LOCALES)} locales at {base or '/'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
