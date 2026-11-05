// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Browser-only stand-in for the Tauri backend, so the UI can be styled with a
// plain static server. Never loaded inside the app (window.__TAURI__ exists).
//
// Serve the repository root and open /app/ui/ so the real data/catalog.json
// loads; otherwise a built-in Ahri sample is used.
//   ?hero=Lux      start with a given champion (engine id)
//   ?phase=ready|no_game|waiting_hero|unsupported|blocked   start in a fixed state
//   ?shot          hide the picker (README screenshots)
//   ?notice        show the first-run usage notice
// A small picker in the corner switches champions like a new match would.

const SAMPLE = {
  version: "16.21",
  champions: [{
    id: "Ahri", key: "103", name: "阿狸 · 九尾妖狐",
    skins: [[0, "九尾妖狐", "base"], [2, "暗影妖狐 阿狸", "Epic"], [14, "星之守护者 阿狸", "Legend"], [16, "至臻 K/DA 阿狸", "Prestige"],
      [27, "灵魂莲华 阿狸", "Legend"], [86, "联盟不朽 阿狸", "HOL"], [89, "金鱼枫姬 莲华温泉 阿狸", "Mythic"]]
      .map(([skin_id, name, rarity]) => ({ skin_id, name, rarity })),
  }],
};
const LABEL = { base: "经典", common: "普通", epic: "史诗", legendary: "传说", mythic: "神话", ultimate: "终极", prestige: "至臻", limited: "限定" };
// Mirrors crates/skinlab-core/src/art.rs.
const ORIGINS = { tencent: "https://game.gtimg.cn/images/lol/act/img/" };
const artUrl = (reference = "") => {
  const [origin, path] = reference.split(/:(.*)/s);
  return ORIGINS[origin] && path ? ORIGINS[origin] + path : null;
};

// Mirrors crates/skinlab-core/src/forms.rs.
const FORM_SETS = [{
  champion: "Lux", skin: 7, label: "元素形态",
  items: ["Lux", "LuxAir", "LuxDark", "LuxFire", "LuxIce", "LuxMagma", "LuxMystic", "LuxNature", "LuxStorm", "LuxWater"]
    .map((id, i) => ({ id, name: ["光", "风", "暗", "火", "冰", "熔岩", "秘术", "自然", "风暴", "水"][i] })),
}];
const formsOf = (model, skin) => {
  const set = FORM_SETS.find((f) => f.champion === model && f.skin === skin);
  return set ? { label: set.label, items: set.items } : null;
};

// Mirrors Skin::rarity in crates/skinlab-core/src/catalog.rs.
function rarity(raw = "") {
  const r = raw.toLowerCase();
  if (r === "base") return "base";
  if (r === "" || r === "common") return "common";
  if (r === "epic") return "epic";
  if (r === "legend" || r === "legendary") return "legendary";
  if (r === "mythic") return "mythic";
  if (r === "ultimate") return "ultimate";
  if (r.startsWith("prestige")) return "prestige";
  return "limited";
}

// Mirrors skin_views in crates/skinlab-session/src/controller.rs.
function view(champion) {
  const skins = champion.skins.map((s) => {
    const tier = rarity(s.rarity);
    const forms = formsOf(champion.id, s.skin_id);
    return {
      id: s.skin_id, name: s.name, rarity: tier, rarity_label: LABEL[tier],
      chromas: (s.chromas ?? []).map((c) => ({ id: c.skin_id, name: c.name, colors: c.colors ?? [], art: artUrl(c.art) })),
      forms, art: artUrl(s.art),
    };
  });
  return { hero: { model: champion.id, name: champion.name, key: champion.key, in_catalog: true, art: artUrl(champion.art) }, skins,
    current_form: null, desired_form: null };
}

async function loadCatalog() {
  try {
    const response = await fetch("../../data/catalog.json");
    if (response.ok) return await response.json();
  } catch {}
  return SAMPLE;
}

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

export async function createMock() {
  const catalog = await loadCatalog();
  const champions = [...catalog.champions].sort((a, b) => a.name.localeCompare(b.name, "zh-CN"));
  const byId = (id) => champions.find((c) => c.id.toLowerCase() === String(id).toLowerCase());
  const params = new URLSearchParams(location.search);
  const listeners = [];
  const version = "16.21.832.5350";
  let settings = { show_art: true, always_on_top: false, notice_accepted: params.has("notice") ? 0 : 1 };
  let snap = {
    phase: "no_game", message: "请先打开游戏", hero: null, skins: [], current_skin: null, original_skin: null,
    desired_skin: null, game_version: null, catalog_version: catalog.version,
  };
  const emit = (patch) => {
    snap = { ...snap, ...patch };
    listeners.forEach((fn) => fn(structuredClone(snap)));
  };

  let match = 0;
  async function playMatch(champion, fast = false) {
    const id = ++match;
    const live = () => id === match;
    const { hero, skins } = view(champion);
    const pace = fast ? 0.4 : 1;
    if (snap.hero) {
      emit({ phase: "no_game", message: "对局已结束 · 等待下一局", hero: null, skins: [], current_skin: null, original_skin: null, desired_skin: null, game_version: null });
      await wait(700 * pace);
    }
    if (!live()) return;
    emit({ phase: "waiting_hero", message: "等待英雄载入…", game_version: version });
    await wait(1100 * pace);
    if (!live()) return;
    emit({ phase: "connecting", message: "正在载入…", hero, skins });
    await wait(900 * pace);
    if (!live()) return;
    emit({ phase: "ready", message: "选择皮肤即生效", current_skin: 0, original_skin: 0 });
  }

  const start = byId(params.get("hero") ?? "") ?? byId("Ahri") ?? champions[0];
  const phase = params.get("phase");
  if (phase === "ready") snap = { ...snap, ...view(start), phase: "ready", message: "选择皮肤即生效", current_skin: 0, original_skin: 0, game_version: version };
  else if (phase === "waiting_hero") snap = { ...snap, phase: "waiting_hero", message: "等待英雄载入…", game_version: version };
  else if (phase === "unsupported") snap = { ...snap, ...view(start), phase: "unsupported", message: "无法读取当前游戏数据，请更新 SkinLab 后再试", game_version: "16.22.1.0" };
  else if (phase === "blocked") snap = { ...snap, phase: "blocked", message: "游戏权限高于 SkinLab，请以管理员身份重新打开", game_version: version };
  else if (phase !== "no_game") setTimeout(() => playMatch(start), 1200);

  if (!params.has("shot")) mountPicker(champions, start, (champion) => playMatch(champion, true));

  const apply = async (skin, done) => {
    emit({ phase: "applying", message: "正在切换…", desired_skin: skin });
    await wait(650);
    emit({ phase: "ready", message: done, current_skin: skin, desired_skin: null, desired_form: null,
      current_form: formsOf(snap.hero?.model, skin)?.items[0].id ?? null });
  };
  const handlers = {
    get_snapshot: () => structuredClone(snap),
    get_settings: () => settings,
    set_settings: ({ settings: next }) => { settings = next; },
    app_info: () => ({
      version: "0.4.0", commit: "dev", repository: "https://github.com/wty92911/skinlab",
      license: "PolyForm Noncommercial 1.0.0", notice_version: 1, mode: "demo", hotkey: true, catalog_version: catalog.version,
      champions: catalog.champions.length, skins: catalog.champions.reduce((n, c) => n + c.skins.length, 0),
      chromas: catalog.champions.reduce((n, c) => n + c.skins.reduce((m, s) => m + (s.chromas?.length ?? 0), 0), 0),
      supported_builds: [version],
    }),
    select_skin: ({ skin }) => { apply(skin, "已生效"); },
    select_form: async ({ form }) => {
      if (!formsOf(snap.hero?.model, snap.current_skin)?.items.some((entry) => entry.id === form)) return;
      emit({ phase: "applying", message: "正在切换…", desired_form: form });
      await wait(400);
      emit({ phase: "ready", message: "已生效", current_form: form, desired_form: null });
    },
    restore_skin: () => { apply(snap.original_skin, "已恢复原皮"); },
    refresh: () => {},
    open_logs: () => {},
    accept_notice: () => { settings = { ...settings, notice_accepted: 1 }; },
    quit_app: () => {},
    open_repository: () => window.open("https://github.com/wty92911/skinlab", "_blank"),
  };
  return {
    invoke: async (name, args) => handlers[name](args ?? {}),
    listen: async (_name, fn) => { listeners.push(fn); },
  };
}

// Dev-only champion switcher; styled inline so styles.css stays app-only.
function mountPicker(champions, selected, onPick) {
  const select = document.createElement("select");
  select.title = "演示：切换英雄（模拟新对局）";
  select.style.cssText = [
    "position:fixed", "left:12px", "bottom:44px", "z-index:5", "max-width:180px", "height:28px", "padding:0 8px",
    "border-radius:8px", "border:1px dashed var(--accent)", "background:var(--surface)", "color:var(--text-2)",
    "font:12px var(--sans)", "box-shadow:var(--shadow)", "cursor:pointer",
  ].join(";");
  for (const champion of champions) {
    const option = new Option(`${champion.name} (${champion.skins.length})`, champion.id, false, champion === selected);
    select.add(option);
  }
  select.onchange = () => {
    const champion = champions.find((c) => c.id === select.value);
    const url = new URL(location.href);
    url.searchParams.set("hero", champion.id);
    url.searchParams.delete("phase");
    history.replaceState(null, "", url);
    onPick(champion);
  };
  document.body.append(select);
}
