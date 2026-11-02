// SkinLab UI. All state comes from the Rust controller as `snapshot` events;
// this file only renders it and forwards clicks.

const api = window.__TAURI__ ? tauriApi() : await (await import("./dev-mock.js")).createMock();

function tauriApi() {
  const { invoke } = window.__TAURI__.core;
  const { listen } = window.__TAURI__.event;
  return { invoke, listen: (name, fn) => listen(name, (event) => fn(event.payload)) };
}

const ART = "https://game.gtimg.cn/images/lol/act/img/";
const TIERS = ["epic", "legendary", "mythic", "ultimate", "prestige", "limited", "common"];
const TIER_LABEL = { base: "经典", common: "普通", epic: "史诗", legendary: "传说", mythic: "神话", ultimate: "终极", prestige: "至臻", limited: "限定" };
const CONNECTED = new Set(["ready", "applying"]);
const ROUTINE = new Set(["选择皮肤即生效", "已生效", "已恢复原皮", "正在切换…", "正在恢复…"]);

const PHASES = {
  no_game:        { tone: "idle",   label: "等待对局",  title: "请先打开游戏", body: "进入对局后，SkinLab 会自动识别你的英雄。", art: "idle" },
  multiple_games: { tone: "warn",   label: "多个对局",  title: "检测到多个对局", art: "idle" },
  waiting_hero:   { tone: "busy",   label: "等待英雄",  title: "等待英雄载入", body: "游戏载入完成后会自动接入。", art: "busy" },
  connecting:     { tone: "busy",   label: "正在载入",  title: "正在载入", body: "正在检查游戏版本并建立会话…", art: "busy" },
  ready:          { tone: "ok",     label: "已接入" },
  applying:       { tone: "busy",   label: "正在切换" },
  unsupported:    { tone: "warn",   label: "版本未适配", title: "游戏版本尚未适配", art: "warn" },
  blocked:        { tone: "danger", label: "未接入",    title: "暂时无法接入", art: "warn" },
  closing:        { tone: "busy",   label: "正在退出",  title: "正在恢复原皮", body: "完成后 SkinLab 会自动退出。", art: "busy" },
};

const $ = (id) => document.getElementById(id);
const state = { snapshot: null, settings: { show_art: true, always_on_top: false }, info: null, query: "", tier: "all", gridKey: "", pendingLocal: null };

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === false || value == null) continue;
    if (key === "class") node.className = value;
    else if (key === "style") node.style.cssText = value;
    else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value === true ? "" : value);
  }
  node.append(...children.filter((c) => c != null && c !== false));
  return node;
}

function heroParts(name) {
  const [champion, title] = name.split(" · ");
  return { champion, title: title ?? "" };
}

function skinName(id) {
  return state.snapshot?.skins.find((s) => s.id === id)?.name;
}

function emptyArt(kind) {
  const hex = "M38 8 64 23v30L38 68 12 53V23z";
  const inner = "M38 26 48 32v12L38 50 28 44V32z";
  if (kind === "busy") {
    return `<svg viewBox="0 0 76 76" fill="none" stroke="currentColor" stroke-width="3" stroke-linejoin="round"><path d="${hex}" opacity=".22"/><g class="spin"><path d="M38 8 64 23" stroke-linecap="round"/></g><path d="${inner}" fill="currentColor" stroke="none" class="breathe"/></svg>`;
  }
  if (kind === "warn") {
    return `<svg viewBox="0 0 76 76" fill="none" stroke="currentColor" stroke-width="3" stroke-linejoin="round"><path d="${hex}" opacity=".35"/><path d="M38 27v14" stroke-linecap="round"/><circle cx="38" cy="49" r="2.2" fill="currentColor" stroke="none"/></svg>`;
  }
  return `<svg viewBox="0 0 76 76" fill="none" stroke="currentColor" stroke-width="3" stroke-linejoin="round"><path d="${hex}" opacity=".35"/><path d="${inner}" fill="currentColor" stroke="none" opacity=".85" class="breathe"/></svg>`;
}

function renderStatus(s) {
  const phase = PHASES[s.phase] ?? PHASES.no_game;
  $("status-pill").dataset.tone = phase.tone;
  $("status-label").textContent = phase.label;
}

function renderHero(s) {
  const hero = s.hero;
  $("hero").hidden = !hero;
  if (!hero) return;
  const { champion, title } = heroParts(hero.name);
  const h1 = $("hero-name");
  h1.replaceChildren(champion, title ? el("small", { class: "hero-title" }, title) : null);

  const portrait = $("hero-portrait");
  const want = state.settings.show_art && hero.in_catalog ? `${ART}champion/${hero.model}.png` : "";
  if (portrait.dataset.src !== want) {
    portrait.dataset.src = want;
    portrait.replaceChildren(el("span", {}, champion.slice(0, 1)));
    if (want) {
      const img = el("img", { src: want, alt: "", draggable: "false" });
      img.onload = () => portrait.replaceChildren(img);
    }
  }

  const connected = CONNECTED.has(s.phase);
  let line;
  if (s.phase === "applying") line = `正在切换到 ${skinName(s.desired_skin) ?? "所选皮肤"}…`;
  else if (connected && s.current_skin != null) line = `当前：${skinName(s.current_skin) ?? `编号 ${s.current_skin}`}`;
  else if (s.phase === "unsupported" || s.phase === "blocked") line = "未接入 · 不会改动游戏";
  else line = s.message;
  $("hero-current").textContent = line;

  const restore = $("restore");
  restore.hidden = !connected || s.original_skin == null;
  restore.disabled = s.phase === "applying" || s.current_skin === s.original_skin;
}

function renderNotice(s) {
  const notice = $("notice");
  let tone = null;
  if (s.hero && (s.phase === "unsupported" || s.phase === "blocked")) tone = "danger";
  else if (CONNECTED.has(s.phase) && s.message && !ROUTINE.has(s.message)) tone = "warn";
  notice.hidden = !tone;
  if (tone) {
    notice.dataset.tone = tone;
    notice.textContent = s.message;
  }
}

function renderEmpty(s) {
  const showEmpty = !s.hero || s.skins.length === 0;
  $("empty").hidden = !showEmpty;
  $("tools").hidden = showEmpty;
  $("grid").hidden = showEmpty;
  if (!showEmpty) return;
  const phase = PHASES[s.phase] ?? PHASES.no_game;
  let title = phase.title ?? "暂无可选皮肤";
  let body = phase.body ?? s.message;
  if (s.hero && s.skins.length === 0 && CONNECTED.has(s.phase)) {
    title = "目录中没有该英雄";
    body = "请更新 SkinLab 后再试。";
  }
  const art = phase.art ?? "idle";
  const holder = $("empty-art");
  if (holder.dataset.kind !== art) {
    holder.dataset.kind = art;
    holder.innerHTML = emptyArt(art);
  }
  $("empty-title").textContent = title;
  $("empty-body").textContent = body;
}

function renderFilters(s) {
  const counts = {};
  for (const skin of s.skins) counts[skin.rarity] = (counts[skin.rarity] ?? 0) + 1;
  const tiers = TIERS.filter((t) => counts[t]);
  if (state.tier !== "all" && !counts[state.tier]) state.tier = "all";
  const chip = (id, label, n) =>
    el("button", { class: "chip", role: "tab", "aria-selected": String(state.tier === id), onclick: () => { state.tier = id; render(); } },
      label, el("span", { class: "n" }, String(n)));
  $("filters").replaceChildren(chip("all", "全部", s.skins.length), ...tiers.map((t) => chip(t, TIER_LABEL[t], counts[t])));
}

function visibleSkins(s) {
  const query = state.query.trim().toLowerCase();
  return s.skins.filter((skin) =>
    (state.tier === "all" || skin.rarity === state.tier) && (!query || skin.name.toLowerCase().includes(query)));
}

function card(skin, index) {
  const art = state.settings.show_art && skin.art_id ? `${ART}skinloading/${skin.art_id}.jpg` : null;
  const img = art && el("img", { src: art, alt: "", loading: "lazy", decoding: "async", draggable: "false" });
  if (img) {
    img.onload = () => img.classList.add("loaded");
    img.onerror = () => img.remove();
  }
  const label = skin.id === 0 ? "经典" : skin.rarity_label;
  const flagTip = "终极／多形态皮肤：特殊形态尚未适配，可能只有部分效果";
  return el("button",
    { class: "card", "data-id": skin.id, "data-tier": skin.rarity, title: skin.name, style: `animation-delay:${Math.min(index, 18) * 14}ms`,
      onclick: () => select(skin.id) },
    el("div", { class: "placeholder" }, skin.name.replace(/^(至臻\s*)/, "").slice(0, 1)),
    img,
    el("div", { class: "shade" }),
    el("span", { class: "tier" }, label),
    skin.special ? el("span", { class: "flag", title: flagTip, "aria-label": flagTip }, "!") : null,
    el("span", { class: "name" }, skin.name),
    el("span", { class: "spinner" }));
}

function renderGrid(s) {
  if (!s.hero || s.skins.length === 0) return;
  const key = [s.hero.model, s.skins.length, state.settings.show_art, state.query, state.tier].join("|");
  const grid = $("grid");
  const scroller = grid.parentElement;
  if (key !== state.gridKey) {
    const keepScroll = state.gridKey.split("|")[0] === s.hero.model ? scroller.scrollTop : 0;
    state.gridKey = key;
    renderFilters(s);
    const skins = visibleSkins(s);
    grid.replaceChildren(...(skins.length ? skins.map(card) : [el("div", { class: "no-results" }, "没有匹配的皮肤")]));
    scroller.scrollTop = keepScroll;
  }
  const connected = CONNECTED.has(s.phase);
  grid.classList.toggle("locked", !connected);
  const pending = s.phase === "applying" ? s.desired_skin : state.pendingLocal;
  for (const node of grid.querySelectorAll(".card")) {
    const id = Number(node.dataset.id);
    node.classList.toggle("current", connected && id === s.current_skin);
    node.classList.toggle("pending", connected && id === pending && id !== s.current_skin);
    node.disabled = !connected;
  }
}

function renderFooter() {
  const info = state.info;
  const s = state.snapshot;
  const parts = [];
  if (info?.mode === "demo") parts.push("演示模式");
  if (info?.mode === "fixture") parts.push("自建测试窗口");
  if (s?.catalog_version) parts.push(`目录 ${s.catalog_version}`);
  if (s?.game_version) parts.push(`游戏 ${s.game_version}`);
  $("foot-meta").textContent = parts.join(" · ");
  if (info && !info.hotkey) $("hotkey-hint").textContent = "Insert 被占用 · 可最小化收起";
}

function render() {
  const s = state.snapshot;
  if (!s) return;
  if (s.phase !== "applying" && state.pendingLocal != null && s.desired_skin == null) state.pendingLocal = null;
  renderStatus(s);
  renderHero(s);
  renderNotice(s);
  renderEmpty(s);
  renderGrid(s);
  renderFooter();
}

function select(id) {
  const s = state.snapshot;
  if (!s || !CONNECTED.has(s.phase) || id === s.current_skin) return;
  state.pendingLocal = id;
  render();
  api.invoke("select_skin", { skin: id });
}

// Settings sheet
function renderAbout() {
  const info = state.info;
  if (!info) return;
  const modes = { game: "游戏", fixture: "自建测试窗口", demo: "演示" };
  const rows = [
    ["版本", `SkinLab ${info.version}`],
    ["模式", modes[info.mode] ?? info.mode],
    ["皮肤目录", `${info.catalog_version} · ${info.champions} 位英雄 · ${info.skins} 款`],
    ["已适配游戏", info.supported_builds.join("、") || "无"],
  ];
  $("about").replaceChildren(...rows.flatMap(([k, v]) => [el("dt", {}, k), el("dd", {}, v)]));
}

function openSettings(open) {
  $("settings").classList.toggle("open", open);
  $("settings").setAttribute("aria-hidden", String(!open));
  $("sheet-backdrop").hidden = !open;
}

async function saveSettings(patch) {
  const next = { ...state.settings, ...patch };
  try {
    await api.invoke("set_settings", { settings: next });
    state.settings = next;
  } catch (error) {
    console.error(error);
  }
  $("set-art").checked = state.settings.show_art;
  $("set-top").checked = state.settings.always_on_top;
  state.gridKey = "";
  $("hero-portrait").dataset.src = "-";
  render();
}

$("open-settings").onclick = () => openSettings(true);
$("close-settings").onclick = () => openSettings(false);
$("sheet-backdrop").onclick = () => openSettings(false);
$("set-art").onchange = (e) => saveSettings({ show_art: e.target.checked });
$("set-top").onchange = (e) => saveSettings({ always_on_top: e.target.checked });
$("open-logs").onclick = () => api.invoke("open_logs").catch(console.error);
$("restore").onclick = () => api.invoke("restore_skin");
$("search").oninput = (e) => { state.query = e.target.value; render(); };

document.addEventListener("keydown", (e) => {
  const search = $("search");
  if (e.key === "Escape") {
    if ($("settings").classList.contains("open")) openSettings(false);
    else if (search.value) { search.value = ""; state.query = ""; render(); }
    return;
  }
  const typing = e.target instanceof HTMLInputElement;
  if (!typing && !$("tools").hidden && e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) search.focus();
});
document.addEventListener("contextmenu", (e) => { if (!(e.target instanceof HTMLInputElement)) e.preventDefault(); });

// Boot
const [snapshot, settings, info] = await Promise.all([
  api.invoke("get_snapshot"),
  api.invoke("get_settings"),
  api.invoke("app_info"),
]);
state.settings = settings;
state.info = info;
$("set-art").checked = settings.show_art;
$("set-top").checked = settings.always_on_top;
renderAbout();
state.snapshot = snapshot;
render();
api.listen("snapshot", (next) => { state.snapshot = next; render(); });
