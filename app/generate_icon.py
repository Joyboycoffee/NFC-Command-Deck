import math
from PIL import Image, ImageDraw, ImageFilter

def make_pro_icon(size):
    # Render at 4x for super-sampled anti-aliasing
    scale = 4
    dim = size * scale
    img = Image.new("RGBA", (dim, dim), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    pad = int(dim * 0.05)
    w = dim - pad * 2
    h = dim - pad * 2
    radius = int(dim * 0.22)

    # 1. Outer squircle shadow / glow
    shadow_pad = pad - int(dim * 0.02)
    shadow_dim = dim - shadow_pad * 2
    shadow = Image.new("RGBA", (dim, dim), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(shadow)
    sdraw.rounded_rectangle(
        [shadow_pad, shadow_pad, shadow_pad + shadow_dim, shadow_pad + shadow_dim],
        radius=radius + int(dim * 0.02),
        fill=(14, 165, 233, 40)
    )
    shadow = shadow.filter(ImageFilter.GaussianBlur(int(dim * 0.03)))
    img = Image.alpha_composite(img, shadow)
    draw = ImageDraw.Draw(img)

    # 2. Main squircle background - deep obsidian graphite
    base_rect = [pad, pad, pad + w, pad + h]
    draw.rounded_rectangle(base_rect, radius=radius, fill=(15, 18, 25, 255))

    # Inner bevel / subtle gradient highlight
    inner_pad = pad + max(1, int(dim * 0.015))
    inner_w = dim - inner_pad * 2
    inner_radius = max(2, radius - int(dim * 0.015))
    draw.rounded_rectangle(
        [inner_pad, inner_pad, inner_pad + inner_w, inner_pad + inner_w],
        radius=inner_radius,
        outline=(30, 41, 59, 200),
        width=max(1, int(dim * 0.015))
    )

    # Top highlight line
    draw.rounded_rectangle(
        base_rect,
        radius=radius,
        outline=(56, 189, 248, 140),
        width=max(1, int(dim * 0.02))
    )

    # 3. Card Chip & Waves
    cx = dim * 0.52
    cy = dim * 0.50

    radii = [dim * 0.30, dim * 0.20, dim * 0.10]
    widths = [max(2, int(dim * 0.045)), max(2, int(dim * 0.042)), max(2, int(dim * 0.038))]
    colors = [
        (14, 165, 233, 255),  # Electric cyan
        (56, 189, 248, 255),  # Sky cyan
        (224, 242, 254, 255)   # Ice blue
    ]

    for r, width, col in zip(radii, widths, colors):
        bbox = [cx - r, cy - r, cx + r, cy + r]
        draw.arc(bbox, start=130, end=230, fill=col, width=width)

    # Center transmitter core dot
    core_r = max(2, int(dim * 0.035))
    innermost_r = radii[-1]
    dot_x = cx - innermost_r
    draw.ellipse([dot_x - core_r, cy - core_r, dot_x + core_r, cy + core_r], fill=(255, 255, 255, 255))

    # Contactless card frame accent on right
    card_w = int(dim * 0.32)
    card_h = int(dim * 0.44)
    card_x = int(dim * 0.56)
    card_y = int(cy - card_h / 2)
    card_r = max(2, int(dim * 0.04))
    
    draw.rounded_rectangle(
        [card_x, card_y, card_x + card_w, card_y + card_h],
        radius=card_r,
        fill=(22, 29, 43, 200),
        outline=(56, 189, 248, 220),
        width=max(1, int(dim * 0.02))
    )
    
    # Gold microchip on card
    chip_w = int(card_w * 0.40)
    chip_h = int(card_h * 0.28)
    chip_x = card_x + int(card_w * 0.20)
    chip_y = card_y + int(card_h * 0.22)
    draw.rounded_rectangle(
        [chip_x, chip_y, chip_x + chip_w, chip_y + chip_h],
        radius=max(1, int(dim * 0.015)),
        fill=(245, 158, 11, 240),
        outline=(251, 191, 36, 255),
        width=max(1, int(dim * 0.01))
    )
    draw.line([chip_x + chip_w // 2, chip_y, chip_x + chip_w // 2, chip_y + chip_h], fill=(180, 83, 9, 255), width=max(1, int(dim * 0.008)))
    draw.line([chip_x, chip_y + chip_h // 2, chip_x + chip_w, chip_y + chip_h // 2], fill=(180, 83, 9, 255), width=max(1, int(dim * 0.008)))

    return img.resize((size, size), Image.Resampling.LANCZOS)

if __name__ == "__main__":
    big = make_pro_icon(256)
    sizes = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (96, 96), (128, 128), (256, 256)]
    big.save("app.ico", format="ICO", sizes=sizes)
    print("app.ico successfully generated with full Windows icon suite!")
