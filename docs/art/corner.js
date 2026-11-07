// Same coverage math as RoundedCorners.c: 16x16 samples per pixel.
function cornerAlpha(r) {
  const R = 32 * r, R2 = R * R, a = [];
  for (let y = 0; y < r; y++) {
    const row = [];
    for (let x = 0; x < r; x++) {
      let out = 0;
      for (let sy = 0; sy < 16; sy++) {
        const dy = R - (32 * y + 2 * sy + 1), dy2 = dy * dy;
        for (let sx = 0; sx < 16; sx++) { const dx = R - (32 * x + 2 * sx + 1); if (dx * dx + dy2 > R2) out++; }
      }
      row.push((out * 255 + 128) >> 8);
    }
    a.push(row);
  }
  return a;
}
// Dawn wallpaper colour at a point (0..1, 0..1), matching the CSS gradient.
function dawn(u, v) {
  const t = Math.min(1, Math.max(0, (u + v) / 2));
  const stops = [[0, [92, 52, 20]], [0.55, [200, 134, 42]], [1, [246, 212, 138]]];
  for (let i = 1; i < stops.length; i++) if (t <= stops[i][0]) {
    const [t0, c0] = stops[i - 1], [t1, c1] = stops[i], k = (t - t0) / (t1 - t0);
    return c0.map((c, j) => Math.round(c + (c1[j] - c) * k));
  }
  return stops[2][1];
}
// Draw an r x r corner zoomed by z: each pixel a square with a hairline grid.
function drawZoom(ctx, x0, y0, r, z, alpha, bg) {
  for (let y = 0; y < r; y++) for (let x = 0; x < r; x++) {
    const a = alpha[y][x] / 255, c = bg(x / r, y / r).map(v => Math.round(v * (1 - a)));
    ctx.fillStyle = `rgb(${c[0]},${c[1]},${c[2]})`;
    ctx.fillRect(x0 + x * z, y0 + y * z, z - 1, z - 1);
  }
}
