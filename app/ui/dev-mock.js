// Browser-only stand-in for the Tauri backend, so the UI can be styled with a
// plain static server. Never loaded inside the app (window.__TAURI__ exists).
// Start a specific state with `?phase=no_game|waiting_hero|ready|unsupported|blocked`.

const AHRI = [
  [0, "九尾妖狐", "base"], [1, "高丽风情 阿狸", "common"], [2, "暗影妖狐 阿狸", "epic"], [3, "焰尾妖狐 阿狸", "legendary"],
  [4, "偶像歌手 阿狸", "legendary"], [5, "勇者 阿狸", "limited"], [6, "风纪委员 阿狸", "common"], [7, "电玩女神 阿狸", "limited"],
  [14, "星之守护者 阿狸", "legendary"], [15, "K/DA 阿狸", "common"], [16, "至臻 K/DA 阿狸", "prestige"], [17, "永恒之森 阿狸", "common"],
  [27, "灵魂莲华 阿狸", "legendary"], [28, "K/DA ALL OUT 阿狸", "common"], [42, "魔女 阿狸", "common"], [65, "至臻 K/DA 阿狸 (2022)", "prestige"],
  [66, "魅惑之月 阿狸", "common"], [76, "净雪之月 阿狸", "common"], [85, "殿堂传奇 阿狸", "limited"], [86, "联盟不朽 阿狸", "limited"],
  [88, "莲华温泉 阿狸", "legendary"], [89, "金鱼枫姬 莲华温泉 阿狸", "mythic"],
];
const LABEL = { base: "经典", common: "普通", epic: "史诗", legendary: "传说", mythic: "神话", ultimate: "终极", prestige: "至臻", limited: "限定" };

export function createMock() {
  const listeners = [];
  const params = new URLSearchParams(location.search);
  const skins = AHRI.map(([id, name, rarity]) => ({
    id, name, rarity, rarity_label: LABEL[rarity], special: id === 86, chromas: false, art_id: 103000 + id,
  }));
  const hero = { model: "Ahri", name: "阿狸 · 九尾妖狐", key: "103", in_catalog: true };
  let snap = {
    phase: "no_game", message: "请先打开游戏", hero: null, skins: [], current_skin: null, original_skin: null,
    desired_skin: null, game_version: null, catalog_version: "16.21",
  };
  let settings = { show_art: true, always_on_top: false };
  const emit = (patch) => {
    snap = { ...snap, ...patch };
    listeners.forEach((fn) => fn(structuredClone(snap)));
  };
  const connected = { phase: "ready", message: "选择皮肤即生效", hero, skins, current_skin: 0, original_skin: 0, game_version: "16.21.832.5350" };
  const start = params.get("phase");
  if (start === "ready") snap = { ...snap, ...connected };
  else if (start === "waiting_hero") snap = { ...snap, phase: "waiting_hero", message: "等待英雄载入…", game_version: "16.21.832.5350" };
  else if (start === "unsupported") snap = { ...snap, phase: "unsupported", message: "游戏版本 16.22.1.0 尚未适配，请等待 SkinLab 更新", hero, skins, game_version: "16.22.1.0" };
  else if (start === "blocked") snap = { ...snap, phase: "blocked", message: "游戏权限高于 SkinLab，请以管理员身份重新打开", game_version: "16.21.832.5350" };
  else if (!start) {
    setTimeout(() => emit({ phase: "waiting_hero", message: "等待英雄载入…", game_version: "16.21.832.5350" }), 1400);
    setTimeout(() => emit({ phase: "connecting", message: "正在载入…", hero, skins }), 2800);
    setTimeout(() => emit(connected), 3800);
  }

  const apply = (skin, done) => {
    emit({ phase: "applying", message: "正在切换…", desired_skin: skin });
    setTimeout(() => emit({ phase: "ready", message: done, current_skin: skin, desired_skin: null }), 650);
  };
  const handlers = {
    get_snapshot: () => structuredClone(snap),
    get_settings: () => settings,
    set_settings: ({ settings: next }) => { settings = next; },
    app_info: () => ({ version: "0.4.0", mode: "demo", hotkey: true, catalog_version: "16.21", champions: 173, skins: 2138, supported_builds: ["16.21.832.5350"] }),
    select_skin: ({ skin }) => apply(skin, "已生效"),
    restore_skin: () => apply(snap.original_skin, "已恢复原皮"),
    refresh: () => {},
    open_logs: () => {},
  };
  return {
    invoke: async (name, args) => handlers[name](args ?? {}),
    listen: async (_name, fn) => { listeners.push(fn); },
  };
}
