# SPDX-License-Identifier: MIT
"""Gera os ícones do EXTIRPA a partir do zero (precisa do Pillow: pip install pillow).

    python3 tools/gerar_icones.py

Desenho-mestre: assets/extirpa.svg (mesmas medidas, grade de 128).

A ideia: extirpar é arrancar pela raiz. O programa (a janela branca, com a seta para
cima) sai inteiro do chão, e as raízes vêm junto, desenhadas como trilhas de circuito
com os pontos de solda na ponta. Embaixo fica só o buraco no chão: nenhuma sobra.

Cria:
  assets/extirpa.ico                  ícone do extirpa.exe (16 a 256 px), embutido pelo app.rc
  assets/extirpa-32.png, -48.png, -128.png, -256.png   README e Notion
"""
import os

from PIL import Image, ImageDraw

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERMELHO = (0xD6, 0x45, 0x45, 255)  # #d64545
BRANCO = (0xFF, 0xFF, 0xFF, 255)


def icone(tamanho):
    """Mesmas medidas de assets/extirpa.svg. Desenha em 8x e reduz, para as bordas ficarem
    suaves até em 16 px."""
    s = tamanho * 8
    k = s / 128
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=round(28 * k), fill=VERMELHO)

    def linha(pontos, largura, cor):
        pts = [(x * k, y * k) for x, y in pontos]
        w = largura * k
        d.line(pts, fill=cor, width=round(w), joint="curve")
        for x, y in pts:  # pontas e cantos arredondados
            d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=cor)

    def ponto(x, y, r, cor):
        d.ellipse([(x - r) * k, (y - r) * k, (x + r) * k, (y + r) * k], fill=cor)

    # raízes (trilhas de circuito) saindo de baixo do programa
    linha([(64, 46), (64, 92)], 7, BRANCO)
    linha([(50, 46), (50, 64), (38, 76), (38, 90)], 7, BRANCO)
    linha([(78, 46), (78, 64), (90, 76), (90, 90)], 7, BRANCO)
    for x, y in ((64, 96), (38, 94), (90, 94)):
        ponto(x, y, 7, BRANCO)
        ponto(x, y, 3, VERMELHO)
    # o programa arrancado: uma janela branca com a seta para cima
    d.rounded_rectangle([34 * k, 12 * k, 94 * k, 50 * k], radius=round(9 * k), fill=BRANCO)
    linha([(52, 37), (64, 25), (76, 37)], 7, VERMELHO)
    # o chão, com o buraco de onde ele saiu
    linha([(16, 112), (44, 112)], 6, BRANCO)
    linha([(84, 112), (112, 112)], 6, BRANCO)
    return img.resize((tamanho, tamanho), Image.LANCZOS)


def main():
    a = os.path.join(RAIZ, "assets")
    os.makedirs(a, exist_ok=True)
    tamanhos = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    imagens = [icone(t) for t in tamanhos]
    imagens[-1].save(os.path.join(a, "extirpa.ico"), format="ICO", sizes=[(t, t) for t in tamanhos], append_images=imagens[:-1])
    for t in (32, 48, 128, 256):
        icone(t).save(os.path.join(a, f"extirpa-{t}.png"))
    print("ícones gerados")


if __name__ == "__main__":
    main()
