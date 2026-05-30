String buildPageHeader(const String& title) {
  String page;
  page.reserve(2300);
  page += F("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>");
  page += htmlEscape(title);
  page += F("</title><style>");
  page += F(":root{font-family:Inter,Segoe UI,Arial,sans-serif;color:#17202a;background:#f4f7f9}");
  page += F("body{margin:0}.bar{background:#111827;color:white;padding:14px 18px}.top{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}");
  page += F("nav{display:flex;gap:8px}nav a{color:#d1fae5;text-decoration:none;font-weight:700;padding:7px 9px;border-radius:6px}nav a:hover{background:#1f2937}");
  page += F(".wrap{max-width:760px;margin:0 auto;padding:18px}.panel{background:white;border:1px solid #d9e1e8;border-radius:8px;padding:16px;margin:14px 0}");
  page += F("h1{font-size:21px;margin:0}.muted{color:#667085}.grid{display:grid;grid-template-columns:150px 1fr;gap:8px 12px}");
  page += F("label{font-weight:700;display:block;margin:12px 0 6px}input{box-sizing:border-box;width:100%;border:1px solid #cbd5e1;border-radius:6px;padding:10px;font-size:15px}");
  page += F(".actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}button{background:#0f766e;color:white;border:0;border-radius:6px;padding:10px 13px;font-weight:700}");
  page += F("button:disabled{background:#94a3b8}.net{display:flex;justify-content:space-between;gap:10px;border-top:1px solid #e5e7eb;padding:10px 0}");
  page += F(".ssid{font-weight:700;overflow-wrap:anywhere}.tag{font-size:12px;color:#475467;background:#eef2f6;border-radius:999px;padding:2px 8px}.use{background:#334155;padding:7px 10px}");
  page += F(".metric{margin:12px 0}.metricTop{display:flex;justify-content:space-between;gap:10px}.track{height:10px;background:#e5e7eb;border-radius:999px;overflow:hidden}.fill{height:100%;background:#0f766e;border-radius:999px}.warn{background:#d97706}.bad{background:#dc2626}");
  page += F(".menuBtn{display:none;background:#1f2937;color:white;border:1px solid #374151;border-radius:6px;padding:8px 10px;font-size:18px}");
  page += F("@media(max-width:560px){.grid{grid-template-columns:1fr}.net{display:block}.top{display:block}.menuBtn{display:block;margin-top:10px}nav{display:none;flex-direction:column;margin-top:10px}nav.open{display:flex}}");
  page += F("</style></head><body><div class=\"bar\"><div class=\"top\"><div><h1>");
  page += htmlEscape(title);
  page += F("</h1><div class=\"muted\">PM1611 RS485 Reader</div></div>");
  page += F("<button class=\"menuBtn\" onclick=\"toggleMenu()\">â˜°</button>");
  page += F("<nav id=\"mainNav\"><a href=\"/\">ðŸ  Home</a>");

  if (currentMode == AppMode::Config || configApStarted) {
      page += F("<a href=\"/network\">ðŸ“¶ Network</a>");
      page += F("<a href=\"/device\">âš™ Device</a>");
      page += F("<a href=\"/mqtt\">ðŸ“¡ MQTT</a>");
      page += F("<a href=\"/modbus\">ðŸ”Œ Modbus</a>");
      page += F("<a href=\"/protection\">ðŸ›¡ Protection</a>");
      page += F("<a href=\"/display\">ðŸ–¥ Display</a>");
      page += F("<a href=\"/history\">ðŸ“Š History</a>");
      page += F("<a href=\"/system\">ðŸ”§ System</a>");
  }

  page += F("<a href=\"#\" onclick=\"rebootDevice();return false;\">ðŸ”„ Reboot</a>");
  page += F("</nav></div></div>");
  page += F("<main class=\"wrap\">");
  return page;
}

String buildPageFooter() {
  String page;
  page.reserve(1800);
  page += F("</main><script>");
  page += F("function toggleMenu(){var nav=document.getElementById('mainNav');nav.classList.toggle('open');}");
  page += F("function rebootDevice(){fetch('/api/reboot',{method:'POST'}).then(()=>{document.body.insertAdjacentHTML('beforeend','<div style=\"position:fixed;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(17,24,39,.86);color:#fff;font:600 18px/1.4 sans-serif;z-index:9999\">Rebooting...</div>');});}");
  page += F("</script></body></html>");
  return page;
}

String buildHomePage() {
  String page = buildPageHeader(F("Home"));
  page += F("<section class=\"panel\"><div class=\"muted\">Device status</div><div class=\"grid\">");
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
  page += F("</div></section>");
  page += F("<section class=\"panel\"><div class=\"metric\"><div class=\"metricTop\"><span>RAM Used</span><span>");
  page += String(getHeapUsedPercent()); page += F("%</span></div><div class=\"track\"><div class=\"fill\" style=\"width:"); page += String(getHeapUsedPercent()); page += F("%\"></div></div><div class=\"muted\">"); page += formatBytesHuman(getHeapUsedBytes()); page += F(" used / "); page += formatBytesHuman(getHeapTotalBytes()); page += F(" total</div></div>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><span>Firmware Slot Used</span><span>");
  page += String(getSketchUsedPercent()); page += F("%</span></div><div class=\"track\"><div class=\"fill\" style=\"width:"); page += String(getSketchUsedPercent()); page += F("%\"></div></div><div class=\"muted\">"); page += formatBytesHuman(ESP.getSketchSize()); page += F(" / "); page += formatBytesHuman(ESP.getFreeSketchSpace() + ESP.getSketchSize()); page += F("</div></div>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><span>WiFi Signal</span><span>");
  page += String(getWifiQualityPercent()); page += F("%</span></div><div class=\"track\"><div class=\"fill\" style=\"width:"); page += String(getWifiQualityPercent()); page += F("%\"></div></div><div class=\"muted\">"); page += wifiConnected ? String(WiFi.RSSI()) + String(" dBm") : String("no link"); page += F("</div></div>");
  page += F("</section>");
  page += buildPageFooter();
  return page;
}

String buildNetworkPage() {
  String page = buildPageHeader(F("Network"));
  page += F("<section class=\"panel\"><div class=\"muted\">WiFi setup</div>");
  page += F("<div class=\"actions\"><button type=\"button\" onclick=\"scanWifi()\">Scan WiFi</button><button type=\"button\" onclick=\"loadCurrentWifi()\">Load Saved</button></div>");
  page += F("<div id=\"wifiStatus\" class=\"muted\" style=\"margin-top:10px\">Ready to scan.</div>");
  page += F("<div id=\"wifiList\" style=\"margin-top:14px\"></div>");
  page += F("</section>");
  page += F("<section class=\"panel\"><form id=\"wifiForm\"><label for=\"ssid\">SSID</label><input id=\"ssid\" name=\"ssid\" maxlength=\"64\" required>");
  page += F("<label for=\"password\">Password</label><input id=\"password\" name=\"password\" type=\"password\" maxlength=\"64\">");
  page += F("<div class=\"actions\"><button type=\"submit\">Save WiFi</button><button type=\"button\" onclick=\"rebootDevice()\">Reboot</button></div>");
  page += F("</form></section>");
  page += F("<script>");
  page += F("const wifiStatus=document.getElementById('wifiStatus');const wifiList=document.getElementById('wifiList');const wifiForm=document.getElementById('wifiForm');");
  page += F("const ssidInput=document.getElementById('ssid');const passwordInput=document.getElementById('password');");
  page += F("async function scanWifi(){wifiStatus.textContent='Scanning...';wifiList.innerHTML='';const res=await fetch('/api/wifi/scan',{cache:'no-store'});const data=await res.json();if(!data.ok){wifiStatus.textContent=data.error||'Scan failed';return;}wifiStatus.textContent='Found '+data.networks.length+' network(s).';wifiList.innerHTML=data.networks.map(n=>'<div class=\"net\"><div><div class=\"ssid\">'+escapeHtml(n.ssid||'(hidden)')+'</div><div class=\"tag\">'+escapeHtml(n.security)+' · '+n.channel+' · '+n.rssi+' dBm</div></div><div><button class=\"use\" type=\"button\" onclick=\"useNetwork(\\''+escapeJs(n.ssid||'')+'\\')\">Use</button></div></div>').join('');}");
  page += F("async function loadCurrentWifi(){const res=await fetch('/api/status',{cache:'no-store'});const data=await res.json();if(data.wifi_ssid){ssidInput.value=data.wifi_ssid;wifiStatus.textContent='Loaded saved SSID.';}else{wifiStatus.textContent='No saved WiFi found.';}}");
  page += F("function useNetwork(ssid){ssidInput.value=ssid;passwordInput.focus();}");
  page += F("wifiForm.addEventListener('submit',async function(e){e.preventDefault();wifiStatus.textContent='Saving...';const body={ssid:ssidInput.value,password:passwordInput.value};const res=await fetch('/api/wifi/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const data=await res.json();wifiStatus.textContent=data.ok?'Saved. Reboot to connect.':(data.error||'Save failed');if(data.ok){setTimeout(()=>rebootDevice(),1200);}});");
  page += F("function escapeHtml(s){return String(s).replace(/[&<>\"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[m]));}");
  page += F("function escapeJs(s){return String(s).replace(/\\\\/g,'\\\\\\\\').replace(/'/g,'\\\\\\'');}");
  page += F("scanWifi();");
  page += F("</script>");
  page += buildPageFooter();
  return page;
}
