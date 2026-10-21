# SPDX-License-Identifier: MIT
"""Gera os arquivos da release em dist/.

    python3 scripts/package.py [--skip-build]

- Compila com tools/build.py all (testes + extirpa.exe), a menos que --skip-build.
- Confere se a versão é a mesma em src/win/platform.h e assets/app.rc.
- Saídas:
    extirpa.exe                          o executável, para baixar direto
    extirpa-<versão>-windows-amd64.zip   extirpa.exe + README.md + LICENSE
    SHA256SUMS
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(RAIZ, "dist")


def ler(rel):
    return open(os.path.join(RAIZ, rel), encoding="utf-8").read()


def versao():
    v = re.search(r'#define EXTIRPA_VERSION u"([^"]+)"', ler("src/win/platform.h")).group(1)
    rc = ler("assets/app.rc")
    num = ",".join(v.split(".") + ["0"])
    erros = []
    if f"FILEVERSION {num}" not in rc or f"PRODUCTVERSION {num}" not in rc:
        erros.append("FILEVERSION/PRODUCTVERSION")
    if f'"FileVersion", "{v}"' not in rc or f'"ProductVersion", "{v}"' not in rc:
        erros.append("FileVersion/ProductVersion")
    if erros:
        sys.exit(f"versão {v} (platform.h) diferente em assets/app.rc: {', '.join(erros)}")
    return v


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-build", action="store_true")
    a = ap.parse_args()
    v = versao()
    if not a.skip_build:
        subprocess.check_call([sys.executable, os.path.join(RAIZ, "tools", "build.py"), "all"])
    exe = os.path.join(RAIZ, "build", "extirpa.exe")
    if not os.path.exists(exe):
        sys.exit("build/extirpa.exe não existe: rode sem --skip-build")
    shutil.rmtree(DIST, ignore_errors=True)
    os.makedirs(DIST)
    saidas = []
    destino = os.path.join(DIST, "extirpa.exe")
    shutil.copy2(exe, destino)
    saidas.append(destino)
    zpath = os.path.join(DIST, f"extirpa-{v}-windows-amd64.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for nome, origem in (("extirpa.exe", exe), ("README.md", "README.md"), ("LICENSE", "LICENSE")):
            z.write(os.path.join(RAIZ, origem), f"extirpa-{v}/{nome}")
    saidas.append(zpath)
    with open(os.path.join(DIST, "SHA256SUMS"), "w", newline="\n") as f:
        for p in saidas:
            f.write(f"{sha256(p)}  {os.path.basename(p)}\n")
    for p in saidas + [os.path.join(DIST, "SHA256SUMS")]:
        print(f"{os.path.getsize(p):>10}  {os.path.relpath(p, RAIZ)}")


if __name__ == "__main__":
    main()
