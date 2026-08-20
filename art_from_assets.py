"""Re-sample the drawings in assets/ into the tables that game.c carries:
the character sheet (src2.png) and the title logo (src_title.png).

    python art_from_assets.py     ->  writes art.c

Paste the result over the "character art" block in game.c. Needs pillow
and numpy; nothing else in the project does, which is why this is a
build time tool and not part of the game.


The sheet was drawn at three screen pixels to the dot, so each frame is
reduced back onto its own grid (the block phase is picked per sprite,
because the sheet does not sit on one global grid), the soft gradients
are folded into a single shared palette, and every frame gets an offset
measured from the actor box so the head stays put and the feet stay on
the floor whatever the pose.  Nothing is redrawn.
"""
from PIL import Image
import numpy as np, os

SP = os.path.dirname(os.path.abspath(__file__))
J = lambda *n: os.path.join(SP, *n)
SRC = J('assets', 'src2.png')     # source material, not shipped

im = Image.open(SRC).convert('RGBA')
A = np.array(im).astype(np.float64)

# ---------------------------------------------------------------- cells
def components():
    op = (A[:, :, 3] >= 200)
    H, W = op.shape
    lab = np.zeros((H, W), np.int32)
    from collections import deque
    out = []
    n = 0
    for y in range(H):
        for x in range(W):
            if op[y, x] and lab[y, x] == 0:
                n += 1
                q = deque([(y, x)]); lab[y, x] = n
                x0 = x1 = x; y0 = y1 = y; cnt = 0
                while q:
                    cy, cx = q.popleft(); cnt += 1
                    x0 = min(x0, cx); x1 = max(x1, cx)
                    y0 = min(y0, cy); y1 = max(y1, cy)
                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        ny, nx = cy + dy, cx + dx
                        if 0 <= ny < H and 0 <= nx < W and op[ny, nx] and lab[ny, nx] == 0:
                            lab[ny, nx] = n; q.append((ny, nx))
                out.append((x0, y0, x1, y1, cnt))
    return out

boxes = components()

big = [b for b in boxes if b[4] > 400]
def is_label(b):
    x0, y0, x1, y1, c = b
    w, h = x1 - x0 + 1, y1 - y0 + 1
    return c / (w * h) > 0.90 and w > 60 and h < 50
cells = [b for b in big if not is_label(b)]
cells.sort(key=lambda b: (b[1] // 45, b[0]))

# ---------------------------------------------------------- downsampling
def downsample(idx, s=3):
    x0, y0, x1, y1, _ = cells[idx]
    pad = s * 2
    X0 = max(0, x0 - pad); Y0 = max(0, y0 - pad)
    X1 = min(A.shape[1], x1 + 1 + pad); Y1 = min(A.shape[0], y1 + 1 + pad)
    sub = A[Y0:Y1, X0:X1]
    best = None
    for py in range(s):
        for pw in range(s):
            ny = (sub.shape[0] - py) // s
            nx = (sub.shape[1] - pw) // s
            if ny < 4 or nx < 4:
                continue
            blk = sub[py:py + ny * s, pw:pw + nx * s].reshape(ny, s, nx, s, 4)
            al = blk[:, :, :, :, 3]
            full = (al > 200).all(1).all(2)
            if full.sum() < 10:
                continue
            sc = blk[:, :, :, :, :3].var(axis=(1, 3)).sum(2)[full].mean()
            if best is None or sc < best[0]:
                best = (sc, blk)
    blk = best[1]
    al = blk[:, :, :, :, 3]
    cov = al.mean(axis=(1, 3)) / 255.0
    tot = al.sum(axis=(1, 3))
    v = (blk[:, :, :, :, :3] * al[..., None]).sum(axis=(1, 3))
    v = np.where(tot[..., None] > 0, v / np.maximum(tot, 1)[..., None], 0)
    out = np.zeros((cov.shape[0], cov.shape[1], 4), np.uint8)
    out[:, :, :3] = np.clip(np.round(v), 0, 255)
    out[:, :, 3] = np.where(cov >= 0.45, 255, 0)
    m = out[:, :, 3] > 0
    ys = np.where(m.any(1))[0]; xs = np.where(m.any(0))[0]
    return out[ys[0]:ys[-1] + 1, xs[0]:xs[-1] + 1]

# frame name -> component index on the sheet
FRAMES = [
    ('IDLE',  0), ('W0', 16), ('W1', 17), ('W2', 18),
    ('W3',   19), ('W4', 20), ('W5', 21),
    ('JUMP', 28), ('FALL', 26), ('HURT', 5),
    ('KICK0', 13), ('KICK1', 15),
    ('P0', 44), ('P1', 45), ('PFLAT', 50),
    ('G0', 56), ('G1', 57), ('GFLAT', 62),
    ('B0', 31), ('B1', 32), ('BFLAT', 40),
]
raw = {name: downsample(i) for name, i in FRAMES}

# ------------------------------------------------------------- palette
# '"', '\', '.' and '?' are kept out: the first two cannot sit in a C
# string, '.' is the transparent dot and '?' would risk a trigraph.
KEY = ''.join(chr(c) for c in range(33, 127) if c not in (34, 92, 46, 63))
N = len(KEY)

X = np.concatenate([f[:, :, :3][f[:, :, 3] > 0] for f in raw.values()]).astype(np.float64)
strip = Image.fromarray(X.reshape(1, -1, 3).astype(np.uint8), 'RGB')
q = strip.quantize(colors=N, method=Image.MEDIANCUT, dither=Image.NONE)
C = np.array(q.getpalette()[:N * 3]).reshape(N, 3).astype(np.float64)
for _ in range(60):                       # k-means refinement
    d = ((X[:, None, :] - C[None, :, :]) ** 2).sum(2)
    lab = d.argmin(1)
    new = C.copy()
    for i in range(N):
        m = lab == i
        if m.any():
            new[i] = X[m].mean(0)
    if np.abs(new - C).max() < 0.3:
        C = new; break
    C = new
PAL = np.clip(np.round(C), 0, 255).astype(np.uint8)
d = ((X[:, None, :] - PAL.astype(float)[None, :, :]) ** 2).sum(2)
err = np.abs(PAL[d.argmin(1)].astype(int) - X).mean()
print(f'palette {N} colours, mean abs error {err:.2f}')

def to_chars(f):
    h, w, _ = f.shape
    out = np.full((h, w), '.', dtype='<U1')
    m = f[:, :, 3] > 0
    if m.any():
        dd = ((f[:, :, :3][m].astype(int)[:, None, :] - PAL.astype(int)[None, :, :]) ** 2).sum(2)
        idx = dd.argmin(1)
        out[m] = [KEY[i] for i in idx]
    return [''.join(r) for r in out]

# --------------------------------------------------------------- anchors
AW, AH = 22, 28

def head_centre(rows):
    """centre of the widest solid run - the head, the one part of the
       character that holds still from pose to pose."""
    best = (0, 0.0)
    for r in rows:
        run = cur = 0; start = bstart = 0
        for i, ch in enumerate(r):
            if ch != '.':
                if cur == 0: start = i
                cur += 1
                if cur > run: run, bstart = cur, start
            else:
                cur = 0
        if run > best[0]:
            best = (run, bstart + run / 2.0)
    return best[1]

AIR = {'JUMP', 'FALL'}
# the kick foot is placed against the kick box, not the body
KICK_OFF = {'KICK0': (16, 3), 'KICK1': (15, -3)}

art = {}
IDLE_H = len(to_chars(raw['IDLE']))
for name, _ in FRAMES:
    rows = to_chars(raw[name])
    h = len(rows); w = len(rows[0])
    if name in KICK_OFF:
        ox, oy = KICK_OFF[name]
    else:
        ox = int(round(AW / 2.0 - head_centre(rows)))
        oy = (AH - IDLE_H) if name in AIR else (AH - h)
    art[name] = (rows, w, h, ox, oy)
    print(f'  {name:6s} {w:2d}x{h:2d}  ox {ox:3d} oy {oy:3d}')

# ------------------------------------------------------------- emit C
o = []
w = o.append
w('/* ------------------------------------------------------------------ */')
w('/*  character art                                                      */')
w('/* ------------------------------------------------------------------ */')
w('/*  the player and the foes come straight off the reference sheet. it')
w('    was drawn at three screen pixels to the dot, so every frame is')
w('    sampled back down onto its own grid and the soft shading folded')
w('    into one shared palette - re-sampled, not redrawn.')
w('')
w('    a frame is one char a pixel: \'.\' is see through and every other')
w('    printable character indexes ART_PAL. each frame also carries the')
w('    offset from the actor box that places it, so the head holds still')
w('    and the feet stay on the floor whatever the pose - which is what')
w('    lets the walk cycle, the jump and the kick share one anchor.    */')
w('')
w('#define ART_NPAL %d' % N)
w('static const unsigned ART_PAL[ART_NPAL] = {')
for i in range(0, N, 6):
    w('    ' + ', '.join('0x%06X' % (int(PAL[j][0]) << 16 | int(PAL[j][1]) << 8 | int(PAL[j][2]))
                         for j in range(i, min(i + 6, N))) + ',')
w('};')
w('/*  ART_PAL[i] is written as ART_KEY[i]; \'"\', \'\\\\\', \'.\' and \'?\' are')
w('    left out - the first two cannot sit in a string, \'.\' is the')
w('    transparent dot and \'?\' would risk a trigraph.                */')
w('static const char ART_KEY[] = "%s";' % KEY.replace('\\', '\\\\').replace('"', '\\"'))
w('')
for name, _ in FRAMES:
    rows, fw, fh, ox, oy = art[name]
    w('static const char *ART_%s[%d] = {' % (name, fh))
    for r in rows:
        w('    "%s",' % r)
    w('};')
w('')
w('typedef struct {')
w('    const char **px;      /* h rows of w characters                */')
w('    short w, h;')
w('    short ox, oy;         /* from the top left of the actor box     */')
w('} Art;')
w('')
w('enum { ' + ', '.join('AF_' + n for n, _ in FRAMES) + ', AF_COUNT };')
w('')
w('static const Art ART[AF_COUNT] = {')
for name, _ in FRAMES:
    rows, fw, fh, ox, oy = art[name]
    w('    { ART_%-6s %2d, %2d, %3d, %3d },' % (name + ',', fw, fh, ox, oy))
w('};')
w('')
w('/*  the three foes, in the order the sheet lays them out. blue comes')
w('    last so the one foe in three that shares the player\'s colour is')
w('    the rarest of them.                                            */')
w('static const short FOE_WALK[3][2] = {')
w('    { AF_P0, AF_P1 }, { AF_G0, AF_G1 }, { AF_B0, AF_B1 }')
w('};')
w('static const short FOE_FLAT[3] = { AF_PFLAT, AF_GFLAT, AF_BFLAT };')
w('')
w('/*  a walk cycle worth of frames, in sheet order.                   */')
w('static const short PLR_WALK[6] = {')
w('    AF_W0, AF_W1, AF_W2, AF_W3, AF_W4, AF_W5')
w('};')

# ------------------------------------------------------- the title logo
# drawn on a far chunkier grid than the characters - about thirteen
# screen pixels to the dot - and it keeps a palette of its own, so
# folding its golds in cannot cost the sprites any colours.
LOGO_W, LOGO_H, LOGO_N = 152, 30, 48

logo = Image.open(J('assets', 'src_title.png')).convert('RGBA')
la = np.array(logo)
lys, lxs = np.where(la[:, :, 3] >= 200)
lc = logo.crop((lxs.min(), lys.min(), lxs.max() + 1, lys.max() + 1))
lr = np.array(lc.resize((LOGO_W, LOGO_H), Image.BOX))
lr[:, :, 3] = np.where(lr[:, :, 3] >= 110, 255, 0)
LX = lr[:, :, :3][lr[:, :, 3] > 0].astype(np.float64)

lstrip = Image.fromarray(LX.reshape(1, -1, 3).astype(np.uint8), 'RGB')
lq = lstrip.quantize(colors=LOGO_N, method=Image.MEDIANCUT, dither=Image.NONE)
LC = np.array(lq.getpalette()[:LOGO_N * 3]).reshape(LOGO_N, 3).astype(np.float64)
for _ in range(60):
    dd = ((LX[:, None, :] - LC[None, :, :]) ** 2).sum(2)
    ll = dd.argmin(1)
    nn = LC.copy()
    for i in range(LOGO_N):
        mm = ll == i
        if mm.any():
            nn[i] = LX[mm].mean(0)
    if np.abs(nn - LC).max() < 0.3:
        LC = nn
        break
    LC = nn
LPAL = np.clip(np.round(LC), 0, 255).astype(np.uint8)
dd = ((LX[:, None, :] - LPAL.astype(float)[None, :, :]) ** 2).sum(2)
print('logo palette %d colours, mean abs error %.2f'
      % (LOGO_N, np.abs(LPAL[dd.argmin(1)].astype(int) - LX).mean()))

lrows = []
for y in range(LOGO_H):
    line = []
    for x in range(LOGO_W):
        if lr[y, x, 3] == 0:
            line.append('.')
        else:
            c = lr[y, x, :3].astype(int)
            line.append(KEY[int(((c[None, :] - LPAL.astype(int)) ** 2).sum(1).argmin())])
    lrows.append(''.join(line))

w('')
w('/* ------------------------------------------------------------------ */')
w('/*  title logo                                                         */')
w('/* ------------------------------------------------------------------ */')
w('/*  the same treatment as the characters, off assets/src_title.png. it')
w('    is drawn on a far chunkier grid - some thirteen screen pixels to')
w('    the dot - so it is held at its own size and blown up by a whole')
w('    number where it is drawn, and it keeps a palette of its own so')
w('    its golds cannot cost the sprites any of theirs.               */')
w('#define LOGO_W %d' % LOGO_W)
w('#define LOGO_H %d' % LOGO_H)
w('#define LOGO_NPAL %d' % LOGO_N)
w('static const unsigned LOGO_PAL[LOGO_NPAL] = {')
for i in range(0, LOGO_N, 6):
    w('    ' + ', '.join('0x%06X' % (int(LPAL[j][0]) << 16 | int(LPAL[j][1]) << 8 | int(LPAL[j][2]))
                         for j in range(i, min(i + 6, LOGO_N))) + ',')
w('};')
w('/*  indexed by ART_KEY, exactly as the character frames are.        */')
w('static const char *LOGO_PX[LOGO_H] = {')
for r in lrows:
    w('    "%s",' % r)
w('};')

src = '\n'.join(o) + '\n'
open(J('art.c'), 'w', encoding='ascii').write(src)
print('wrote art.c  %d lines  %d bytes' % (src.count('\n'), len(src)))
