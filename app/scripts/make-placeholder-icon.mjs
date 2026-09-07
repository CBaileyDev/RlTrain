// Generates a 1024x1024 RGBA PNG placeholder app icon with no image libraries:
// raw scanlines -> zlib deflate -> hand-written PNG chunks.
// A later phase can drop in a real mark and re-run `npx @tauri-apps/cli icon`.
import { deflateSync } from "node:zlib";
import { writeFileSync } from "node:fs";

const S = 1024;
const R = 224; // corner radius of the app-icon squircle-ish rounded rect

function crc32(buf) {
  let c;
  const table = [];
  for (let n = 0; n < 256; n++) {
    c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  let crc = 0xffffffff;
  for (const b of buf) crc = table[(crc ^ b) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}

// smooth 0..1 coverage from a signed distance (negative = inside)
const cov = (d) => Math.max(0, Math.min(1, 0.5 - d));

// signed distance to a rounded rect centred in the canvas
function sdRoundRect(x, y) {
  const hx = S / 2 - R;
  const hy = S / 2 - R;
  const px = Math.abs(x - S / 2) - hx;
  const py = Math.abs(y - S / 2) - hy;
  const qx = Math.max(px, 0);
  const qy = Math.max(py, 0);
  return Math.hypot(qx, qy) + Math.min(Math.max(px, py), 0) - R;
}

const lerp = (a, b, t) => a + (b - a) * t;

// palette matches app.css: deep slate ground, indigo accent
const TOP = [0x1c, 0x24, 0x44];
const BOT = [0x0b, 0x0e, 0x14];
const ACCENT = [0x5b, 0x8c, 0xff];
const ACCENT2 = [0x8f, 0x6b, 0xff];
const BALL = [0xf2, 0xf5, 0xff];

const rows = [];
for (let y = 0; y < S; y++) {
  const row = Buffer.alloc(1 + S * 4);
  row[0] = 0; // PNG filter type: none
  for (let x = 0; x < S; x++) {
    const t = y / (S - 1);
    let r = lerp(TOP[0], BOT[0], t);
    let g = lerp(TOP[1], BOT[1], t);
    let b = lerp(TOP[2], BOT[2], t);

    const cx = x - S / 2;
    const cy = y - S / 2;
    const dist = Math.hypot(cx, cy);

    // orbit ring: an annulus, faded out on the lower-left so it reads as motion
    const ringD = Math.abs(dist - 320) - 22;
    let ringA = cov(ringD);
    const ang = Math.atan2(cy, cx);
    ringA *= Math.max(0.12, Math.min(1, (Math.sin(ang - 0.6) + 1.35) / 1.6));
    const mixT = (x / S + y / S) / 2;
    const rr = lerp(ACCENT[0], ACCENT2[0], mixT);
    const rg = lerp(ACCENT[1], ACCENT2[1], mixT);
    const rb = lerp(ACCENT[2], ACCENT2[2], mixT);
    r = lerp(r, rr, ringA);
    g = lerp(g, rg, ringA);
    b = lerp(b, rb, ringA);

    // the ball at the centre
    const ballA = cov(dist - 150);
    r = lerp(r, BALL[0], ballA);
    g = lerp(g, BALL[1], ballA);
    b = lerp(b, BALL[2], ballA);

    // the agent: a small accent dot riding the orbit at the upper right
    const ad = Math.hypot(x - (S / 2 + 226), y - (S / 2 - 226)) - 62;
    const agentA = cov(ad);
    r = lerp(r, ACCENT[0], agentA);
    g = lerp(g, ACCENT[1], agentA);
    b = lerp(b, ACCENT[2], agentA);

    const a = cov(sdRoundRect(x, y)) * 255;
    const o = 1 + x * 4;
    row[o] = Math.round(r);
    row[o + 1] = Math.round(g);
    row[o + 2] = Math.round(b);
    row[o + 3] = Math.round(a);
  }
  rows.push(row);
}

const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(S, 0);
ihdr.writeUInt32BE(S, 4);
ihdr[8] = 8; // bit depth
ihdr[9] = 6; // colour type: RGBA
ihdr[10] = 0;
ihdr[11] = 0;
ihdr[12] = 0;

const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
  chunk("IHDR", ihdr),
  chunk("IDAT", deflateSync(Buffer.concat(rows), { level: 9 })),
  chunk("IEND", Buffer.alloc(0)),
]);

writeFileSync(process.argv[2], png);
console.log(`wrote ${process.argv[2]} (${png.length} bytes, ${S}x${S} RGBA)`);
