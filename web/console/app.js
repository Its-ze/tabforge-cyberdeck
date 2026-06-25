const state = {
  tab: "mesh",
  mode: "meshtastic",
  events: []
};

const panels = {
  mesh: {
    title: "Mesh Dashboard",
    sub: "C6L status, mesh traffic, and mode switching.",
    actions: [
      ["Scan C6L", "Probe USB CDC for Unit C6L", "primary"],
      ["Nodes", "Read Meshtastic node list with secrets redacted", ""],
      ["Send", "Queue a visible channel message", ""],
      ["MeshCore OTA", "Guide start ota and upload handoff", ""],
      ["GPS Handoff", "Share Tab5 location only after approval", ""],
      ["Canned", "Send saved quick phrases", ""]
    ]
  },
  devices: {
    title: "Device Bay",
    sub: "C6L, T-Deck, IR unit, USB host, and Grove devices.",
    actions: [
      ["C6L", "OLED, RGB, buzzer, button, and radio status", "primary"],
      ["T-Deck", "Z-Deck status and safe companion actions", ""],
      ["IR Unit", "Learn and replay NEC-style commands", ""],
      ["USB Host", "Serial boards, keyboards, and storage", ""],
      ["RS485", "Terminal and future Modbus probe", ""],
      ["Ports", "Show transport priority and current link", ""]
    ]
  },
  tools: {
    title: "Tool Deck",
    sub: "Field tools built around Tab5 hardware.",
    actions: [
      ["Record", "Mic Deck WAV clip to SD", "primary"],
      ["Sound", "Live sound meter and threshold marker", ""],
      ["IR Macro", "Save learned command sequence", ""],
      ["Camera", "QR and label scan staging", ""],
      ["IMU", "Gesture and orientation profile", ""],
      ["Notes", "Append redacted field note", ""]
    ]
  },
  system: {
    title: "System",
    sub: "Updates, SD state, power, and public-safe export.",
    actions: [
      ["Update", "Check GitHub Pages manifest", "primary"],
      ["Backup", "Save local device profiles before changes", ""],
      ["Export", "Create redacted support bundle", ""],
      ["Power", "Battery and runtime estimate", ""],
      ["SD", "Open TabForge SD paths", ""],
      ["Manifest", "Show current update metadata", ""]
    ]
  }
};

function logEvent(message) {
  const stamp = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  state.events.unshift(`${stamp} ${message}`);
  state.events = state.events.slice(0, 80);
  renderEvents();
}

function renderEvents() {
  const log = document.getElementById("eventLog");
  log.innerHTML = "";
  state.events.forEach((event) => {
    const li = document.createElement("li");
    li.textContent = event;
    log.appendChild(li);
  });
}

function renderPanel() {
  const panel = panels[state.tab];
  document.getElementById("panelTitle").textContent = panel.title;
  document.getElementById("panelSub").textContent = panel.sub;
  document.getElementById("modeLabel").textContent = state.mode.toUpperCase();

  const grid = document.getElementById("quickGrid");
  grid.innerHTML = "";
  panel.actions.forEach(([label, copy, variant]) => {
    const tile = document.createElement("article");
    tile.className = "tile";

    const h3 = document.createElement("h3");
    h3.textContent = label;
    const p = document.createElement("p");
    p.textContent = copy;
    const button = document.createElement("button");
    button.textContent = label;
    if (variant === "primary") button.classList.add("primary");
    button.addEventListener("click", () => logEvent(`${panel.title}: ${label}`));

    tile.append(h3, p, button);
    grid.appendChild(tile);
  });
}

document.querySelectorAll(".tab").forEach((button) => {
  button.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((item) => item.classList.remove("active"));
    button.classList.add("active");
    state.tab = button.dataset.tab;
    logEvent(`tab=${state.tab}`);
    renderPanel();
  });
});

document.querySelectorAll(".mode").forEach((button) => {
  button.addEventListener("click", () => {
    document.querySelectorAll(".mode").forEach((item) => item.classList.remove("active"));
    button.classList.add("active");
    state.mode = button.dataset.mode;
    logEvent(`mode=${state.mode}; transport reconnect required`);
    renderPanel();
  });
});

document.getElementById("updateButton").addEventListener("click", async () => {
  logEvent("update check requested");
  try {
    const res = await fetch("../../docs/manifest.json", { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const manifest = await res.json();
    const label = manifest.latest && manifest.latest.firmware && manifest.latest.firmware.available ? "available" : "not published";
    document.getElementById("updateLabel").textContent = manifest.latest.version;
    logEvent(`manifest ${manifest.channel} ${manifest.latest.version}; firmware ${label}`);
  } catch (err) {
    logEvent(`manifest unavailable from file preview: ${err.message}`);
  }
});

document.getElementById("clearLog").addEventListener("click", () => {
  state.events = [];
  renderEvents();
});

renderPanel();
logEvent("TabForge console ready");
