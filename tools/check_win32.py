# SPDX-License-Identifier: MIT
"""Confere src/win/win32.h contra os cabeçalhos oficiais do mingw-w64.

    python3 tools/check_win32.py --mingw <pasta mingw-w64-headers>

A pasta é a "mingw-w64-headers" do código-fonte do mingw-w64
(https://github.com/mingw-w64/mingw-w64), com as subpastas include/ e crt/.
Só é usada nesta conferência: o EXTIRPA não precisa dela para compilar.

O que é conferido:
1. Funções: o win32.h (só as funções) é compilado depois dos cabeçalhos do mingw-w64.
   Qualquer diferença de assinatura vira erro de "conflicting types".
2. Estruturas e constantes: um arquivo com sizeof/offsetof de cada campo e o valor de
   cada constante é compilado duas vezes (com o win32.h e com o mingw-w64) para o alvo
   x86_64 do Windows, e os dois resultados têm de ser iguais byte a byte.
3. GUIDs das pastas conhecidas: comparados com o knownfolders.h.

Precisa de clang e llvm-objcopy.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIN32_H = os.path.join(RAIZ, "src", "win", "win32.h")

MINGW_INCLUDES = """
#define NO_STRICT
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define _WIN32_IE 0x0A00
#define NTDDI_VERSION 0x0A000000
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <srrestoreptapi.h>
#include <uxtheme.h>
#include <knownfolders.h>
#include <psapi.h>
"""

# Constantes que são ponteiros (não dá para comparar como número num inicializador)
PULA = {"WINAPI", "CALLBACK", "W32IMP", "EXTIRPA_WIN32_H", "W32_GUID", "INVALID_HANDLE_VALUE", "HWND_TOPMOST", "HWND_BROADCAST", "EXTIRPA_FAKE_WIN"}


def secao_tipos(texto):
    return texto.split("#endif  // W32_CHECK_FUNCS_ONLY")[0]


def constantes(texto):
    nomes = []
    for m in re.finditer(r"^#define (\w+)\s+(.+)$", texto, re.M):
        nome, valor = m.group(1), m.group(2)
        if nome in PULA or re.search(r"\b(HKEY|HANDLE|HWND|LPWSTR|MAKEINTRESOURCEW)\b", valor):
            continue
        nomes.append(nome)
    for corpo in re.findall(r"typedef enum \w+ \{([^}]*)\}", texto):
        for item in corpo.split(","):
            item = item.strip()
            if item:
                nomes.append(item.split("=")[0].strip())
    return nomes


def estruturas(texto):
    """[(nome_do_tipo, [campos])] de cada typedef struct com corpo."""
    saida = []
    for m in re.finditer(r"typedef struct (\w+) \{(.*?)\}\s*(\w+)", texto, re.S):
        corpo, nome = m.group(2), m.group(3)
        campos = []
        for decl in corpo.split(";"):
            decl = decl.strip()
            if not decl:
                continue
            partes = [p.strip() for p in decl.split(",")]
            for i, p in enumerate(partes):
                p = re.sub(r"\[[^\]]*\]", "", p).replace("*", " ").strip()
                if i == 0:
                    campos.append(p.split()[-1])
                else:
                    campos.append(p.split()[-1])
        saida.append((nome, campos))
    return saida


def guids(texto):
    return {m.group(1): [int(x, 16) for x in m.group(2).split(",")] for m in re.finditer(r"W32_GUID\((FOLDERID_\w+),\s*([^)]*)\)", texto)}


def guids_mingw(mingw):
    txt = open(os.path.join(mingw, "include", "knownfolders.h"), encoding="utf-8", errors="replace").read()
    saida = {}
    for m in re.finditer(r"DEFINE_KNOWN_FOLDER\s*\(\s*(FOLDERID_\w+)\s*,([^)]*)\)", txt):
        saida[m.group(1)] = [int(x.strip(), 16) for x in m.group(2).split(",")]
    return saida


def preparar_mingw(mingw, tmp):
    """Pasta de includes: include/ + crt/ + o _mingw.h gerado a partir do _mingw.h.in."""
    extra = os.path.join(tmp, "gerado")
    os.makedirs(extra, exist_ok=True)
    src = open(os.path.join(mingw, "crt", "_mingw.h.in"), encoding="utf-8").read()
    open(os.path.join(extra, "_mingw.h"), "w", encoding="utf-8").write(src.replace("@DEFAULT_MSVCRT_VERSION@", "0xE00"))
    return ["-isystem", extra, "-isystem", os.path.join(mingw, "include"), "-isystem", os.path.join(mingw, "crt")]


def clang(args, cwd):
    r = subprocess.run(["clang", "--target=x86_64-w64-windows-gnu", "-std=c11", "-fms-extensions", "-Wno-pragma-pack"] + args,
                       cwd=cwd, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mingw", required=True, help="pasta mingw-w64-headers")
    a = ap.parse_args()
    texto = open(WIN32_H, encoding="utf-8").read()
    tipos = secao_tipos(texto)
    falhas = 0
    tmp = tempfile.mkdtemp(prefix="check_win32_")
    try:
        inc = preparar_mingw(a.mingw, tmp)

        # 1. assinaturas das funções
        f = os.path.join(tmp, "funcoes.c")
        open(f, "w").write(MINGW_INCLUDES + "#define W32_CHECK_FUNCS_ONLY\n#include \"%s\"\n" % WIN32_H.replace("\\", "/"))
        rc, out = clang(inc + ["-fsyntax-only", "-ferror-limit=0", f], tmp)
        erros = [l for l in out.splitlines() if "error" in l]
        if rc != 0:
            falhas += len(erros) or 1
            print("FUNÇÕES: diferenças de assinatura")
            print(out)
        else:
            # uma função que o mingw-w64 não declara passaria sem ser comparada
            nomes = re.findall(r"^W32IMP [^(]*?\b(\w+)\(", texto, re.M)
            todos = ""
            for base in ("include", "crt"):
                for arq in os.listdir(os.path.join(a.mingw, base)):
                    if arq.endswith(".h"):
                        todos += open(os.path.join(a.mingw, base, arq), encoding="utf-8", errors="replace").read()
            # declarada direto, ou por um apelido (#define EnumProcesses K32EnumProcesses)
            def declarada(n):
                if re.search(r"\b%s\s*\(" % n, todos):
                    return True
                m = re.search(r"#define\s+(\w+)\s+%s\b" % n, todos)
                return bool(m and re.search(r"\b%s\s*\(" % m.group(1), todos))
            faltam = [n for n in nomes if not declarada(n)]
            for n in faltam:
                falhas += 1
                print(f"FUNÇÃO SEM DECLARAÇÃO NO MINGW-W64 (não conferida): {n}")
            print(f"funções: {len(nomes) - len(faltam)} de {len(nomes)} assinaturas iguais às do mingw-w64")

        # 2. estruturas e constantes
        itens = []
        for nome in constantes(tipos):
            itens.append((f"constante {nome}", f"(unsigned long long)({nome})"))
        for nome, campos in estruturas(tipos):
            itens.append((f"sizeof({nome})", f"sizeof({nome})"))
            for c in campos:
                itens.append((f"offsetof({nome}, {c})", f"offsetof({nome}, {c})"))
        corpo = "#include <stddef.h>\n%s\n__attribute__((used, section(\".chk\"))) const unsigned long long chk[] = {\n%s\n};\n"
        lista = ",\n".join(e for _, e in itens)
        dumps = {}
        for modo, cab in (("mine", '#include "%s"\n' % WIN32_H.replace("\\", "/")), ("mingw", MINGW_INCLUDES)):
            c = os.path.join(tmp, f"layout_{modo}.c")
            o = os.path.join(tmp, f"layout_{modo}.o")
            open(c, "w").write(corpo % (cab, lista))
            rc, out = clang((inc if modo == "mingw" else []) + ["-c", c, "-o", o], tmp)
            if rc != 0:
                print(f"LAYOUT ({modo}): não compilou")
                print(out)
                return 1
            b = os.path.join(tmp, f"layout_{modo}.bin")
            subprocess.check_call(["llvm-objcopy", "-O", "binary", "--only-section=.chk", o, b])
            dados = open(b, "rb").read()
            dumps[modo] = [int.from_bytes(dados[i:i + 8], "little") for i in range(0, len(dados), 8)]
        ok = 0
        for i, (desc, _) in enumerate(itens):
            x, y = dumps["mine"][i], dumps["mingw"][i]
            if x != y:
                falhas += 1
                print(f"DIFERENTE: {desc}: win32.h={x:#x} mingw-w64={y:#x}")
            else:
                ok += 1
        print(f"estruturas e constantes: {ok} de {len(itens)} iguais")

        # 3. GUIDs
        meus, deles = guids(tipos), guids_mingw(a.mingw)
        for nome, v in meus.items():
            if deles.get(nome) != v:
                falhas += 1
                print(f"GUID DIFERENTE: {nome}")
        print(f"GUIDs: {len(meus)} conferidos")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("OK" if not falhas else f"{falhas} problema(s)")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
