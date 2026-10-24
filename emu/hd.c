/* SPDX-License-Identifier: GPL-3.0-only
 * The emulator's sharp screen: every canvas drawn a second time at HD_X x (build/emu/gfx_hd.c, made from
 * firmware/src/gfx.c by emu/mk_gfx_hd.py, calls these next to the device's own drawing):
 *   rectangles scaled, rounded corners anti-aliased at the HD radius, text and keycaps from the HD tables
 *   (emu/gen_hd.py) at the device's pens and line tops, so the layout is the device's to the pixel;
 *   what the UI writes into cv_px by hand (the logo) is scaled up smoothly from the device's pixels.
 * hd_scr (lx_emu_web.c) is the 720 x 720 screen the page shows; hd_y0 .. hd_y1 the rows changed. */
static uint16_t hd_cv[CV_MAX * HD_X * HD_X];
static uint8_t hd_mark[CV_MAX];                   /* device canvas pixels drawn by an HD primitive */

static void hd_begin(uint32_t w, uint32_t h, uint16_t s)
{
    uint32_t i, n = w * HD_X * h * HD_X;
    for (i = 0; i < n; i++)
        hd_cv[i] = s;
    for (i = 0; i < w * h; i++)
        hd_mark[i] = 0;
}
static void hd_markr(int32_t x, int32_t y, int32_t w, int32_t h)   /* device canvas coordinates (cv_oy applied) */
{
    int32_t i, j, x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int32_t)cv_w) x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h) y1 = (int32_t)cv_h;
    for (j = y; j < y1; j++)
        for (i = x; i < x1; i++)
            hd_mark[(uint32_t)j * cv_w + (uint32_t)i] = 1;
}
static void hd_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t sc)   /* HD canvas coordinates */
{
    int32_t i, j, x1 = x + w, y1 = y + h, W = (int32_t)(cv_w * HD_X), H = (int32_t)(cv_h * HD_X);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (j = y; j < y1; j++)
        for (i = x; i < x1; i++)
            hd_cv[j * W + i] = sc;
}
static void hd_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    y += cv_oy;
    hd_fill(x * HD_X, y * HD_X, w * HD_X, h * HD_X, swap16(c));
    hd_markr(x, y, w, h);
}
static uint32_t hd_cov(int32_t r, int32_t i, int32_t j)   /* as gfx.c rr_cov, at any radius: 0..16 */
{
    int32_t sx, sy, n = 0;
    for (sy = 0; sy < 4; sy++)
        for (sx = 0; sx < 4; sx++) {
            int32_t dx = r * 8 - ((i * 4 + sx) * 2 + 1), dy = r * 8 - ((j * 4 + sy) * 2 + 1);
            n += dx * dx + dy * dy <= r * r * 64;
        }
    return (uint32_t)n;
}
static void hd_pset(int32_t x, int32_t y, uint16_t sc)
{
    int32_t W = (int32_t)(cv_w * HD_X);
    if ((uint32_t)x < cv_w * HD_X && (uint32_t)y < cv_h * HD_X)
        hd_cv[y * W + x] = sc;
}
/* the four corners of a rounded rectangle (its straight parts come from the cv_rect calls of cv_rrect) */
static void hd_corners(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t under)
{
    int32_t R = r * HD_X, i, j, X0, Y0, X1, Y1;
    y += cv_oy;
    X0 = x * HD_X; Y0 = y * HD_X; X1 = (x + w) * HD_X - 1; Y1 = (y + h) * HD_X - 1;
    for (j = 0; j < R; j++)
        for (i = 0; i < R; i++) {
            uint32_t a = hd_cov(R, i, j);
            uint16_t px = swap16(a >= 16u ? c : a == 0u ? under : ux_mix(under, c, (int32_t)(a * 100u / 16u)));
            hd_pset(X0 + i, Y0 + j, px);
            hd_pset(X1 - i, Y0 + j, px);
            hd_pset(X0 + i, Y1 - j, px);
            hd_pset(X1 - i, Y1 - j, px);
        }
    hd_markr(x, y, r, r);
    hd_markr(x + w - r, y, r, r);
    hd_markr(x, y + h - r, r, r);
    hd_markr(x + w - r, y + h - r, r, r);
}
/* w x h nibbles at HD canvas (X, Y), the text clip (cv_cy0 .. cv_cy1) scaled */
static void hd_alpha(int32_t X, int32_t Y, uint32_t w, uint32_t h, const uint8_t *d, const uint16_t *rv)
{
    uint32_t k = 0, gx, gy, W = cv_w * HD_X;
    int32_t c0 = cv_cy0 * HD_X, c1 = cv_cy1 * HD_X;
    for (gy = 0; gy < h; gy++) {
        int32_t py = Y + (int32_t)gy;
        if (py < c0 || py >= c1) {
            k += w;
            continue;
        }
        for (gx = 0; gx < w; gx++, k++) {
            uint32_t v = (d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t px = X + (int32_t)gx;
            if (v && (uint32_t)px < W)
                hd_cv[(uint32_t)py * W + (uint32_t)px] = rv[v];
        }
    }
}
/* glyph ci of f with its pen at pen16 (1/16 device px from the canvas' left), line top y; (gx, gy, bw, bh) the
 * device's glyph box (marked) */
static void hd_glyph(const aafont_t *f, uint32_t ci, int32_t pen16, int32_t y, const uint16_t *rv,
                     int32_t gx, int32_t gy, int32_t bw, int32_t bh)
{
    const hdf_t *hf = f == &AF_S ? &HD_S : f == &AF_M ? &HD_M : &HD_L;
    const hdg_t *g = &hf->g[ci];
    int32_t X, Y;
    hd_markr(gx, gy + cv_oy, bw, bh);
    if (!g->bw)
        return;
    X = ((pen16 * HD_X + 8) >> 4) + g->bx;
    Y = (y + cv_oy + f->asc) * HD_X - hf->asc + g->by;
    hd_alpha(X, Y, g->bw, g->bh, hf->data + g->off, rv);
}
static void hd_keycap(int32_t x, int32_t y, uint32_t id, int32_t w, const uint16_t *rv)
{
    int32_t X = x * HD_X + (w * HD_X - (int32_t)HKC[id].w) / 2, Y = (y + cv_oy) * HD_X;
    hd_alpha(X, Y, HKC[id].w, HKC_H, HKC_DATA + HKC[id].off, rv);
    hd_markr(x, y + cv_oy, w, KC_H);
}

/* the canvas rows r0 .. onto the HD screen at device (x, y + r0); what no HD primitive drew (the logo, written
 * into cv_px by hand) is scaled up from the device's pixels, bilinear */
static uint32_t hd_rgb(uint16_t sc) { uint32_t c = swap16(sc); return ((c >> 11) << 3) << 16 | (((c >> 5) & 63u) << 2) << 8 | (c & 31u) << 3; }
static void hd_blit(uint32_t x, uint32_t y, uint32_t r0)
{
    uint32_t W = cv_w * HD_X, i, j, a, b, bgs = swap16(cv_bg);
    if (r0 >= cv_h)
        return;
    for (j = r0 * HD_X; j < cv_h * HD_X; j++) {
        uint32_t sy = (y * HD_X + j);
        if (sy >= HW)
            break;
        for (i = 0; i < W && x * HD_X + i < HW; i++)
            hd_scr[sy * HW + x * HD_X + i] = hd_cv[j * W + i];
    }
    for (j = r0; j < cv_h; j++)
        for (i = 0; i < cv_w; i++) {
            uint32_t p = j * cv_w + i;
            if (hd_mark[p] || cv_px[p] == bgs)
                continue;
            for (b = 0; b < HD_X; b++)
                for (a = 0; a < HD_X; a++) {
                    int32_t u = (int32_t)(i * HD_X + a) * 2 + 1 - HD_X, v = (int32_t)(j * HD_X + b) * 2 + 1 - HD_X;   /* 1/(2X) px */
                    int32_t u0 = u < 0 ? 0 : u / (2 * HD_X), v0 = v < 0 ? 0 : v / (2 * HD_X);
                    int32_t fu = u < 0 ? 0 : (u % (2 * HD_X)) * 256 / (2 * HD_X), fv = v < 0 ? 0 : (v % (2 * HD_X)) * 256 / (2 * HD_X);
                    int32_t u1 = u0 + 1 < (int32_t)cv_w ? u0 + 1 : u0, v1 = v0 + 1 < (int32_t)cv_h ? v0 + 1 : v0;
                    uint32_t c00 = hd_rgb(cv_px[v0 * cv_w + u0]), c10 = hd_rgb(cv_px[v0 * cv_w + u1]);
                    uint32_t c01 = hd_rgb(cv_px[v1 * cv_w + u0]), c11 = hd_rgb(cv_px[v1 * cv_w + u1]), out = 0, k;
                    uint32_t sy = y * HD_X + j * HD_X + b, sx = x * HD_X + i * HD_X + a;
                    for (k = 0; k < 24u; k += 8u) {
                        int32_t t = (int32_t)((c00 >> k) & 255u) * (256 - fu) + (int32_t)((c10 >> k) & 255u) * fu;
                        int32_t m = (int32_t)((c01 >> k) & 255u) * (256 - fu) + (int32_t)((c11 >> k) & 255u) * fu;
                        out |= (uint32_t)(((t * (256 - fv) + m * fv) >> 16) & 255) << k;
                    }
                    if (sy < HW && sx < HW)
                        hd_scr[sy * HW + sx] = swap16(RGB(out >> 16, (out >> 8) & 255u, out & 255u));
                }
        }
    hd_dirty(y * HD_X + r0 * HD_X, (y + cv_h) * HD_X);
}
