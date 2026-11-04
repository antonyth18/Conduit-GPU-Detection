/* Conduit visualizer. Plays back traces recorded by build/conduit_trace
 * (visualizer/trace_data.js): every number on screen comes from the project's
 * own code run on that block, not from a re-implementation in JavaScript. */
(function () {
  "use strict";

  const TRACES = window.CONDUIT_TRACES || [];
  const $ = (id) => document.getElementById(id);
  const SVGNS = "http://www.w3.org/2000/svg";

  function h(tag, attrs, ...kids) {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v == null || v === false) continue;
      if (k === "class") el.className = v;
      else if (k === "html") el.innerHTML = v;
      else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
      else el.setAttribute(k, v);
    }
    for (const c of kids.flat(Infinity)) if (c != null && c !== false) el.append(c.nodeType ? c : document.createTextNode(String(c)));
    return el;
  }
  function s(tag, attrs, ...kids) {
    const el = document.createElementNS(SVGNS, tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v == null) continue;
      if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
      else el.setAttribute(k, v);
    }
    for (const c of kids.flat(Infinity)) if (c != null) el.append(c.nodeType ? c : document.createTextNode(String(c)));
    return el;
  }
  const fmt = (x) => Number(x).toLocaleString("en-US");

  // ---------------------------------------------------------------- sources
  const REFS = {
    JP93: "M. T. Jones and P. E. Plassmann. A parallel graph coloring heuristic. SIAM J. Sci. Comput. 14(3):654–669, 1993.",
    LUBY86: "M. Luby. A simple parallel algorithm for the maximal independent set problem. SIAM J. Comput. 15(4):1036–1053, 1986.",
    HKSL14: "W. Hasenplaugh, T. Kaler, T. B. Schardl, C. E. Leiserson. Ordering heuristics for parallel graph coloring. SPAA 2014, 166–177.",
    KAHN62: "A. B. Kahn. Topological sorting of large networks. Commun. ACM 5(11):558–562, 1962.",
    MG11: "D. Merrill and A. Grimshaw. High performance and scalable radix sorting: a case study of implementing dynamic parallelism for GPU computing. Parallel Processing Letters 21(2):245–272, 2011.",
    HS86: "W. D. Hillis and G. L. Steele Jr. Data parallel algorithms. Commun. ACM 29(12):1170–1183, 1986.",
    MG16S: "D. Merrill and M. Garland. Single-pass parallel prefix scan with decoupled look-back. NVIDIA Technical Report NVR-2016-002, 2016.",
    BLE90: "G. E. Blelloch. Prefix sums and their applications. Tech. Rep. CMU-CS-90-190, Carnegie Mellon University, 1990.",
    MG16: "D. Merrill and M. Garland. Merge-based parallel sparse matrix-vector multiplication. SC16, 678–689, 2016.",
    MGG12: "D. Merrill, M. Garland, A. Grimshaw. Scalable GPU graph traversal. PPoPP 2012.",
    SAAD03: "Y. Saad. Iterative Methods for Sparse Linear Systems, 2nd ed. SIAM, 2003 (compressed sparse row storage).",
    BHG87: "P. A. Bernstein, V. Hadzilacos, N. Goodman. Concurrency Control and Recovery in Database Systems. Addison-Wesley, 1987.",
    WEI88: "W. E. Weihl. Commutativity-based concurrency control for abstract data types. IEEE Trans. Computers 37(12):1488–1505, 1988.",
    PKS21: "G. Pîrlea, A. Kumar, I. Sergey. Practical smart contract sharding with ownership and commutativity analysis. PLDI 2021.",
    DGHK17: "T. Dickerson, P. Gazzillo, M. Herlihy, E. Koskinen. Adding concurrency to smart contracts. PODC 2017, 303–312.",
    GEL23: "R. Gelashvili et al. Block-STM: scaling blockchain execution by turning ordering curse to a performance blessing. PPoPP 2023, 232–244.",
    WOOD: "G. Wood. Ethereum: a secure decentralised generalised transaction ledger (Yellow Paper).",
    GRA69: "R. L. Graham. Bounds on multiprocessing timing anomalies. SIAM J. Appl. Math. 17(2):416–429, 1969.",
    KA99: "Y.-K. Kwok and I. Ahmad. Static scheduling algorithms for allocating directed task graphs to multiprocessors. ACM Comput. Surv. 31(4):406–471, 1999.",
    VAL90: "L. G. Valiant. A bridging model for parallel computation. Commun. ACM 33(8):103–111, 1990.",
    GSO12: "K. Gupta, J. A. Stuart, J. D. Owens. A study of persistent threads style GPU programming for GPGPU workloads. InPar 2012.",
    GH11: "C. Gregg and K. Hazelwood. Where is the data? Why you cannot debate CPU vs. GPU performance without the answer. ISPASS 2011, 134–144.",
    BQ: "Google BigQuery public dataset bigquery-public-data.crypto_ethereum (transactions, receipts), Ethereum mainnet 2026-09-01 and 2026-09-13.",
  };
  const cite = (...keys) => h("p", { class: "src" }, "Source: ", keys.map((k, i) => [i ? "; " : "", REFS[k]]));

  // ---------------------------------------------------------------- state
  const st = { ti: 0, pol: "P2", stage: 0, sub: 0, jpMode: "index", playing: null };
  const T = () => TRACES[st.ti];
  const P = () => T().policies[st.pol];
  const n = () => T().n;
  const tl = (i) => "t" + i;
  const modeChip = (key, m) => h("span", { class: "chip m-" + m, title: { W: "write", R: "read", C: "commutative update" }[m] }, h("b", {}, m), key);
  const BATCH = ["#3B5BDB", "#0CA678", "#E67700", "#C2255C", "#7048E8", "#1098AD", "#5C940D", "#D9480F", "#9C36B5", "#1971C2", "#2F9E44", "#F08C00"];
  const bcol = (b) => BATCH[b % BATCH.length];

  // tooltip
  const tip = $("tip");
  function hover(el, text) {
    el.addEventListener("mousemove", (e) => { tip.textContent = text; tip.style.display = "block"; tip.style.left = e.clientX + 14 + "px"; tip.style.top = e.clientY + 12 + "px"; });
    el.addEventListener("mouseleave", () => { tip.style.display = "none"; });
  }

  // ---------------------------------------------------------------- shared views
  function txTable(highlight) {
    const t = h("table", { class: "t" },
      h("thead", {}, h("tr", {}, h("th", {}, "tx"), h("th", {}, "block / index"), h("th", {}, "from"), h("th", {}, "to"),
        h("th", {}, "call"), h("th", {}, "decoded arguments"), h("th", { class: "num" }, "gas used"))));
    const tb = h("tbody");
    for (const x of T().txs) {
      const args = [x.a1, x.a2].filter((a) => a && a !== "-").join(", ");
      tb.append(h("tr", { class: highlight && highlight.has(x.i) ? "hl" : null },
        h("td", { class: "tx" }, tl(x.i)), h("td", {}, `${x.block} / ${x.idx}`), h("td", {}, x.from), h("td", {}, x.to),
        h("td", {}, x.kind, h("br"), h("small", { style: "color:var(--muted)" }, x.sel)), h("td", {}, args || "–"), h("td", { class: "num" }, fmt(x.gas))));
    }
    t.append(tb);
    return t;
  }

  function arrRow(label, values, opts = {}) {
    const wrap = h("div");
    wrap.append(h("div", { class: "arr-label" }, label));
    const a = h("div", { class: "arr" });
    values.forEach((v, i) => a.append(h("div", { class: "cell" + (opts.on && opts.on(i) ? " on" : "") }, opts.sub ? h("small", {}, opts.sub(i)) : null, String(v))));
    wrap.append(a);
    return wrap;
  }

  // layered DAG layout
  function dagLayout(levels) {
    const L = Math.max(0, ...levels) + 1;
    const cols = Array.from({ length: L }, () => []);
    levels.forEach((lv, i) => cols[lv].push(i));
    const colW = 96, rowH = 46, pad = 34;
    const maxRows = Math.max(...cols.map((c) => c.length));
    const pos = [];
    cols.forEach((c, x) => c.forEach((i, k) => {
      pos[i] = { x: pad + x * colW, y: pad + (k + (maxRows - c.length) / 2) * rowH };
    }));
    return { pos, w: pad * 2 + (L - 1) * colW, h: pad * 2 + (maxRows - 1) * rowH, L, cols };
  }

  function dagSVG(opts = {}) {
    const p = P();
    const lv = p.levels;
    const lay = dagLayout(lv);
    const svg = s("svg", { width: Math.max(lay.w + 20, 300), height: lay.h + 30, role: "img", "aria-label": "dependency DAG" });
    svg.append(s("defs", {}, s("marker", { id: "ah", viewBox: "0 0 10 10", refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse" },
      s("path", { d: "M0,0 L10,5 L0,10 z", fill: "#7E8BA5" }))));
    // level bands
    for (let x = 0; x < lay.L; x++) {
      const on = opts.level != null && x === opts.level;
      svg.append(s("rect", { x: 34 + x * 96 - 30, y: 4, width: 60, height: lay.h + 22, rx: 6, fill: on ? "#ECEAFB" : "transparent" }));
      svg.append(s("text", { x: 34 + x * 96, y: lay.h + 22, "text-anchor": "middle", "font-size": 11, fill: "#5A6782" }, "level " + x));
    }
    const ro = p.row_offsets, cols = p.cols;
    const keyOf = edgeKeys();
    for (let u = 0; u < n(); u++)
      for (let e = ro[u]; e < ro[u + 1]; e++) {
        const v = cols[e], a = lay.pos[u], b = lay.pos[v];
        const path = s("path", { class: "edge", d: `M${a.x + 15},${a.y} C${(a.x + b.x) / 2},${a.y} ${(a.x + b.x) / 2},${b.y} ${b.x - 16},${b.y}`, "marker-end": "url(#ah)" });
        hover(path, `${tl(u)} → ${tl(v)}: conflict on ${keyOf.get(u + "," + v) || "a shared key"}`);
        svg.append(path);
      }
    lv.forEach((l, i) => {
      const a = lay.pos[i];
      const shown = opts.level == null || l <= opts.level;
      const g = s("g", { class: "node", transform: `translate(${a.x},${a.y})`, opacity: shown ? 1 : 0.25 });
      g.append(s("circle", { r: 15, fill: opts.color ? bcol(l) : "#fff" }));
      g.append(s("text", { "text-anchor": "middle", dy: 4, fill: opts.color ? "#fff" : "#14213D" }, tl(i)));
      hover(g, `${tl(i)}: ${T().txs[i].kind} from ${T().txs[i].from}, level ${l}`);
      svg.append(g);
    });
    return svg;
  }

  function edgeKeys() {
    const p = P(), m = new Map();
    for (const e of p.edges) m.set(e[0] + "," + e[1], p.sorted[e[2]].key);
    return m;
  }

  function conflictSVG(colorOf, active, winners, rank) {
    const p = P(), N = n();
    const R = Math.max(120, N * 15), cx = R + 50, cy = R + 40;
    const svg = s("svg", { width: 2 * R + 100, height: 2 * R + 80, role: "img", "aria-label": "conflict graph" });
    const pt = (i) => ({ x: cx + R * Math.cos(-Math.PI / 2 + (2 * Math.PI * i) / N), y: cy + R * Math.sin(-Math.PI / 2 + (2 * Math.PI * i) / N) });
    for (const [a, b, key] of p.conflicts) {
      const A = pt(a), B = pt(b);
      const hot = winners && (winners.has(a) || winners.has(b)) && active && active.has(a) && active.has(b);
      const ln = s("line", { x1: A.x, y1: A.y, x2: B.x, y2: B.y, class: "edge" + (hot ? " hot" : "") });
      hover(ln, `${tl(a)} — ${tl(b)} conflict on ${key}`);
      svg.append(ln);
    }
    for (let i = 0; i < N; i++) {
      const A = pt(i), c = colorOf ? colorOf(i) : null;
      const isActive = active ? active.has(i) : true;
      const g = s("g", { class: "node", transform: `translate(${A.x},${A.y})` });
      g.append(s("circle", { r: 16, fill: c != null ? bcol(c) : "#fff", "stroke-dasharray": isActive && c == null ? "0" : null, "stroke-width": winners && winners.has(i) ? 3.5 : 1.5, stroke: winners && winners.has(i) ? "#5546D6" : "#14213D", opacity: !isActive && c == null ? 0.35 : 1 }));
      g.append(s("text", { "text-anchor": "middle", dy: 4, fill: c != null ? "#fff" : "#14213D" }, tl(i)));
      if (rank) {
        const out = { x: cx + (R + 30) * Math.cos(-Math.PI / 2 + (2 * Math.PI * i) / N) - A.x, y: cy + (R + 30) * Math.sin(-Math.PI / 2 + (2 * Math.PI * i) / N) - A.y };
        g.append(s("text", { x: out.x, y: out.y + 4, "text-anchor": "middle", "font-size": 11, fill: "#5546D6", style: "font-weight:400" }, "#" + (rank[i] + 1)));
      }
      hover(g, `${tl(i)}: ${T().txs[i].kind}${c != null ? ", colour " + c : ""}${rank ? ", priority rank " + (rank[i] + 1) : ""}`);
      svg.append(g);
    }
    return svg;
  }

  function gantt(sched, rows, barriers, maxT, label, colorFn) {
    const W = 760, lane = 26, left = 120, top = 18;
    const scale = (W - left - 60) / maxT;
    const height = top + rows * lane + 30;
    const svg = s("svg", { width: W, height, class: "gantt", role: "img", "aria-label": label });
    svg.append(s("text", { x: 0, y: 13, "font-size": 13, "font-weight": 600, fill: "#14213D" }, label));
    for (let r = 0; r < rows; r++) {
      svg.append(s("text", { class: "lane", x: left - 10, y: top + r * lane + 17, "text-anchor": "end" }, rows === 1 ? "1 thread" : "worker " + (r + 1)));
      svg.append(s("line", { x1: left, x2: W - 20, y1: top + (r + 1) * lane, y2: top + (r + 1) * lane, stroke: "#EDF1F6" }));
    }
    for (const b of barriers || []) svg.append(s("line", { x1: left + b * scale, x2: left + b * scale, y1: top - 2, y2: top + rows * lane + 2, stroke: "#14213D", "stroke-dasharray": "3 3", "stroke-width": 1 }));
    for (const x of sched) {
      const g = s("g", { class: "bar", "data-s": x.s, "data-e": x.e });
      g.append(s("rect", { x: left + x.s * scale, y: top + x.w * lane + 3, width: Math.max(2, (x.e - x.s) * scale - 1), height: lane - 6, rx: 3, fill: colorFn(x.tx) }));
      if ((x.e - x.s) * scale > 24) g.append(s("text", { x: left + x.s * scale + 5, y: top + x.w * lane + 17 }, tl(x.tx)));
      hover(g, `${tl(x.tx)} ${T().txs[x.tx].kind}: runs ${x.s.toFixed(0)}–${x.e.toFixed(0)} (cost = gas/1000)`);
      svg.append(g);
    }
    const end = Math.max(...sched.map((x) => x.e));
    svg.append(s("text", { x: left + end * scale + 6, y: top + rows * lane - 4, "font-size": 12, fill: "#14213D" }, end.toFixed(0)));
    const cursor = s("line", { x1: left, x2: left, y1: top - 4, y2: top + rows * lane + 4, stroke: "#5546D6", "stroke-width": 2, class: "cursor", opacity: 0 });
    svg.append(cursor);
    svg.__scale = scale; svg.__left = left;
    return { svg, end };
  }

  // ---------------------------------------------------------------- live animation player
  // Every live view is a pure function of time: frame(t) redraws the scene for
  // second t, so Play, Pause, Restart and dragging the slider all agree.
  const anim = { speed: 1, cur: null };
  const RM = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const prog = (t, a, d) => (d <= 0 ? (t >= a ? 1 : 0) : clamp((t - a) / d, 0, 1));
  const ease = (q) => (1 - Math.cos(Math.PI * clamp(q, 0, 1))) / 2;
  const lerp = (a, b, q) => a + (b - a) * q;

  function player(dur, frame, title) {
    const btn = h("button", { class: "pbtn main" }, "Pause");
    const rst = h("button", { class: "pbtn" }, "Restart");
    const rng = h("input", { type: "range", min: 0, max: 1000, value: 0, class: "prange", "aria-label": "Position in the animation" });
    const time = h("span", { class: "ptime" });
    const cap = h("div", { class: "pcap", "aria-live": "polite" });
    const speeds = h("span", { class: "pspeed", role: "group", "aria-label": "Playback speed" });
    [0.5, 1, 2].forEach((v) => speeds.append(h("button", { "aria-pressed": anim.speed === v, title: "playback speed", onclick: (e) => { anim.speed = v; for (const b of speeds.children) b.setAttribute("aria-pressed", b === e.currentTarget); } }, v + "×")));
    const el = h("div", { class: "player" },
      h("div", { class: "prow" }, h("span", { class: "plive" }, h("i"), "Live"), h("span", { class: "ptitle" }, title), btn, rst, rng, time, speeds), cap);
    let t = 0, playing = false, last = 0, raf = 0;
    const draw = () => {
      const c = frame(t);
      if (c != null) cap.textContent = c;
      rng.value = Math.round((t / dur) * 1000);
      time.textContent = `${t.toFixed(1)} / ${dur.toFixed(1)} s`;
    };
    function stop() { playing = false; cancelAnimationFrame(raf); btn.textContent = t >= dur ? "Replay" : "Play"; el.classList.remove("running"); }
    function tick(now) {
      t = clamp(t + (Math.max(0, now - last) / 1000) * anim.speed, 0, dur); last = now; draw();
      if (t >= dur) { stop(); return; }
      raf = requestAnimationFrame(tick);
    }
    function play() { if (t >= dur) t = 0; playing = true; last = performance.now(); btn.textContent = "Pause"; el.classList.add("running"); raf = requestAnimationFrame(tick); }
    const api = { el, play, stop, toggle: () => (playing ? stop() : play()), restart: () => { stop(); t = 0; draw(); play(); } };
    btn.addEventListener("click", api.toggle);
    rst.addEventListener("click", api.restart);
    rng.addEventListener("input", () => { stop(); t = (rng.value / 1000) * dur; draw(); btn.textContent = t >= dur ? "Replay" : "Play"; });
    anim.cur = api;
    requestAnimationFrame(() => {
      if (anim.cur !== api) return;
      if (RM) { t = dur; draw(); stop(); } else { draw(); play(); }
    });
    return api;
  }

  // ---- overview: a token travels through the pipeline
  function liveOverview(root, steps) {
    const svg = s("svg", { viewBox: "0 0 892 132", width: "100%", style: "max-width:900px", role: "img", "aria-label": "pipeline overview" });
    svg.append(s("defs", {}, s("marker", { id: "ov", viewBox: "0 0 10 10", refX: 9, refY: 5, markerWidth: 6, markerHeight: 6, orient: "auto" }, s("path", { d: "M0,0 L10,5 L0,10 z", fill: "#14213D" }))));
    const boxes = steps.map(([a, b], i) => {
      const x = 8 + i * 148;
      const r = s("rect", { x, y: 14, width: 128, height: 84, rx: 6, fill: "#fff", stroke: "#C9D3E3", "stroke-width": 1.5 });
      svg.append(r);
      svg.append(s("text", { x: x + 64, y: 38, "text-anchor": "middle", "font-size": 15, "font-weight": 600, fill: "#14213D", style: "font-family:var(--head)" }, a));
      const words = b.split(" "), lines = [];
      let cur = "";
      for (const w of words) { if ((cur + " " + w).length > 17) { lines.push(cur); cur = w; } else cur = (cur ? cur + " " : "") + w; }
      lines.push(cur);
      lines.forEach((ln, k) => svg.append(s("text", { x: x + 64, y: 57 + k * 14, "text-anchor": "middle", "font-size": 11.5, fill: "#5A6782" }, ln)));
      if (i < steps.length - 1) svg.append(s("path", { d: `M${x + 130},56 L${x + 146},56`, stroke: "#14213D", "stroke-width": 1.5, "marker-end": "url(#ov)" }));
      return r;
    });
    svg.append(s("text", { x: 8 + 148 + 2 * 148 - 10, y: 124, "text-anchor": "middle", "font-size": 12, fill: "#5546D6" }, "stages 1–4 run on the GPU (CUDA), on OpenMP threads or on one core, with identical output"));
    const dots = Array.from({ length: 5 }, () => { const c = s("circle", { r: 5, fill: "#5546D6" }); svg.append(c); return c; });
    const per = 1.05, dur = steps.length * per + 0.6;
    const pl = player(dur, (t) => {
      const k = Math.min(steps.length - 1, Math.floor(t / per));
      boxes.forEach((r, i) => { const on = i === k; const gpu = i >= 1 && i <= 4; r.setAttribute("fill", on ? "#ECEAFB" : i < k ? (gpu ? "#F5F4FE" : "#F7F9FC") : "#fff"); r.setAttribute("stroke", on ? "#5546D6" : i < k ? "#9C93E8" : "#C9D3E3"); r.setAttribute("stroke-width", on ? 2.5 : 1.5); });
      dots.forEach((d, j) => { const x = 8 + 64 + clamp((t - j * 0.08) / per - 0.5, 0, steps.length - 1) * 148; d.setAttribute("cx", x); d.setAttribute("cy", 106 - j * 0); d.setAttribute("opacity", 1 - j * 0.18); });
      return `${steps[k][0]}: ${steps[k][1]}.`;
    }, "One block through the pipeline");
    root.append(pl.el, h("div", { class: "sheet" }, svg));
  }

  // ---- stage 1: every thread extracts its read/write set at the same time
  function liveRW(root) {
    const p = P(), N = n();
    const maxC = Math.max(1, ...p.counts);
    const CH = clamp(4.4 / maxC, 0.5, 1.4), T0 = 1.0;
    const dur = T0 + maxC * CH + 1.3;
    const rows = [];
    const tb = h("tbody");
    p.access.forEach((acc, i) => {
      const badge = h("span", { class: "thread off" }, i);
      const chips = acc.map((a) => modeChip(a.key, a.m));
      const cnt = h("td", { class: "num" }, "");
      tb.append(h("tr", {}, h("td", {}, badge), h("td", { class: "tx" }, tl(i)), h("td", {}, T().txs[i].kind), h("td", {}, chips), cnt));
      rows.push({ badge, chips, cnt, k: acc.length });
    });
    const table = h("table", { class: "t" }, h("thead", {}, h("tr", {}, h("th", {}, "GPU thread"), h("th", {}, "tx"), h("th", {}, "call"), h("th", {}, "accesses found so far"), h("th", { class: "num" }, "count"))), tb);
    const pl = player(dur, (t) => {
      let found = 0, done = 0;
      rows.forEach((r) => {
        const launched = t >= 0.35;
        const fin = t >= T0 + r.k * CH;
        r.badge.className = "thread" + (!launched ? " off" : fin ? " fin" : " busy");
        r.chips.forEach((c, j) => { const q = prog(t, T0 + j * CH, 0.3); c.style.opacity = q; c.style.transform = `scale(${0.6 + 0.4 * q})`; if (q > 0) found++; });
        r.cnt.textContent = fin ? r.k : "";
        if (fin) done++;
      });
      if (t < T0) return `Launching ${N} GPU threads, one per transaction. They all start at the same time.`;
      if (done < N) return `All threads decode their own transaction at once: ${found} of ${p.records.length} accesses found, ${done} of ${N} threads finished. Threads with more accesses run longer.`;
      return `All ${N} threads finished: ${p.records.length} state accesses under ${st.pol}. Each thread now knows how many records it will write.`;
    }, "Threads extract read/write sets");
    root.append(pl.el, h("div", { class: "legend" }, h("span", {}, h("i", { class: "sw", style: "background:var(--write)" }), "W write"), h("span", {}, h("i", { class: "sw", style: "background:var(--read)" }), "R read"), h("span", {}, h("i", { class: "sw", style: "background:var(--comm)" }), "C commutative update"), h("span", {}, h("i", { class: "sw", style: "background:var(--thread)" }), "thread running"), h("span", {}, h("i", { class: "sw", style: "background:var(--ok)" }), "thread finished")), h("div", { class: "sheet" }, table));
  }

  // ---- stage 1: parallel prefix sum (Hillis–Steele), checked against the recorded offsets
  function liveScan(root) {
    const p = P(), N = n();
    const steps = [p.counts.slice()];
    for (let d = 1; d < N; d *= 2) { const pr = steps[steps.length - 1]; steps.push(pr.map((v, i) => v + (i >= d ? pr[i - d] : 0))); }
    const incl = steps[steps.length - 1];
    const excl = [0, ...incl];
    const ok = excl.length === p.offsets.length && excl.every((v, i) => v === p.offsets[i]);
    const S = 1.3, T0 = 0.7, K = steps.length - 1;
    const dur = T0 + K * S + 1.8;
    const blocks = steps.map((arr, k) => {
      const d = k ? 2 ** (k - 1) : 0;
      return arrRow(k === 0 ? "counts: the input" : `step ${k}: every thread i ≥ ${d} adds the value ${d} place${d > 1 ? "s" : ""} to its left, all at once`, arr,
        { on: (i) => k > 0 && i >= d, sub: (i) => (k > 0 && i >= d ? "+" + tl(i - d) : tl(i)) });
    });
    const last = arrRow("shift right by one, 0 first: the exclusive scan = write offsets (last cell = total)", excl, { sub: (i) => (i < N ? tl(i) : "total"), on: () => true });
    const sheet = h("div", { class: "sheet" }, h("h3", {}, "Parallel prefix sum, step by step"), ...blocks, last,
      h("p", { class: "src" }, ok ? "✓ Same offsets as the project's code recorded for this block." : "✗ Differs from the recorded offsets."));
    const pl = player(dur, (t) => {
      let k = 0;
      blocks.forEach((b, i) => { const q = i === 0 ? 1 : prog(t, T0 + (i - 1) * S, 0.45); b.style.opacity = 0.08 + 0.92 * q; b.style.transform = `translateY(${(1 - q) * 10}px)`; if (q > 0) k = i; });
      const ql = prog(t, T0 + K * S, 0.5);
      last.style.opacity = 0.08 + 0.92 * ql;
      if (ql > 0) return `Shift right and put 0 first: thread i now knows where its records start. Total: ${excl[N]} records, ${K} parallel steps instead of ${N - 1} sequential additions.`;
      if (k === 0) return "Input: how many accesses each thread found.";
      return `Step ${k} of ${K}: each thread adds the value ${2 ** (k - 1)} positions to its left. After ⌈log₂ ${N}⌉ = ${K} steps, every cell holds the sum of everything up to it.`;
    }, "Prefix sum turns counts into offsets");
    root.append(pl.el, sheet);
  }

  // ---- stage 1: threads write their records into their own slots
  function liveRecords(root) {
    const p = P(), N = n();
    const maxC = Math.max(1, ...p.counts);
    const W = clamp(4.2 / maxC, 0.6, 1.5), T0 = 0.8;
    const dur = T0 + maxC * W + 1.8;
    const thr = p.access.map((_, i) => h("span", { class: "thread off" }, i));
    const left = h("div", { class: "trows" }, ...p.access.map((_, i) => h("div", { class: "trow" }, thr[i], h("span", { class: "tx" }, tl(i)),
      h("small", {}, p.counts[i] ? ` slots ${p.offsets[i]}–${p.offsets[i + 1] - 1}` : " no records"))));
    const slots = p.records.map((r, k) => h("div", { class: "slot" }, h("small", {}, k), h("span", { class: "tx" }, tl(r.tx)), modeChip("", r.m), h("span", { class: "k" }, r.key)));
    const grid = h("div", { class: "slots" }, ...slots);
    const pl = player(dur, (t) => {
      let written = 0;
      p.access.forEach((_, i) => { const fin = t >= T0 + p.counts[i] * W; thr[i].className = "thread" + (t < 0.3 ? " off" : fin ? " fin" : " busy"); });
      p.records.forEach((r, k) => {
        const j = k - p.offsets[r.tx], tk = T0 + j * W;
        const q = prog(t, tk, 0.35);
        slots[k].style.opacity = 0.12 + 0.88 * q;
        slots[k].classList.toggle("fresh", t >= tk && t < tk + 0.7);
        if (q > 0) written++;
      });
      if (t < T0) return "Each thread starts at its own offset from the prefix sum.";
      if (written < p.records.length) return `All threads write at the same time, each into its own slots: ${written} of ${p.records.length} records written. No locks are needed and the order never depends on timing.`;
      return `${p.records.length} records, in block order, ready to sort. Each is packed as two 64-bit words: key, then tx × 8 + mode.`;
    }, "Threads write records in parallel");
    root.append(pl.el, h("div", { class: "row" }, h("div", { class: "sheet", style: "flex:0 0 210px" }, h("h3", {}, "Threads"), left), h("div", { class: "sheet", style: "flex:1 1 420px" }, h("h3", {}, "Record array"), grid)));
  }

  // ---- stage 2a: LSD radix sort, pass by pass
  function liveSort(root) {
    const p = P(), R = p.records.length;
    const keys = [], rank = new Map();
    p.sorted.forEach((r) => { if (!rank.has(r.key)) { rank.set(r.key, keys.length); keys.push(r.key); } });
    const K = keys.length, D = Math.max(1, Math.ceil(Math.log(Math.max(2, K)) / Math.log(4)));
    const digit = (x, d) => Math.floor(x / 4 ** d) % 4;
    const rk = p.records.map((r) => rank.get(r.key));
    let order = p.records.map((_, i) => i);
    const orders = [order.slice()];
    for (let d = 0; d < D; d++) { const b = [[], [], [], []]; order.forEach((i) => b[digit(rk[i], d)].push(i)); order = b.flat(); orders.push(order.slice()); }
    const sIdx = new Map(); p.sorted.forEach((r, i) => sIdx.set(r.key + "|" + r.tx + "|" + r.m, i));
    const where = orders.map((o) => { const w = []; o.forEach((i, sl) => (w[i] = sl)); return w; });
    const finalOk = p.records.every((r, i) => sIdx.get(r.key + "|" + r.tx + "|" + r.m) === where[D][i]);
    if (!finalOk) where[D] = p.records.map((r) => sIdx.get(r.key + "|" + r.tx + "|" + r.m));
    const gid = []; let g = -1;
    p.sorted.forEach((r, i) => { if (i === 0 || r.key !== p.sorted[i - 1].key) g++; gid[i] = g; });
    const perCol = Math.max(14, Math.ceil(R / 3)), C = Math.ceil(R / perCol), rowH = 28;
    const colW = 100 / C;
    const place = (slot) => ({ x: Math.floor(slot / perCol), y: slot % perCol });
    const box = h("div", { class: "sortbox", style: `height:${perCol * rowH + 4}px` });
    const items = p.records.map((r, i) => {
      const digs = Array.from({ length: D }, (_, d) => h("span", {}, digit(rk[i], D - 1 - d)));
      const el = h("div", { class: "srec", style: `width:calc(${colW}% - 8px)` }, h("span", { class: "i" }, i), h("span", { class: "k" }, r.key), h("span", { class: "tx" }, tl(r.tx)), modeChip("", r.m), h("span", { class: "dig", title: "rank of the key, in base 4" }, digs));
      box.append(el);
      return { el, digs, posEl: el.firstChild };
    });
    const T0 = 1.0, Wp = clamp(6.2 / D, 1.6, 3.0);
    const dur = T0 + D * Wp + 1.4;
    const pl = player(dur, (t) => {
      const pi = clamp(Math.floor((t - T0) / Wp), -1, D);
      const local = t - T0 - Math.max(0, pi) * Wp;
      const q = pi < 0 ? 0 : pi >= D ? 1 : ease((local - 0.35 * Wp) / (0.55 * Wp));
      const from = pi < 0 ? 0 : Math.min(pi, D), to = pi < 0 ? 0 : Math.min(pi + 1, D);
      const done = t >= T0 + D * Wp;
      items.forEach((it, i) => {
        const a = place(where[done ? D : from][i]), b = place(where[done ? D : to][i]);
        const qq = done ? 1 : q;
        const x = lerp(a.x, b.x, qq), y = lerp(a.y, b.y, qq);
        it.el.style.left = `calc(${x * colW}% + 4px)`;
        it.el.style.top = y * rowH + "px";
        const slot = Math.round(lerp(where[done ? D : from][i], where[done ? D : to][i], qq));
        it.posEl.textContent = slot;
        it.digs.forEach((dg, k) => dg.classList.toggle("cur", !done && pi >= 0 && pi < D && D - 1 - k === pi));
        it.el.classList.toggle("g0", done && gid[where[D][i]] % 2 === 0);
        it.el.classList.toggle("g1", done && gid[where[D][i]] % 2 === 1);
      });
      if (pi < 0) return `${R} records in block order, as the threads wrote them. Each key has a rank (shown in base 4); the sort works on one digit per pass.`;
      if (!done) return `Pass ${pi + 1} of ${D}: every record is put in bucket 0–3 by digit ${pi + 1} from the right, keeping the previous order inside each bucket (a stable pass). Counting the buckets is again a prefix sum.`;
      return `Sorted after ${D} passes: each of the ${K} keys is one contiguous group (shaded), and inside a group the transactions are still in block order. Conflicts can now only be between neighbours.`;
    }, "Radix sort by state key");
    root.append(pl.el, h("div", { class: "sheet" }, box));
  }

  // ---- stage 2b: one thread per record compares with its neighbour
  function liveGroups(root) {
    const p = P(), R = p.sorted.length;
    const cols = "30px 40px 1fr 52px 44px 92px 64px 64px";
    const head = h("div", { class: "rechead", style: `grid-template-columns:${cols}` }, h("span", {}, "thr"), h("span", {}, "pos"), h("span", {}, "key"), h("span", {}, "tx"), h("span", {}, "mode"), h("span", {}, "vs row above"), h("span", { class: "f" }, "group flag"), h("span", { class: "f" }, "run flag"));
    const rows = p.sorted.map((r, i) => {
      const thr = h("span", { class: "thread off" }, i);
      const cmp = h("span", { class: "cmp" }, i === 0 ? "first" : p.gflag[i] ? "new key" : p.rflag[i] ? "new run" : "same run");
      const gf = h("span", { class: "f" + (p.gflag[i] ? " one" : "") }, p.gflag[i]);
      const rf = h("span", { class: "f" + (p.rflag[i] ? " one" : "") }, p.rflag[i]);
      const el = h("div", { class: "grec", style: `grid-template-columns:${cols}` }, thr, h("span", { class: "i" }, i), h("span", {}, r.key), h("span", { class: "tx" }, tl(r.tx)), h("span", {}, modeChip("", r.m)), cmp, gf, rf);
      return { el, thr, cmp, gf, rf, g: p.gscan[i] - 1 };
    });
    const dur = 6.4;
    const pl = player(dur, (t) => {
      rows.forEach((r) => {
        r.thr.className = "thread" + (t < 0.6 ? " off" : t < 3.4 ? " busy" : " fin");
        r.cmp.style.opacity = prog(t, 1.0, 0.8);
        r.gf.style.opacity = prog(t, 2.2, 0.5);
        r.rf.style.opacity = prog(t, 3.0, 0.5);
        r.el.classList.toggle("g0", t >= 4.2 && r.g % 2 === 0);
        r.el.classList.toggle("g1", t >= 4.2 && r.g % 2 === 1);
      });
      const G = p.gstart.length - 1, Rn = p.rstart.length - 1;
      if (t < 1.0) return `${R} threads, one per sorted record.`;
      if (t < 2.2) return "Every thread compares its record with the one just above it, all at the same time.";
      if (t < 3.0) return "Group flag = 1 where the key changes: a new state key starts here.";
      if (t < 4.2) return "Run flag = 1 where a new run starts: a new key, a write, or a change between reads and commutative updates.";
      return `${G} key groups and ${Rn} runs. Next: prefix sums number them, so every record knows its group and run.`;
    }, "Head flags, all records in parallel");
    root.append(pl.el, h("div", { class: "sheet" }, head, h("div", {}, rows.map((r) => r.el))));
  }

  // ---- stage 3: the DAG is built edge by edge, then levelled node by node
  function liveDAG(root) {
    const p = P(), N = n(), lv = p.levels;
    const lay = dagLayout(lv);
    const rowGap = clamp(820 / Math.max(1, N - 1), 40, 74);
    const topY = 112, y0 = 200;
    const start = Array.from({ length: N }, (_, i) => ({ x: 34 + i * rowGap, y: topY }));
    const end = lay.pos.map((q) => ({ x: q.x, y: q.y + y0 }));
    const W = Math.max(68 + (N - 1) * rowGap, lay.w) + 70, H = y0 + lay.h + 46;
    const svg = s("svg", { width: W, height: H, viewBox: `0 0 ${W} ${H}`, role: "img", "aria-label": "dependency DAG being built" });
    svg.append(s("defs", {}, s("marker", { id: "ahL", viewBox: "0 0 10 10", refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse" }, s("path", { d: "M0,0 L10,5 L0,10 z", fill: "#7E8BA5" })),
      s("marker", { id: "ahH", viewBox: "0 0 10 10", refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse" }, s("path", { d: "M0,0 L10,5 L0,10 z", fill: "#5546D6" }))));
    svg.append(s("text", { x: 10, y: 24, "font-size": 12, fill: "#5A6782" }, "block order"));
    svg.append(s("line", { x1: 10, x2: W - 10, y1: y0 - 28, y2: y0 - 28, stroke: "#E1E7F0" }));
    const lvLab = [], bands = [];
    for (let x = 0; x < lay.L; x++) {
      const b = s("rect", { x: 34 + x * 96 - 30, y: y0 - 18, width: 60, height: lay.h + 40, rx: 6, fill: bcol(x), opacity: 0 });
      svg.append(b); bands.push(b);
      const tx = s("text", { x: 34 + x * 96, y: H - 8, "text-anchor": "middle", "font-size": 11.5, fill: "#5A6782", opacity: 0 }, "level " + x);
      svg.append(tx); lvLab.push(tx);
    }
    const keyOf = edgeKeys();
    const first = new Map(); p.edges.forEach((e, k) => { const id = e[0] + "," + e[1]; if (!first.has(id)) first.set(id, k); });
    const E = [];
    for (let u = 0; u < N; u++) for (let e = p.row_offsets[u]; e < p.row_offsets[u + 1]; e++) E.push({ u, v: p.cols[e], key: keyOf.get(u + "," + p.cols[e]) || "a shared key" });
    E.sort((a, b) => (first.get(a.u + "," + a.v) ?? 0) - (first.get(b.u + "," + b.v) ?? 0));
    const preds = Array.from({ length: N }, () => []);
    E.forEach((e) => preds[e.v].push(e.u));
    const edgeLayer = s("g"); svg.append(edgeLayer);
    E.forEach((e) => {
      e.path = s("path", { class: "edge", pathLength: 1, "stroke-dasharray": "1 1" });
      e.lab = s("text", { "font-size": 10.5, fill: "#5546D6", "text-anchor": "middle", opacity: 0 }, e.key);
      hover(e.path, `${tl(e.u)} → ${tl(e.v)}: conflict on ${e.key}`);
      edgeLayer.append(e.path); e.labLayer = e.lab;
    });
    const nodes = lv.map((l, i) => {
      const g = s("g", { class: "node" });
      const c = s("circle", { r: 15, fill: "#fff" });
      const tx = s("text", { "text-anchor": "middle", dy: 4 }, tl(i));
      const lab = s("text", { "text-anchor": "middle", y: -22, "font-size": 11, fill: "#5546D6", style: "font-weight:700" }, "");
      g.append(c, tx, lab);
      hover(g, `${tl(i)}: ${T().txs[i].kind}, level ${l}`);
      svg.append(g);
      return { g, c, tx, lab };
    });
    E.forEach((e) => svg.append(e.lab));
    const TE0 = 0.9, TE = E.length ? 2.9 : 0.4, ED = 0.55;
    const TS0 = TE0 + TE + 0.5, S = clamp(3.3 / N, 0.18, 0.6), MV = 0.6;
    const dur = TS0 + N * S + MV + 1.0;
    const ts = (i) => TS0 + i * S;
    const et = (k) => TE0 + (E.length > 1 ? (k / (E.length - 1)) * (TE - ED) : 0);
    const pl = player(dur, (t) => {
      const mv = nodes.map((_, i) => ease(prog(t, ts(i) + 0.15, MV)));
      const cur = mv.map((q, i) => ({ x: lerp(start[i].x, end[i].x, q), y: lerp(start[i].y, end[i].y, q) }));
      let lastEdge = null, drawn = 0;
      E.forEach((e, k) => {
        const g = prog(t, et(k), ED);
        const A = cur[e.u], B = cur[e.v];
        const m = Math.min(mv[e.u], mv[e.v]);
        const dx = B.x - A.x, dy = B.y - A.y, len = Math.hypot(dx, dy) || 1;
        const lift = (1 - m) * -Math.min(110, 0.32 * Math.abs(dx) + 18);
        const S0 = { x: A.x + (dx / len) * 15 * m + 0 * (1 - m), y: A.y + (dy / len) * 15 * m - 15 * (1 - m) };
        const E0 = { x: B.x - (dx / len) * 17 * m, y: B.y - (dy / len) * 17 * m - 16 * (1 - m) };
        const mx = (S0.x + E0.x) / 2;
        e.path.setAttribute("d", `M${S0.x},${S0.y} C${mx},${S0.y + lift} ${mx},${E0.y + lift} ${E0.x},${E0.y}`);
        e.path.setAttribute("stroke-dashoffset", 1 - g);
        const hot = (t >= ts(e.v) - 0.05 && t < ts(e.v) + S + 0.35) || (g > 0 && g < 1);
        e.path.classList.toggle("hot", hot);
        e.path.setAttribute("marker-end", g >= 0.97 ? (hot ? "url(#ahH)" : "url(#ahL)") : "");
        const showLab = g > 0 && t < et(k) + ED + 0.5;
        e.lab.setAttribute("opacity", showLab ? 1 : 0);
        e.lab.setAttribute("x", mx); e.lab.setAttribute("y", (S0.y + E0.y) / 2 + lift * 0.75 - 4);
        if (g > 0) { drawn++; lastEdge = e; }
      });
      let deciding = -1;
      nodes.forEach((nd, i) => {
        const appear = prog(t, 0.08 * i * (8 / Math.max(8, N)), 0.3);
        nd.g.setAttribute("opacity", appear);
        nd.g.setAttribute("transform", `translate(${cur[i].x},${cur[i].y})`);
        const dec = t >= ts(i) && t < ts(i) + S + 0.5;
        if (t >= ts(i) && t < ts(i) + S) deciding = i;
        nd.c.setAttribute("stroke", dec ? "#5546D6" : "#14213D");
        nd.c.setAttribute("stroke-width", dec ? 3.5 : 1.5);
        const filled = mv[i] >= 0.999;
        nd.c.setAttribute("fill", filled ? bcol(lv[i]) : "#fff");
        nd.tx.setAttribute("fill", filled ? "#fff" : "#14213D");
        nd.lab.textContent = t >= ts(i) && !filled ? "L" + lv[i] : "";
      });
      for (let x = 0; x < lay.L; x++) {
        const arrived = lv.some((l, i) => l === x && mv[i] > 0.5);
        lvLab[x].setAttribute("opacity", arrived ? 1 : 0);
        bands[x].setAttribute("opacity", arrived ? 0.07 : 0);
      }
      if (t < TE0) return `${N} transactions in block order. Every dependency edge will point from an earlier transaction to a later one.`;
      if (t < TE0 + TE) return E.length ? `Detection threads emit dependency edges: ${drawn} of ${E.length}. Latest: ${tl(lastEdge.u)} → ${tl(lastEdge.v)}, both touch ${lastEdge.key}. (Shown one after another so you can follow them; on the GPU they are written together.)` : "No two transactions conflict under this policy: the graph has no edges.";
      if (deciding >= 0) {
        const pr = preds[deciding];
        return pr.length ? `Sweep at ${tl(deciding)}: level = 1 + highest level among its predecessors ${pr.map((u) => tl(u) + " (L" + lv[u] + ")").join(", ")} = ${lv[deciding]}. It moves to column ${lv[deciding]}.`
          : `Sweep at ${tl(deciding)}: no predecessors, so level 0. It can run in the first batch.`;
      }
      if (t < ts(N - 1) + S + MV) return "The sweep visits the transactions in block order; every predecessor already has its level when it is needed.";
      return `Done: ${lay.L} level${lay.L > 1 ? "s" : ""} from ${E.length} edges. All edges point left to right, so each column is a batch whose transactions can run in parallel.`;
    }, "Building the dependency DAG");
    root.append(pl.el, h("div", { class: "sheet", style: "overflow:auto" }, svg));
  }

  // circle layout shared by the conflict graph and Jones–Plassmann
  function ring(N) {
    const R = Math.max(120, N * 15), cx = R + 50, cy = R + 40;
    const pt = (i) => ({ x: cx + R * Math.cos(-Math.PI / 2 + (2 * Math.PI * i) / Math.max(1, N)), y: cy + R * Math.sin(-Math.PI / 2 + (2 * Math.PI * i) / Math.max(1, N)) });
    const out = (i) => ({ x: cx + (R + 30) * Math.cos(-Math.PI / 2 + (2 * Math.PI * i) / Math.max(1, N)), y: cy + (R + 30) * Math.sin(-Math.PI / 2 + (2 * Math.PI * i) / Math.max(1, N)) });
    return { pt, out, W: 2 * R + 100, H: 2 * R + 80 };
  }

  // ---- stage 4: the conflict graph appears key by key
  function liveConflict(root) {
    const p = P(), N = n(), G = ring(N);
    const svg = s("svg", { width: G.W, height: G.H, role: "img", "aria-label": "conflict graph being built" });
    const byKey = new Map();
    p.conflicts.forEach(([a, b, key]) => { if (!byKey.has(key)) byKey.set(key, []); byKey.get(key).push([a, b]); });
    const keys = [...byKey.keys()];
    const w = clamp(5.2 / Math.max(1, keys.length), 0.3, 1.4), T0 = 0.8;
    const dur = T0 + keys.length * w + 1.2;
    const lines = [];
    keys.forEach((key, ki) => byKey.get(key).forEach(([a, b]) => {
      const A = G.pt(a), B = G.pt(b);
      const ln = s("line", { x1: A.x, y1: A.y, x2: B.x, y2: B.y, class: "edge", pathLength: 1, "stroke-dasharray": "1 1" });
      hover(ln, `${tl(a)} — ${tl(b)} conflict on ${key}`);
      svg.append(ln); lines.push({ ln, ki, a, b });
    }));
    const nodes = Array.from({ length: N }, (_, i) => {
      const A = G.pt(i), g = s("g", { class: "node", transform: `translate(${A.x},${A.y})` });
      const c = s("circle", { r: 16, fill: "#fff" });
      g.append(c, s("text", { "text-anchor": "middle", dy: 4 }, tl(i)));
      svg.append(g); return c;
    });
    const pl = player(dur, (t) => {
      const ki = Math.floor((t - T0) / w);
      lines.forEach((l) => { const q = prog(t, T0 + l.ki * w, w * 0.7); l.ln.setAttribute("stroke-dashoffset", 1 - q); l.ln.classList.toggle("hot", l.ki === ki && q > 0); });
      const inKey = new Set(); if (ki >= 0 && ki < keys.length) byKey.get(keys[ki]).forEach(([a, b]) => { inKey.add(a); inKey.add(b); });
      nodes.forEach((c, i) => { c.setAttribute("stroke", inKey.has(i) ? "#5546D6" : "#14213D"); c.setAttribute("stroke-width", inKey.has(i) ? 3.5 : 1.5); });
      if (!keys.length) return "No conflicts under this policy: every transaction can run at once.";
      if (t < T0) return `${N} transactions. Edges are added one state key at a time.`;
      if (ki < keys.length) { const pr = byKey.get(keys[ki]); return `Key ${keys[ki]}: ${inKey.size} transactions access it in conflicting modes → ${pr.length} undirected edge${pr.length > 1 ? "s" : ""}.`; }
      return `${p.conflicts.length} conflict edges from ${keys.length} keys. A set of transactions with no edge between them can run together.`;
    }, "The full conflict graph");
    root.append(pl.el, h("div", { class: "sheet" }, svg));
  }

  // ---- stage 4: Jones–Plassmann colouring, round after round
  function liveJP(root, J) {
    const p = P(), N = n(), G = ring(N);
    const Rn = J.rounds.length;
    const colorOf = new Array(N).fill(-1);
    J.rounds.forEach((r, k) => r.winners.forEach((w) => (colorOf[w] = k)));
    const actOf = J.rounds.map((r) => new Set(r.active));
    const svg = s("svg", { width: G.W, height: G.H, role: "img", "aria-label": "Jones–Plassmann colouring" });
    const lines = p.conflicts.map(([a, b, key]) => { const A = G.pt(a), B = G.pt(b); const ln = s("line", { x1: A.x, y1: A.y, x2: B.x, y2: B.y, class: "edge" }); hover(ln, `${tl(a)} — ${tl(b)} conflict on ${key}`); svg.append(ln); return { ln, a, b }; });
    const nodes = Array.from({ length: N }, (_, i) => {
      const A = G.pt(i), O = G.out(i);
      const g = s("g", { class: "node", transform: `translate(${A.x},${A.y})` });
      const c = s("circle", { r: 16, fill: "#fff" });
      const tx = s("text", { "text-anchor": "middle", dy: 4 }, tl(i));
      const rk = s("text", { x: O.x - A.x, y: O.y - A.y + 4, "text-anchor": "middle", "font-size": 11, fill: "#5546D6", style: "font-weight:400", opacity: 0 }, "#" + (J.rank[i] + 1));
      g.append(c, tx, rk); svg.append(g);
      hover(g, `${tl(i)}: priority rank ${J.rank[i] + 1}, colour ${colorOf[i]}`);
      return { g, c, tx, rk };
    });
    const strip = h("div", { class: "classes" });
    const rowsC = J.rounds.map((r, k) => { const el = h("div", { class: "cls" }, h("span", { class: "cl", style: `background:${bcol(k)}` }, "colour " + k), r.winners.map((w) => h("span", { class: "tx" }, tl(w)))); strip.append(el); return el; });
    const T0 = 1.3, Dr = clamp(6.2 / Math.max(1, Rn), 0.9, 1.9);
    const dur = T0 + Rn * Dr + 1.3;
    const pl = player(dur, (t) => {
      nodes.forEach((nd, i) => nd.rk.setAttribute("opacity", prog(t, 0.2 + (J.rank[i] / Math.max(1, N)) * 0.8, 0.2)));
      const r = t < T0 ? -1 : Math.min(Rn, Math.floor((t - T0) / Dr));
      const q = r < 0 || r >= Rn ? 0 : (t - T0 - r * Dr) / Dr;
      const isCol = (i) => colorOf[i] >= 0 && (colorOf[i] < r || (colorOf[i] === r && q >= 0.62) || r >= Rn);
      const act = r >= 0 && r < Rn ? actOf[r] : null;
      const win = r >= 0 && r < Rn ? new Set(J.rounds[r].winners) : null;
      nodes.forEach((nd, i) => {
        const col = isCol(i);
        let fill = "#fff", stroke = "#14213D", sw = 1.5, op = 1, sc = 1;
        if (col) { fill = bcol(colorOf[i]); if (colorOf[i] === r) sc = 1 + 0.28 * Math.sin(Math.PI * prog(q, 0.62, 0.3)); }
        else if (act && act.has(i)) {
          if (q < 0.33) { stroke = "#5546D6"; sw = 2 + 1.5 * Math.abs(Math.sin(q * 30)); }
          else if (win.has(i)) { stroke = "#5546D6"; sw = 4.5; }
          else op = 0.45;
        }
        nd.c.setAttribute("fill", fill); nd.c.setAttribute("stroke", stroke); nd.c.setAttribute("stroke-width", sw);
        nd.tx.setAttribute("fill", col ? "#fff" : "#14213D");
        nd.g.setAttribute("opacity", op);
        const A = G.pt(i);
        nd.g.setAttribute("transform", `translate(${A.x},${A.y}) scale(${sc})`);
      });
      lines.forEach((l) => {
        const gone = isCol(l.a) || isCol(l.b);
        l.ln.setAttribute("opacity", gone ? 0.08 : 1);
        l.ln.classList.toggle("hot", !gone && act && act.has(l.a) && act.has(l.b) && q < 0.62 && (q < 0.33 || win.has(l.a) || win.has(l.b)));
      });
      rowsC.forEach((el, k) => (el.style.opacity = k < r || (k === r && q >= 0.62) || r >= Rn ? 1 : 0.12));
      if (r < 0) return `Each transaction gets a unique priority (#1 is highest${st.jpMode === "index" ? ": here, block order" : ": here, random"}). No graph is built on the GPU; keys stand in for edges.`;
      if (r >= Rn) return `${Rn} colours in ${Rn} rounds. Each colour class is an independent set: a batch that runs in parallel.${st.jpMode === "index" ? " With block-order priority the colours equal the levels." : " Random priority may reorder transactions."}`;
      const R = J.rounds[r];
      if (q < 0.33) return `Round ${r}, phase A: the ${R.active.length} uncoloured transactions each write their priority into every key they touch (atomic maximum), all at once.`;
      if (q < 0.62) return `Round ${r}, phase B: a transaction wins if it holds the maximum on all its conflicting keys. Winners: ${R.winners.map(tl).join(", ")}.`;
      return `Round ${r}, phase C: the winners take colour ${r} together; no two of them conflict. ${R.active.length - R.winners.length} transaction${R.active.length - R.winners.length === 1 ? "" : "s"} left.`;
    }, "Jones–Plassmann colouring");
    root.append(pl.el, h("div", { class: "row" }, h("div", { class: "sheet" }, svg), h("div", { class: "sheet", style: "flex:1 1 240px" }, h("h3", {}, "Colour classes = batches"), strip)));
  }

  // ---------------------------------------------------------------- stages
  const STAGES = [
    {
      title: "The pipeline", part: "overview",
      subs: () => 1,
      view(root) {
        root.append(h("h2", {}, "From a block to parallel batches"));
        root.append(h("p", { class: "lede" }, `This walk-through runs the project's pipeline on a small block (${n()} transactions) and shows every intermediate result. Use Next / Back or the arrow keys. Change the policy at the top at any time: every stage is recomputed for it.`));
        const steps = [["Block", "transactions in block order"], ["1 Extract", "read/write set per transaction"], ["2 Sort + detect", "group accesses by state key, emit conflict edges"],
          ["3 CSR", "dependency DAG"], ["4 Batches", "levels or Jones–Plassmann colours"], ["Execute", "batches or dataflow, checked against serial"]];
        liveOverview(root, steps);
        const p = P().stats;
        const k = h("div", { class: "sheet" }, h("h3", {}, `This block under ${st.pol}`),
          h("div", { class: "kpi" }, h("b", {}, n()), h("span", {}, "transactions")),
          h("div", { class: "kpi" }, h("b", {}, p.records), h("span", {}, "state accesses")),
          h("div", { class: "kpi" }, h("b", {}, p.edges), h("span", {}, "dependency edges")),
          h("div", { class: "kpi" }, h("b", {}, p.batches), h("span", {}, "batches (levels)")),
          h("div", { class: "kpi" }, h("b", {}, (n() / p.batches).toFixed(1)), h("span", {}, "transactions per batch")));
        root.append(k);
      },
      notes: () => [
        h("h3", {}, "What the project does"),
        h("p", {}, "A blockchain executes a block one transaction after another. Two transactions can run at the same time only if they do not conflict: they touch no common piece of state, or both only read it. The pipeline finds every conflict and groups the transactions into batches that can run in parallel without changing the result."),
        h("h3", {}, "Where the numbers come from"),
        h("p", {}, "Everything on these pages was produced by the project's code (", h("code", {}, "build/conduit_trace"), ") on this block: the GPU kernel bodies through the host emulation, checked against the CPU stages."),
        h("div", { class: "now" }, T().source),
        cite("DGHK17", "GEL23"),
      ],
    },
    {
      title: "The block", part: "input",
      subs: () => 1,
      view(root) {
        root.append(h("h2", {}, "The block, in order"));
        root.append(h("p", { class: "lede" }, "Order matters: the parallel result must equal running these transactions top to bottom. The pipeline only reads the sender, the receiver, the called function and, for token calls, the decoded arguments."));
        root.append(h("div", { class: "sheet" }, txTable()));
      },
      notes: () => [
        h("h3", {}, "Input"),
        h("p", {}, "Rows come from the Ethereum transaction export (sender, receiver, 4-byte function selector, gas used) joined with the call arguments for ERC-20 transfer, approve and transferFrom."),
        h("h3", {}, "Why order is kept"),
        h("p", {}, "Every node must reach the same state as executing the block serially, so a parallel schedule has to be equivalent to this order (conflict-serializable to it)."),
        cite("WOOD", "BHG87", "BQ"),
      ],
    },
    {
      title: "Read and write sets", part: "stage 1",
      subs: () => 3,
      view(root) {
        const p = P();
        root.append(h("h2", {}, "Stage 1: what each transaction touches"));
        if (st.sub === 0) {
          root.append(h("p", { class: "lede" }, `One GPU thread per transaction infers its accesses under ${st.pol}. ${({ P1: "P1 locks whole accounts: the sender and the called contract.", P2: "P2 locks individual slots: nonce, ETH balance, a token balance, an allowance. Calls it cannot decode fall back to the whole contract.", P3: "P3 is P2, but balance updates are commutative: two additions to one balance do not conflict." })[st.pol]}`));
          liveRW(root);
          return;
        }
        if (st.sub === 2) {
          root.append(h("p", { class: "lede" }, "Each thread must know where in one shared array to write its records. An exclusive prefix sum of the counts gives every thread its own start position, in a logarithmic number of parallel steps."));
          liveScan(root);
          return;
        }
        root.append(h("p", { class: "lede" }, `One GPU thread per transaction infers its accesses under ${st.pol}. ${({ P1: "P1 locks whole accounts: the sender and the called contract.", P2: "P2 locks individual slots: nonce, ETH balance, a token balance, an allowance. Calls it cannot decode fall back to the whole contract.", P3: "P3 is P2, but balance updates are commutative: two additions to one balance do not conflict." })[st.pol]}`));
        root.append(h("div", { class: "legend" }, h("span", {}, h("i", { class: "sw", style: "background:var(--write)" }), "W write"), h("span", {}, h("i", { class: "sw", style: "background:var(--read)" }), "R read"), h("span", {}, h("i", { class: "sw", style: "background:var(--comm)" }), "C commutative update"), h("span", {}, h("i", { class: "sw", style: "background:var(--thread)" }), "GPU thread")));
        const t = h("table", { class: "t" }, h("thead", {}, h("tr", {}, h("th", {}, "thread"), h("th", {}, "tx"), h("th", {}, "call"), h("th", {}, "accesses (keys ascending)"), st.sub >= 1 ? h("th", { class: "num" }, "count") : null)));
        const tb = h("tbody");
        p.access.forEach((acc, i) => tb.append(h("tr", {}, h("td", {}, h("span", { class: "thread" }, i)), h("td", { class: "tx" }, tl(i)), h("td", {}, T().txs[i].kind),
          h("td", {}, acc.map((a) => modeChip(a.key, a.m))), st.sub >= 1 ? h("td", { class: "num" }, p.counts[i]) : null)));
        t.append(tb);
        root.append(h("div", { class: "sheet" }, t));
        if (st.sub >= 1) {
          const sh = h("div", { class: "sheet" }, h("h3", {}, "Counting, then an exclusive prefix sum"));
          sh.append(arrRow("counts[i] (kernel extract_count, one thread per tx)", p.counts, { sub: (i) => tl(i) }));
          if (st.sub >= 2) sh.append(arrRow("offsets = exclusive scan of counts (CUB DeviceScan): where thread i writes its records", p.offsets, { sub: (i) => (i < n() ? tl(i) : "total") }));
          root.append(sh);
        }
      },
      notes: () => [
        h("div", { class: "now" }, ["Each thread decodes one transaction into (state key, mode) pairs.", "Each thread reports how many records it will write.", "An exclusive prefix sum turns the counts into write offsets, so every thread writes its records without any locking, and the output order never depends on thread timing."][st.sub]),
        h("h3", {}, "On the GPU"),
        h("p", {}, h("code", {}, "k_extract_count"), " → ", h("code", {}, "cub::DeviceScan::ExclusiveSum"), " → ", h("code", {}, "k_extract_write"), ". The policy code is the same function on CPU and GPU (", h("code", {}, "k_tx_access"), "), unit-tested equal to the CPU port."),
        h("h3", {}, "Why this rule"),
        h("p", {}, "Two accesses conflict unless both are reads or both are commutative updates: the read/write conflict rule of serializability theory, extended with commutativity for P3. Contract-level fallback is conservative: it can only add conflicts."),
        st.sub === 2 ? h("p", {}, "The animation shows the Hillis–Steele scan, the simplest parallel scan (log₂ n steps). CUB's DeviceScan computes the same result with a work-efficient single-pass scan.") : null,
        cite("BHG87", "WEI88", "PKS21", "BLE90", ...(st.sub === 2 ? ["HS86", "MG16S"] : [])),
      ],
    },
    {
      title: "Records", part: "stage 1",
      subs: () => 1,
      view(root) {
        const p = P();
        root.append(h("h2", {}, "Records: one row per access"));
        root.append(h("p", { class: "lede" }, `${p.records.length} records, packed as two 64-bit words: hi = state key, lo = tx index × 8 + mode. Every thread writes its own records, starting at its offset; the result is in block order.`));
        liveRecords(root);
        const t = h("table", { class: "t" }, h("thead", {}, h("tr", {}, h("th", { class: "num" }, "#"), h("th", {}, "state key (hi)"), h("th", {}, "tx"), h("th", {}, "mode"), h("th", { class: "num" }, "lo = tx·8 + mode"))));
        const tb = h("tbody");
        p.records.forEach((r, i) => tb.append(h("tr", {}, h("td", { class: "num" }, i), h("td", {}, h("code", {}, r.key)), h("td", { class: "tx" }, tl(r.tx)), h("td", {}, modeChip("", r.m)),
          h("td", { class: "num" }, r.tx * 8 + { R: 1, C: 2, W: 4 }[r.m]))));
        t.append(tb);
        root.append(h("div", { class: "sheet" }, h("h3", {}, "The packed records"), t));
      },
      notes: () => [
        h("div", { class: "now" }, "Records sorted as 128-bit integers group by key and, inside a key, by block position. That one property is what the next stages rely on."),
        h("h3", {}, "Why pack"),
        h("p", {}, "Fixed-width integer records can be radix-sorted. A stable sort by the key alone keeps block order inside each key, because the records were written in block order."),
        cite("MG11"),
      ],
    },
    {
      title: "Sort by key", part: "stage 2",
      subs: () => 1,
      view(root) {
        root.append(h("h2", {}, "Stage 2a: radix sort by state key"));
        root.append(h("p", { class: "lede" }, "A least-significant-digit radix sort makes one stable pass per digit. Here each key is replaced by its rank and sorted two bits at a time so every pass is visible; the GPU sorts the 64-bit key itself, several bits per pass."));
        liveSort(root);
      },
      notes: () => [
        h("div", { class: "now" }, "Watch the records move pass by pass. Use Pause, Restart or the slider to replay any moment."),
        h("h3", {}, "On the GPU"),
        h("p", {}, h("code", {}, "cub::DeviceRadixSort::SortPairs"), " (key = state key, value = tx/mode word). Radix sort replaces the O(n²) pairwise comparison of the original plan: after sorting, conflicts can only be between neighbours."),
        h("h3", {}, "Fairness"),
        h("p", {}, "The CPU baselines include a sequential radix sort too (seq-radix), so a GPU speedup is not credited for the change from comparison sort to radix sort."),
        cite("MG11"),
      ],
    },
    {
      title: "Groups and runs", part: "stage 2",
      subs: () => 3,
      view(root) {
        const p = P();
        root.append(h("h2", {}, "Stage 2b: group heads, run heads, prefix sums"));
        root.append(h("p", { class: "lede" }, "A group = all records of one key. A run = consecutive reads, or consecutive commutative updates, or a single write. Flags mark where groups and runs start; inclusive prefix sums turn the flags into group and run numbers."));
        if (st.sub === 0) { liveGroups(root); return; }
        const rowH = 30;
        const cols = "44px 1fr 56px 46px 66px 66px 66px 66px";
        const head = h("div", { class: "rechead", style: `grid-template-columns:${cols}` }, h("span", {}, "pos"), h("span", {}, "key"), h("span", {}, "tx"), h("span", {}, "mode"),
          h("span", { class: "f" }, "group flag"), h("span", { class: "f" }, st.sub >= 1 ? "group #" : ""), h("span", { class: "f" }, "run flag"), h("span", { class: "f" }, st.sub >= 1 ? "run #" : ""));
        const list = h("div", { class: "reclist", style: `height:${p.sorted.length * rowH}px` });
        p.sorted.forEach((r, i) => {
          list.append(h("div", { class: "rec" + ((p.gscan[i] - 1) % 2 ? "" : " g0"), style: `transform:translateY(${i * rowH}px);grid-template-columns:${cols}` },
            h("span", { class: "i" }, i), h("span", {}, r.key), h("span", { class: "tx" }, tl(r.tx)), h("span", {}, modeChip("", r.m)),
            h("span", { class: "f" + (p.gflag[i] ? " one" : "") }, p.gflag[i]), h("span", { class: "f" }, st.sub >= 1 ? p.gscan[i] - 1 : ""),
            h("span", { class: "f" + (p.rflag[i] ? " one" : "") }, p.rflag[i]), h("span", { class: "f" }, st.sub >= 1 ? p.rscan[i] - 1 : "")));
        });
        root.append(h("div", { class: "sheet" }, head, list));
        if (st.sub >= 2) {
          root.append(h("div", { class: "sheet" }, h("h3", {}, "Scatter: where every group and run starts"),
            arrRow("gstart[g] (last entry = number of records, a sentinel)", p.gstart, { sub: (i) => "g" + i }),
            arrRow("rstart[r]", p.rstart, { sub: (i) => "r" + i })));
        }
      },
      notes: () => [
        h("div", { class: "now" }, ["One thread per record compares itself with its left neighbour: new key → group flag; write, or a mode change → run flag.", "Inclusive prefix sums (CUB) number the groups and runs.", "Each head record writes its position: group and run boundaries without any searching."][st.sub]),
        h("h3", {}, "On the GPU"),
        h("p", {}, h("code", {}, "k_head_flags"), ", two ", h("code", {}, "cub::DeviceScan::InclusiveSum"), ", ", h("code", {}, "k_scatter_starts"), ". Prefix sums are how irregular work is laid out in parallel without locks."),
        cite("BLE90", "MGG12"),
      ],
    },
    {
      title: "Conflict edges", part: "stage 2",
      subs: () => 2,
      view(root) {
        const p = P();
        root.append(h("h2", {}, "Stage 2c: conflict edges (the reduced rule)"));
        root.append(h("p", { class: "lede" }, "Inside a key, every access depends on every access of the previous run, and on nothing earlier. Older conflicts are implied by a path, so the dependency order is the same as with all conflicting pairs, with far fewer edges."));
        const groups = [];
        for (let g = 0; g + 1 < p.gstart.length; g++) {
          const b = p.gstart[g], e = p.gstart[g + 1];
          const runs = [];
          for (let r = 0; r + 1 < p.rstart.length; r++) if (p.rstart[r] >= b && p.rstart[r] < e) runs.push([p.rstart[r], p.rstart[r + 1]]);
          if (runs.length > 1) groups.push({ key: p.sorted[b].key, runs });
        }
        const sh = h("div", { class: "sheet" }, h("h3", {}, `${groups.length} keys with more than one run (the others produce no edges)`));
        for (const gr of groups) {
          sh.append(h("div", { class: "group-title" }, gr.key));
          const row = h("div", { class: "runs" });
          gr.runs.forEach(([a, b], k) => {
            if (k) row.append(h("div", { class: "runsep", title: "every record here depends on every record of the previous run" }, "→"));
            const run = h("div", { class: "run" });
            for (let i = a; i < b; i++) run.append(h("span", {}, h("span", { class: "tx" }, tl(p.sorted[i].tx)), " ", modeChip("", p.sorted[i].m)));
            row.append(run);
          });
          sh.append(row);
        }
        root.append(sh);
        if (st.sub >= 1) {
          const t = h("table", { class: "t" }, h("thead", {}, h("tr", {}, h("th", {}, "thread = edge e"), h("th", {}, "finds record c by binary search in eoff"), h("th", {}, "previous-run record a"), h("th", {}, "edge"), h("th", {}, "key"))));
          const tb = h("tbody");
          p.edges.forEach((e, k) => tb.append(h("tr", {}, h("td", {}, h("span", { class: "thread" }, k)), h("td", { class: "num", style: "text-align:left" }, "c = " + e[3]), h("td", {}, "a = " + e[2]),
            h("td", { class: "tx" }, `${tl(e[0])} → ${tl(e[1])}`), h("td", {}, h("code", {}, p.sorted[e[3]].key)))));
          t.append(tb);
          root.append(h("div", { class: "sheet" }, h("h3", {}, "Load-balanced emission: one GPU thread per output edge"),
            arrRow("ecount[i] = size of the previous run (0 for the first run of a key)", p.ecount, { sub: (i) => "r" + i }),
            arrRow("eoff = exclusive scan of ecount", p.eoff, { sub: (i) => (i < p.ecount.length ? "r" + i : "E") }), h("br"), t));
        }
      },
      notes: () => [
        h("div", { class: "now" }, st.sub === 0 ? "Adjacent runs of the same key produce edges; a run of reads (or of commutative updates) is not ordered internally." : "Each output edge gets its own thread, which finds its record by binary search over the per-record offsets. The work per thread is the same however hot a key is."),
        h("h3", {}, "Phase 9: skew"),
        h("p", {}, "A naive kernel gives each key group to one thread. On real data one key (the USDT contract under P1) holds hundreds of thousands of records, so one thread does almost all the work. The project measures three mappings: one thread per group (before), per record, and per edge (after)."),
        h("h3", {}, "On the GPU"),
        h("p", {}, h("code", {}, "k_record_count"), ", ", h("code", {}, "DeviceScan::ExclusiveSum"), ", ", h("code", {}, "k_edge_emit"), " (or ", h("code", {}, "k_group_count/k_group_emit"), " for the before-number)."),
        cite("MG16", "MGG12", "BLE90"),
      ],
    },
    {
      title: "CSR graph", part: "stage 3",
      subs: () => 3,
      view(root) {
        const p = P(), N = n();
        root.append(h("h2", {}, "Stage 3: edges into a compressed sparse row graph"));
        root.append(h("p", { class: "lede" }, `Edge u → v becomes the integer u·n + v (n = ${N}), which sorts exactly like (u, v) but needs fewer radix passes. Sorting and removing duplicates leaves one entry per dependency; binary searches give each row's start.`));
        const sh = h("div", { class: "sheet" });
        sh.append(arrRow("raw edges as emitted (u→v), duplicates possible when two transactions share several keys", p.edges.map((e) => `${e[0]}→${e[1]}`)));
        sh.append(arrRow("u·n + v", p.lin));
        if (st.sub >= 1) {
          sh.append(arrRow("sorted (cub::DeviceRadixSort::SortKeys)", p.lin_sorted));
          sh.append(arrRow(`unique (cub::DeviceSelect::Unique): ${p.unique.length} edges`, p.unique, { sub: (i) => `${Math.floor(p.unique[i] / N)}→${p.unique[i] % N}` }));
        }
        root.append(sh);
        if (st.sub >= 2) {
          root.append(h("div", { class: "sheet" }, h("h3", {}, "CSR: successors of every transaction"),
            arrRow("row_offsets[u] = first edge of row u (lower_bound of u·n)", p.row_offsets, { sub: (i) => (i < N ? tl(i) : "end") }),
            arrRow("cols = v of every edge, row by row", p.cols, { sub: (i) => { let u = 0; while (p.row_offsets[u + 1] <= i) u++; return "from " + tl(u); } })));
        }
      },
      notes: () => [
        h("div", { class: "now" }, ["The raw edge list, as stage 2 wrote it.", "Radix sort and unique: duplicates disappear, edges are in row order.", "CSR: row_offsets plus column indices, the standard sparse-matrix layout. This is what is copied back to the CPU."][st.sub]),
        h("h3", {}, "On the GPU"),
        h("p", {}, h("code", {}, "k_edge_to_linear"), ", ", h("code", {}, "SortKeys"), ", ", h("code", {}, "Unique"), ", ", h("code", {}, "k_csr_cols"), ", ", h("code", {}, "k_csr_rows"), ". The output is bit-identical to the CPU build, and checked stage by stage (", h("code", {}, "conduit_gpu stages"), ")."),
        cite("SAAD03", "MG11"),
      ],
    },
    {
      title: "Dependency DAG", part: "stage 3",
      subs: () => 1,
      view(root) {
        root.append(h("h2", {}, "The dependency graph"));
        root.append(h("p", { class: "lede" }, "First the detection threads emit the dependency edges, then the levelling sweep visits the transactions in block order and moves each one into its column. Hover an edge to see which state key causes it."));
        liveDAG(root);
      },
      notes: () => [
        h("div", { class: "now" }, `${P().stats.edges} edges after removing duplicates. Each transaction's column is its level: 1 + the highest level of its predecessors.`),
        h("h3", {}, "What an edge means"),
        h("p", {}, "u → v: v must not run before u finishes, because they access a common key in conflicting modes and u comes first in the block."),
        cite("BHG87", "KA99"),
      ],
    },
    {
      title: "Levels", part: "stage 4",
      subs: () => P().stats.batches + 1,
      view(root) {
        const p = P(), L = p.stats.batches;
        const lv = st.sub === 0 ? null : st.sub - 1;
        root.append(h("h2", {}, "Stage 4: levels are the batches"));
        root.append(h("p", { class: "lede" }, "level(v) = 1 + the highest level of v's predecessors (0 if none): the length of the longest dependency chain ending at v. Running level 0, then 1, then 2 … gives exactly the serial result, and no order-preserving schedule needs fewer batches."));
        root.append(h("div", { class: "sheet" }, dagSVG({ level: lv == null ? L - 1 : lv, color: true })));
        const t = h("table", { class: "t" }, h("thead", {}, h("tr", {}, h("th", {}, "batch"), h("th", {}, "transactions that can run together"))));
        const tb = h("tbody");
        for (let b = 0; b < L; b++) {
          const members = p.levels.map((l, i) => (l === b ? i : -1)).filter((i) => i >= 0);
          tb.append(h("tr", { class: lv === b ? "hl" : null }, h("td", {}, h("i", { class: "sw", style: `background:${bcol(b)}` }), " ", b), h("td", {}, members.map((i) => h("span", { class: "tx", style: "margin-right:10px" }, tl(i))))));
        }
        t.append(tb);
        root.append(h("div", { class: "sheet" }, t));
      },
      notes: () => {
        const lv = st.sub - 1;
        return [
          h("div", { class: "now" }, st.sub === 0 ? "All levels at once. Next walks through the ascending sweep, one level per step." : `The sweep has reached level ${lv}: these transactions have all their predecessors in earlier levels.`),
          h("h3", {}, "Which algorithm, and where"),
          h("p", {}, "The project's default is a sequential ascending sweep on the CPU: one pass over the edges. Kahn's algorithm on the GPU gives identical levels but needs one synchronised round per level; on real blocks with hundreds of thousands of levels that loses to the sweep (Phase 7, Phase 8)."),
          cite("KAHN62", "VAL90", "GSO12"),
        ];
      },
    },
    {
      title: "Conflict graph", part: "stage 4",
      subs: () => 1,
      view(root) {
        root.append(h("h2", {}, "The full conflict graph"));
        root.append(h("p", { class: "lede" }, `Graph colouring needs every conflicting pair as an undirected edge (${P().conflicts.length} here), not just the reduced dependencies: two transactions with the same colour must never conflict.`));
        liveConflict(root);
      },
      notes: () => [
        h("div", { class: "now" }, "A colour class is an independent set: transactions with no conflict between them, which can run at the same time."),
        h("h3", {}, "On the GPU, without building this graph"),
        h("p", {}, "The full graph has a clique per hot key (hundreds of thousands of transactions, billions of edges). The project's Jones–Plassmann kernel never builds it: it works on the sorted key groups, where 'conflicts with everything else in this group' is a single atomic maximum."),
        cite("JP93", "LUBY86"),
      ],
    },
    {
      title: "Jones–Plassmann", part: "stage 4",
      subs: () => (st.jpMode === "index" ? P().jp_index : P().jp_random).rounds.length + 1,
      view(root) {
        const J = st.jpMode === "index" ? P().jp_index : P().jp_random;
        const r = st.sub - 1;
        if (st.sub === 0) {
          root.append(h("h2", {}, "Jones–Plassmann colouring"));
          const tg = h("span", { class: "seg", style: "border-color:var(--rule)" },
            ...["index", "random"].map((m) => h("button", { "aria-pressed": st.jpMode === m, style: "color:var(--ink)", onclick: () => { st.jpMode = m; st.sub = 0; render(); } }, m === "index" ? "priority = block order" : "random priority")));
          root.append(h("p", { class: "lede" }, "Each round, every uncoloured transaction whose priority beats all its uncoloured conflicting neighbours takes the round's colour, all in parallel. Next steps through the rounds one at a time. ", tg));
          liveJP(root, J);
          return;
        }
        root.append(h("h2", {}, "Jones–Plassmann colouring, round by round"));
        const toggle = h("span", { class: "seg", style: "border-color:var(--rule)" },
          ...["index", "random"].map((m) => h("button", { "aria-pressed": st.jpMode === m, style: "color:var(--ink)", onclick: () => { st.jpMode = m; st.sub = 0; render(); } }, m === "index" ? "priority = block order" : "random priority")));
        root.append(h("p", { class: "lede" }, "Each transaction gets a unique priority. In every round, a transaction whose priority beats every uncoloured transaction it conflicts with takes the round's colour; all of them do so in parallel. ", toggle));
        const colored = new Map();
        for (let k = 0; k < Math.max(0, r + 1); k++) for (const w of J.rounds[k].winners) colored.set(w, k);
        const active = r >= 0 ? new Set(J.rounds[r].active) : new Set(Array.from({ length: n() }, (_, i) => i));
        const winners = r >= 0 ? new Set(J.rounds[r].winners) : null;
        const svg = conflictSVG((i) => (colored.has(i) ? colored.get(i) : null), active, winners, J.rank);
        const row = h("div", { class: "row" }, h("div", { class: "sheet" }, svg));
        if (r >= 0) {
          const R = J.rounds[r];
          const t = h("table", { class: "t" }, h("thead", {}, h("tr", {}, h("th", {}, "contested key"), h("th", {}, "uncoloured accessors (priority rank)"), h("th", {}, "local maximum"))));
          const tb = h("tbody");
          for (const g of R.groups) tb.append(h("tr", {}, h("td", {}, h("code", {}, g.key)),
            h("td", {}, g.acc.map((a) => h("span", { style: "margin-right:8px;white-space:nowrap" }, h("span", { class: "tx" }, tl(a.tx)), " ", modeChip("#" + (J.rank[a.tx] + 1), a.m)))),
            h("td", {}, g.acc.filter((a) => a.ok).map((a) => h("span", { class: "tx", style: "margin-right:8px;color:var(--ok)" }, tl(a.tx))))));
          t.append(tb);
          row.append(h("div", { class: "sheet", style: "flex:1 1 360px" }, h("h3", {}, `Round ${r}: ${R.active.length} uncoloured, ${R.winners.length} take colour ${r}`),
            h("p", {}, "Winners: ", R.winners.map((w) => h("span", { class: "tx", style: "margin-right:8px;color:" + bcol(r) }, tl(w)))), R.groups.length ? t : h("p", {}, "No key is contested: every remaining transaction wins.")));
        }
        root.append(row);
        if (r === J.rounds.length - 1) {
          const same = JSON.stringify(J.color) === JSON.stringify(P().levels);
          root.append(h("div", { class: "sheet" }, h("h3", {}, `Done: ${J.rounds.length} colours`),
            h("span", { class: "check" + (P().stats.jp_random_conflict_free ? "" : " no") }, h("b", {}, P().stats.jp_random_conflict_free ? "✓" : "✗"), "every colour class is conflict-free (checked from the access sets)"),
            st.jpMode === "index" ? h("span", { class: "check" + (same ? "" : " no") }, h("b", {}, same ? "✓" : "✗"), "colours equal the levels: block-order priority reproduces the order-preserving schedule") :
              h("span", { class: "check" }, h("b", {}, "!"), "random priorities may reorder transactions: valid for a block builder, not for replaying a fixed block")));
        }
      },
      notes: () => {
        const J = st.jpMode === "index" ? P().jp_index : P().jp_random;
        const r = st.sub - 1;
        return [
          h("div", { class: "now" }, r < 0 ? "The whole colouring plays live. Next goes through it again one round per step, with the contested keys of each round." : `Round ${r}: ${J.rounds[r].winners.length} transactions were local maxima on all their keys and took colour ${r}. They form an independent set.`),
          h("h3", {}, "The test each record makes"),
          h("ul", {}, h("li", {}, "a write wins its key if its priority is the maximum of all uncoloured accessors;"), h("li", {}, "a read wins if it beats every uncoloured write (and commutative update);"), h("li", {}, "a commutative update wins if it beats every uncoloured write (and read).")),
          h("p", {}, "A transaction takes the colour only if it wins on all its keys. Two conflicting transactions can never both win, and the highest-priority remaining transaction always wins, so every round makes progress."),
          h("h3", {}, "On the GPU"),
          h("p", {}, h("code", {}, "jp_phase_a"), " (atomicMax per key), ", h("code", {}, "jp_phase_b"), " (local-maximum test), ", h("code", {}, "jp_phase_c"), " (colour and compact). The round number is packed above the priority so maxima from older rounds lose automatically; nothing is reset between rounds."),
          cite("JP93", "LUBY86", "HKSL14"),
        ];
      },
    },
    {
      title: "Parallel execution", part: "execute",
      subs: () => 1,
      view(root) {
        const E = P().exec;
        root.append(h("h2", {}, "Running the batches: barrier or dataflow"));
        root.append(h("p", { class: "lede" }, `A model on ${E.workers} workers, with each transaction's cost proportional to its gas. With batches, a batch waits for its slowest member (dashed lines are barriers). With dataflow, a transaction starts as soon as its own predecessors finish.`));
        const lvl = P().levels, jp = P().jp_random.color;
        const maxT = Math.max(...[E.serial, E.levels, E.jp, E.dag].map((x) => Math.max(...x.map((y) => y.e))));
        const charts = [
          gantt(E.serial, 1, null, maxT, "Serial, block order", () => "#8A95AB"),
          gantt(E.levels, E.workers, E.levels_barriers, maxT, "Level batches (order-preserving)", (t) => bcol(lvl[t])),
          gantt(E.jp, E.workers, E.jp_barriers, maxT, "Jones–Plassmann colours, random priority (may reorder)", (t) => bcol(jp[t])),
          gantt(E.dag, E.workers, null, maxT, "Dataflow on the dependency DAG (order-preserving)", (t) => bcol(lvl[t])),
        ];
        const sheet = h("div", { class: "sheet" });
        for (const c of charts) sheet.append(c.svg);
        const names = ["serial", "level batches", "JP batches", "dataflow"];
        const scheds = [E.serial, E.levels, E.jp, E.dag];
        const dur = 8.0, run = 7.0;
        const bars = charts.map((c) => [...c.svg.querySelectorAll(".bar")].map((g) => ({ g, r: g.querySelector("rect"), tx: g.querySelector("text"), s0: +g.dataset.s, e0: +g.dataset.e })));
        const pl = player(dur, (t) => {
          const T = (Math.min(t, run) / run) * maxT;
          charts.forEach((c, k) => {
            const cur = c.svg.querySelector(".cursor"), x = c.svg.__left + Math.min(T, c.end) * c.svg.__scale;
            cur.setAttribute("opacity", 1); cur.setAttribute("x1", x); cur.setAttribute("x2", x);
            for (const b of bars[k]) {
              const w = Math.max(0, (Math.min(T, b.e0) - b.s0) * c.svg.__scale - 1);
              b.r.setAttribute("width", T >= b.s0 ? Math.max(2, w) : 0);
              b.g.style.opacity = T >= b.s0 ? 1 : 0;
              if (b.tx) b.tx.style.opacity = T >= b.e0 ? 1 : 0;
            }
          });
          const done = scheds.map((sc) => sc.filter((x) => x.e <= T).length);
          if (t >= run) return `Finished. Serial ${charts[0].end.toFixed(0)} units; level batches ${charts[1].end.toFixed(0)}; JP batches ${charts[2].end.toFixed(0)}; dataflow ${charts[3].end.toFixed(0)}. All parallel runs end in the serial state.`;
          return `Model time ${T.toFixed(0)}: ` + names.map((nm, k) => `${nm} ${done[k]}/${n()} done`).join(", ") + ". Batches wait at each dashed barrier; dataflow starts a transaction as soon as its own predecessors finish.";
        }, "Executing the schedules");
        root.append(pl.el, sheet);
        const ser = charts[0].end;
        const k = h("div", { class: "sheet" }, h("h3", {}, "Makespan (model units) and the state check"),
          ...charts.slice(1).map((c, i) => h("div", { class: "kpi" }, h("b", {}, (ser / c.end).toFixed(2) + "×"), h("span", {}, ["level batches", "Jones–Plassmann batches", "dataflow"][i] + " vs serial"))));
        const H = E.hash;
        k.append(h("div", {},
          h("span", { class: "check" + (H.levels === H.serial ? "" : " no") }, h("b", {}, H.levels === H.serial ? "✓" : "✗"), "level batches end in the serial state"),
          h("span", { class: "check" + (H.dag === H.serial ? "" : " no") }, h("b", {}, H.dag === H.serial ? "✓" : "✗"), "dataflow ends in the serial state"),
          h("span", { class: "check" + (H.jp === H.jp_serial_order ? "" : " no") }, h("b", {}, H.jp === H.jp_serial_order ? "✓" : "✗"), "JP batches equal serial execution in colour order")));
        k.append(h("p", { class: "src" }, `State fingerprints from the project's executor (execute.hpp): serial ${H.serial}, levels ${H.levels}, dataflow ${H.dag}.`));
        root.append(k);
      },
      notes: () => [
        h("div", { class: "now" }, "The four schedules run side by side on the same clock. The batch schedules idle while a batch waits for its slowest transaction; dataflow keeps workers busy while preserving every dependency."),
        h("h3", {}, "This is a model"),
        h("p", {}, "Execution here is the project's toy state machine: each transaction reads and writes its real keys and spins in proportion to its gas. It checks serial equivalence and compares schedules; it is not an Ethereum client."),
        h("h3", {}, "Standard techniques"),
        h("p", {}, "Batches with barriers are bulk-synchronous execution; dataflow is list scheduling on a task graph. Inside a batch, longest-processing-time-first assignment is used."),
        cite("VAL90", "GRA69", "KA99", "DGHK17"),
      ],
    },
    {
      title: "At full scale", part: "results",
      subs: () => 1,
      view(root) {
        const s0 = P().stats;
        root.append(h("h2", {}, "What the same code measured on two full days"));
        root.append(h("p", { class: "lede" }, "The block above is tiny so every step fits on screen. The same pipeline runs on all 3,489,388 transactions of 2026-09-01 and 2026-09-13."));
        const checks = h("div", { class: "sheet" }, h("h3", {}, "Checks on this block"),
          h("span", { class: "check" + (s0.gpu_identical ? "" : " no") }, h("b", {}, s0.gpu_identical ? "✓" : "✗"), "GPU pipeline (emulated kernels) = CPU reference"),
          h("span", { class: "check" + (s0.jp_index_equals_levels ? "" : " no") }, h("b", {}, s0.jp_index_equals_levels ? "✓" : "✗"), "Jones–Plassmann with block-order priority = levels"),
          h("span", { class: "check" + (s0.jp_random_conflict_free ? "" : " no") }, h("b", {}, s0.jp_random_conflict_free ? "✓" : "✗"), "random-priority colours are conflict-free"),
          h("p", {}, `Lower bound on batches for any schedule: ${s0.bound}; levels use ${s0.batches}.`));
        root.append(checks);
        const t = h("table", { class: "t" }, h("thead", {}, h("tr", {}, h("th", {}, "full day"), h("th", { class: "num" }, "transactions"), h("th", { class: "num" }, "batches P1"), h("th", { class: "num" }, "P2"), h("th", { class: "num" }, "P3"))),
          h("tbody", {}, h("tr", {}, h("td", {}, "2026-09-01"), h("td", { class: "num" }, "2,013,158"), h("td", { class: "num" }, "401,223"), h("td", { class: "num" }, "89,380"), h("td", { class: "num" }, "42,309")),
            h("tr", {}, h("td", {}, "2026-09-13"), h("td", { class: "num" }, "1,476,230"), h("td", { class: "num" }, "269,610"), h("td", { class: "num" }, "30,150"), h("td", { class: "num" }, "21,378"))));
        root.append(h("div", { class: "sheet" }, h("h3", {}, "Batches needed for a whole day"), t,
          h("p", { class: "src" }, "From results/phase8/…-real-large (RTX 4050 laptop run with calldata). Finer state granularity (P1 → P3) cuts the batch count by about 10×.")));
        root.append(h("div", { class: "sheet" }, h("h3", {}, "Measured on an RTX 4050 laptop (2 warm-up + 7 runs, wall time, transfers included)"),
          h("ul", {},
            h("li", {}, "Whole day 2026-09-01 (2,013,158 transactions) under P2: GPU 73 ms, against 548 ms for sequential radix sort and 491 ms for the best OpenMP run (7.5× and 6.7×)."),
            h("li", {}, "At 100k transactions: 3.5× (P1), 6.0× (P2) and 5.1× (P3) faster than seq-radix; 2.2×, 3.7× and 2.8× faster than OpenMP on 8 threads."),
            h("li", {}, "The GPU starts to win from about 10k–20k transactions on this machine (2k–5k on a second laptop): below that, its fixed cost per call dominates."),
            h("li", {}, "Phase 9: one thread per key group lets one hot key (the USDT contract) serialise detection; one thread per edge recovers 19–39× on that stage at full-day size."),
            h("li", {}, "Levelling with Kahn on the GPU is 7–29× slower than the CPU sweep, as Phase 7 predicted for deep graphs."),
            h("li", {}, "Profiler: the radix sort is 56–58% of GPU kernel time and the kernels are memory-bound.")),
          h("p", { class: "src" }, "From results/phase8/Nikhil-PC-real-large/summary.md, results/phase8/Nikhil-PC-real/summary.md and results/phase10/Nikhil-PC-profile/.")));
      },
      notes: () => [
        h("div", { class: "now" }, "The project's contribution is characterisation: what conflict detection and scheduling cost on CPU and GPU, and when offloading pays, on two days of Ethereum mainnet."),
        h("h3", {}, "Scope"),
        h("p", {}, "Two days of mainnet, inferred (not traced) read/write sets, one laptop GPU. Claims are limited to that."),
        cite("GH11", "MG16", "GEL23"),
      ],
    },
  ];

  // ---------------------------------------------------------------- shell
  function render() {
    if (anim.cur) { anim.cur.stop(); anim.cur = null; }
    const S = STAGES[st.stage];
    const subs = S.subs();
    st.sub = Math.min(st.sub, subs - 1);
    const root = $("stage");
    root.replaceChildren();
    S.view(root);
    $("notes").replaceChildren(...S.notes());
    // rail
    const rail = $("rail");
    rail.replaceChildren(...STAGES.map((x, i) => h("li", { class: i < st.stage ? "done" : null },
      h("button", { "aria-current": i === st.stage ? "step" : null, onclick: () => { st.stage = i; st.sub = 0; render(); } },
        h("span", { class: "n" }, String(i + 1).padStart(2, "0")), h("span", { class: "t" }, x.title), h("span", { class: "p" }, x.part)))));
    for (const b of $("polSeg").querySelectorAll("button")) b.setAttribute("aria-pressed", b.dataset.p === st.pol);
    $("pos").textContent = `Stage ${st.stage + 1} of ${STAGES.length}: ${S.title}` + (subs > 1 ? `, step ${st.sub + 1} of ${subs}` : "");
    if (anim.cur) $("pos").textContent += ". Space pauses or plays, R restarts.";
    const total = STAGES.length;
    $("barfill").style.width = ((st.stage + (st.sub + 1) / subs) / total) * 100 + "%";
    $("prev").disabled = st.stage === 0 && st.sub === 0;
    $("next").disabled = st.stage === total - 1 && st.sub === subs - 1;
    $("next").textContent = st.sub < subs - 1 ? "Next step" : st.stage < total - 1 ? "Next stage" : "Done";
  }
  function next() {
    const subs = STAGES[st.stage].subs();
    if (st.sub < subs - 1) st.sub++;
    else if (st.stage < STAGES.length - 1) { st.stage++; st.sub = 0; }
    render();
  }
  function prev() {
    if (st.sub > 0) st.sub--;
    else if (st.stage > 0) { st.stage--; st.sub = STAGES[st.stage].subs() - 1; }
    render();
  }

  function init() {
    if (!TRACES.length) {
      $("stage").replaceChildren(h("h2", {}, "No trace data"), h("p", {}, "Generate it with: build/conduit_trace --example (see README, section Visualizer)."));
      return;
    }
    const sel = $("traceSel");
    TRACES.forEach((t, i) => sel.append(h("option", { value: i }, t.title)));
    sel.addEventListener("change", () => { st.ti = +sel.value; st.sub = 0; render(); });
    for (const b of $("polSeg").querySelectorAll("button")) b.addEventListener("click", () => { st.pol = b.dataset.p; render(); });
    $("next").addEventListener("click", next);
    $("prev").addEventListener("click", prev);
    document.addEventListener("keydown", (e) => {
      if (e.target.tagName === "SELECT") return;
      if (e.key === " " && !["BUTTON", "INPUT"].includes(e.target.tagName) && anim.cur) { e.preventDefault(); anim.cur.toggle(); }
      if ((e.key === "r" || e.key === "R") && anim.cur && !e.ctrlKey && !e.metaKey) { e.preventDefault(); anim.cur.restart(); }
      if (e.key === "ArrowRight") { e.preventDefault(); next(); }
      if (e.key === "ArrowLeft") { e.preventDefault(); prev(); }
    });
    $("srcList").replaceChildren(...Object.values(REFS).map((r) => h("li", {}, r)));
    $("srcBtn").addEventListener("click", () => $("srcDlg").showModal());
    render();
  }
  init();
})();
