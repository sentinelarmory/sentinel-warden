#!/usr/bin/env bash
# Renders the README artwork from the SVG sources in this folder into ../
# Needs rsvg-convert and the Orbitron + Rajdhani fonts (Google Fonts, OFL).
set -euo pipefail
cd "$(dirname "$0")"
rsvg-convert -w 512 logo.svg -o ../logo.png
for f in banner social; do
    [ -f "$f.svg" ] || continue
    python3 -c 'import base64,sys; s=open(sys.argv[1]).read(); d="data:image/png;base64,"+base64.b64encode(open("../logo.png","rb").read()).decode(); open(sys.argv[2],"w").write(s.replace("LOGO_PNG",d))' "$f.svg" "/tmp/$f.rendered.svg"
    rsvg-convert "/tmp/$f.rendered.svg" -o "../$f.png"
    rm "/tmp/$f.rendered.svg"
done
echo "rendered: $(ls ../*.png | xargs -n1 basename | tr '\n' ' ')"
