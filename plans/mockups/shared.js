// Shared pieces for the Omaplay mockups: made-up video frames drawn as SVG,
// the stroked icon set, and helpers every direction reuses. Nothing here is
// anyone's real footage.

const ICON = {
  play: "M7 4 L20 12 L7 20 Z",
  pause: "M7 4 L10 4 L10 20 L7 20 Z M14 4 L17 4 L17 20 L14 20 Z",
  back: "M11 6 L4 12 L11 18 Z M20 6 L13 12 L20 18 Z",
  fwd: "M13 6 L20 12 L13 18 Z M4 6 L11 12 L4 18 Z",
  prev: "M6 5 L6 19 M19 5 L9 12 L19 19 Z",
  next: "M18 5 L18 19 M5 5 L15 12 L5 19 Z",
  volume: "M4 9 L8 9 L13 5 L13 19 L8 15 L4 15 Z M16 9 C17.5 10.5 17.5 13.5 16 15 M18.5 6.5 C21.5 9.5 21.5 14.5 18.5 17.5",
  mute: "M4 9 L8 9 L13 5 L13 19 L8 15 L4 15 Z M17 9 L22 14 M22 9 L17 14",
  subs: "M3 5 L21 5 L21 19 L3 19 Z M6 12 L10 12 M12 12 L18 12 M6 15 L14 15 M16 15 L18 15",
  audio: "M9 18 A3 3 0 1 1 8.9 18 Z M12 18 L12 4 L19 6 L19 9 L12 7",
  full: "M4 9 L4 4 L9 4 M15 4 L20 4 L20 9 M20 15 L20 20 L15 20 M9 20 L4 20 L4 15",
  speed: "M12 20 A8 8 0 1 1 12.1 20 Z M12 12 L16 8",
  copy: "M8 8 L20 8 L20 20 L8 20 Z M4 16 L4 4 L16 4",
  scissors: "M6 6 A2.5 2.5 0 1 1 5.9 6 Z M6 18 A2.5 2.5 0 1 1 5.9 18 Z M8 7.5 L20 17 M8 16.5 L20 7",
  info: "M12 3 A9 9 0 1 1 11.9 3 Z M12 11 L12 17 M12 7.5 L12 7.6",
  folder: "M3 6 L9 6 L11 8 L21 8 L21 19 L3 19 Z",
  list: "M8 6 L21 6 M8 12 L21 12 M8 18 L21 18 M3.5 6 L4 6 M3.5 12 L4 12 M3.5 18 L4 18",
  open: "M3 6 L9 6 L11 8 L21 8 L21 19 L3 19 Z M12 11 L12 17 M9 14 L12 11 L15 14",
  dots: "M5 12 L5.1 12 M12 12 L12.1 12 M19 12 L19.1 12",
  check: "M5 12 L10 17 L19 7",
  close: "M6 6 L18 18 M18 6 L6 18",
  resume: "M3 12 A9 9 0 1 0 6 5.3 M3 4 L3 9 L8 9 M11 9 L16 12 L11 15 Z",
};

function icon(name, size = 16, color = "currentColor", width = 2, fill = false) {
  const filled = ["play", "pause", "back", "fwd"].includes(name) || fill;
  return `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="${filled ? color : "none"}" stroke="${color}" stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round"><path d="${ICON[name]}"/></svg>`;
}

// Made-up frames from a made-up film. `t` nudges each scene so the scrub
// previews and the playlist's thumbnails are not all the same picture.
const FRAME = {
  coast(id = "c", t = 0) {
    const sun = 300 + t * 40;
    return `<svg viewBox="0 0 1600 900" preserveAspectRatio="xMidYMid slice" xmlns="http://www.w3.org/2000/svg">
      <defs>
        <linearGradient id="${id}s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#16213e"/><stop offset=".45" stop-color="#c0566b"/><stop offset=".62" stop-color="#f6a25a"/></linearGradient>
        <linearGradient id="${id}w" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#e88a5c"/><stop offset=".4" stop-color="#5b4a73"/><stop offset="1" stop-color="#14182c"/></linearGradient>
        <radialGradient id="${id}g" cx=".5" cy=".5" r=".5"><stop offset="0" stop-color="#fff4cf"/><stop offset=".35" stop-color="#ffc977" stop-opacity=".9"/><stop offset="1" stop-color="#ff9a5a" stop-opacity="0"/></radialGradient>
      </defs>
      <rect width="1600" height="900" fill="url(#${id}s)"/>
      <circle cx="${980 - t * 60}" cy="${sun + 220}" r="210" fill="url(#${id}g)"/>
      <rect y="560" width="1600" height="340" fill="url(#${id}w)"/>
      <path d="M${860 - t * 60} 575 L${1100 - t * 60} 575 L${1070 - t * 60} 600 L${890 - t * 60} 600 Z M${900 - t * 60} 625 L${1060 - t * 60} 625 L${1040 - t * 60} 645 L${920 - t * 60} 645 Z M${930 - t * 60} 670 L${1030 - t * 60} 670 L${1015 - t * 60} 685 L${945 - t * 60} 685 Z" fill="#ffe2b0" opacity=".55"/>
      <path d="M0 470 L120 420 L260 470 L380 400 L520 455 L560 560 L0 560 Z" fill="#2a1f3d"/>
      <path d="M0 520 L200 500 L340 540 L420 560 L0 560 Z" fill="#1b1529"/>
      <path d="M1180 900 L1260 610 L1280 600 L1300 610 L1360 900 Z" fill="#0d0f1a"/>
      <path d="M1252 640 L1308 640 L1300 610 L1260 610 Z" fill="#141626"/>
      <circle cx="1280" cy="598" r="9" fill="#fff6d0"/>
      <path d="M1280 598 L${1600} ${540 + t * 30} L1600 ${600 + t * 30} Z" fill="#fff6d0" opacity=".12"/>
      <path d="M0 780 C200 740 420 760 600 800 L600 900 L0 900 Z" fill="#0b0d16"/>
      <path d="M140 790 L150 700 L162 790 Z M210 800 L224 680 L238 800 Z" fill="#090a12"/>
    </svg>`;
  },
  city(id = "n", t = 0) {
    let windows = "";
    const blocks = [[60, 330, 150, 570], [230, 220, 130, 680], [380, 390, 170, 510], [570, 170, 140, 730], [730, 300, 180, 600], [930, 250, 120, 650], [1070, 360, 190, 540], [1280, 200, 120, 700], [1420, 320, 160, 580]];
    let seed = 7 + t;
    const rnd = () => (seed = (seed * 9301 + 49297) % 233280) / 233280;
    for (const [x, y] of blocks)
      for (let wy = y + 22; wy < 890; wy += 36)
        for (let wx = x + 16; wx < x + 120; wx += 28)
          if (rnd() > 0.45) windows += `<rect x="${wx}" y="${wy}" width="13" height="18" fill="${rnd() > 0.3 ? "#ffd97a" : "#9fd3ff"}" opacity="${0.5 + rnd() * 0.5}"/>`;
    return `<svg viewBox="0 0 1600 900" preserveAspectRatio="xMidYMid slice" xmlns="http://www.w3.org/2000/svg">
      <defs><linearGradient id="${id}n" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0b1026"/><stop offset=".7" stop-color="#3b2a5c"/><stop offset="1" stop-color="#8a4b6d"/></linearGradient></defs>
      <rect width="1600" height="900" fill="url(#${id}n)"/>
      <circle cx="${1200 + t * 30}" cy="130" r="54" fill="#f4efe1"/>
      ${blocks.map(([x, y, w, h]) => `<rect x="${x}" y="${y}" width="${w}" height="${h}" fill="#151628"/>`).join("")}
      ${windows}
    </svg>`;
  },
  forest(id = "f", t = 0) {
    let trees = "";
    let seed = 3 + t;
    const rnd = () => (seed = (seed * 9301 + 49297) % 233280) / 233280;
    for (let i = 0; i < 26; i++) {
      const x = rnd() * 1600, h = 260 + rnd() * 380, w = 60 + rnd() * 60, base = 760 + rnd() * 80;
      trees += `<path d="M${x} ${base - h} L${x + w / 2} ${base} L${x - w / 2} ${base} Z" fill="${["#1f3b2d", "#24473a", "#1a2f26", "#2d5544"][i % 4]}"/>`;
    }
    return `<svg viewBox="0 0 1600 900" preserveAspectRatio="xMidYMid slice" xmlns="http://www.w3.org/2000/svg">
      <defs><linearGradient id="${id}m" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#cfe0d8"/><stop offset=".6" stop-color="#8fb3a4"/><stop offset="1" stop-color="#3e5f52"/></linearGradient></defs>
      <rect width="1600" height="900" fill="url(#${id}m)"/>
      <path d="M0 520 L300 380 L560 500 L840 340 L1120 480 L1400 360 L1600 450 L1600 900 L0 900 Z" fill="#a9c4b8" opacity=".6"/>
      ${trees}
      <rect y="820" width="1600" height="80" fill="#14241d"/>
    </svg>`;
  },
  desk(id = "d", t = 0) {
    return `<svg viewBox="0 0 1600 900" preserveAspectRatio="xMidYMid slice" xmlns="http://www.w3.org/2000/svg">
      <defs><linearGradient id="${id}w" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2b2f3a"/><stop offset="1" stop-color="#171a22"/></linearGradient></defs>
      <rect width="1600" height="900" fill="url(#${id}w)"/>
      <rect x="230" y="120" width="1140" height="640" fill="#0f1117" stroke="#3a3f4d" stroke-width="10"/>
      <rect x="270" y="160" width="1060" height="44" fill="#1b1e27"/>
      ${[0, 1, 2, 3, 4, 5, 6, 7, 8].map((i) => `<rect x="${300 + (i % 2) * 40}" y="${240 + i * 52}" width="${300 + ((i * 137 + t * 90) % 520)}" height="20" fill="${["#FFD60A", "#7aa2f7", "#9ece6a", "#bb9af7"][(i + t) % 4]}" opacity=".75"/>`).join("")}
      <rect x="700" y="760" width="200" height="60" fill="#2a2e39"/>
      <rect x="560" y="815" width="480" height="20" fill="#2a2e39"/>
    </svg>`;
  },
};

function frame(kind, id, t = 0) {
  return FRAME[kind](id, t);
}

// Fill every [data-icon] and [data-frame] placeholder.
function hydrate() {
  document.querySelectorAll("[data-icon]").forEach((el) => {
    el.innerHTML = icon(el.dataset.icon, +(el.dataset.size || 16), el.dataset.color || "currentColor", +(el.dataset.stroke || 2));
  });
  let n = 0;
  document.querySelectorAll("[data-frame]").forEach((el) => {
    el.innerHTML = frame(el.dataset.frame, "f" + n++, +(el.dataset.t || 0));
  });
}
