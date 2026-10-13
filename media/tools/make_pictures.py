# make_pictures.py: the pictures of Juraic Field that are drawn by calculation (no picture-making AI).
#   background/field-1p-silhouette.png   828 x 1656, a deformed person as a silhouette with a light edge
#   background/field-2p-juraic.png       828 x 1656, the character Juraic Vaid (source/juraic-vaid-original.webp,
#                                        white ground cut away) on a dark ground
#   background/stage-grid.png            1920 x 1080, a floor of light lines under a dark sky
# It writes them into background/ beside tools/. The package keeps them one folder further down, by the place
# they are for: background/1p, background/2p, background/other.
# Run with a Python that has numpy, scipy and Pillow.
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
out = os.path.join(root, "background")
os.makedirs(out, exist_ok=True)
rng = np.random.default_rng(20261010)

FW, FH = 828, 1656
KEEP_COLOUR, LIGHT = 0.35, 0.34   # Juraic Vaid on the 2P field: how much of its colour and of its light is kept


def gradient(w, h, top, bottom):
    t = np.linspace(0.0, 1.0, h)[:, None, None]
    a = np.array(top, dtype=np.float32)[None, None, :] * (1 - t) + np.array(bottom, dtype=np.float32)[None, None, :] * t
    return np.repeat(a, w, axis=1)


def glow(w, h, cx, cy, rx, ry, colour, strength):
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
    return np.exp(-d)[:, :, None] * np.array(colour, dtype=np.float32)[None, None, :] * strength


def grid_lines(w, h, step, colour, alpha, width=2):
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for x in range(step // 2, w, step):
        d.line([(x, 0), (x, h)], fill=colour + (alpha,), width=width)
    for y in range(step // 2, h, step):
        d.line([(0, y), (w, y)], fill=colour + (alpha,), width=width)
    return layer


def to_image(a):
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGB")


def dither(a):
    return a + rng.uniform(-0.6, 0.6, a.shape).astype(np.float32)


# ---------------------------------------------------------------- 2P: Juraic Vaid
def cut_out(path):
    src = Image.open(path).convert("RGB")
    a = np.array(src).astype(np.int16)
    white = a.min(axis=2) >= 236
    # the white ground is what reaches the picture's edge; the white is narrowed first, so that it does not run in
    # through a gap in the outline (the muzzle is white and its line is open under the chin)
    narrow = ndimage.binary_erosion(white, iterations=5, border_value=1)
    labels, _ = ndimage.label(narrow)
    edge = np.unique(np.concatenate([labels[0, :], labels[-1, :], labels[:, 0], labels[:, -1]]))
    ground = ndimage.binary_dilation(np.isin(labels, edge[edge != 0]), iterations=7) & white
    # all the white that reaches the edge without the narrowing: what it has more is the slits between the feathers
    # (ground, small) and the white of the body it ran into (large, kept)
    full, _ = ndimage.label(white)
    edge = np.unique(np.concatenate([full[0, :], full[-1, :], full[:, 0], full[:, -1]]))
    more, count = ndimage.label(np.isin(full, edge[edge != 0]) & ~ground)
    sizes = ndimage.sum(np.ones_like(more), more, range(1, count + 1))
    print("kept as body:", [int(v) for v in sizes if v >= 2000])
    ground |= np.isin(more, [i + 1 for i, v in enumerate(sizes) if v < 2000])
    # the white ground shut in between the arm and the tail
    pockets, _ = ndimage.label(white & ~ground)
    for sx, sy in [(491, 707)]:
        if pockets[sy, sx]:
            ground |= pockets == pockets[sy, sx]
            print("pocket at", (sx, sy), "cells", int((pockets == pockets[sy, sx]).sum()))
    body = ndimage.binary_erosion(~ground, iterations=1)
    alpha = ndimage.gaussian_filter(body.astype(np.float32), 0.8)
    rgba = np.dstack([a.astype(np.uint8), (np.clip(alpha, 0, 1) * 255).astype(np.uint8)])
    ys, xs = np.where(body)
    return Image.fromarray(rgba, "RGBA").crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))


def field_2p():
    base = gradient(FW, FH, (9, 11, 20), (13, 11, 22))
    base += glow(FW, FH, FW * 0.5, FH * 0.36, FW * 0.75, FH * 0.36, (16, 22, 34), 0.5)
    img = to_image(dither(base)).convert("RGBA")
    img.alpha_composite(grid_lines(FW, FH, 138, (120, 140, 170), 12))
    who = cut_out(os.path.join(root, "source", "juraic-vaid-original.webp"))
    # the character is made dark and nearly colourless, so that the coloured pieces in front of it stand out
    # (in its own colours it was in the way of reading the pieces)
    a = np.array(who).astype(np.float32)
    grey = (a[:, :, 0] * 0.3 + a[:, :, 1] * 0.59 + a[:, :, 2] * 0.11)[:, :, None]
    a[:, :, :3] = (grey * (1 - KEEP_COLOUR) + a[:, :, :3] * KEEP_COLOUR) * LIGHT + np.array([2, 5, 12], dtype=np.float32)
    who = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA")
    scale = FW * 0.97 / who.width
    who = who.resize((round(who.width * scale), round(who.height * scale)), Image.LANCZOS)
    # the body (head to feet, 0.30 of the cut-out's width from its left) stands in the middle; the tail runs out of
    # the picture on the right (further left, the figure stood off the middle)
    x = FW // 2 - round(who.width * 0.30)
    who = who.crop((0, 0, FW - x, who.height))
    y = (FH - who.height) // 2 - 30
    halo = Image.new("RGBA", (FW, FH), (0, 0, 0, 0))
    tint = Image.new("RGBA", who.size, (110, 130, 160, 255))
    tint.putalpha(who.getchannel("A"))
    halo.alpha_composite(tint, (x, y))
    halo = halo.filter(ImageFilter.GaussianBlur(14))
    halo.putalpha(halo.getchannel("A").point(lambda v: v * 50 // 255))
    img.alpha_composite(halo)
    img.alpha_composite(who, (x, y))
    img.convert("RGB").save(os.path.join(out, "field-2p-juraic.png"), optimize=True)


# ---------------------------------------------------------------- 1P: a person
def person_mask(w, h, s=2):
    """A standing grown-up, about eight heads tall, seen from the front; a unit is 1 % of the width."""
    m = Image.new("L", (w * s, h * s), 0)
    d = ImageDraw.Draw(m)
    u = w * s / 100.0

    def ell(cx, cy, rx, ry):
        for x in (cx, 100 - cx):
            d.ellipse([(x - rx) * u, (cy - ry) * u, (x + rx) * u, (cy + ry) * u], fill=255)

    def limb(x0, y0, r0, x1, y1, r1):
        for a0, a1 in ((x0, x1), (100 - x0, 100 - x1)):
            ang = np.arctan2(y1 - y0, a1 - a0) + np.pi / 2
            dx, dy = np.cos(ang), np.sin(ang)
            d.polygon([((a0 + dx * r0) * u, (y0 + dy * r0) * u), ((a1 + dx * r1) * u, (y1 + dy * r1) * u),
                       ((a1 - dx * r1) * u, (y1 - dy * r1) * u), ((a0 - dx * r0) * u, (y0 - dy * r0) * u)], fill=255)
        ell(x0, y0, r0, r0)
        ell(x1, y1, r1, r1)

    ell(50, 30, 8.6, 10.8)                                        # head
    d.rectangle([46.4 * u, 38 * u, 53.6 * u, 46 * u], fill=255)   # neck
    d.polygon([(x * u, y * u) for x, y in [(43, 43.5), (57, 43.5), (67, 47.5), (69.5, 53), (64, 72), (61.5, 82), (64.5, 95), (64.5, 102),
                                           (35.5, 102), (35.5, 95), (38.5, 82), (36, 72), (30.5, 53), (33, 47.5)]], fill=255)   # trunk
    limb(33.5, 51, 5.0, 28.5, 77, 4.0)                            # upper arms
    limb(28.5, 77, 4.0, 25.5, 100, 3.2)                           # forearms
    ell(24.8, 106.5, 3.4, 6.0)                                    # hands
    limb(43, 100, 8.0, 42.5, 141, 5.6)                            # thighs
    limb(42.5, 141, 5.6, 42, 181, 3.6)                            # lower legs
    ell(40.5, 186.5, 6.2, 3.2)                                    # feet
    m = m.filter(ImageFilter.GaussianBlur(4 * s)).point(lambda v: 255 if v > 118 else 0).filter(ImageFilter.GaussianBlur(s))
    return m.resize((w, h), Image.LANCZOS)


def field_1p():
    base = gradient(FW, FH, (15, 17, 26), (9, 10, 18))
    base += glow(FW, FH, FW * 0.5, FH * 0.45, FW * 0.7, FH * 0.4, (20, 22, 30), 0.5)
    img = to_image(dither(base)).convert("RGBA")
    img.alpha_composite(grid_lines(FW, FH, 138, (130, 140, 165), 12))
    m = person_mask(FW, FH)
    far = m.filter(ImageFilter.GaussianBlur(18))
    near = m.filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.GaussianBlur(2))
    # the light round the figure is a pale grey, none of the four colours of the pieces (the pink of the first
    # version was in the way of reading them)
    layer = Image.new("RGBA", (FW, FH), (150, 160, 185, 0))
    layer.putalpha(far.point(lambda v: v * 22 // 255))
    img.alpha_composite(layer)
    rim = Image.new("RGBA", (FW, FH), (165, 172, 195, 0))
    rim.putalpha(near.point(lambda v: v * 62 // 255))   # (150 is too strong)
    img.alpha_composite(rim)
    inner = to_image(gradient(FW, FH, (18, 20, 29), (11, 12, 20))).convert("RGBA")
    inner.putalpha(m)
    img.alpha_composite(inner)
    img.convert("RGB").save(os.path.join(out, "field-1p-silhouette.png"), optimize=True)


# ---------------------------------------------------------------- the rest of the screen
def stage():
    W, H, s = 1920, 1080, 2
    horizon = 0.56
    sky = gradient(W, H, (6, 8, 26), (30, 14, 62))
    sky += glow(W, H, W * 0.5, H * horizon, W * 0.55, H * 0.16, (120, 60, 200), 0.6)
    sky += glow(W, H, W * 0.5, H * horizon, W * 0.9, H * 0.035, (80, 200, 255), 0.75)
    ground = np.arange(H)[:, None, None] > H * horizon
    sky = np.where(ground, sky * 0.42 + np.array([4, 6, 18], dtype=np.float32), sky)
    img = to_image(dither(sky)).convert("RGBA")

    layer = Image.new("RGBA", (W * s, H * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    hy = H * horizon * s
    for i in range(1, 400):                      # stars
        x, y = rng.uniform(0, W * s), rng.uniform(0, hy * 0.92)
        r = rng.choice([1, 1, 1, 2, 2, 3])
        d.ellipse([x - r, y - r, x + r, y + r], fill=(200, 220, 255, int(rng.uniform(40, 170))))
    for k in range(-40, 41):                     # lines that run to the horizon
        x_far = W * s * 0.5 + k * W * s * 0.012
        x_near = W * s * 0.5 + k * W * s * 0.16
        d.line([(x_far, hy), (x_near, H * s)], fill=(90, 210, 255, 120), width=3)
    z = 1.0
    while z < 60:                                # lines across, closer together far away
        y = hy + (H * s - hy) / z
        d.line([(0, y), (W * s, y)], fill=(255, 90, 210, int(40 + 110 / z ** 0.5)), width=3)
        z *= 1.28
    layer = layer.resize((W, H), Image.LANCZOS)
    bloom = layer.filter(ImageFilter.GaussianBlur(6))
    img.alpha_composite(bloom)
    img.alpha_composite(layer)
    img.convert("RGB").save(os.path.join(out, "stage-grid.png"), optimize=True)


field_2p()
field_1p()
stage()
print("made field-2p-juraic.png, field-1p-silhouette.png, stage-grid.png in", out)
