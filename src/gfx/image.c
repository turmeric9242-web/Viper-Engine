#include "gfx/image.h"
#include "platform/fs.h"
#include "core/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#define MAX_DIM 16384

static unsigned le16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned le32(const unsigned char *p) { return le16(p) | (le16(p + 2) << 16); }
static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

/* ============================================================ inflate (RFC 1951) ============ */
#define MAXBITS 15
typedef struct { short *count, *symbol; } Huff;
typedef struct {
    const unsigned char *in; size_t inlen, incnt; int bitbuf, bitcnt;
    unsigned char *out; size_t outlen, outcap; int err;
} Inf;

static int bits(Inf *s, int need) {
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) { s->err = 1; return 0; }
        val |= (long)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int decode(Inf *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -10;
}

static int construct(Huff *h, const short *length, int n) {
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) { left <<= 1; left -= h->count[len]; if (left < 0) return left; }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++) if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static int codes(Inf *s, const Huff *lc, const Huff *dc) {
    static const short lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    int symbol;
    do {
        symbol = decode(s, lc);
        if (symbol < 0) return symbol;
        if (symbol < 256) {
            if (s->outlen >= s->outcap) return -1;
            s->out[s->outlen++] = (unsigned char)symbol;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return -10;
            int len = lens[symbol] + bits(s, lext[symbol]);
            symbol = decode(s, dc);
            if (symbol < 0) return symbol;
            if (symbol >= 30) return -10;
            size_t dist = (size_t)dists[symbol] + (size_t)bits(s, dext[symbol]);
            if (s->err) return -1;
            if (dist > s->outlen) return -11;
            if (s->outlen + (size_t)len > s->outcap) return -1;
            while (len--) { s->out[s->outlen] = s->out[s->outlen - dist]; s->outlen++; }
        }
    } while (symbol != 256);
    return 0;
}

static int stored(Inf *s) {
    s->bitbuf = 0; s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return 2;
    unsigned len = s->in[s->incnt++]; len |= (unsigned)s->in[s->incnt++] << 8;
    unsigned nlen = s->in[s->incnt++]; nlen |= (unsigned)s->in[s->incnt++] << 8;
    if ((len ^ 0xFFFFu) != nlen) return -2;
    if (s->incnt + len > s->inlen) return 2;
    if (s->outlen + len > s->outcap) return -1;
    memcpy(s->out + s->outlen, s->in + s->incnt, len);
    s->incnt += len; s->outlen += len;
    return 0;
}

static int fixed(Inf *s) {
    static int virgin = 1;
    static short lencnt[MAXBITS + 1], lensym[288], distcnt[MAXBITS + 1], distsym[30];
    static Huff lc, dc;
    if (virgin) {
        short lengths[288]; int sym;
        lc.count = lencnt; lc.symbol = lensym; dc.count = distcnt; dc.symbol = distsym;
        for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < 288; sym++) lengths[sym] = 8;
        construct(&lc, lengths, 288);
        for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
        construct(&dc, lengths, 30);
        virgin = 0;
    }
    return codes(s, &lc, &dc);
}

static int dynamic(Inf *s) {
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    short lengths[320], lencnt[MAXBITS + 1], lensym[288], distcnt[MAXBITS + 1], distsym[30];
    Huff lc = {lencnt, lensym}, dc = {distcnt, distsym};
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4, index;
    if (s->err) return -1;
    if (nlen > 286 || ndist > 30) return -3;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (construct(&lc, lengths, 19) != 0) return -4;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lc);
        if (symbol < 0) return symbol;
        if (symbol < 16) lengths[index++] = (short)symbol;
        else {
            int len = 0;
            if (symbol == 16) { if (index == 0) return -5; len = lengths[index - 1]; symbol = 3 + bits(s, 2); }
            else if (symbol == 17) symbol = 3 + bits(s, 3);
            else symbol = 11 + bits(s, 7);
            if (s->err) return -1;
            if (index + symbol > nlen + ndist) return -6;
            while (symbol--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -9;
    int err = construct(&lc, lengths, nlen);
    if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return -7;
    err = construct(&dc, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return -8;
    return codes(s, &lc, &dc);
}

static int inflate_buf(const unsigned char *in, size_t inlen, unsigned char *out, size_t outcap, size_t *outlen) {
    Inf s; memset(&s, 0, sizeof s);
    s.in = in; s.inlen = inlen; s.out = out; s.outcap = outcap;
    int last, type, err;
    do {
        last = bits(&s, 1); type = bits(&s, 2);
        if (s.err) return -1;
        err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (err) return err;
    } while (!last);
    *outlen = s.outlen;
    return 0;
}

/* ============================================================ PNG ======================== */
static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}
static int getsample(const unsigned char *row, int i, int depth) {
    if (depth == 8) return row[i];
    if (depth == 16) return row[i * 2];                 /* high byte only */
    int per = 8 / depth, shift = 8 - depth * (i % per + 1);
    return (row[i / per] >> shift) & ((1 << depth) - 1);
}

unsigned char *image_decode_png(const unsigned char *d, size_t n, int *ow, int *oh) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n < 33 || memcmp(d, sig, 8) != 0) return NULL;
    unsigned w = 0, h = 0; int depth = 0, ctype = 0, interlace = 0, npal = 0, have_hdr = 0;
    unsigned char pal[256 * 4];
    for (int i = 0; i < 256; i++) { pal[i * 4] = pal[i * 4 + 1] = pal[i * 4 + 2] = 0; pal[i * 4 + 3] = 255; }
    unsigned char *idat = NULL; size_t idat_len = 0, idat_cap = 0;
    for (size_t p = 8; p + 12 <= n;) {
        unsigned len = be32(d + p);
        if ((size_t)len > n - p - 12) break;
        const unsigned char *type = d + p + 4, *body = d + p + 8;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(body); h = be32(body + 4); depth = body[8]; ctype = body[9]; interlace = body[12];
            if (body[10] != 0 || body[11] != 0) goto fail;
            have_hdr = 1;
        } else if (!memcmp(type, "PLTE", 4)) {
            npal = (int)(len / 3); if (npal > 256) npal = 256;
            for (int i = 0; i < npal; i++) memcpy(pal + i * 4, body + i * 3, 3);
        } else if (!memcmp(type, "tRNS", 4) && ctype == 3) {
            for (unsigned i = 0; i < len && i < 256; i++) pal[i * 4 + 3] = body[i];
        } else if (!memcmp(type, "IDAT", 4)) {
            if (idat_len + len > idat_cap) {
                idat_cap = (idat_len + len) * 2 + 1024;
                unsigned char *t = (unsigned char *)realloc(idat, idat_cap);
                if (!t) goto fail;
                idat = t;
            }
            memcpy(idat + idat_len, body, len); idat_len += len;
        } else if (!memcmp(type, "IEND", 4)) break;
        p += 12 + (size_t)len;
    }
    if (!have_hdr || !idat || idat_len < 3) goto fail;
    if (w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM) goto fail;
    if (interlace) { VLOG("PNG: interlaced files are not supported - re-save without interlacing"); goto fail; }
    int channels;
    switch (ctype) {
        case 0: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) goto fail; break;
        case 2: channels = 3; if (depth != 8 && depth != 16) goto fail; break;
        case 3: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8) goto fail; break;
        case 4: channels = 2; if (depth != 8 && depth != 16) goto fail; break;
        case 6: channels = 4; if (depth != 8 && depth != 16) goto fail; break;
        default: goto fail;
    }
    {
        size_t bpp = (size_t)channels * depth, stride = (w * bpp + 7) / 8, rawsize = (stride + 1) * h;
        size_t bp = bpp / 8 ? bpp / 8 : 1;
        if ((idat[0] & 15) != 8) goto fail;                          /* zlib: deflate only */
        unsigned char *raw = (unsigned char *)malloc(rawsize);
        unsigned char *out = (unsigned char *)malloc((size_t)w * h * 4);
        size_t got = 0;
        if (!raw || !out || inflate_buf(idat + 2, idat_len - 2, raw, rawsize, &got) != 0 || got < rawsize) {
            free(raw); free(out); goto fail;
        }
        for (unsigned y = 0; y < h; y++) {
            unsigned char *row = raw + y * (stride + 1), *cur = row + 1;
            const unsigned char *prev = y ? raw + (y - 1) * (stride + 1) + 1 : NULL;
            int ft = row[0];
            if (ft > 4) { free(raw); free(out); goto fail; }
            for (size_t i = 0; i < stride; i++) {
                int a = i >= bp ? cur[i - bp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bp) ? prev[i - bp] : 0;
                int x = cur[i];
                switch (ft) {
                    case 1: x += a; break;
                    case 2: x += b; break;
                    case 3: x += (a + b) >> 1; break;
                    case 4: x += paeth(a, b, c); break;
                }
                cur[i] = (unsigned char)x;
            }
            int maxv = depth == 16 ? 255 : (1 << depth) - 1;
            unsigned char *o = out + (size_t)y * w * 4;
            for (unsigned x = 0; x < w; x++, o += 4) {
                int r, g, b, a = 255;
                if (ctype == 3) {
                    int idx = getsample(cur, (int)x, depth);
                    if (idx >= npal) idx = 0;
                    r = pal[idx * 4]; g = pal[idx * 4 + 1]; b = pal[idx * 4 + 2]; a = pal[idx * 4 + 3];
                } else if (ctype == 0) { r = g = b = getsample(cur, (int)x, depth) * 255 / maxv; }
                else if (ctype == 4) { r = g = b = getsample(cur, (int)x * 2, depth); a = getsample(cur, (int)x * 2 + 1, depth); }
                else {
                    r = getsample(cur, (int)x * channels, depth); g = getsample(cur, (int)x * channels + 1, depth);
                    b = getsample(cur, (int)x * channels + 2, depth);
                    if (ctype == 6) a = getsample(cur, (int)x * 4 + 3, depth);
                }
                o[0] = (unsigned char)r; o[1] = (unsigned char)g; o[2] = (unsigned char)b; o[3] = (unsigned char)a;
            }
        }
        free(raw); free(idat);
        *ow = (int)w; *oh = (int)h;
        return out;
    }
fail:
    free(idat);
    return NULL;
}

/* ============================================================ BMP ======================== */
static unsigned char *bmp_decode(const unsigned char *d, size_t n, int *ow, int *oh) {
    if (n < 54 || d[0] != 'B' || d[1] != 'M') return NULL;
    unsigned off = le32(d + 10), dib = le32(d + 14);
    if (dib < 40) return NULL;
    int w = (int)le32(d + 18), h = (int)le32(d + 22), bpp = (int)le16(d + 28);
    unsigned comp = le32(d + 30), ncol = le32(d + 46);
    int topdown = h < 0; if (topdown) h = -h;
    if (w <= 0 || h <= 0 || w > MAX_DIM || h > MAX_DIM || (comp != 0 && comp != 3)) return NULL;
    if (bpp != 8 && bpp != 24 && bpp != 32) return NULL;
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    if ((size_t)off > n || stride * (size_t)h > n - off) return NULL;
    const unsigned char *pal = d + 14 + dib; unsigned pn = 0;
    if (bpp == 8) { pn = ncol ? ncol : 256; if (pn > 256 || (size_t)(pal - d) + pn * 4 > n) return NULL; }
    unsigned char *out = (unsigned char *)malloc((size_t)w * h * 4);
    if (!out) return NULL;
    for (int y = 0; y < h; y++) {
        const unsigned char *src = d + off + stride * (size_t)(topdown ? y : h - 1 - y);
        unsigned char *o = out + (size_t)y * w * 4;
        for (int x = 0; x < w; x++, o += 4) {
            if (bpp == 8) { unsigned i = src[x]; if (i >= pn) i = 0; o[0] = pal[i * 4 + 2]; o[1] = pal[i * 4 + 1]; o[2] = pal[i * 4]; }
            else { const unsigned char *p = src + x * (bpp / 8); o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; }
            o[3] = 255;
        }
    }
    *ow = w; *oh = h;
    return out;
}

/* ============================================================ TGA ======================== */
static unsigned char *tga_decode(const unsigned char *d, size_t n, int *ow, int *oh) {
    if (n < 18) return NULL;
    int idlen = d[0], cmap = d[1], type = d[2], w = (int)le16(d + 12), h = (int)le16(d + 14), bpp = d[16], desc = d[17];
    if (type != 2 && type != 3 && type != 10 && type != 11) return NULL;
    int bpx = bpp / 8, rle = type >= 9;
    if (w <= 0 || h <= 0 || w > MAX_DIM || h > MAX_DIM) return NULL;
    if (!((type == 2 || type == 10) ? (bpp == 24 || bpp == 32) : bpp == 8)) return NULL;
    size_t off = 18 + (size_t)idlen;
    if (cmap) off += (size_t)le16(d + 5) * (size_t)((d[7] + 7) / 8);
    if (off >= n) return NULL;
    unsigned char *out = (unsigned char *)malloc((size_t)w * h * 4);
    if (!out) return NULL;
    size_t total = (size_t)w * h, i = 0; const unsigned char *s = d + off, *end = d + n;
    while (i < total) {
        int cnt = (int)(total - i), run = 0;
        if (rle) { if (s >= end) goto bad; unsigned char hd = *s++; cnt = (hd & 127) + 1; run = hd & 128; }
        const unsigned char *px = s;
        if (run) { if (s + bpx > end) goto bad; s += bpx; }
        for (int k = 0; k < cnt && i < total; k++, i++) {
            if (!run) { if (s + bpx > end) goto bad; px = s; s += bpx; }
            size_t x = i % (size_t)w, y = i / (size_t)w;
            if (!(desc & 0x20)) y = (size_t)h - 1 - y;
            unsigned char *o = out + (y * (size_t)w + x) * 4;
            if (bpx == 1) { o[0] = o[1] = o[2] = px[0]; o[3] = 255; }
            else { o[0] = px[2]; o[1] = px[1]; o[2] = px[0]; o[3] = bpx == 4 ? px[3] : 255; }
        }
    }
    *ow = w; *oh = h;
    return out;
bad:
    free(out);
    return NULL;
}

/* ============================================================ file loading =============== */
static unsigned char *read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (64L << 20)) { fclose(f); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f);
    *n = (size_t)sz;
    return b;
}

unsigned char *image_load(const char *path, int *w, int *h) {
    size_t n = 0; unsigned char *d = read_file(path, &n);
    if (!d) return NULL;
    unsigned char *img = NULL;
    if (n > 8 && d[0] == 0x89 && d[1] == 'P') img = image_decode_png(d, n, w, h);
    else if (n > 2 && d[0] == 'B' && d[1] == 'M') img = bmp_decode(d, n, w, h);
    else img = tga_decode(d, n, w, h);
    free(d);
    if (!img) VLOG("could not decode image %s (PNG, BMP and TGA are supported)", path);
    return img;
}

/* ============================================================ built-in textures ========== */
#define TS 64      /* all built-ins are 64x64 and tile seamlessly */
static unsigned hsh(unsigned x, unsigned y, unsigned seed) {
    unsigned h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float rnd(int x, int y, int seed) { return (float)(hsh((unsigned)(x & (TS - 1)), (unsigned)(y & (TS - 1)), (unsigned)seed) & 0xFFFF) / 65535.0f; }
static float vnoise(int x, int y, int cell, int seed) {   /* tileable value noise */
    int n = TS / cell, ix = x / cell, iy = y / cell;
    float fx = (float)(x % cell) / (float)cell, fy = (float)(y % cell) / (float)cell;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = (float)(hsh((unsigned)(ix % n), (unsigned)(iy % n), (unsigned)seed) & 0xFFFF) / 65535.0f;
    float b = (float)(hsh((unsigned)((ix + 1) % n), (unsigned)(iy % n), (unsigned)seed) & 0xFFFF) / 65535.0f;
    float c = (float)(hsh((unsigned)(ix % n), (unsigned)((iy + 1) % n), (unsigned)seed) & 0xFFFF) / 65535.0f;
    float d = (float)(hsh((unsigned)((ix + 1) % n), (unsigned)((iy + 1) % n), (unsigned)seed) & 0xFFFF) / 65535.0f;
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static void put(unsigned char *p, float r, float g, float b) {
    float v[3] = {r, g, b};
    for (int i = 0; i < 3; i++) { float c = v[i] < 0 ? 0 : (v[i] > 1 ? 1 : v[i]); p[i] = (unsigned char)(c * 255.0f + 0.5f); }
    p[3] = 255;
}

static const char *BUILTIN[] = {"grass", "dirt", "stone", "brick", "wood", "sand", "metal", "tiles", "checker"};
int image_builtin_count(void) { return (int)(sizeof BUILTIN / sizeof BUILTIN[0]); }
const char *image_builtin_name(int i) { return (i >= 0 && i < image_builtin_count()) ? BUILTIN[i] : NULL; }

unsigned char *image_builtin(const char *name, int *w, int *h) {
    int id = -1;
    for (int i = 0; i < image_builtin_count(); i++) if (!strcmp(name, BUILTIN[i])) id = i;
    if (id < 0) return NULL;
    unsigned char *img = (unsigned char *)malloc(TS * TS * 4);
    if (!img) return NULL;
    for (int y = 0; y < TS; y++) for (int x = 0; x < TS; x++) {
        unsigned char *p = img + (y * TS + x) * 4;
        float fine = rnd(x, y, 3);
        switch (id) {
        case 0: { float t = 0.5f * vnoise(x, y, 16, 2) + 0.3f * vnoise(x, y, 8, 1) + 0.2f * fine;
                  put(p, mixf(0.16f, 0.36f, t), mixf(0.38f, 0.64f, t), mixf(0.12f, 0.22f, t)); break; }
        case 1: { float s = 0.8f + 0.35f * (0.5f * vnoise(x, y, 8, 4) + 0.5f * fine);
                  if (rnd(x, y, 5) > 0.95f) s *= 0.6f;
                  put(p, 0.42f * s, 0.30f * s, 0.19f * s); break; }
        case 2: { float t = 0.6f * vnoise(x, y, 16, 7) + 0.25f * vnoise(x, y, 4, 8) + 0.15f * fine, g = 0.38f + 0.3f * t;
                  put(p, g * 0.98f, g, g * 1.04f); break; }
        case 3: { int row = y / 16, xs = (x + (row & 1) * 16) & (TS - 1);
                  if ((y % 16) < 2 || (xs % 32) < 2) { float m = 0.74f + 0.1f * fine; put(p, m, m * 0.97f, m * 0.92f); }
                  else { float v = 0.85f + 0.3f * rnd(xs / 32, row, 6), k = (0.9f + 0.2f * fine) * v;
                         put(p, 0.65f * k, 0.27f * k, 0.2f * k); }
                  break; }
        case 4: { int plank = x / 16, px = x % 16;
                  if (px < 1) { put(p, 0.2f, 0.12f, 0.06f); break; }
                  float grain = sinf((float)y * 6.2831853f * 3.0f / TS + (float)plank * 2.3f + vnoise(x, y, 16, 11) * 4.0f) * 0.5f + 0.5f;
                  float s = 0.8f + 0.28f * grain + 0.06f * fine, pv = 0.92f + 0.16f * rnd(plank, 0, 12);
                  put(p, 0.62f * s * pv, 0.43f * s * pv, 0.24f * s * pv); break; }
        case 5: { float s = (0.92f + 0.16f * fine) * (0.96f + 0.08f * vnoise(x, y, 8, 9));
                  put(p, 0.85f * s, 0.77f * s, 0.55f * s); break; }
        case 6: { float s = 0.55f + 0.15f * rnd(0, y, 13) + 0.06f * rnd(x, y, 14);
                  if ((x % 32) == 0 || (y % 32) == 0) s *= 0.55f;
                  put(p, s * 0.96f, s, s * 1.06f); break; }
        case 7: { int tx = x / 16, ty = y / 16;
                  if ((x % 16) == 0 || (y % 16) == 0) put(p, 0.45f, 0.45f, 0.43f);
                  else { float s = 0.93f + 0.1f * rnd(tx, ty, 15) + 0.02f * fine; put(p, 0.9f * s, 0.9f * s, 0.85f * s); }
                  break; }
        default: { float v = (((x / 8) + (y / 8)) & 1) ? 0.85f : 0.35f; put(p, v, v, v); break; }
        }
    }
    *w = TS; *h = TS;
    return img;
}

/* ============================================================ texture lookup ============= */
int image_texture_name_ok(const char *name) {
    size_t n = name ? strlen(name) : 0;
    if (n == 0 || n > 31) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == ' ' || c == '.' || c == '(' || c == ')')) return 0;
    }
    return strstr(name, "..") == NULL;
}

static const char *const TEX_EXTS[] = {"png", "bmp", "tga", NULL};

unsigned char *image_load_texture(const char *name, int *w, int *h) {
    if (!image_texture_name_ok(name)) return NULL;
    for (int i = 0; TEX_EXTS[i]; i++) {
        char path[96]; snprintf(path, sizeof path, "textures/%s.%s", name, TEX_EXTS[i]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fclose(f);
        unsigned char *img = image_load(path, w, h);
        if (img) return img;
    }
    return image_builtin(name, w, h);
}

int image_list_textures(char names[][32], int max) {
    int n = 0;
    for (int i = 0; i < image_builtin_count() && n < max; i++) { strncpy(names[n], BUILTIN[i], 31); names[n][31] = 0; n++; }
    char files[64][32];
    int nf = fs_list("textures", TEX_EXTS, files, 64);
    for (int i = 0; i < nf && n < max; i++) {
        int dup = 0;
        for (int j = 0; j < n; j++) if (!strcmp(names[j], files[i])) dup = 1;
        if (!dup && image_texture_name_ok(files[i])) { memcpy(names[n], files[i], 32); n++; }
    }
    return n;
}
