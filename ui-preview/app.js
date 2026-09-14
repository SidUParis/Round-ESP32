const $ = (s) => document.querySelector(s);
const icon = (n) => `<svg aria-hidden="true"><use href="#i-${n}"/></svg>`;
const orb = () =>
  '<span class="voice-orb"><i class="orb-film film-a"></i><i class="orb-film film-b"></i><i class="orb-film film-c"></i><span class="orb-glyph"><i></i><i></i><i></i></span></span>';
const view = $("#view");
const state = {
  view: "home",
  page: 0,
  ai: "idle",
  device: "phone",
  bluetooth: true,
  wifi: true,
  music: false,
  ambient: true,
  track: 0,
  quotaDemo: false,
  screenOn: true,
  islandOpen: false,
  brightness: 70,
  volume: 50,
  tour: false,
};
const aiTimers = new Set();
let tourTimer,
  gravityFrame,
  toastTimer,
  linkTimer,
  toastText = "",
  idleCopyIndex = 0,
  contextUntil = 0;
const idleCopy = [
  ["有什么想聊的？", "轻触说话 · 或按下侧键 2"],
  ["今天，想从哪里开始？", "把一个小想法，讲给我听"],
  ["让灵感，落在这一刻。", "我在这里，随时可以聊"],
  ["慢一点，也没关系。", "按下侧键 2，说说看"],
];
const tracks = ["微风经过", "夜晚的漫步", "留一点空白"];
const info = {
  home: [
    "01 / 05",
    "对话，就在桌面。",
    "无需进入应用。对话在圆心展开，连接和播放状态留在上方，随时可以打断或继续。",
    "轻触圆心，直接开始对话",
  ],
  music: [
    "02 / 05",
    "音乐在手机，节奏在这里。",
    "用它暂停、切歌，或让圆屏跟着外放音乐呼吸。遥控状态与声音感知，是两种独立能力。",
    "播放键遥控手机；随声动效模拟麦克风感知",
  ],
  devices: [
    "03 / 05",
    "连接，让小屏有了上下文。",
    "手机用来控制音乐，电脑用来同步状态。始终告诉你连着谁、数据来自哪里。",
    "切换右侧连接场景，体验连接和断开",
  ],
  computer: [
    "04 / 05",
    "工作进展，抬眼可见。",
    "把额度和任务状态留在小屏。缺少数据时明确留空，断开后保留过期标记。",
    "额度尚未接入；可加载示例查看布局",
  ],
  apps: [
    "05 / 05",
    "少一点入口，多一点直接。",
    "AI 已经住进桌面。留下音乐、连接、重力球、频谱和设置，去掉绘画。",
    "重力球和频谱继续保留",
  ],
  settings: [
    "05 / 05",
    "少量控制，恰到好处。",
    "无线连接与显示音量分开管理。设备上的两个按键，承担最常用的动作。",
    "Wi-Fi、蓝牙和实体按键均为界面模拟",
  ],
  gravity: [
    "05 / 05",
    "一颗球，感受空间。",
    "重力球跟随倾斜，始终留在圆形边界内。沿用现有 IMU 应用的方向。",
    "指针或方向键模拟倾斜",
  ],
  spectrum: [
    "05 / 05",
    "听见节奏，也看见节奏。",
    "独立的本地声音感知。未来由设备麦克风计算频谱，不依赖蓝牙传输音频。",
    "模拟频谱 · 没有开启麦克风",
  ],
  video: [
    "05 / 05",
    "留住已有的小功能。",
    "视频入口保留在第二页。主界面优先留给 AI 和与你的设备连接。",
    "原型未加载媒体文件",
  ],
};
function later(fn, ms) {
  const id = setTimeout(() => {
    aiTimers.delete(id);
    fn();
  }, ms);
  aiTimers.add(id);
}
function clearAI() {
  for (const id of aiTimers) clearTimeout(id);
  aiTimers.clear();
  state.ai = "idle";
}
function linked() {
  return state.device === "phone"
    ? state.bluetooth
    : state.device === "computer"
      ? state.wifi
      : false;
}
function deviceName() {
  return state.device === "phone"
    ? "我的手机"
    : state.device === "computer"
      ? "我的电脑"
      : "尚未连接";
}
function heading(title) {
  return `<button class="back-button" data-view="home" aria-label="返回桌面">${icon("back")}</button><h2 class="page-heading">${title}</h2>`;
}
function frequencyRing(active = true) {
  return `<div class="frequency-ring ${active ? "active" : ""}"><svg viewBox="0 0 180 180" aria-hidden="true">${Array.from({ length: 48 }, (_, i) => `<g transform="translate(90 90) rotate(${i * 7.5})"><rect x="-1.6" y="-81" width="3.2" height="8" rx="1.6" style="--delay:-${i * 0.11}s;--size:${7 + (Math.sin(i * 0.63) + 1) * 8}px"/></g>`).join("")}</svg><span class="sound-center">${icon(state.device === "computer" ? "computer" : "phone")}</span></div>`;
}
function updateIsland() {
  const phase = {
    listening: "正在聆听",
    thinking: "正在思考",
    speaking: "正在回应",
  }[state.ai];
  $("#island").classList.toggle(
    "quiet",
    !phase && !toastText && !state.islandOpen,
  );
  $("#island").classList.toggle("voice-active", !!phase);
  $("#island").classList.toggle("notice-active", !!toastText);
  $("#island").classList.toggle("expanded", state.islandOpen);
  $("#island").classList.toggle("playing", state.music && linked());
  $("#island-toggle").setAttribute("aria-expanded", state.islandOpen);
  $("#island-toggle").setAttribute(
    "aria-label",
    state.islandOpen ? "收起动态状态区" : "展开动态状态区",
  );
  $("#island-symbol").innerHTML = icon(
    phase
      ? "mic"
      : linked()
        ? state.music
          ? "music"
          : state.device === "phone"
            ? "phone"
            : "computer"
        : "link",
  );
  $("#island-label").textContent =
    phase ||
    (toastText ? toastText.split(" · ")[0] : "") ||
    (linked() ? (state.music ? "手机正在播放" : deviceName()) : "等待连接");
  $("#island-tail").innerHTML = phase
    ? '<span class="mini-level"><i></i><i></i><i></i></span>'
    : `<span class="link-light ${linked() ? "on" : ""}"></span><span class="mini-battery">82${icon("battery")}</span>`;
  const body = $("#island-body");
  body.hidden = !state.islandOpen;
  if (state.islandOpen)
    body.innerHTML = `<div class="island-connections"><span>${icon("bluetooth")}蓝牙 <b>${state.bluetooth ? "开启" : "关闭"}</b></span><span>${icon("wifi")}Wi-Fi <b>${state.wifi ? "开启" : "关闭"}</b></span></div><div class="island-context"><span>${linked() ? deviceName() + " · 已连接" : "没有已连接设备"}</span><small>演示状态</small></div><button class="island-manage" data-view="devices">管理连接 ${icon("arrow")}</button>`;
}
function closeIsland() {
  state.islandOpen = false;
  updateIsland();
}
function toast(text) {
  clearTimeout(toastTimer);
  toastText = text;
  let el = $("#screen-toast");
  if (!el) {
    el = document.createElement("div");
    el.id = "screen-toast";
    el.className = "sr-announcement";
    el.setAttribute("role", "status");
    $("#screen").append(el);
  }
  el.textContent = text;
  updateIsland();
  toastTimer = setTimeout(() => {
    toastText = "";
    updateIsland();
  }, 2600);
}
function render(name = state.view, { manual = true } = {}) {
  if (manual && state.tour) stopTour();
  cancelAnimationFrame(gravityFrame);
  if (name !== state.view) clearAI();
  state.view = name;
  state.islandOpen = false;
  document.querySelectorAll(".screen-nav button").forEach((b) => {
    const active =
      b.dataset.view === name ||
      (b.dataset.view === "apps" &&
        ["settings", "gravity", "spectrum", "video"].includes(name));
    b.classList.toggle("active", active);
    b.setAttribute("aria-current", active ? "page" : "false");
  });
  const copy = info[name];
  $("#view-number").textContent = copy[0];
  $("#detail-title").textContent = copy[1];
  $("#detail-copy").textContent = copy[2];
  $("#caption").textContent = copy[3];
  view.className = "view view-" + name;
  view.style.animation = "none";
  void view.offsetWidth;
  view.style.animation = "";
  $("#screen").classList.remove("voice-engaged");
  if (name === "home") {
    view.innerHTML = `<div class="clock" id="clock"></div><div class="date" id="date"></div><button class="home-ai" id="home-ai" aria-label="在桌面开始模拟对话">${orb()}</button><div class="home-dialogue" aria-live="polite"><h2 id="ai-state">有什么想聊的？</h2><p id="ai-hint">轻触说话 · 或按下侧键 2</p></div><button class="context-strip" id="context-strip"></button><div class="home-dock"><button data-view="apps" aria-label="打开应用">${icon("grid")}</button><button data-view="devices" aria-label="查看连接设备">${icon("link")}</button><button data-view="settings" aria-label="打开设置">${icon("settings")}</button></div>`;
    $("#home-ai").onclick = startConversation;
    updateClock();
    updateAI();
    updateHomeContext();
  } else if (name === "apps") {
    const apps = [
      ["music", "music", "音乐伴侣"],
      ["computer", "computer", "电脑看板"],
      ["gravity", "gravity", "重力球"],
      ["settings", "settings", "设置"],
      ["devices", "link", "设备连接"],
      ["spectrum", "wave", "频谱"],
      ["video", "video", "视频"],
    ];
    view.innerHTML = `<h2 class="page-heading">随身工具</h2><div class="app-grid">${apps
      .slice(state.page * 4, state.page * 4 + 4)
      .map(
        ([key, img, label]) =>
          `<button class="app-tile" data-view="${key}"><span class="app-icon">${icon(img)}</span>${label}</button>`,
      )
      .join(
        "",
      )}</div><div class="pager"><button id="page-prev" aria-label="上一页" ${state.page === 0 ? "disabled" : ""}>${icon("back")}</button><span class="page-dot ${state.page === 0 ? "on" : ""}"></span><span class="page-dot ${state.page === 1 ? "on" : ""}"></span><button id="page-next" aria-label="下一页" ${state.page === 1 ? "disabled" : ""}>${icon("arrow")}</button></div>`;
    $("#page-prev").onclick = () => {
      state.page = 0;
      render();
    };
    $("#page-next").onclick = () => {
      state.page = 1;
      render();
    };
  } else if (name === "music") {
    const phone = linked() && state.device === "phone";
    view.innerHTML = `${heading("音乐伴侣")}<div class="music-source">${icon("phone")} ${phone ? "来自我的手机" : "等待手机连接"}</div><div class="music-orbit" id="music-orbit">${frequencyRing(state.ambient)}</div><h3 class="song-title" id="song-title">${phone ? tracks[state.track] : "让手机负责播放"}</h3><div class="song-subtitle">${phone ? "播放信息 · 演示数据" : "连接后可用播放与切歌遥控"}</div><div class="companion-controls"><button id="prev-track" aria-label="遥控上一首" ${phone ? "" : "disabled"}>${icon("prev")}</button><button id="play" class="play" aria-label="${state.music ? "遥控暂停" : "遥控播放"}" ${phone ? "" : "disabled"}>${icon(state.music ? "pause" : "play")}</button><button id="next-track" aria-label="遥控下一首" ${phone ? "" : "disabled"}>${icon("next")}</button></div><button class="ambient-toggle" id="ambient" aria-pressed="${state.ambient}">${icon("wave")}随声动效 <b>${state.ambient ? "开" : "关"}</b></button>`;
    $("#play").onclick = toggleMusic;
    $("#prev-track").onclick = () => changeTrack(-1);
    $("#next-track").onclick = () => changeTrack(1);
    $("#ambient").onclick = () => {
      state.ambient = !state.ambient;
      $("#ambient").setAttribute("aria-pressed", state.ambient);
      $("#ambient b").textContent = state.ambient ? "开" : "关";
      updateMusic();
      toast(state.ambient ? "随声动效已开启 · 模拟" : "随声动效已关闭");
    };
  } else if (name === "devices") {
    view.innerHTML = `${heading("设备连接")}<div class="connection-hero"><span class="device-glyph ${linked() ? "connected" : ""}">${icon(state.device === "computer" ? "computer" : "phone")}</span><span class="link-trail"><i></i><i></i><i></i></span><span class="round-glyph">${icon("ai")}</span></div><h3 class="device-title">${deviceName()}</h3><p class="device-status">${linked() ? (state.device === "phone" ? "蓝牙连接 · 遥控与播放信息" : "Wi-Fi 连接 · 电脑状态桥接") : "等待配对 · 选择一个演示设备"}</p><div class="device-options"><button data-scenario="phone" class="${state.device === "phone" ? "selected" : ""}">${icon("phone")}手机</button><button data-scenario="computer" class="${state.device === "computer" ? "selected" : ""}">${icon("computer")}电脑</button></div><button class="disconnect" id="disconnect">${linked() ? "断开演示连接" : "查看连接能力"}</button>`;
    $("#disconnect").onclick = () =>
      linked()
        ? setScenario("none")
        : toast("遥控用 BLE；电脑状态可用 Wi-Fi / USB");
  } else if (name === "computer") {
    const online = linked() && state.device === "computer",
      data = state.quotaDemo;
    view.innerHTML = `${heading("电脑看板")}<p class="computer-source">${icon("computer")}${online ? "我的电脑 · 已连接" : "电脑未连接"}<span>${data ? "示例数据" : "未接入"}</span></p><div class="quota-panel ${!online && data ? "is-stale" : ""}"><div class="quota-item"><span class="quota-logo codex-logo">⌘</span><div class="quota-copy"><strong>Codex</strong><small>${data ? "当前窗口剩余" : "等待电脑同步"}</small><div class="quota-track"><i style="width:${data ? 68 : 0}%"></i></div></div><b>${data ? "68" : "—"}<small>${data ? "%" : ""}</small></b></div><div class="quota-item"><span class="quota-logo claude-logo">✳</span><div class="quota-copy"><strong>Claude Code</strong><small>${data ? "当前窗口剩余" : "等待电脑同步"}</small><div class="quota-track"><i style="width:${data ? 42 : 0}%"></i></div></div><b>${data ? "42" : "—"}<small>${data ? "%" : ""}</small></b></div></div><div class="sync-state"><i class="${online ? "on" : ""}"></i>${data ? (online ? "刚刚同步 · 全部为示例" : "连接已断开 · 数据已过期") : "不会凭配对自动读取额度"}</div><button class="sync-button" id="sync">${online ? (data ? "清除示例数据" : "加载额度示例") : "模拟连接电脑"}</button>`;
    $("#sync").onclick = () => {
      if (!online) {
        setScenario("computer");
        return;
      }
      state.quotaDemo = !state.quotaDemo;
      render("computer");
      toast(
        state.quotaDemo ? "已加载示例 · 不是实际账号额度" : "已清除示例数据",
      );
    };
  } else if (name === "settings") {
    view.innerHTML = `${heading("快捷设置")}<div class="setting-panel"><div class="wireless-pair"><button id="wifi-toggle" aria-pressed="${state.wifi}">${icon("wifi")}<span>Wi-Fi</span><small>${state.wifi ? "开启" : "关闭"}</small></button><button id="bt-toggle" aria-pressed="${state.bluetooth}">${icon("bluetooth")}<span>蓝牙</span><small>${state.bluetooth ? "开启" : "关闭"}</small></button></div><label class="device-slider"><span class="slider-label"><span>${icon("sun")}亮度</span><output id="brightness-output">${state.brightness}%</output></span><input type="range" id="brightness" min="15" max="100" value="${state.brightness}" aria-label="屏幕亮度"></label><label class="device-slider"><span class="slider-label"><span>${icon("volume")}设备音量</span><output id="volume-output">${state.volume}%</output></span><input type="range" id="volume" min="0" max="100" value="${state.volume}" aria-label="设备音量"></label><p class="device-note">界面演示 · 不改变设备状态</p></div>`;
    $("#wifi-toggle").onclick = () => {
      state.wifi = !state.wifi;
      render();
      toast(
        state.wifi ? "Wi-Fi 已开启 · 模拟" : "Wi-Fi 已关闭 · AI 联网不可用",
      );
    };
    $("#bt-toggle").onclick = () => {
      state.bluetooth = !state.bluetooth;
      if (!state.bluetooth) state.music = false;
      render();
      toast(
        state.bluetooth ? "蓝牙已开启 · 模拟" : "蓝牙已关闭 · 手机遥控暂停",
      );
    };
    $("#brightness").oninput = (e) => {
      state.brightness = +e.target.value;
      $("#brightness-output").textContent = state.brightness + "%";
      $("#screen").style.filter = `brightness(${0.5 + state.brightness / 140})`;
    };
    $("#volume").oninput = (e) => {
      state.volume = +e.target.value;
      $("#volume-output").textContent = state.volume + "%";
    };
  } else if (name === "gravity") {
    view.innerHTML = `${heading("重力球")}<div class="gravity-field" id="gravity-field" tabindex="0" role="application" aria-label="重力球演示，移动指针或使用方向键"><div class="gravity-cross"></div><div class="gravity-ball" id="gravity-ball"></div></div><p class="gravity-note">指针模拟倾斜 · 方向键也可操作</p>`;
    initGravity();
  } else if (name === "spectrum") {
    view.innerHTML = `${heading("声音感知")}<div class="standalone-spectrum">${frequencyRing(state.ambient)}</div><p class="utility-copy">感受周围的音乐<br><small>本地频谱模拟 · 未开启麦克风</small></p><button class="small-action" id="spectrum-toggle">${state.ambient ? "暂停动效" : "继续动效"}</button>`;
    $("#spectrum-toggle").onclick = () => {
      state.ambient = !state.ambient;
      render();
    };
  } else if (name === "video") {
    view.innerHTML = `${heading("视频")}<div class="empty-icon">${icon("video")}</div><h3 class="empty-title">还没有预览视频</h3><p class="utility-copy">原有视频入口保留<br>主界面优先呈现 AI 与连接状态</p>`;
  }
  updateIsland();
  updateKeyLabels();
}
function updateClock() {
  const d = new Date();
  if ($("#clock"))
    $("#clock").textContent = d.toLocaleTimeString("en-GB", {
      hour: "2-digit",
      minute: "2-digit",
    });
  if ($("#date"))
    $("#date").textContent = d
      .toLocaleDateString("zh-CN", {
        month: "long",
        day: "numeric",
        weekday: "long",
      })
      .replace("日", "日 · ");
}
function updateHomeContext() {
  if (!$("#context-strip")) return;
  const el = $("#context-strip");
  el.classList.toggle("context-hidden", Date.now() > contextUntil);
  if (!linked()) {
    el.innerHTML = `${icon("link")}<span>连接手机或电脑</span>${icon("arrow")}`;
    el.onclick = () => render("devices");
  } else if (state.device === "phone") {
    el.innerHTML = `${icon("music")}<span>${state.music ? tracks[state.track] : "手机音乐 · 轻触播放"}</span>${icon(state.music ? "pause" : "play")}`;
    el.onclick = toggleMusic;
  } else {
    el.innerHTML = `${icon("computer")}<span>${state.quotaDemo ? "Codex 68% · Claude 42% · 示例" : "电脑已连接 · 额度待同步"}</span>${icon("arrow")}`;
    el.onclick = () => render("computer");
  }
}
function updateAI() {
  if (state.view !== "home") return;
  const texts = {
    idle: idleCopy[idleCopyIndex],
    listening: ["我在听", "今天，想从什么开始？"],
    thinking: ["让我想一想", "不必离开桌面"],
    speaking: ["慢一点，也很好。", "先做一件你真正想做的小事。"],
  };
  const busy = state.ai !== "idle";
  $("#screen").classList.toggle("voice-engaged", busy);
  $("#home-ai").dataset.phase = state.ai;
  $("#ai-state").textContent = texts[state.ai][0];
  $("#ai-hint").textContent = texts[state.ai][1];
  $("#home-ai").setAttribute(
    "aria-label",
    busy ? "结束桌面模拟对话" : "在桌面开始模拟对话",
  );
  updateIsland();
}
function startConversation() {
  if (state.tour) stopTour();
  if (!state.screenOn) wake();
  if (state.view !== "home") render("home");
  if (!state.wifi) {
    toast("先连接 Wi-Fi，才能使用联网 AI");
    return;
  }
  if (state.ai !== "idle") {
    clearAI();
    updateAI();
    return;
  }
  clearAI();
  closeIsland();
  state.ai = "listening";
  updateAI();
  later(() => {
    state.ai = "thinking";
    updateAI();
  }, 2300);
  later(() => {
    state.ai = "speaking";
    updateAI();
  }, 4300);
  later(() => {
    clearAI();
    updateAI();
  }, 9500);
}
function toggleMusic() {
  if (!(linked() && state.device === "phone")) {
    toast("请先连接手机");
    return;
  }
  state.music = !state.music;
  contextUntil = Date.now() + 5000;
  updateMusic();
  updateIsland();
  updateHomeContext();
  toast(state.music ? "已发送播放指令 · 模拟" : "已发送暂停指令 · 模拟");
}
function updateMusic() {
  if (state.view !== "music") return;
  $("#play").innerHTML = icon(state.music ? "pause" : "play");
  $("#play").setAttribute("aria-label", state.music ? "遥控暂停" : "遥控播放");
  if (linked() && state.device === "phone")
    $("#song-title").textContent = tracks[state.track];
  $("#music-orbit .frequency-ring").classList.toggle("active", state.ambient);
}
function changeTrack(delta) {
  state.track = (state.track + delta + tracks.length) % tracks.length;
  updateMusic();
  updateIsland();
  updateHomeContext();
  toast("已发送切歌指令 · 模拟");
}
function setScenario(device) {
  if (state.tour) stopTour();
  clearTimeout(linkTimer);
  clearAI();
  state.device = device;
  state.music = false;
  if (device === "phone") state.bluetooth = true;
  if (device === "computer") state.wifi = true;
  document.querySelectorAll("[data-scenario]").forEach((b) => {
    const selected = b.dataset.scenario === device;
    b.classList.toggle("selected", selected);
    b.setAttribute("aria-pressed", selected);
  });
  $("#scenario-note").textContent = {
    none: "独立设备 · 等待配对",
    phone: "手机伴侣 · BLE 连接示意",
    computer: "电脑伴侣 · Wi-Fi / USB 桥接示意",
  }[device];
  render();
  $("#island").classList.add("connection-event");
  toast(
    device === "none"
      ? "连接已断开 · 模拟"
      : device === "phone"
        ? "手机已连接 · 模拟"
        : "电脑已连接 · 模拟",
  );
  linkTimer = setTimeout(
    () => $("#island").classList.remove("connection-event"),
    1800,
  );
}
function updateKeyLabels() {
  $("#key-two-demo").innerHTML =
    `<span>2</span>${state.view === "music" ? "播放 / 暂停" : "开始 / 结束对话"}`;
  $("#key-two").setAttribute(
    "aria-label",
    state.view === "music" ? "实体键二：播放暂停" : "实体键二：开始结束对话",
  );
}
function wake() {
  state.screenOn = true;
  $("#screen").classList.remove("sleeping");
  $("#sleep-cover")?.remove();
}
function sleep() {
  clearAI();
  updateAI();
  state.screenOn = false;
  state.islandOpen = false;
  updateIsland();
  $("#screen").classList.add("sleeping");
  const cover = document.createElement("button");
  cover.id = "sleep-cover";
  cover.className = "sleep-cover";
  cover.setAttribute("aria-label", "唤醒屏幕");
  cover.innerHTML = `${icon("power")}<span>已息屏</span><small>轻触或按键 1 唤醒</small>`;
  cover.onclick = wake;
  $("#screen").append(cover);
}
function keyOne() {
  if (!state.screenOn) {
    wake();
    return;
  }
  if (state.view === "home" && state.ai === "idle" && !state.islandOpen) {
    sleep();
    return;
  }
  clearAI();
  render("home");
}
function keyTwo() {
  if (!state.screenOn) wake();
  if (state.view === "music") toggleMusic();
  else startConversation();
}
$("#key-one").onclick = $("#key-one-demo").onclick = keyOne;
$("#key-two").onclick = $("#key-two-demo").onclick = keyTwo;
$("#island-toggle").onclick = () => {
  state.islandOpen = !state.islandOpen;
  updateIsland();
};
document.addEventListener("click", (e) => {
  const scenario = e.target.closest("[data-scenario]");
  if (scenario) {
    setScenario(scenario.dataset.scenario);
    return;
  }
  const button = e.target.closest("[data-view]");
  if (button) {
    wake();
    render(button.dataset.view);
  }
});
$("#home").onclick = () => {
  wake();
  clearAI();
  render("home");
};
$(".brand").onclick = (e) => {
  e.preventDefault();
  wake();
  render("home");
};
document.querySelectorAll("[data-theme]").forEach(
  (button) =>
    (button.onclick = () => {
      document.body.dataset.theme = button.dataset.theme;
      document.querySelectorAll(".swatch").forEach((b) => {
        b.classList.toggle("selected", b === button);
        b.setAttribute("aria-pressed", b === button);
      });
      $("#theme-label").textContent =
        button.dataset.theme === "jade" ? "青玉" : "暖琥珀";
    }),
);
const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
$("#motion").checked = !reduced;
document.body.classList.toggle("no-motion", reduced);
$("#motion").onchange = (e) => {
  document.body.classList.toggle("no-motion", !e.target.checked);
  document.body.classList.toggle("allow-motion", e.target.checked);
};
function stopTour() {
  state.tour = false;
  clearInterval(tourTimer);
  $("#tour").innerHTML = icon("play") + "<span>播放交互导览</span>";
}
$("#tour").onclick = () => {
  if (state.tour) {
    stopTour();
    return;
  }
  wake();
  render("home");
  state.tour = true;
  $("#tour").innerHTML = icon("pause") + "<span>停止交互导览</span>";
  let step = 0;
  const names = ["home", "music", "devices", "computer", "apps"];
  tourTimer = setInterval(() => {
    step++;
    if (step === names.length) {
      stopTour();
      return;
    }
    render(names[step], { manual: false });
  }, 4500);
};
let pointerStart = null;
$("#screen").addEventListener("pointerdown", (e) => {
  if (
    e.target.closest("button,input,[role=application]") ||
    !["home", "apps"].includes(state.view)
  )
    return;
  pointerStart = [e.clientX, e.clientY];
});
$("#screen").addEventListener("pointerup", (e) => {
  if (!pointerStart) return;
  const [x, y] = pointerStart;
  pointerStart = null;
  const dx = e.clientX - x,
    dy = e.clientY - y;
  if (Math.abs(dx) < 45 || Math.abs(dx) < Math.abs(dy)) return;
  if (state.view === "home") {
    state.page = 0;
    render("apps");
  } else if (dx < 0 && state.page === 0) {
    state.page = 1;
    render();
  } else if (dx > 0 && state.page === 1) {
    state.page = 0;
    render();
  } else if (dx > 0) render("home");
});
$("#screen").addEventListener("pointercancel", () => (pointerStart = null));
document.addEventListener("keydown", (e) => {
  if (e.target.matches("input") || e.target.closest("[role=application]"))
    return;
  if (e.key === "Escape") {
    wake();
    clearAI();
    render("home");
  }
  if (state.view === "apps" && ["ArrowLeft", "ArrowRight"].includes(e.key)) {
    state.page = e.key === "ArrowRight" ? 1 : 0;
    render();
  }
});
const resize = () => {
  const scale = Math.min(1, ($("#stage").clientWidth - 22) / 506);
  document.documentElement.style.setProperty("--scale", scale);
  $("#stage").style.height = 506 * scale + 24 + "px";
};
new ResizeObserver(resize).observe($("#stage"));
setInterval(() => {
  updateClock();
  updateHomeContext();
}, 1000);
setInterval(() => {
  if (state.view === "home" && state.ai === "idle" && state.screenOn) {
    idleCopyIndex = (idleCopyIndex + 1) % idleCopy.length;
    updateAI();
    const el = $(".home-dialogue");
    el.classList.remove("copy-arrive");
    void el.offsetWidth;
    el.classList.add("copy-arrive");
  }
}, 14000);
function initGravity() {
  const field = $("#gravity-field"),
    ball = $("#gravity-ball");
  let x = 0,
    y = 0,
    tx = 0,
    ty = 0,
    vx = 0,
    vy = 0;
  const target = (a, b) => {
    const length = Math.hypot(a, b);
    const factor = length > 90 ? 90 / length : 1;
    tx = a * factor;
    ty = b * factor;
  };
  field.onpointermove = (e) => {
    const r = field.getBoundingClientRect();
    target(
      ((e.clientX - r.left - r.width / 2) * 228) / r.width,
      ((e.clientY - r.top - r.height / 2) * 228) / r.height,
    );
  };
  field.onpointerdown = (e) => field.setPointerCapture(e.pointerId);
  field.onkeydown = (e) => {
    const d = {
      ArrowLeft: [-25, 0],
      ArrowRight: [25, 0],
      ArrowUp: [0, -25],
      ArrowDown: [0, 25],
    }[e.key];
    if (d) {
      e.preventDefault();
      target(tx + d[0], ty + d[1]);
    }
  };
  let last = 0;
  function frame(now) {
    if (now - last >= 32) {
      last = now;
      const instant = document.body.classList.contains("no-motion");
      vx = (vx + (tx - x) * 0.055) * 0.8;
      vy = (vy + (ty - y) * 0.055) * 0.8;
      x = instant ? tx : x + vx;
      y = instant ? ty : y + vy;
      const length = Math.hypot(x, y);
      if (length > 90) {
        x = (x / length) * 90;
        y = (y / length) * 90;
        vx *= 0.3;
        vy *= 0.3;
      }
      ball.style.transform = `translate(${x}px,${y}px)`;
    }
    gravityFrame = requestAnimationFrame(frame);
  }
  gravityFrame = requestAnimationFrame(frame);
}
render();
resize();
