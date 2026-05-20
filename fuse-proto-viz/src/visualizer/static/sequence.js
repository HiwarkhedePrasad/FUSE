/**
 * sequence.js  —  fuse-proto-viz sequence diagram helpers
 *
 * Used server-side (Node/Deno) or in-browser by the Flask dashboard.
 * The Flask app embeds the live renderer directly; this module is the
 * canonical definition of actor positions and opcode-to-actor routing
 * so tests and the server can import from one source.
 */

export const ACTORS = ['Kernel', 'fuse_viz', 'VFS'];

export const ACTOR_X = [30, 130, 220];

export const OPCODE_MAP = {
  1:  'FUSE_LOOKUP',
  3:  'FUSE_GETATTR',
  14: 'FUSE_OPEN',
  15: 'FUSE_READ',
  16: 'FUSE_WRITE',
  18: 'FUSE_RELEASE',
  26: 'FUSE_INIT',
  28: 'FUSE_READDIR',
};

export const OPCODE_COLORS = {
  FUSE_INIT:    '#a78bfa',
  FUSE_GETATTR: '#38bdf8',
  FUSE_LOOKUP:  '#4ade80',
  FUSE_OPEN:    '#fbbf24',
  FUSE_READ:    '#2dd4bf',
  FUSE_READDIR: '#34d399',
  FUSE_WRITE:   '#fb7185',
  FUSE_RELEASE: '#f97316',
  unknown:      '#94a3b8',
};

/**
 * Determine which actor pair an opcode travels between.
 * Returns [srcIndex, dstIndex] into ACTORS / ACTOR_X.
 */
export function actorPair(opName) {
  const responses = new Set(['FUSE_INIT', 'FUSE_GETATTR']);
  if (responses.has(opName)) return [1, 2]; // fuse_viz → VFS
  return [0, 1];                             // Kernel  → fuse_viz
}

/**
 * Build a minimal SVG string for a sequence diagram from an array of events.
 * Each event: { op: number, node: bigint|number, desc: string }
 *
 * Returns an SVG string suitable for embedding.
 */
export function renderSequenceDiagram(events = [], maxRows = 40) {
  const rows = events.slice(-maxRows);
  const ROW_H = 24, HEADER_H = 36, W = 260;
  const H = HEADER_H + rows.length * ROW_H + 20;

  const lines = [
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="100%" font-family="monospace">`,
  ];

  // Lifelines + actor headers
  ACTORS.forEach((name, i) => {
    const x = ACTOR_X[i];
    lines.push(`<line x1="${x}" y1="${HEADER_H}" x2="${x}" y2="${H - 10}" stroke="#1e2330" stroke-width="1"/>`);
    lines.push(`<rect x="${x - 32}" y="6" width="64" height="20" rx="4" fill="#111318" stroke="#252a38" stroke-width="0.5"/>`);
    lines.push(`<text x="${x}" y="20" text-anchor="middle" font-size="9" fill="#64748b" font-weight="600">${name}</text>`);
  });

  if (rows.length === 0) {
    lines.push(`<text x="${W / 2}" y="${HEADER_H + 16}" text-anchor="middle" font-size="11" fill="#64748b">no events yet</text>`);
  }

  rows.forEach((ev, i) => {
    const opName = OPCODE_MAP[ev.op] ?? 'unknown';
    const color  = OPCODE_COLORS[opName] ?? OPCODE_COLORS.unknown;
    const [si, di] = actorPair(opName);
    const x1 = ACTOR_X[si], x2 = ACTOR_X[di];
    const dir = x2 > x1 ? 1 : -1;
    const y = HEADER_H + i * ROW_H + ROW_H / 2;

    lines.push(`<line x1="${x1 + dir * 4}" y1="${y}" x2="${x2 - dir * 8}" y2="${y}" stroke="${color}" stroke-width="1" opacity="0.7"/>`);
    lines.push(`<polygon points="${x2 - dir * 8},${y - 3} ${x2 - dir * 2},${y} ${x2 - dir * 8},${y + 3}" fill="${color}" opacity="0.7"/>`);
    const label = opName.replace('FUSE_', '');
    const lx = (x1 + x2) / 2;
    lines.push(`<text x="${lx}" y="${y - 3}" text-anchor="middle" font-size="8" fill="${color}" opacity="0.9">${label}</text>`);
  });

  lines.push('</svg>');
  return lines.join('\n');
}