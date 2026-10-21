# SPDX-License-Identifier: MIT
"""Compila o EXTIRPA. Só precisa do LLVM (clang, lld-link, llvm-rc e llvm-dlltool),
no Windows ou no Linux: não usa SDK do Windows, MinGW nem biblioteca C.

    python3 tools/build.py          # build/extirpa.exe
    python3 tools/build.py test     # testes do núcleo (com o compilador C do sistema)
    python3 tools/build.py selftest # build/extirpa-selftest.exe (autoteste: --selftest)
    python3 tools/build.py all      # testes, o executável e o de autoteste

Variáveis de ambiente para apontar ferramentas fora do PATH: CLANG, LLD_LINK, LLVM_RC,
LLVM_DLLTOOL (ou LLVM_LIB) e, para os testes, CC.
"""
import os
import re
import shutil
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(RAIZ, "build")
WIN32_H = os.path.join(RAIZ, "src", "win", "win32.h")
DLLS = ["kernel32", "user32", "gdi32", "comctl32", "shell32", "ole32", "advapi32", "comdlg32", "uxtheme"]

CFLAGS = [
    "--target=x86_64-pc-windows-msvc", "-std=c11", "-O2", "-ffreestanding", "-fno-stack-protector",
    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DUNICODE", "-D_UNICODE",
]


def achar(nomes, env):
    if os.environ.get(env):
        return os.environ[env]
    for n in nomes:
        p = shutil.which(n)
        if p:
            return p
    # versões numeradas do Ubuntu (clang-18, lld-link-18...)
    for n in nomes:
        for v in range(30, 13, -1):
            p = shutil.which(f"{n}-{v}")
            if p:
                return p
    return None


def rodar(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        print(" ".join(cmd))
        print(r.stdout + r.stderr)
        sys.exit(r.returncode)
    return r.stdout


def gerar_defs():
    """Uma lista de exportações (.def) por DLL, a partir das seções de win32.h."""
    texto = open(WIN32_H, encoding="utf-8").read().split("// ---------------------------------------------------------------- funções")[1]
    defs, atual = {}, None
    for linha in texto.splitlines():
        m = re.match(r"^// (\w+)\s*$", linha)
        if m and m.group(1) in DLLS:
            atual = m.group(1)
            continue
        m = re.match(r"^W32IMP [^(]*?\b(\w+)\(", linha)
        if m and atual:
            defs.setdefault(atual, []).append(m.group(1))
    return defs


def libs():
    dlltool = achar(["llvm-dlltool"], "LLVM_DLLTOOL")
    llvmlib = None if dlltool else achar(["llvm-lib"], "LLVM_LIB")
    if not dlltool and not llvmlib:
        sys.exit("llvm-dlltool (ou llvm-lib) não encontrado")
    pasta = os.path.join(BUILD, "lib")
    os.makedirs(pasta, exist_ok=True)
    saida = []
    for dll, funcs in gerar_defs().items():
        d = os.path.join(pasta, dll + ".def")
        with open(d, "w") as f:
            f.write(f"LIBRARY {dll}.dll\nEXPORTS\n")
            for fn in funcs:
                f.write(f"{fn}\n")
        lib = os.path.join(pasta, dll + ".lib")
        if dlltool:
            rodar([dlltool, "-m", "i386:x86-64", "-d", d, "-l", lib])
        else:
            rodar([llvmlib, f"/def:{d}", f"/out:{lib}", "/machine:x64"])
        saida.append(lib)
    return saida


def fontes():
    lista = []
    for sub in ("core", "win"):
        p = os.path.join(RAIZ, "src", sub)
        lista += [os.path.join(p, f) for f in sorted(os.listdir(p)) if f.endswith(".c")]
    return lista


def build_exe(selftest=False):
    """selftest=True gera build/extirpa-selftest.exe: o mesmo programa mais o autoteste
    (src/win/selftest.c, opção --selftest), para conferir num Windows de verdade."""
    clang = achar(["clang"], "CLANG")
    lld = achar(["lld-link"], "LLD_LINK")
    rc = achar(["llvm-rc"], "LLVM_RC")
    for nome, p in (("clang", clang), ("lld-link", lld), ("llvm-rc", rc)):
        if not p:
            sys.exit(f"{nome} não encontrado (instale o LLVM)")
    pasta_obj = os.path.join(BUILD, "obj-selftest" if selftest else "obj")
    os.makedirs(pasta_obj, exist_ok=True)
    objs = []
    for c in fontes():
        if os.path.basename(c) == "selftest.c" and not selftest:
            continue
        o = os.path.join(pasta_obj, os.path.basename(os.path.dirname(c)) + "_" + os.path.basename(c)[:-2] + ".obj")
        rodar([clang] + CFLAGS + (["-DEXTIRPA_SELFTEST"] if selftest else []) + ["-c", c, "-o", o])
        objs.append(o)
    res = os.path.join(BUILD, "app.res")
    rodar([rc, "/nologo", "/FO", res, "app.rc"], cwd=os.path.join(RAIZ, "assets"))
    exe = os.path.join(BUILD, "extirpa-selftest.exe" if selftest else "extirpa.exe")
    # Versão de subsistema 6.0 (o padrão): com 10.0 o Windows recusa o executável na partida
    # (0xc000007b, conferido no Windows Server 2025). As APIs usadas já exigem o Windows 10.
    rodar([lld, "/nologo", "/subsystem:windows,6.0", "/entry:WinMainCRTStartup", "/nodefaultlib", "/opt:ref", "/opt:icf",
           "/dynamicbase", "/nxcompat", "/highentropyva", "/guard:no", f"/out:{exe}"] + objs + libs() + [res])
    print(f"{os.path.relpath(exe, RAIZ)}: {os.path.getsize(exe)} bytes")
    return exe


# Módulos da parte Windows testados contra o Registro e o disco simulados (tests/fakewin.c)
WIN_TESTADOS = ["platform", "reg", "fsx", "programs", "scan", "startup", "monitor", "uninst", "i18n", "appx"]


def compilar_e_rodar(cc, nome, fontes, extra, env=None):
    exe = os.path.join(BUILD, nome + (".exe" if os.name == "nt" else ""))
    base = [cc, "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror"] + extra + fontes + ["-o", exe]
    # com sanitizers quando o compilador e o sistema têm (Linux); sem eles nos outros casos
    r = subprocess.run(base[:1] + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"] + base[1:], capture_output=True, text=True)
    if r.returncode != 0:
        rodar(base)
    r = subprocess.run([exe], env=dict(os.environ, **(env or {})))
    if r.returncode != 0:
        sys.exit(r.returncode)


def build_tests():
    cc = os.environ.get("CC") or achar(["cc", "gcc", "clang"], "CC")
    if not cc:
        sys.exit("compilador C não encontrado para os testes")
    os.makedirs(BUILD, exist_ok=True)
    core = [os.path.join(RAIZ, "src", "core", f) for f in sorted(os.listdir(os.path.join(RAIZ, "src", "core"))) if f.endswith(".c")]
    compilar_e_rodar(cc, "test_core", core + [os.path.join(RAIZ, "tests", "test_core.c")], [])
    if os.name != "nt":
        # o Windows simulado usa malloc_usable_size (glibc); vaza memória de propósito a cada
        # cenário novo, por isso o detector de vazamentos fica desligado
        win = [os.path.join(RAIZ, "src", "win", m + ".c") for m in WIN_TESTADOS]
        testes = [os.path.join(RAIZ, "tests", f) for f in ("fakewin.c", "test_win.c")]
        compilar_e_rodar(cc, "test_win", core + win + testes, ["-DEXTIRPA_FAKE_WIN", "-Wno-unused-parameter"], {"ASAN_OPTIONS": "detect_leaks=0"})


def main():
    alvo = sys.argv[1] if len(sys.argv) > 1 else "exe"
    if alvo in ("test", "all"):
        build_tests()
    if alvo in ("exe", "all"):
        build_exe()
    if alvo in ("selftest", "all"):
        build_exe(selftest=True)


if __name__ == "__main__":
    main()
