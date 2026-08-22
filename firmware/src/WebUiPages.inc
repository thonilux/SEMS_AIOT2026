String buildPageHeader(const String& title) {
  String page;
  page.reserve(2300);
  page += F("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>");
  page += htmlEscape(title);
  page += F("</title><style>");
  page += F("*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:#f1f5f9;color:#0f172a}");
  page += F(".bar{background:#fff;border-bottom:1px solid #e2e8f0;padding:12px 16px}.top{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}");
  page += F("nav{display:flex;gap:6px;flex-wrap:wrap}nav a{color:#0f766e;text-decoration:none;font-weight:600;font-size:13px;padding:6px 10px;border-radius:6px}nav a:hover{background:#f0fdf4}");
  page += F(".wrap{max-width:640px;margin:0 auto;padding:16px}.panel{background:#fff;border-radius:10px;padding:14px 16px;margin:10px 0;box-shadow:0 1px 2px rgba(0,0,0,.06)}");
  page += F("h1{font-size:18px;margin:0}.muted{color:#64748b;font-size:13px}.grid{display:grid;grid-template-columns:140px 1fr;gap:6px 10px;font-size:14px}");
  page += F("label{font-weight:600;font-size:13px;color:#334155;display:block;margin:10px 0 4px}input{width:100%;border:1px solid #cbd5e1;border-radius:6px;padding:9px 10px;font-size:14px}");
  page += F(".actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}button{background:#0f766e;color:#fff;border:0;border-radius:6px;padding:9px 14px;font-weight:600;font-size:14px}");
  page += F("button:disabled{background:#94a3b8}.net{display:flex;justify-content:space-between;gap:10px;border-top:1px solid #f1f5f9;padding:8px 0}");
  page += F(".ssid{font-weight:600;font-size:13px;overflow-wrap:anywhere}.tag{font-size:11px;color:#64748b;background:#f1f5f9;border-radius:999px;padding:2px 8px}.use{background:#334155;padding:6px 10px;font-size:12px}");
  page += F(".metric{margin:10px 0}.metricTop{display:flex;justify-content:space-between;gap:10px;font-size:13px}.track{height:8px;background:#e2e8f0;border-radius:99px;overflow:hidden}.fill{height:100%;background:#0f766e}.warn{background:#d97706}.bad{background:#dc2626}");
  page += F(".menuBtn{display:none;background:#f1f5f9;color:#0f172a;border:1px solid #e2e8f0;border-radius:6px;padding:7px 10px;font-size:14px}");
  page += F(".sw{position:relative;display:inline-block;width:40px;height:22px;vertical-align:middle;margin:4px 0}.sw input{opacity:0;width:0;height:0}.sl{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#cbd5e1;border-radius:22px}.sl:before{position:absolute;content:\"\";height:16px;width:16px;left:3px;bottom:3px;background:#fff;border-radius:50%}input:checked+.sl{background:#0f766e}input:checked+.sl:before{transform:translateX(18px)}");
  page += F("@media(max-width:560px){.grid{grid-template-columns:1fr}.net{display:block}.top{display:block}.menuBtn{display:block;margin-top:8px}nav{display:none;flex-direction:column;margin-top:8px}nav.open{display:flex}}");
  page += F("</style></head><body><div class=\"bar\"><div class=\"top\"><div><h1>");
  page += htmlEscape(title);
  page += F("</h1><div class=\"muted\">SEMS AIoT</div></div>");
  page += F("<button class=\"menuBtn\" onclick=\"toggleMenu()\">☰</button>");
  page += F("<nav id=\"mainNav\"><a href=\"/\">Home</a><a href=\"/meter\">Meter</a>");

  if (currentMode == AppMode::Config || configApStarted || webConfigUnlocked) {
      page += F("<a href=\"/network\">Network</a>");
      page += F("<a href=\"/device\">Device</a>");
      page += F("<a href=\"/mqtt\">MQTT</a>");
      page += F("<a href=\"/modbus\">Modbus</a>");
      page += F("<a href=\"/protection\">Protection</a>");
      page += F("<a href=\"/display\">Display</a>");
      page += F("<a href=\"/history\">History</a>");
      page += F("<a href=\"/system\">🛠 System</a>");
  }

  page += F("<a href=\"#\" onclick=\"rebootDevice();return false;\">🔄 Reboot</a>");
  page += F("</nav></div></div>");
  page += F("<main class=\"wrap\">");
  return page;
}

String buildPageFooter() {
  String page;
  page.reserve(2500);
  page += F("</main>");

  if (!(currentMode == AppMode::Config || configApStarted || webConfigUnlocked)) {
    page += F("<footer style=\"text-align:center;margin:40px 0 20px;border-top:1px solid #cbd5e1;padding-top:20px;\">");
    page += F("<button type=\"button\" onclick=\"promptUnlock()\" style=\"background:none;border:none;color:#64748b;font-size:24px;cursor:pointer;padding:8px;line-height:1;\" title=\"Unlock Settings\">⚙️</button>");
    page += F("</footer>");
  }

  page += F("<script>");
  page += F("function toggleMenu(){var nav=document.getElementById('mainNav');nav.classList.toggle('open');}");
  page += F("function rebootDevice(){fetch('/api/reboot',{method:'POST'}).then(()=>{document.body.insertAdjacentHTML('beforeend','<div style=\"position:fixed;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(17,24,39,.86);color:#fff;font:600 18px/1.4 sans-serif;z-index:9999\">Rebooting...</div>');});}");
  page += F("function promptUnlock(){");
  page += F("const pin=prompt('Enter PIN to unlock settings:');");
  page += F("if(pin===null)return;");
  page += F("fetch('/api/unlock',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+encodeURIComponent(pin)})");
  page += F(".then(r=>r.json())");
  page += F(".then(d=>{if(d.ok){alert('Settings unlocked!');location.reload();}else{alert('Invalid PIN!');}});");
  page += F("}");
  page += F("</script></body></html>");
  return page;
}

String buildHomePage() {
  String page = buildPageHeader(F("Home"));
  page += F("<section class=\"panel\"><div class=\"muted\">🏠 Device status</div><div class=\"grid\">");
  page += F("<div>Mode</div><div>"); page += appModeToString(currentMode); page += F("</div>");
  page += F("<div>Firmware</div><div>"); page += FW_VERSION; page += F("</div>");
  page += F("<div>Chip</div><div>ESP32"); page += F("</div>");
  page += F("<div>Config AP</div><div>"); page += configApStarted ? F("active") : F("inactive"); page += F("</div>");
  page += F("<div>AP SSID</div><div>"); page += htmlEscape(getConfigApSsid()); page += F("</div>");
  page += F("<div>WiFi SSID</div><div>"); page += hasSavedWifi ? htmlEscape(savedWifiSsid) : F("(none)"); page += F("</div>");
  page += F("<div>WiFi state</div><div>"); page += wifiConnected ? F("connected") : (wifiConnecting ? F("connecting") : F("disconnected")); page += F("</div>");
  page += F("<div>RTC</div><div>"); page += getRtcString(); page += F("</div>");
  page += F("<div>NTP sync</div><div>"); page += timeSynced ? formatDateTime(lastNtpSyncEpoch) : String("pending"); page += F("</div>");
  page += F("<div>Uptime</div><div>"); page += formatUptime(millis()); page += F("</div>");
  page += F("<div>Heap used</div><div>"); page += formatBytesHuman(getHeapUsedBytes()); page += F(" ("); page += String(getHeapUsedPercent()); page += F("%)</div>");
  page += F("<div>Heap free</div><div>"); page += formatBytesHuman(ESP.getFreeHeap()); page += F("</div>");
  page += F("<div>Sketch size</div><div>"); page += formatBytesHuman(ESP.getSketchSize()); page += F("</div>");
  page += F("<div>Sketch free</div><div>"); page += formatBytesHuman(ESP.getFreeSketchSpace()); page += F("</div>");
  page += F("<div>Flash size</div><div>"); page += formatBytesHuman(ESP.getFlashChipSize()); page += F("</div>");
  page += F("<div>Flash speed</div><div>"); page += String(ESP.getFlashChipSpeed() / 1000000UL); page += F(" MHz</div>");
  page += F("<div>WiFi RSSI</div><div>");
  page += wifiConnected ? String(WiFi.RSSI()) + String(" dBm") : String("(n/a)");
  page += F("</div>");
  page += F("<div>MQTT</div><div>"); page += mqttConnected ? F("connected") : (currentMqttConfig.enabled ? F("offline") : F("disabled")); page += F("</div>");
  page += F("<div>Relays (R1-R4)</div><div>R1:"); page += buildRelayStateText(0); page += F(" | R2:"); page += buildRelayStateText(1); page += F(" | R3:"); page += buildRelayStateText(2); page += F(" | R4:"); page += buildRelayStateText(3); page += F("</div>");
  page += F("<div>Display</div><div>"); page += currentDisplayConfig.enabled ? (displayReady ? F("ready") : F("starting")) : F("disabled"); page += F("</div>");
  page += F("<div>History</div><div>"); page += currentHistoryConfig.enabled ? formatDayKey(historyRuntime[0].dayKey) : F("disabled"); page += F("</div>");
  page += F("</div></section>");
  page += F("<section class=\"panel\"><div class=\"metric\"><div class=\"metricTop\"><span>RAM Used</span><span>");
  page += String(getHeapUsedPercent()); page += F("%</span></div><div class=\"track\"><div class=\"fill\" style=\"width:"); page += String(getHeapUsedPercent()); page += F("%\"></div></div><div class=\"muted\">"); page += formatBytesHuman(getHeapUsedBytes()); page += F(" used / "); page += formatBytesHuman(getHeapTotalBytes()); page += F(" total</div></div>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><span>Firmware Slot Used</span><span>");
  page += String(getSketchUsedPercent()); page += F("%</span></div><div class=\"track\"><div class=\"fill\" style=\"width:"); page += String(getSketchUsedPercent()); page += F("%\"></div></div><div class=\"muted\">"); page += formatBytesHuman(ESP.getSketchSize()); page += F(" / "); page += formatBytesHuman(ESP.getFreeSketchSpace() + ESP.getSketchSize()); page += F("</div></div>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><span>WiFi Signal</span><span>");
  page += String(getWifiQualityPercent()); page += F("%</span></div><div class=\"track\"><div class=\"fill\" style=\"width:"); page += String(getWifiQualityPercent()); page += F("%\"></div></div><div class=\"muted\">"); page += wifiConnected ? String(WiFi.RSSI()) + String(" dBm") : String("no link"); page += F("</div></div>");
  page += F("</section>");
  for (size_t i = 0; i < 3; i++) {
    const MeterSnapshot& s = meterSnapshots[i];
    page += F("<section class=\"panel\"><div class=\"muted\">⚡ Meter ");
    page += String(i + 1);
    page += F(" (slave ");
    page += String(currentModbusConfig.slave_id[i]);
    page += F(")</div>");
    page += F("<div class=\"grid\">");
    page += F("<div>Status</div><div>"); page += s.online ? F("online") : F("offline"); page += F("</div>");
    page += F("<div>Voltage</div><div>"); page += formatFloatValue(s.voltage, 2, "V"); page += F("</div>");
    page += F("<div>Current</div><div>"); page += formatFloatValue(s.current, 2, "A"); page += F("</div>");
    page += F("<div>Power</div><div>"); page += formatFloatValue(s.power, 2, "kW"); page += F("</div>");
    page += F("<div>Frequency</div><div>"); page += formatFloatValue(s.frequency, 2, "Hz"); page += F("</div>");
    page += F("<div>Energy</div><div>"); page += formatFloatValue(s.energy, 3, "kWh"); page += F("</div>");
    page += F("<div>Last poll</div><div>"); page += s.lastPollMs > 0 ? formatUptime(s.lastPollMs) : String("pending"); page += F("</div>");
    page += F("</div></section>");
  }
  page += buildPageFooter();
  return page;
}

String buildMeterPage() {
  String page = buildPageHeader(F("Meter"));
  for (size_t i = 0; i < 3; i++) {
    const MeterSnapshot& s = meterSnapshots[i];
    page += F("<section class=\"panel\"><div class=\"muted\">⚡ Renatta AX9L meter ");
    page += String(i + 1);
    page += F(" (slave ");
    page += String(currentModbusConfig.slave_id[i]);
    page += F(")</div>");
    page += F("<div class=\"grid\">");
    page += F("<div>Status</div><div>"); page += s.online ? F("online") : F("offline"); page += F("</div>");
    page += F("<div>Valid</div><div>"); page += s.valid ? F("yes") : F("no"); page += F("</div>");
    page += F("<div>Voltage A-N</div><div>"); page += formatFloatValue(s.voltage, 2, "V"); page += F("</div>");
    page += F("<div>Current A</div><div>"); page += formatFloatValue(s.current, 2, "A"); page += F("</div>");
    page += F("<div>Active Power A</div><div>"); page += formatFloatValue(s.power, 2, "kW"); page += F("</div>");
    page += F("<div>Frequency</div><div>"); page += formatFloatValue(s.frequency, 2, "Hz"); page += F("</div>");
    page += F("<div>Power factor</div><div>"); page += formatFloatValue(s.pf, 2, ""); page += F("</div>");
    page += F("<div>Energy</div><div>"); page += formatFloatValue(s.energy, 3, "kWh"); page += F("</div>");
    page += F("<div>Last success</div><div>"); page += s.lastSuccessMs > 0 ? formatUptime(s.lastSuccessMs) : String("pending"); page += F("</div>");
    page += F("</div></section>");
    page += F("<section class=\"panel\"><div class=\"metric\"><div class=\"metricTop\"><span>Meter online</span><span>");
    page += s.online ? F("yes") : F("no");
    page += F("</span></div><div class=\"track\"><div class=\"fill\" style=\"width:");
    page += s.online ? F("100") : F("0");
    page += F("%\"></div></div><div class=\"muted\">");
    page += s.online ? F("RS485 responds.") : F("Waiting for a valid Modbus reply.");
    page += F("</div></div></section>");
  }
  page += F("<section class=\"panel\"><div class=\"muted\">⚡ 4-Channel Relay Control</div>");
  page += F("<div class=\"grid\">");
  const uint8_t defPins[4] = {2, 15, 14, 13};
  for (size_t r = 0; r < 4; r++) {
    uint8_t pin = (currentProtectionConfig.relay_pin[r] == 0) ? defPins[r] : currentProtectionConfig.relay_pin[r];
    page += F("<div>Relay "); page += String(r + 1); page += F(" (GPIO "); page += String(pin); page += F(")</div>");
    page += F("<div>State: <b>"); page += buildRelayStateText(r); page += F("</b> &nbsp; ");
    page += F("<button type=\"button\" onclick=\"relayAction("); page += String(r + 1); page += F(",'set')\">ON</button> ");
    page += F("<button type=\"button\" onclick=\"relayAction("); page += String(r + 1); page += F(",'reset')\">OFF</button></div>");
  }
  page += F("</div><div class=\"actions\"><button type=\"button\" onclick=\"relayAction(0,'set')\">ALL ON</button><button type=\"button\" onclick=\"relayAction(0,'reset')\">ALL OFF</button><button type=\"button\" onclick=\"location.reload()\">Refresh</button></div></section>");
  page += F("<script>function relayAction(r,act){fetch('/api/relay/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'relay='+r+'&action='+encodeURIComponent(act)}).then(()=>location.reload());}</script>");
  page += buildPageFooter();
  return page;
}

String buildNetworkPage() {
  String page = buildPageHeader(F("Network"));
  page += F("<section class=\"panel\"><div class=\"muted\">🔌 Ethernet (W5500)</div>");
  page += F("<div id=\"ethStatus\" class=\"muted\">Loading...</div>");
  page += F("</section>");
  page += F("<script>fetch('/api/status',{cache:'no-store'}).then(r=>r.json()).then(d=>{const el=document.getElementById('ethStatus');el.textContent=d.eth_link?'Link UP — IP: '+d.eth_ip:'Link DOWN (no cable or DHCP failed)';el.style.color=d.eth_link?'#4caf50':'#f44336';});</script>");
  page += F("<section class=\"panel\"><div class=\"muted\">📶 WiFi setup</div>");
  page += F("<div class=\"actions\"><button type=\"button\" onclick=\"scanWifi()\">Scan WiFi</button><button type=\"button\" onclick=\"loadCurrentWifi()\">Load Saved</button></div>");
  page += F("<div id=\"wifiStatus\" class=\"muted\" style=\"margin-top:10px\">Ready to scan.</div>");
  page += F("<div id=\"wifiList\" style=\"margin-top:14px\"></div>");
  page += F("</section>");
  page += F("<section class=\"panel\"><div class=\"muted\">💾 Saved networks</div>");
  page += F("<div id=\"savedStatus\" class=\"muted\" style=\"margin-top:6px\">Loading...</div>");
  page += F("<div id=\"savedList\" style=\"margin-top:10px\"></div>");
  page += F("</section>");
  page += F("<section class=\"panel\"><form id=\"wifiForm\"><label for=\"ssid\">SSID</label><input id=\"ssid\" name=\"ssid\" maxlength=\"64\" required>");
  page += F("<label for=\"password\">Password</label><input id=\"password\" name=\"password\" type=\"password\" maxlength=\"64\">");
  page += F("<div class=\"actions\"><button type=\"submit\">💾 Save WiFi</button><button type=\"button\" onclick=\"rebootDevice()\">🔄 Reboot</button></div>");
  page += F("</form></section>");
  page += F("<script>");
  page += F("const wifiStatus=document.getElementById('wifiStatus');const wifiList=document.getElementById('wifiList');const wifiForm=document.getElementById('wifiForm');");
  page += F("const ssidInput=document.getElementById('ssid');const passwordInput=document.getElementById('password');");
  page += F("const savedStatus=document.getElementById('savedStatus');const savedList=document.getElementById('savedList');");
  page += F("async function scanWifi(){wifiStatus.textContent='Scanning...';wifiList.innerHTML='';try{const res=await fetch('/api/wifi/scan',{cache:'no-store'});const data=await res.json();if(!data.ok){wifiStatus.textContent=data.error||'Scan failed';return;}wifiStatus.textContent='Found '+data.networks.length+' network(s).';wifiList.innerHTML=data.networks.map(n=>'<div class=\"net\"><div><div class=\"ssid\">'+escapeHtml(n.ssid||'(hidden)')+'</div><div class=\"tag\">'+escapeHtml(n.security)+' | Ch '+n.channel+' | '+n.rssi+' dBm</div></div><div><button class=\"use\" type=\"button\" onclick=\"useNetwork(\\''+escapeJs(n.ssid||'')+'\\')\">Use</button></div></div>').join('');}catch(e){wifiStatus.textContent='Scan error: '+e.message;}}");
  page += F("async function loadCurrentWifi(){try{const res=await fetch('/api/status',{cache:'no-store'});const data=await res.json();const saved=data.saved_wifi_ssid||data.wifi_ssid||'';if(saved){ssidInput.value=saved;wifiStatus.textContent='Loaded saved SSID: '+saved;}else{wifiStatus.textContent='No saved WiFi found.';}}catch(e){wifiStatus.textContent='Load failed.';}}");
  page += F("function useNetwork(ssid){ssidInput.value=ssid;passwordInput.focus();}");
  page += F("async function loadSavedList(){try{const res=await fetch('/api/wifi/list',{cache:'no-store'});const data=await res.json();if(!data.ok){savedStatus.textContent='Load failed';return;}if(!data.networks.length){savedStatus.textContent='No saved networks yet.';savedList.innerHTML='';return;}savedStatus.textContent=data.networks.length+' saved network(s).';savedList.innerHTML=data.networks.map(n=>'<div class=\"net\"><div><div class=\"ssid\">'+escapeHtml(n.ssid)+'</div></div><div><button class=\"use\" type=\"button\" onclick=\"deleteNetwork(\\''+escapeJs(n.ssid)+'\\')\">Delete</button></div></div>').join('');}catch(e){savedStatus.textContent='Load error: '+e.message;}}");
  page += F("async function deleteNetwork(ssid){if(!confirm('Delete saved network \"'+ssid+'\"?'))return;try{const params=new URLSearchParams();params.append('ssid',ssid);const res=await fetch('/api/wifi/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});const data=await res.json();savedStatus.textContent=data.ok?'Deleted.':'Delete failed.';loadSavedList();}catch(e){savedStatus.textContent='Delete error: '+e.message;}}");
  page += F("wifiForm.addEventListener('submit',async function(e){e.preventDefault();wifiStatus.textContent='Saving...';try{const params=new URLSearchParams();params.append('ssid',ssidInput.value);params.append('password',passwordInput.value);const res=await fetch('/api/wifi/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});const data=await res.json();wifiStatus.textContent=data.ok?'Saved! Rebooting device to connect...':(data.error||'Save failed');if(data.ok){setTimeout(()=>rebootDevice(),1500);}}catch(e){wifiStatus.textContent='Save error: '+e.message;}});");
  page += F("function escapeHtml(s){return String(s).replace(/[&<>\"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[m]));}");
  page += F("function escapeJs(s){return String(s).replace(/\\\\/g,'\\\\\\\\').replace(/'/g,\"\\\\'\");}");
  page += F("scanWifi();loadSavedList();");
  page += F("</script>");
  page += buildPageFooter();
  return page;
}
