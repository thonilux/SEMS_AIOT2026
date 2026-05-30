(function () {
  const $ = (id) => document.getElementById(id);

  function text(id, value) {
    const el = $(id);
    if (el) el.textContent = value;
  }

  function value(id, nextValue) {
    const el = $(id);
    if (el && typeof nextValue !== "undefined") el.value = nextValue;
    return el ? el.value : "";
  }

  function show(id, visible) {
    const el = $(id);
    if (el) el.style.display = visible ? "" : "none";
  }

  function setBar(fillId, labelId, percent, label) {
    const fill = $(fillId);
    const labelEl = $(labelId);
    if (fill) {
      fill.classList.remove("warn", "bad");
      fill.style.width = `${percent}%`;
    }
    if (labelEl) labelEl.textContent = label;
  }

  function esc(v) {
    return String(v).replace(/[&<>"']/g, (m) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[m]));
  }

  function humanWifi(rssi) {
    if (typeof rssi !== "number" || rssi <= -120) return "-";
    return `${rssi} dBm`;
  }

  async function fetchJson(url, options) {
    const res = await fetch(url, options);
    return res.json();
  }

  async function refreshHome() {
    try {
      const d = await fetchJson(`/api/status?_=${Date.now()}`, { cache: "no-store" });
      text("firmware_value", d.firmware || "-");
      text("mode_value", d.mode || "-");
      text("ap_ssid_value", d.ap_ssid || "-");
      text("ap_ip_value", d.ap_ip || "-");
      text("saved_wifi_value", d.saved_wifi_ssid || "(none)");
      text("sta_status_value", d.sta_status || "-");
      text("sta_ip_value", d.sta_ip || "-");
      text("rtc_value", d.rtc || "-");
      text("last_ntp_value", d.last_ntp_sync || "-");
      text("uptime_value", d.uptime_text || "-");
      text("cpu_value", `${d.cpu_mhz || 240} MHz`);
      text("wifi_rssi_value", humanWifi(d.wifi_rssi));
      text("mac_suffix_value", d.mac_suffix || "-");
      text("heap_value", `${d.heap_total_human || "-"} total, ${d.heap_used_human || "-"} used`);

      const heapPct = Number(d.heap_used_percent || 0);
      const sketchPct = Number(d.sketch_used_percent || 0);
      const fsPct = Number(d.littlefs_used_percent || 0);
      const wifiPct = Number(d.wifi_quality_percent || 0);

      setBar("heap_fill", "heap_pct", heapPct, `${heapPct}% · ${d.heap_used_human || "-"} / ${d.heap_total_human || "-"}`);
      setBar("sketch_fill", "sketch_pct", sketchPct, `${sketchPct}% · ${d.sketch_size_human || "-"} / ${d.sketch_capacity_human || "-"}`);
      setBar("fs_fill", "fs_pct", fsPct, `${fsPct}% · ${d.littlefs_used_human || "-"} / ${d.littlefs_total_human || "-"}`);
      setBar("wifi_fill", "wifi_pct", wifiPct, `${wifiPct}% · ${humanWifi(d.wifi_rssi)}`);

      if ($("heap_fill")) {
        $("heap_fill").classList.remove("warn", "bad");
        if (d.heap_used_percent >= 85) $("heap_fill").classList.add("bad");
        else if (d.heap_used_percent >= 70) $("heap_fill").classList.add("warn");
      }
      if ($("sketch_fill")) {
        $("sketch_fill").classList.remove("warn", "bad");
        if (d.sketch_used_percent >= 85) $("sketch_fill").classList.add("bad");
        else if (d.sketch_used_percent >= 70) $("sketch_fill").classList.add("warn");
      }
      if ($("fs_fill")) {
        $("fs_fill").classList.remove("warn", "bad");
        if (d.littlefs_used_percent >= 85) $("fs_fill").classList.add("bad");
        else if (d.littlefs_used_percent >= 70) $("fs_fill").classList.add("warn");
      }
      if ($("wifi_fill")) {
        $("wifi_fill").classList.remove("warn", "bad");
        if (d.wifi_quality_percent <= 30) $("wifi_fill").classList.add("bad");
        else if (d.wifi_quality_percent <= 55) $("wifi_fill").classList.add("warn");
      }
    } catch (err) {
      console.log("refreshHome failed", err);
    }
  }

  function renderNetworks(list) {
    const container = $("networks");
    if (!container) return;
    container.innerHTML = "";

    list.forEach((item) => {
      const row = document.createElement("div");
      row.className = "net";

      const left = document.createElement("div");
      const ssid = document.createElement("div");
      ssid.className = "ssid";
      ssid.textContent = item.ssid || "(hidden)";
      const meta = document.createElement("div");
      meta.className = "muted subtle";
      meta.textContent = `CH ${item.channel} · ${item.encryption}`;
      left.appendChild(ssid);
      left.appendChild(meta);

      const right = document.createElement("div");
      const tag = document.createElement("span");
      tag.className = "tag";
      tag.textContent = `${item.rssi} dBm`;
      const btn = document.createElement("button");
      btn.className = "use";
      btn.type = "button";
      btn.textContent = "Use";
      btn.addEventListener("click", () => useSsid(item.ssid || ""));
      right.appendChild(tag);
      right.appendChild(document.createTextNode(" "));
      right.appendChild(btn);

      row.appendChild(left);
      row.appendChild(right);
      container.appendChild(row);
    });
  }

  async function scanWifi() {
    const button = $("scanBtn");
    const state = $("scanState");
    const networks = $("networks");
    if (button) button.disabled = true;
    if (state) state.textContent = "Scanning...";
    if (networks) networks.innerHTML = "";
    try {
      const d = await fetchJson("/api/wifi/scan");
      if (state) state.textContent = `${d.count || 0} network(s) found`;
      renderNetworks(Array.isArray(d.networks) ? d.networks : []);
    } catch (err) {
      if (state) state.textContent = "Scan failed";
      console.log("scanWifi failed", err);
    } finally {
      if (button) button.disabled = false;
    }
  }

  function useSsid(next) {
    value("ssid", next);
    const pw = $("password");
    if (pw) pw.focus();
  }

  async function saveWifi() {
    const state = $("saveState");
    const ssid = value("ssid").trim();
    const password = value("password");
    if (!ssid) {
      if (state) state.textContent = "SSID is required";
      return;
    }

    if (state) state.textContent = "Saving...";
    try {
      const body = new URLSearchParams({ ssid, password });
      const d = await fetchJson("/api/wifi/save", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body,
      });
      if (state) state.textContent = d.ok ? "Saved. Reboot to connect." : d.error || "Save failed";
    } catch (err) {
      if (state) state.textContent = "Save failed";
      console.log("saveWifi failed", err);
    }
  }

  async function rebootDevice() {
    const state = $("saveState") || $("homeState");
    if (state) state.textContent = "Rebooting...";
    try {
      await fetch("/api/reboot", { method: "POST" });
    } catch (err) {
      console.log("rebootDevice failed", err);
    }
  }

  function initMenu() {
    const button = $("menuBtn");
    const nav = $("mainNav");
    if (!button || !nav) return;
    button.addEventListener("click", () => nav.classList.toggle("open"));
  }

  function initHome() {
    refreshHome();
    setInterval(refreshHome, 1000);
  }

  async function initNetwork() {
    try {
      const d = await fetchJson("/api/status?_=" + Date.now(), { cache: "no-store" });
      if (d.saved_wifi_ssid && !value("ssid")) {
        value("ssid", d.saved_wifi_ssid);
      }
    } catch (err) {
      console.log("network init status failed", err);
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    initMenu();
    const page = document.body.dataset.page || "";
    if (page === "home") {
      initHome();
    }
    if (page === "network") {
      initNetwork();
    }
  });

  window.scanWifi = scanWifi;
  window.saveWifi = saveWifi;
  window.rebootDevice = rebootDevice;
  window.useSsid = useSsid;
})();
