"""Renders a README through GitHub's own markdown API and screenshots it in a
GitHub-dark page, with the README's relative images served from a local folder.

Usage: python3 assets/src/preview_readme.py <readme.md> <assets_base_dir> <repo context owner/name> <out.png>
"""
import json
import subprocess
import sys
from pathlib import Path

from playwright.sync_api import sync_playwright

CSS = "https://cdn.jsdelivr.net/npm/github-markdown-css@5.8.1/github-markdown-dark.css"


def main(readme, base, context, out):
    md = Path(readme).read_text()
    payload = json.dumps({"text": md, "mode": "gfm", "context": context})
    html = subprocess.run(["gh", "api", "markdown", "--input", "-"], input=payload, capture_output=True,
                          text=True, check=True).stdout
    page = f"""<!doctype html><html><head><meta charset="utf-8"><base href="file://{Path(base).resolve()}/">
<link rel="stylesheet" href="{CSS}">
<style>body{{background:#0d1117;margin:0}} .markdown-body{{box-sizing:border-box;max-width:1012px;margin:0 auto;padding:32px}}</style>
</head><body><article class="markdown-body">{html}</article></body></html>"""
    tmp = Path(out).with_suffix(".html")
    tmp.write_text(page)
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": 1100, "height": 900})
        pg.goto(f"file://{tmp.resolve()}")
        pg.wait_for_timeout(1500)
        pg.screenshot(path=out, full_page=True)
        b.close()
    print("wrote", out)


if __name__ == "__main__":
    main(*sys.argv[1:5])
