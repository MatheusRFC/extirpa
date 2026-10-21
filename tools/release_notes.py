# SPDX-License-Identifier: MIT
"""Texto da release do GitHub, usado pelo workflow de release.

    python3 tools/release_notes.py <versão>              só confere se há texto para a versão
    python3 tools/release_notes.py <versão> --out ARQ    grava o texto (com o SHA256SUMS de dist/)

O texto vem de docs/releases/v<versão>.md, se existir; senão, da seção [<versão>] do CHANGELOG.md.
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def texto(v):
    p = os.path.join(RAIZ, "docs", "releases", f"v{v}.md")
    if os.path.exists(p):
        return open(p, encoding="utf-8").read().strip()
    log = open(os.path.join(RAIZ, "CHANGELOG.md"), encoding="utf-8").read()
    m = re.search(r"^## \[%s\][^\n]*\n(.*?)(?=^## \[|\Z)" % re.escape(v), log, re.S | re.M)
    if not m or not m.group(1).strip():
        sys.exit(f"sem texto para a release {v}: crie docs/releases/v{v}.md ou a seção [{v}] no CHANGELOG.md")
    return m.group(1).strip()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    v = sys.argv[1]
    corpo = texto(v)
    if "--out" in sys.argv:
        somas = open(os.path.join(RAIZ, "dist", "SHA256SUMS"), encoding="utf-8").read().strip()
        corpo += "\n\n### SHA-256\n```\n" + somas + "\n```\n"
        with open(sys.argv[sys.argv.index("--out") + 1], "w", encoding="utf-8", newline="\n") as f:
            f.write(corpo)
    print(f"texto da release {v}: {len(corpo)} caracteres")


if __name__ == "__main__":
    main()
