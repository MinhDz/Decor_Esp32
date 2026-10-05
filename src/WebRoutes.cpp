#include "WebRoutes.h"
#include "Config.h"
#include "HardwareManager.h"
#include "WifiManager.h"
#include "XiaoZhiClient.h"
#include "ImageManager.h"
#include "PcStatsManager.h"
#include "tests/TestDisplay.h"
#include "tests/TestSensors.h"
#include "tests/TestButtons.h"
#include "tests/TestAudio.h"
#include "tests/TestSDCard.h"
#include "OtaManager.h"

#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <Update.h>

static WebServer server(80);
static bool uploadAllowed = true;

WebServer& WebRoutes::getServer() {
  return server;
}

// ---------------- REST APIS MẠNG & HỆ THỐNG ----------------

static const char SD_CONVERTER_INJECT_SCRIPT[] PROGMEM = R"rawliteral(
<script>
(function(){
  window.pngBlobToUpload = null;
  window.rgb565BlobToUpload = null;
  window.currentConvertBaseName = "";
  let lastEspSyncedBg = "";

  window.resolveWebImgUrl = function(name) {
    if (!name) return '';
    const clean = name.replace(/^sd:/, '');
    if (clean.startsWith('/sd_images/')) return '/api/sd/file?path=' + encodeURIComponent(clean);
    return clean;
  };

  // Khi bấm chọn ảnh nền ở Page 1 (Màn hình Demo & thiết kế): Đổi ngay trên Web VÀ đồng bộ ngay xuống ESP32!
  window.selectStandbyWallpaper = async function(imageName) {
    const clean = (imageName || '').replace(/^sd:/, '');
    activeImageName = clean;
    lastEspSyncedBg = clean;
    const bgModeEl = document.getElementById('cfg-bg-mode');
    if (bgModeEl) bgModeEl.value = 'image';
    if (typeof onBgModeChange === 'function') onBgModeChange();
    window.renderWallpaperThumbnails();
    window.updateLivePreview();
    if (typeof saveStandbyConfig === 'function') {
      await saveStandbyConfig();
    } else {
      await fetch('/api/images/select?name=' + encodeURIComponent(clean), { method: 'POST' });
    }
  };

  window.setAsStandbyWallpaper = async function(name) {
    await window.selectStandbyWallpaper(name);
    if (typeof switchTab === 'function') switchTab('screen');
  };

  window.confirmAndUploadToSdCard = async function() {
    if (!window.pngBlobToUpload || !window.rgb565BlobToUpload) {
      return alert("Đang chuẩn bị dữ liệu PNG + RGB565, vui lòng thử lại sau 1 giây!");
    }
    const base = window.currentConvertBaseName || ('sd_' + Math.floor(10000 + Math.random() * 89999));
    const formData = new FormData();
    formData.append('rgb565', window.rgb565BlobToUpload, base + '.rgb565');
    formData.append('png', window.pngBlobToUpload, base + '.png');

    if (typeof showToast === 'function') showToast("💾 Đang lưu .PNG (/sd_images) + .RGB565 (/sd_rgb565) vào Thẻ nhớ SD...", false);
    try {
      const res = await fetch('/api/sd/upload', { method: 'POST', body: formData });
      const data = await res.json();
      if (res.ok) {
        if (data.active_img) {
          activeImageName = data.active_img.replace(/^sd:/, '');
          lastEspSyncedBg = activeImageName;
        }
        if (typeof showToast === 'function') showToast(data.message || "Đã lưu PNG + RGB565 vào Thẻ nhớ SD thành công!");
        if (typeof cancelCompression === 'function') cancelCompression();
        await window.loadImages();
        window.updateLivePreview();
      } else {
        if (typeof showToast === 'function') showToast(data.error || "Lỗi khi lưu vào Thẻ nhớ SD!", true);
      }
    } catch (e) {
      if (typeof showToast === 'function') showToast("Lỗi kết nối khi tải ảnh lên Thẻ nhớ SD!", true);
    }
  };

  // Nâng cấp khung Preview để thêm nút Convert JS (PNG + RGB565) & Lưu vào Thẻ SD
  const card = document.getElementById('compress-preview-card');
  if (card && !document.getElementById('btn-sd-convert-upload')) {
    const btnSd = document.createElement('button');
    btnSd.id = 'btn-sd-convert-upload';
    btnSd.className = 'btn-primary';
    btnSd.style.cssText = 'width:100%;margin-bottom:6px;background:linear-gradient(135deg,#059669,#0284c7);font-weight:700;';
    btnSd.innerText = '💾 Convert JS (PNG + 0ms RGB565) & Lưu vào Thẻ SD (/sd_images)';
    btnSd.onclick = window.confirmAndUploadToSdCard;
    card.appendChild(btnSd);
  }

  window.compressImageToPortrait = function(img, originalName) {
    const canvas = document.getElementById('compress-canvas');
    const ctx = canvas.getContext('2d');
    const sdCanvas = document.createElement('canvas');
    sdCanvas.width = 240; sdCanvas.height = 320;
    const sdCtx = sdCanvas.getContext('2d');

    const targetW = 480, targetH = 640;
    canvas.width = targetW; canvas.height = targetH;
    const imgAspect = img.width / img.height;
    const targetAspect = targetW / targetH;
    let srcX = 0, srcY = 0, srcW = img.width, srcH = img.height;
    if (imgAspect > targetAspect) {
      srcW = img.height * targetAspect; srcX = (img.width - srcW) / 2;
    } else {
      srcH = img.width / targetAspect; srcY = (img.height - srcH) / 2;
    }
    ctx.drawImage(img, srcX, srcY, srcW, srcH, 0, 0, targetW, targetH);
    sdCtx.drawImage(img, srcX, srcY, srcW, srcH, 0, 0, 240, 320);

    // Chuyển đổi từng pixel sang 16-bit RGB565 chuẩn màn hình ST7789 (240x320x2 = 153,600 bytes)
    const imgData = sdCtx.getImageData(0, 0, 240, 320).data;
    const rgb565Arr = new Uint16Array(240 * 320);
    for (let i = 0, p = 0; i < imgData.length; i += 4, p++) {
      rgb565Arr[p] = ((imgData[i] & 0xF8) << 8) | ((imgData[i+1] & 0xFC) << 3) | (imgData[i+2] >> 3);
    }
    window.rgb565BlobToUpload = new Blob([rgb565Arr.buffer], { type: 'application/octet-stream' });
    window.currentConvertBaseName = 'sd_' + Math.floor(10000 + Math.random() * 89999);

    sdCanvas.toBlob(function(pngBlob) {
      window.pngBlobToUpload = pngBlob;
      const pngKb = pngBlob ? (pngBlob.size / 1024).toFixed(0) : '95';
      const infoEl = document.getElementById('info-comp-size');
      if (infoEl) infoEl.innerText = `PNG: ${pngKb} KB + RGB565: 150 KB (0ms Decode)`;
      const previewImg = document.getElementById('compress-thumb');
      if (previewImg && pngBlob) previewImg.src = URL.createObjectURL(pngBlob);
      if (card) card.style.display = 'flex';
      if (typeof showToast === 'function') showToast(`Đã Convert JS xong: .PNG (${pngKb}KB) + .RGB565 (150KB)!`);
    }, 'image/png');

    canvas.toBlob(function(blob) {
      compressedBlobToUpload = blob;
    }, 'image/jpeg', 0.82);
  };

  window.loadImages = async function() {
    const grid = document.getElementById('image-list');
    if (!grid) return;
    try {
      const res = await fetch('/api/images');
      const data = await res.json();
      uploadedImages = Array.isArray(data) ? data : (data.images || []);
      const count = uploadedImages.length;
      const fsCount = data.count !== undefined ? data.count : count;
      const sdCount = data.sd_count || 0;
      const tc = document.getElementById('tab-img-count'); if (tc) tc.innerText = `${count} Ảnh`;
      const sb = document.getElementById('storage-badge'); if (sb) sb.innerText = `SD: ${sdCount} | Flash: ${fsCount}/15`;
      const lc = document.getElementById('list-count-label'); if (lc) lc.innerText = `${count} (SD: ${sdCount}, Flash: ${fsCount})`;
      const fi = document.getElementById('file-input'); if (fi) fi.disabled = false;

      const curActive = (activeImageName || '').replace(/^sd:/, '');
      grid.innerHTML = '';
      if (count === 0) {
        grid.innerHTML = '<p style="font-size:0.8rem; color:#94a3b8; grid-column: 1/-1;">Chưa có ảnh nào. Hãy chọn ảnh để Convert JS (PNG + RGB565) vào Thẻ nhớ SD!</p>';
        return;
      }
      uploadedImages.forEach(img => {
        const cleanName = (img.name || '').replace(/^sd:/, '');
        const isActive = (cleanName === curActive);
        const sizeKb = img.size ? (img.size / 1024).toFixed(0) : '--';
        const imgUrl = img.url || window.resolveWebImgUrl(cleanName);
        const isSd = (img.storage === 'SD' || cleanName.startsWith('/sd_images/'));
        const badgeHtml = isSd
          ? `<span style="position:absolute;top:5px;left:5px;background:rgba(5,150,105,0.92);color:#fff;font-size:0.62rem;font-weight:800;padding:2px 6px;border-radius:4px;border:1px solid #34d399;">💾 SD · ${img.has_rgb565 ? '0ms RGB565' : 'PNG'}</span>`
          : `<span style="position:absolute;top:5px;left:5px;background:rgba(30,41,59,0.88);color:#38bdf8;font-size:0.62rem;font-weight:700;padding:2px 6px;border-radius:4px;">⚡ Flash</span>`;
        const cleanLabel = cleanName.replace('/sd_images/', '').replace('/', '');
        grid.innerHTML += `
          <div class="img-card ${isActive ? 'active-img' : ''}">
            ${badgeHtml}
            <img src="${imgUrl}" alt="img" loading="lazy">
            <div class="img-info" title="${cleanName}">${cleanLabel} (${sizeKb}KB)</div>
            <div class="img-actions">
              <button class="btn-sm ${isActive ? 'btn-active-badge' : 'btn-success'}" onclick="setAsStandbyWallpaper('${cleanName}')">
                ${isActive ? '✓ Đang làm nền' : '⭐ Đặt làm nền'}
              </button>
              <button class="btn-sm btn-danger" onclick="deleteImage('${cleanName}')">🗑️ Xóa</button>
            </div>
          </div>`;
      });
      window.renderWallpaperThumbnails();
    } catch (e) {}
  };

  window.renderWallpaperThumbnails = function() {
    const box = document.getElementById('wallpaper-thumbnails');
    if (!box) return;
    box.innerHTML = '';
    const list = uploadedImages || [];
    if (list.length === 0) {
      box.innerHTML = '<p style="font-size:0.75rem; color:#94a3b8; grid-column:1/-1; padding:8px;">Chưa có ảnh nào. Hãy vào tab "Quản Lý Ảnh" để tải ảnh lên!</p>';
      return;
    }
    const curActive = (activeImageName || '').replace(/^sd:/, '');
    list.forEach(img => {
      const cleanName = (img.name || '').replace(/^sd:/, '');
      const isSelected = (cleanName === curActive);
      const thumbUrl = img.url || window.resolveWebImgUrl(cleanName);
      box.innerHTML += `
        <div class="wall-thumb ${isSelected ? 'active' : ''}" onclick="selectStandbyWallpaper('${cleanName}')" title="${cleanName}">
          <img src="${thumbUrl}" alt="wallpaper" loading="lazy">
        </div>`;
    });
  };

  const origUpdateLivePreview = window.updateLivePreview;
  window.updateLivePreview = function() {
    if (activeImageName && activeImageName.startsWith('sd:')) {
      activeImageName = activeImageName.substring(3);
    }
    if (typeof origUpdateLivePreview === 'function') origUpdateLivePreview();
    const bgModeEl = document.getElementById('cfg-bg-mode');
    if (bgModeEl && bgModeEl.value === 'image' && activeImageName) {
      const resolvedUrl = window.resolveWebImgUrl(activeImageName);
      ['screen-standby', 'chat-screen-standby'].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.style.backgroundImage = `url('${resolvedUrl}')`;
      });
    }
  };

  let lastEspConfigSig = "";
  let isWebSaving = false;

  function buildStandbySig(cfg) {
    if (!cfg) return "";
    const cleanImg = (cfg.bg_image || '').replace(/^sd:/, '');
    return (cfg.bg_mode || 'image') + '|' + cleanImg + '|' + (cfg.clock_color || '') + '|' +
           (cfg.clock_pos || '') + '|' + (cfg.clock_style || '') + '|' + (cfg.dim_overlay ?? 35);
  }

  const origSaveStandbyConfig = window.saveStandbyConfig;
  window.saveStandbyConfig = async function() {
    isWebSaving = true;
    try {
      if (typeof origSaveStandbyConfig === 'function') {
        await origSaveStandbyConfig();
      }
      const res = await fetch('/api/screen/standby', { cache: 'no-store' });
      if (res.ok) {
        const cfg = await res.json();
        lastEspConfigSig = buildStandbySig(cfg);
      }
    } catch (e) {}
    isWebSaving = false;
  };

  // Vòng lặp đồng bộ thời gian thực (mỗi 1.5 giây) từ ESP32 -> Web UI:
  // Bất cứ khi nào người dùng bấm nút đổi Ảnh nền hoặc đổi chế độ Gradient/Nền đen dưới ESP32, Web tự động cập nhật ngay!
  async function pollEspStandbyConfig() {
    if (isWebSaving) return;
    try {
      const res = await fetch('/api/screen/standby', { cache: 'no-store' });
      if (!res.ok) return;
      const cfg = await res.json();
      const sig = buildStandbySig(cfg);
      if (!lastEspConfigSig) {
        lastEspConfigSig = sig;
        return;
      }
      if (sig !== lastEspConfigSig) {
        lastEspConfigSig = sig;
        const cleanImg = (cfg.bg_image || '').replace(/^sd:/, '');
        if (cleanImg) activeImageName = cleanImg;
        lastEspSyncedBg = activeImageName || "";

        if (cfg.bg_mode) {
          const bm = document.getElementById('cfg-bg-mode');
          if (bm) bm.value = cfg.bg_mode;
        }
        if (cfg.clock_pos) {
          const cp = document.getElementById('cfg-clock-pos');
          if (cp) cp.value = cfg.clock_pos;
        }
        if (cfg.clock_style) {
          const cs = document.getElementById('cfg-clock-style');
          if (cs) cs.value = cfg.clock_style;
        }
        if (cfg.dim_overlay !== undefined) {
          const dm = document.getElementById('cfg-dim');
          if (dm) dm.value = cfg.dim_overlay;
        }
        if (cfg.clock_color && typeof pickColor === 'function') {
          pickColor(cfg.clock_color);
        }
        if (typeof onBgModeChange === 'function') onBgModeChange();
        window.renderWallpaperThumbnails();
        window.updateLivePreview();
        window.loadImages();
        if (typeof showToast === 'function') {
          showToast("🔄 Đã đồng bộ cấu hình Hình nền mới từ ESP32!");
        }
      }
    } catch (e) {}
  }

  setTimeout(async () => {
    await window.loadImages();
    if (typeof loadStandbyConfig === 'function') await loadStandbyConfig();
    if (activeImageName) activeImageName = activeImageName.replace(/^sd:/, '');
    lastEspSyncedBg = activeImageName || "";
    window.renderWallpaperThumbnails();
    window.updateLivePreview();
    await pollEspStandbyConfig();
    setInterval(pollEspStandbyConfig, 1500);
  }, 150);
})();
</script>
)rawliteral";

static const char MEDIA_PAGE_INJECT_SCRIPT[] PROGMEM = R"rawliteral(
<style>
  @media (min-width: 820px) {
    .container { max-width: 880px !important; transition: max-width 0.25s ease; }
  }
  #tab-music {
    width: 100%;
    max-width: 100%;
    overflow: hidden;
  }
  .media-grid-wrap {
    display: grid;
    grid-template-columns: 274px minmax(0, 1fr);
    gap: 16px;
    align-items: start;
    width: 100%;
    max-width: 100%;
  }
  @media (max-width: 780px) {
    .media-grid-wrap { grid-template-columns: minmax(0, 1fr); }
  }
  #tab-music .designer-panel {
    width: 100%;
    max-width: 100%;
    min-width: 0;
    box-sizing: border-box;
    overflow: hidden;
    margin-bottom: 0;
  }
  #tab-music .designer-title {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 6px;
    white-space: normal;
  }
  .esp-media-screen {
    width: 240px;
    height: 320px;
    background: #060913;
    border-radius: 8px;
    overflow: hidden;
    display: flex;
    flex-direction: column;
    font-family: 'Courier New', monospace;
    user-select: none;
    box-shadow: inset 0 0 12px rgba(0,229,255,0.15);
    border: 1px solid #1e293b;
  }
  .esp-s40-topbar {
    height: 22px;
    background: linear-gradient(90deg, #0c192c, #172554);
    border-bottom: 1px solid #00e5ff;
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0 6px;
    font-size: 9px;
    font-weight: 700;
    color: #facc15;
  }
  .esp-s40-softkeys {
    height: 22px;
    background: #090d16;
    border-top: 1px solid #00e5ff;
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0 6px;
    font-size: 9px;
    font-weight: 700;
    color: #38bdf8;
    margin-top: auto;
  }
  .esp-media-info-card {
    margin: 4px 8px 0 8px;
    background: #0b1324;
    border: 1px solid #1e3a5f;
    border-radius: 6px;
    padding: 5px 7px;
    font-size: 10px;
  }
  .esp-media-dock {
    margin: 4px 8px;
    background: #0b1324;
    border: 1px solid #1e3a5f;
    border-radius: 7px;
    padding: 6px 4px 4px 4px;
    display: flex;
    flex-direction: column;
    align-items: center;
  }
  .esp-dock-btns {
    display: flex;
    align-items: center;
    justify-content: space-around;
    width: 100%;
    gap: 3px;
  }
  .esp-dock-btn {
    width: 35px;
    height: 31px;
    background: #0f172a;
    border: 1px solid #00e5ff;
    border-radius: 5px;
    color: #f8fafc;
    font-size: 11px;
    font-weight: 800;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: all 0.15s;
  }
  .esp-dock-btn:hover { transform: scale(1.06); background: #1e293b; }
  .esp-dock-btn.play-center {
    width: 38px;
    height: 38px;
    border-radius: 50%;
    border: 2px solid #facc15;
    box-shadow: 0 0 8px rgba(244,63,94,0.6);
    background: #172554;
    color: #facc15;
    font-size: 13px;
  }
  .music-track-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    background: #0f172a;
    border: 1px solid #1e293b;
    border-radius: 8px;
    padding: 9px 12px;
    margin-bottom: 7px;
    transition: all 0.15s;
    width: 100%;
    max-width: 100%;
    min-width: 0;
    box-sizing: border-box;
  }
  .music-track-row:hover { border-color: #38bdf8; }
  .music-track-row.active {
    background: linear-gradient(90deg, rgba(6,182,212,0.16), rgba(15,23,42,0.95));
    border-color: #00e5ff;
    box-shadow: 0 0 10px rgba(0,229,255,0.2);
  }
  .vis-mode-grid {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 6px;
    width: 100%;
  }
  @media (max-width: 560px) {
    .vis-mode-grid { grid-template-columns: 1fr; }
  }
  .vis-mode-pill {
    padding: 8px 6px;
    border-radius: 7px;
    border: 1px solid #334155;
    background: #0f172a;
    color: #cbd5e1;
    font-size: 0.74rem;
    font-weight: 700;
    cursor: pointer;
    text-align: center;
    transition: all 0.15s;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    min-width: 0;
  }
  .vis-mode-pill.active {
    background: linear-gradient(135deg, #0284c7, #db2777);
    border-color: #38bdf8;
    color: #fff;
  }
  .vol-repeat-grid {
    display: grid;
    grid-template-columns: minmax(0, 1.25fr) minmax(0, 1fr);
    gap: 10px;
    align-items: stretch;
    margin-bottom: 10px;
  }
  @media (max-width: 560px) {
    .vol-repeat-grid { grid-template-columns: 1fr; }
  }
</style>
<script>
(function(){
  function injectMusicTabAndPage() {
    const navTabs = document.getElementById('nav-tabs');
    if (!navTabs || document.getElementById('tab-btn-music')) return;

    // 1. Thêm nút Tab "🎵 Đa Phương Tiện (/Musics)" vào thanh điều hướng
    const btn = document.createElement('button');
    btn.id = 'tab-btn-music';
    btn.className = 'tab-btn';
    btn.innerHTML = '🎵 Đa Phương Tiện (<span id="tab-music-count">SD</span>)';
    btn.onclick = function() { window.switchTab('music'); };
    navTabs.insertBefore(btn, navTabs.children[3] || null);

    // 2. Tạo Section Page "#tab-music"
    const sec = document.createElement('div');
    sec.id = 'tab-music';
    sec.className = 'section';
    sec.innerHTML = `
      <div class="media-grid-wrap">
        <!-- CỘT TRÁI: MÀN HÌNH DEMO ST7789 240x320 ĐỒNG BỘ THỜI GIAN THỰC VỚI ESP32 -->
        <div class="device-bezel" style="margin:0 auto;width:274px;max-width:100%;box-sizing:border-box;">
          <div class="device-header">
            <span>ESP32 MODE 12 // 240x320</span>
            <span id="web-media-sync-dot" style="color:#10b981;font-size:0.68rem;">● LIVE SYNC</span>
          </div>
          <div class="esp-media-screen">
            <div class="esp-s40-topbar">
              <span>DA PHUONG TIEN (PLAYER)</span>
              <span id="wm-sd-badge">SD OK</span>
            </div>

            <!-- Vùng 1/3 Trên: Canvas Visualizer (Đĩa quay / Sóng Sin / 16-Bar VU Meter) -->
            <div style="padding:4px 8px 0 8px;">
              <canvas id="web-media-vis-canvas" width="224" height="82" style="width:224px;height:82px;border-radius:6px;display:block;cursor:pointer;" title="Bấm để đổi kiểu hiển thị (Đĩa quay / Sóng Sin / VU Meter)" onclick="window.sendMediaCmd('vis', -1)"></canvas>
            </div>

            <!-- Vùng Thông tin bài hát & Âm lượng -->
            <div class="esp-media-info-card">
              <div style="display:flex;justify-content:space-between;color:#facc15;font-weight:700;margin-bottom:3px;">
                <span id="wm-track-num">BAI 01/06 [THE SD]</span>
                <span id="wm-vol-txt" style="color:#10b981;">VOL: 75%</span>
              </div>
              <div id="wm-track-title" style="color:#ffffff;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-bottom:3px;">Cyberpunk 2077 - Night City.mp3</div>
              <div style="display:flex;justify-content:space-between;font-size:9px;">
                <span id="wm-vis-label" style="color:#f43f5e;">Che do: Dia Quay</span>
                <span id="wm-play-state" style="color:#00e5ff;font-weight:700;">[DANG PHAT]</span>
              </div>
            </div>

            <!-- Thời gian & Thanh trượt Slidebar (Click để tua) -->
            <div style="padding:4px 12px 2px 12px;">
              <div style="display:flex;justify-content:space-between;font-size:10px;color:#00e5ff;margin-bottom:3px;">
                <span id="wm-cur-time">00:42</span>
                <span id="wm-tot-time" style="color:#94a3b8;">03:35</span>
              </div>
              <div id="wm-slidebar-box" style="height:12px;display:flex;align-items:center;cursor:pointer;" title="Bấm để tua thời gian bài hát">
                <div style="width:100%;height:4px;background:#1e293b;border-radius:2px;position:relative;">
                  <div id="wm-slidebar-fill" style="width:20%;height:100%;background:#00e5ff;border-radius:2px;"></div>
                  <div id="wm-slidebar-knob" style="width:10px;height:10px;border-radius:50%;background:#facc15;border:1.5px solid #fff;position:absolute;top:-3px;left:calc(20% - 5px);"></div>
                </div>
              </div>
            </div>

            <!-- Thanh 5 Biểu tượng chức năng điều khiển -->
            <div class="esp-media-dock">
              <div class="esp-dock-btns">
                <button class="esp-dock-btn" onclick="window.sendMediaCmd('prev')" title="Bài trước (LEFT)">|◀</button>
                <button class="esp-dock-btn play-center" id="wm-btn-play" onclick="window.sendMediaCmd('toggle')" title="Phát / Tạm dừng (OK)">⏸</button>
                <button class="esp-dock-btn" onclick="window.sendMediaCmd('next')" title="Bài tiếp (RIGHT)">▶|</button>
                <button class="esp-dock-btn" style="border-color:#10b981;color:#10b981;" onclick="window.sendMediaCmd('volume', (window.mediaState.volume + 10) % 105)" title="Tăng âm lượng (UP/DOWN)">🔊</button>
                <button class="esp-dock-btn" style="border-color:#f43f5e;color:#facc15;" onclick="window.sendMediaCmd('vis', -1)" title="Đổi kiểu trình phát (MENU)">☰</button>
              </div>
              <div style="font-size:8px;color:#64748b;margin-top:4px;">&lt;Prev &nbsp; OK:Play &nbsp; Next&gt; &nbsp; Up/Dn:Vol Menu</div>
            </div>

            <div class="esp-s40-softkeys">
              <span style="cursor:pointer;" onclick="window.sendMediaCmd('vis', -1)">[MENU:TuyChon]</span>
              <span style="cursor:pointer;color:#facc15;" onclick="window.sendMediaCmd('toggle')">[OK:Play]</span>
              <span style="cursor:pointer;" onclick="window.sendMediaCmd('open_on_esp')">[ESP32:Mo]</span>
            </div>
          </div>

          <button class="btn-primary" style="width:100%;margin-top:10px;padding:9px 10px;font-size:0.78rem;background:linear-gradient(135deg,#0284c7,#7c3aed);font-weight:700;" onclick="window.sendMediaCmd('open_on_esp')">
            📺 Mở Màn Hình Nhạc Trên ESP32
          </button>
        </div>

        <!-- CỘT PHẢI: TẢI NHẠC MP3/WAV LÊN THẺ SD (/Musics) & ĐIỀU KHIỂN ĐỒNG BỘ 2 CHIỀU -->
        <div style="display:flex;flex-direction:column;gap:14px;min-width:0;width:100%;max-width:100%;">
          <!-- 1. KHUNG TẢI 1 HOẶC NHIỀU TỆP MP3 / WAV VÀO THƯ MỤC /Musics TRÊN THẺ SD -->
          <div class="designer-panel">
            <div class="designer-title">
              <span>📤 Tải Nhạc MP3 / WAV Vào Thẻ SD (<code>/Musics</code>)</span>
              <span id="music-sd-status-badge" style="font-size:0.72rem;color:#10b981;">Thẻ SD: Sẵn sàng</span>
            </div>
            <p style="font-size:0.75rem;color:#94a3b8;margin-bottom:8px;line-height:1.35;">
              Chọn <strong>1 tệp hoặc nhiều tệp (Batch Upload)</strong> định dạng <code>.mp3</code>, <code>.wav</code> để lưu vào thư mục <strong><code>/Musics</code></strong> trên Thẻ nhớ SD của ESP32.
            </p>
            <div style="display:flex;gap:8px;flex-wrap:wrap;align-items:center;width:100%;">
              <input type="file" id="music-upload-input" accept=".mp3,.wav,.flac,.ogg,audio/*" multiple style="flex:1 1 180px;min-width:0;max-width:100%;padding:7px;font-size:0.76rem;background:#0f172a;border:1px solid #334155;border-radius:6px;color:#cbd5e1;box-sizing:border-box;">
              <button class="btn-primary" id="btn-upload-musics" style="flex:0 0 auto;background:linear-gradient(135deg,#059669,#0284c7);font-weight:700;padding:8px 14px;font-size:0.78rem;white-space:nowrap;" onclick="window.uploadSelectedMusicFiles()">
                💾 Tải Lên /Musics
              </button>
            </div>
            <div id="music-upload-progress-wrap" style="display:none;margin-top:8px;">
              <div style="display:flex;justify-content:space-between;font-size:0.74rem;color:#38bdf8;margin-bottom:4px;gap:8px;">
                <span id="music-upload-status-txt" style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-width:0;flex:1;">Đang tải lên /Musics...</span>
                <strong id="music-upload-pct-txt" style="flex-shrink:0;">0%</strong>
              </div>
              <div class="storage-progress">
                <div id="music-upload-bar" class="storage-bar" style="width:0%;transition:width 0.2s;"></div>
              </div>
            </div>
          </div>

          <!-- 2. BẢNG ĐIỀU KHIỂN TRÌNH PHÁT, CHẾ ĐỘ VISUALIZER & NGUỒN ÂM THANH VU METER -->
          <div class="designer-panel">
            <div class="designer-title">
              <span>🎛️ Điều Khiển & Hiệu Ứng Sóng Âm (Web ⇄ ESP32)</span>
            </div>

            <div style="margin-bottom:10px;">
              <div style="font-size:0.73rem;color:#94a3b8;margin-bottom:5px;">Kiểu hiển thị ở 1/3 màn hình trên (Web hoặc phím MENU dưới ESP32):</div>
              <div class="vis-mode-grid">
                <button class="vis-mode-pill active" id="vis-pill-0" onclick="window.sendMediaCmd('vis', 0)" title="Chế Độ Đĩa Quay (Vinyl)">💿 1. Đĩa Quay</button>
                <button class="vis-mode-pill" id="vis-pill-1" onclick="window.sendMediaCmd('vis', 1)" title="Dạng Sóng Hình Sin (Oscilloscope)">〰️ 2. Sóng Sin</button>
                <button class="vis-mode-pill" id="vis-pill-2" onclick="window.sendMediaCmd('vis', 2)" title="VU Meter 16 Thanh (Audio + Mic)">📊 3. VU Meter</button>
              </div>
            </div>

            <div class="vol-repeat-grid">
              <div style="background:#0f172a;padding:8px 10px;border-radius:8px;border:1px solid #1e293b;min-width:0;">
                <div style="display:flex;justify-content:space-between;font-size:0.73rem;margin-bottom:4px;">
                  <span style="color:#94a3b8;">🔊 Âm lượng (UP/DOWN):</span>
                  <strong id="web-media-vol-val" style="color:#10b981;">75%</strong>
                </div>
                <input type="range" id="web-media-vol-slider" min="0" max="100" step="5" value="75" style="width:100%;display:block;" onchange="window.sendMediaCmd('volume', parseInt(this.value))" oninput="document.getElementById('web-media-vol-val').innerText=this.value+'%'">
              </div>

              <div style="background:#0f172a;padding:8px 10px;border-radius:8px;border:1px solid #1e293b;display:flex;justify-content:space-between;align-items:center;gap:8px;min-width:0;">
                <div style="min-width:0;">
                  <div style="font-size:0.7rem;color:#94a3b8;">Phát lặp:</div>
                  <strong id="web-media-repeat-txt" style="font-size:0.76rem;color:#38bdf8;white-space:nowrap;">🔁 Lặp Tất Cả</strong>
                </div>
                <button class="btn-secondary" style="width:auto;padding:6px 12px;font-size:0.74rem;flex-shrink:0;white-space:nowrap;" onclick="window.sendMediaCmd('repeat', -1)">Đổi</button>
              </div>
            </div>

            <!-- Trình phát Audio Web đồng bộ phổ FFT thực xuống VU Meter của ESP32 -->
            <div style="background:#0b1120;border:1px dashed #334155;border-radius:8px;padding:8px 10px;display:flex;flex-direction:column;gap:6px;">
              <div style="font-size:0.72rem;color:#cbd5e1;line-height:1.35;">
                🎵 <strong>Trích xuất VU Meter từ nhạc & Mic INMP441:</strong> Giải mã FFT trực tiếp khi phát nhạc từ <code>/Musics</code> kết hợp cảm biến <strong>Mic INMP441 (GPIO 6)</strong>.
              </div>
              <audio id="web-sd-audio-player" controls style="height:30px;width:100%;max-width:100%;"></audio>
            </div>
          </div>
        </div>
      </div>

      <!-- 3. CHỌN NHANH 6 PHONG CÁCH GIAO DIỆN SYMBIAN S40 (ĐỒNG BỘ CÀI ĐẶT ESP32 <-> WEB) -->
      <div class="designer-panel" style="margin-top:16px;width:100%;max-width:100%;">
        <div class="designer-title">
          <span>🎨 6 Phong Cách Giao Diện Hệ Thống Symbian S40 (Đồng Bộ Mục Cài Đặt ESP32)</span>
          <span id="web-s40-theme-badge" style="font-size:0.74rem;color:#38bdf8;font-weight:700;">SPACE OS</span>
        </div>
        <div style="font-size:0.74rem;color:#94a3b8;margin-bottom:8px;">
          Đổi trực tiếp tại <strong>Menu &rarr; Cài đặt &rarr; 1. Giao diện (6 Theme S40)</strong> dưới mạch hoặc bấm chọn nhanh bên dưới để đổi toàn bộ màu nền, kiểu bo khung (Bo mềm / Giáp vát góc đinh tán Mecha / Cổ điển Nokia 6300 / Mèo Gome Chibi) và bảng màu biểu tượng:
        </div>
        <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:8px;width:100%;">
          <button class="vis-mode-pill active" id="s40-theme-btn-0" onclick="window.sendMediaCmd('theme', 0)">🚀 1. Trạm Vũ Trụ</button>
          <button class="vis-mode-pill" id="s40-theme-btn-1" onclick="window.sendMediaCmd('theme', 1)">🌿 2. Chill Thảo Mộc</button>
          <button class="vis-mode-pill" id="s40-theme-btn-2" onclick="window.sendMediaCmd('theme', 2)">⚙️ 3. Mecha Mạnh Mẽ</button>
          <button class="vis-mode-pill" id="s40-theme-btn-3" onclick="window.sendMediaCmd('theme', 3)">🌇 4. Hoàng Hôn Lofi</button>
          <button class="vis-mode-pill" id="s40-theme-btn-4" onclick="window.sendMediaCmd('theme', 4)">📱 5. Nokia S40 Cổ Điển</button>
          <button class="vis-mode-pill" id="s40-theme-btn-5" onclick="window.sendMediaCmd('theme', 5)">🐱 6. Mèo Gome Chibi</button>
        </div>
      </div>

      <!-- 4. DANH SÁCH BÀI HÁT TRONG /Musics (THẺ NHỚ SD) - RỘNG TOÀN KHUNG (FULL WIDTH) -->
      <div class="designer-panel" style="margin-top:16px;width:100%;max-width:100%;">
        <div class="designer-title">
          <span>📂 Danh Sách Bài Hát Trong <code>/Musics</code> (Thẻ Nhớ SD) & Playlist</span>
          <button class="btn-secondary" style="width:auto;padding:5px 12px;font-size:0.74rem;flex-shrink:0;white-space:nowrap;" onclick="window.loadMusicPlaylist(true)">🔄 Quét Lại Thẻ SD</button>
        </div>
        <div id="music-playlist-container" style="max-height:360px;overflow-y:auto;overflow-x:hidden;padding-right:4px;width:100%;">
          <div style="color:#64748b;font-size:0.8rem;padding:12px;">Đang tải danh sách bài hát từ thư mục /Musics...</div>
        </div>
      </div>
    `;

    const wifiSec = document.getElementById('tab-wifi');
    if (wifiSec && wifiSec.parentNode) {
      wifiSec.parentNode.insertBefore(sec, wifiSec);
    } else {
      document.body.appendChild(sec);
    }

    // Gắn sự kiện click tua trên Slidebar của màn hình Demo
    const sbBox = document.getElementById('wm-slidebar-box');
    if (sbBox) {
      sbBox.addEventListener('click', function(e) {
        const rect = sbBox.getBoundingClientRect();
        const ratio = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
        const targetSec = Math.round(ratio * (window.mediaState.total_sec || 180));
        window.sendMediaCmd('seek', targetSec);
      });
    }
  }

  function injectSystemTabAndPage() {
    const navTabs = document.getElementById('nav-tabs');
    if (!navTabs || document.getElementById('tab-btn-system')) return;

    // Nút Tab "💻 Hệ Thống & OTA"
    const btn = document.createElement('button');
    btn.id = 'tab-btn-system';
    btn.className = 'tab-btn';
    btn.innerHTML = '💻 Hệ Thống & OTA';
    btn.onclick = function() { window.switchTab('system'); };
    navTabs.appendChild(btn);

    // Section Page "#tab-system"
    const sec = document.createElement('div');
    sec.id = 'tab-system';
    sec.className = 'section';
    sec.innerHTML = `
      <!-- 1. THÔNG SỐ KỸ THUẬT HỆ THỐNG -->
      <div class="designer-panel" style="width:100%;max-width:100%;">
        <div class="designer-title">
          <span>💻 Thông Tin Phần Cứng & Phần Mềm Thiết Bị</span>
          <button class="btn-secondary" style="width:auto;padding:5px 12px;font-size:0.74rem;" onclick="window.loadSystemInfo()">🔄 Làm Mới</button>
        </div>
        <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-top:10px;">
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Hệ Điều Hành & Phiên Bản</div>
            <div id="sys-os-name" style="font-size:1.15rem;font-weight:700;color:#38bdf8;margin-top:4px;">Space OS</div>
            <div id="sys-fw-ver" style="font-size:0.8rem;color:#e2e8f0;margin-top:2px;">Phiên bản: v3.1.0</div>
            <div id="sys-build-date" style="font-size:0.7rem;color:#64748b;margin-top:2px;"></div>
          </div>
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Phân Vùng Đang Chạy</div>
            <div id="sys-running-part" style="font-size:1.1rem;font-weight:700;color:#10b981;margin-top:4px;">Đang tải...</div>
            <div id="sys-next-part" style="font-size:0.7rem;color:#64748b;margin-top:2px;"></div>
          </div>
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Vi Điều Khiển & Phần Cứng</div>
            <div id="sys-chip" style="font-size:1.0rem;font-weight:700;color:#e2e8f0;margin-top:4px;">ESP32-S3</div>
            <div id="sys-flash" style="font-size:0.7rem;color:#64748b;margin-top:2px;">Flash: 16MB</div>
          </div>
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Bộ Nhớ RAM & PSRAM</div>
            <div id="sys-heap" style="font-size:0.95rem;font-weight:600;color:#f59e0b;margin-top:4px;">Heap: --</div>
            <div id="sys-psram" style="font-size:0.8rem;color:#a855f7;margin-top:2px;">PSRAM: --</div>
          </div>
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Mạng Wi-Fi & Địa Chỉ IP</div>
            <div id="sys-wifi" style="font-size:0.95rem;font-weight:600;color:#38bdf8;margin-top:4px;">--</div>
            <div id="sys-ip" style="font-size:0.8rem;color:#64748b;margin-top:2px;">IP: --</div>
          </div>
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Thời Gian Hoạt Động (Uptime)</div>
            <div id="sys-uptime" style="font-size:1.1rem;font-weight:700;color:#ec4899;margin-top:4px;">--</div>
          </div>
        </div>
      </div>

      <!-- 2. CẬP NHẬT TỪ XA QUA INTERNET (CLOUD ONE-CLICK OTA) -->
      <div class="designer-panel" style="margin-top:16px;width:100%;max-width:100%;">
        <div class="designer-title">
          <span>☁️ Cập Nhật Từ Xa Qua Internet (Cloud One-Click OTA)</span>
          <span style="font-size:0.74rem;color:#10b981;font-weight:700;">DUAL BOOT SAFE</span>
        </div>
        <div style="font-size:0.78rem;color:#94a3b8;margin-bottom:12px;line-height:1.5;">
          Hệ thống hỗ trợ kiểm tra phiên bản mới từ máy chủ từ xa và tự động tải nạp qua Wi-Fi. Có cơ chế <strong>Rollback tự động chống treo mạch</strong> an toàn tuyệt đối.
        </div>
        <div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px;">
          <input type="text" id="ota-manifest-url" style="flex:1;min-width:260px;background:#0f172a;color:#e2e8f0;border:1px solid #334155;padding:8px 12px;border-radius:6px;font-size:0.82rem;" placeholder="Đường dẫn version.json trên Cloud" value="https://raw.githubusercontent.com/MinhDz/Decor_Esp32/main/release/version.json" />
          <button class="btn-primary" style="width:auto;padding:8px 16px;font-size:0.82rem;white-space:nowrap;" onclick="window.checkCloudUpdate()">🔍 Kiểm Tra Cập Nhật</button>
        </div>
        <div id="ota-check-result" style="display:none;background:#0f172a;padding:14px;border-radius:8px;border:1px solid #334155;"></div>
      </div>

      <!-- 3. CẬP NHẬT THỦ CÔNG TỪ MÁY TÍNH (LOCAL WEB OTA) -->
      <div class="designer-panel" style="margin-top:16px;width:100%;max-width:100%;">
        <div class="designer-title">
          <span>📤 Cập Nhật Thủ Công Từ Máy Tính / Điện Thoại (Local Web OTA)</span>
        </div>
        <div style="font-size:0.78rem;color:#94a3b8;margin-bottom:12px;">
          Dành cho nạp thử nghiệm hoặc khi không có kết nối Internet: Chọn tệp <code>firmware.bin</code> đã biên dịch từ máy tính để nạp trực tiếp qua trình duyệt Web.
        </div>
        <div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap;">
          <input type="file" id="manual-ota-file" accept=".bin" style="flex:1;min-width:240px;color:#94a3b8;font-size:0.82rem;" />
          <button class="btn-secondary" style="width:auto;padding:8px 16px;font-size:0.82rem;white-space:nowrap;" onclick="window.uploadManualOta()">⬆️ Nạp Firmware Ngay</button>
        </div>
        <div id="manual-ota-progress-box" style="display:none;margin-top:12px;">
          <div style="display:flex;justify-content:space-between;font-size:0.75rem;color:#94a3b8;margin-bottom:4px;">
            <span id="manual-ota-status">Đang nạp firmware...</span>
            <span id="manual-ota-pct">0%</span>
          </div>
          <div style="width:100%;height:8px;background:#1e293b;border-radius:4px;overflow:hidden;">
            <div id="manual-ota-bar" style="width:0%;height:100%;background:#38bdf8;transition:width 0.2s;"></div>
          </div>
        </div>
      </div>
    `;

    const wifiSec = document.getElementById('tab-wifi');
    if (wifiSec && wifiSec.parentNode) {
      wifiSec.parentNode.appendChild(sec);
    } else {
      document.body.appendChild(sec);
    }
  }

  // Mở rộng hàm switchTab để hỗ trợ cả tab 'music' và 'system'
  const origSwitchTab = window.switchTab;
  window.switchTab = function(tabId) {
    injectMusicTabAndPage();
    injectSystemTabAndPage();
    if (tabId === 'music') {
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      const mSec = document.getElementById('tab-music');
      const mBtn = document.getElementById('tab-btn-music');
      if (mSec) mSec.classList.add('active');
      if (mBtn) mBtn.classList.add('active');
      window.loadMusicPlaylist();
      window.pollMediaStatus();
      return;
    }
    if (tabId === 'system') {
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      const sSec = document.getElementById('tab-system');
      const sBtn = document.getElementById('tab-btn-system');
      if (sSec) sSec.classList.add('active');
      if (sBtn) sBtn.classList.add('active');
      window.loadSystemInfo();
      return;
    }
    const mSec = document.getElementById('tab-music');
    const mBtn = document.getElementById('tab-btn-music');
    if (mSec) mSec.classList.remove('active');
    if (mBtn) mBtn.classList.remove('active');
    const sSec = document.getElementById('tab-system');
    const sBtn = document.getElementById('tab-btn-system');
    if (sSec) sSec.classList.remove('active');
    if (sBtn) sBtn.classList.remove('active');
    if (typeof origSwitchTab === 'function') origSwitchTab(tabId);
  };

  // Trạng thái trình phát nhạc đồng bộ giữa ESP32 và Web
  window.mediaState = {
    playing: true,
    track_idx: 0,
    track_name: "Cyberpunk 2077 - Night City.mp3",
    track_count: 6,
    cur_sec: 0,
    total_sec: 215,
    volume: 75,
    vis_mode: 0,
    repeat_mode: 0,
    from_sd: false,
    mic_level: 0,
    spectrum: [15,25,35,45,30,50,60,40,35,55,45,30,25,40,30,20]
  };
  window.mediaPlaylist = [];
  let discAngleWeb = 0;
  let wavePhaseWeb = 0;
  let vuPeaksWeb = new Array(16).fill(0);

  // Web Audio API FFT Analyser cho trình phát <audio> trên Web
  let audioCtx = null, analyserNode = null, audioSrcNode = null, fftDataArray = null;
  let lastFftPushMs = 0;

  function setupWebAudioAnalyser() {
    const audioEl = document.getElementById('web-sd-audio-player');
    if (!audioEl || audioCtx) return;
    try {
      const AudioContextClass = window.AudioContext || window.webkitAudioContext;
      audioCtx = new AudioContextClass();
      analyserNode = audioCtx.createAnalyser();
      analyserNode.fftSize = 64;
      fftDataArray = new Uint8Array(analyserNode.frequencyBinCount);
      audioSrcNode = audioCtx.createMediaElementSource(audioEl);
      audioSrcNode.connect(analyserNode);
      analyserNode.connect(audioCtx.destination);
    } catch (e) {
      console.warn("Web Audio Analyser init:", e);
    }
  }

  function fmtMMSS(sec) {
    sec = Math.max(0, Math.floor(sec || 0));
    const m = String(Math.floor(sec / 60)).padStart(2, '0');
    const s = String(sec % 60).padStart(2, '0');
    return m + ':' + s;
  }

  function updateMediaDomFromState() {
    const st = window.mediaState;
    const numEl = document.getElementById('wm-track-num');
    if (numEl) numEl.innerText = `BAI ${String(st.track_idx + 1).padStart(2,'0')}/${String(st.track_count || 1).padStart(2,'0')} [${st.from_sd ? 'THE SD' : 'DEMO'}]`;

    const volEl = document.getElementById('wm-vol-txt');
    if (volEl) volEl.innerText = `VOL:${String(st.volume).padStart(3,' ')}%`;

    const titleEl = document.getElementById('wm-track-title');
    if (titleEl) titleEl.innerText = st.track_name || 'Chua co bai hat';

    const visNames = ['Che do: Dia Quay', 'Che do: Song Sin', 'Che do: VU Thanh'];
    const visEl = document.getElementById('wm-vis-label');
    if (visEl) visEl.innerText = visNames[(st.vis_mode || 0) % 3];

    const playEl = document.getElementById('wm-play-state');
    if (playEl) {
      playEl.innerText = st.playing ? '[DANG PHAT]' : '[TAM DUNG]';
      playEl.style.color = st.playing ? '#00e5ff' : '#f59e0b';
    }

    const btnPlay = document.getElementById('wm-btn-play');
    if (btnPlay) btnPlay.innerText = st.playing ? '⏸' : '▶';

    const curEl = document.getElementById('wm-cur-time');
    const totEl = document.getElementById('wm-tot-time');
    if (curEl) curEl.innerText = fmtMMSS(st.cur_sec);
    if (totEl) totEl.innerText = fmtMMSS(st.total_sec);

    const pct = st.total_sec > 0 ? Math.min(100, (st.cur_sec * 100) / st.total_sec) : 0;
    const fillEl = document.getElementById('wm-slidebar-fill');
    const knobEl = document.getElementById('wm-slidebar-knob');
    if (fillEl) fillEl.style.width = pct + '%';
    if (knobEl) knobEl.style.left = `calc(${pct}% - 5px)`;

    for (let i = 0; i < 3; i++) {
      const p = document.getElementById('vis-pill-' + i);
      if (p) p.classList.toggle('active', (st.vis_mode % 3) === i);
    }

    const vSlider = document.getElementById('web-media-vol-slider');
    const vVal = document.getElementById('web-media-vol-val');
    if (vSlider && document.activeElement !== vSlider) vSlider.value = st.volume;
    if (vVal) vVal.innerText = st.volume + '%';

    const repNames = ['🔁 Lặp Tất Cả', '🔂 Lặp 1 Bài', '🔀 Ngẫu Nhiên'];
    const repEl = document.getElementById('web-media-repeat-txt');
    if (repEl) repEl.innerText = repNames[(st.repeat_mode || 0) % 3];

    const thIdx = ((st.ui_theme || 0) % 6 + 6) % 6;
    const thNames = ['SPACE OS (Trạm Vũ Trụ)', 'CHILL ZEN (Thảo Mộc)', 'MECHA TAC (Mạnh Mẽ)', 'LOFI DUSK (Hoàng Hôn)', 'NOKIA S40 (Cổ Điển)', 'GOME CHIBI (Mèo Anime Kawaii)'];
    const thColors = ['#38bdf8', '#4ade80', '#fb923c', '#f43f5e', '#a855f7', '#f472b6'];
    const thBadge = document.getElementById('web-s40-theme-badge');
    if (thBadge) {
      thBadge.innerText = thNames[thIdx];
      thBadge.style.color = thColors[thIdx];
    }
    for (let t = 0; t < 6; t++) {
      const tb = document.getElementById('s40-theme-btn-' + t);
      if (tb) tb.classList.toggle('active', t === thIdx);
    }

    // Đánh dấu bài đang phát trong danh sách
    document.querySelectorAll('.music-track-row').forEach((row, idx) => {
      row.classList.toggle('active', idx === st.track_idx);
    });
  }

  // Vẽ Canvas 224x82 trên Web giống 100% với màn hình ST7789 trên ESP32
  function renderWebMediaVisualizerFrame() {
    const canvas = document.getElementById('web-media-vis-canvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const vw = canvas.width, vh = canvas.height;
    const st = window.mediaState;

    const audioEl = document.getElementById('web-sd-audio-player');
    // Đồng bộ: Nếu trạng thái đang PAUSE / STOP thì tự động tạm dừng luôn trình phát <audio> trên Web
    if (!st.playing && audioEl && !audioEl.paused) {
      audioEl.pause();
    }

    // Nếu đang PLAY và trình phát <audio> trên Web đang chạy -> lấy phổ FFT thực tế và gửi xuống ESP32 mỗi 250ms
    let webAudioActive = false;
    if (st.playing && audioEl && !audioEl.paused && analyserNode && fftDataArray) {
      analyserNode.getByteFrequencyData(fftDataArray);
      const bands16 = [];
      let maxB = 0;
      for (let i = 0; i < 16; i++) {
        const v = Math.min(100, Math.round((fftDataArray[i + 1] || 0) * 100 / 235));
        bands16.push(v);
        if (v > maxB) maxB = v;
      }
      if (maxB > 2) {
        webAudioActive = true;
        st.spectrum = bands16;
        st.mic_level = maxB;
        const nowMs = Date.now();
        if (nowMs - lastFftPushMs > 260) {
          lastFftPushMs = nowMs;
          fetch('/api/media/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ spectrum: bands16, mic_level: maxB })
          }).catch(() => {});
        }
      }
    }

    if (st.playing) {
      discAngleWeb = (discAngleWeb + 0.12) % (Math.PI * 2);
      wavePhaseWeb += 0.24;
    }

    ctx.fillStyle = '#081020';
    ctx.fillRect(0, 0, vw, vh);
    ctx.strokeStyle = '#00e5ff';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(1, 1, vw - 2, vh - 2);

    const effMic = st.playing ? (st.mic_level || 0) : 0;
    const ampScale = st.playing ? (0.45 + (st.volume / 160) + (effMic / 140)) : 0;

    if ((st.vis_mode % 3) === 0) {
      // CHẾ ĐỘ 0: ĐĨA QUAY VINYL
      const dcx = 48, dcy = 41, dr = 33;
      ctx.beginPath(); ctx.arc(dcx, dcy, dr, 0, Math.PI * 2);
      ctx.fillStyle = '#000'; ctx.fill();
      ctx.strokeStyle = '#f43f5e'; ctx.lineWidth = 1.5; ctx.stroke();

      [dr - 6, dr - 12, dr - 18].forEach(r => {
        ctx.beginPath(); ctx.arc(dcx, dcy, r, 0, Math.PI * 2);
        ctx.strokeStyle = '#1e293b'; ctx.lineWidth = 1; ctx.stroke();
      });

      for (let k = 0; k < 2; k++) {
        const a1 = discAngleWeb + k * Math.PI;
        const a2 = a1 + 0.35;
        ctx.beginPath();
        ctx.moveTo(dcx, dcy);
        ctx.arc(dcx, dcy, dr - 2, a1, a2);
        ctx.closePath();
        ctx.fillStyle = 'rgba(0, 229, 255, 0.25)';
        ctx.fill();
      }

      ctx.beginPath(); ctx.arc(dcx, dcy, 11, 0, Math.PI * 2);
      ctx.fillStyle = '#f43f5e'; ctx.fill();
      ctx.strokeStyle = '#facc15'; ctx.stroke();

      const dotX = dcx + Math.cos(discAngleWeb) * 6;
      const dotY = dcy + Math.sin(discAngleWeb) * 6;
      ctx.beginPath(); ctx.arc(dotX, dotY, 2, 0, Math.PI * 2);
      ctx.fillStyle = '#facc15'; ctx.fill();

      // Cần kim đĩa
      ctx.beginPath();
      ctx.moveTo(88, 12);
      ctx.lineTo(st.playing ? 74 : 88, st.playing ? 48 : 54);
      ctx.strokeStyle = st.playing ? '#fff' : '#64748b';
      ctx.lineWidth = 2; ctx.stroke();

      ctx.fillStyle = '#facc15';
      ctx.font = 'bold 10px monospace';
      ctx.fillText('VINYL TURNTABLE', 100, 17);
      ctx.fillStyle = st.playing ? '#00e5ff' : '#64748b';
      ctx.fillText(`STATUS: ${st.playing ? 'PLAYING' : 'PAUSED '}`, 100, 30);

      for (let b = 0; b < 8; b++) {
        let bh = 3;
        if (st.playing) {
          const specVal = (st.spectrum && st.spectrum[b * 2]) ? (st.spectrum[b * 2] * 34 / 100) : 0;
          const simVal = 6 + Math.abs(Math.sin(wavePhaseWeb + b * 0.7)) * 24 * ampScale;
          bh = Math.min(34, Math.max(3, (effMic >= 4 || webAudioActive) ? Math.max(specVal, simVal * 0.4) : simVal));
        }
        ctx.fillStyle = !st.playing ? '#475569' : ((b % 2 === 0) ? '#10b981' : '#f43f5e');
        ctx.fillRect(100 + b * 14, 72 - bh, 9, bh);
      }
    } else if ((st.vis_mode % 3) === 1) {
      // CHẾ ĐỘ 1: SÓNG HÌNH SIN (PHẲNG TĨNH KHI PAUSE/STOP)
      ctx.fillStyle = '#facc15';
      ctx.font = 'bold 10px monospace';
      ctx.fillText(`SINE WAVE [${st.playing ? 'LIVE AUDIO' : 'PAUSED'}]`, 10, 14);
      const midY = vh / 2 + 5;

      ctx.beginPath();
      for (let x = 8; x < vw - 8; x += 2) {
        const t = x * 0.065;
        const s2 = st.playing ? Math.sin(t * 1.35 - wavePhaseWeb * 1.2) : 0;
        const y2 = Math.max(18, Math.min(vh - 6, midY + s2 * 18 * ampScale));
        if (x === 8) ctx.moveTo(x, y2); else ctx.lineTo(x, y2);
      }
      ctx.strokeStyle = st.playing ? '#f43f5e' : '#475569'; ctx.lineWidth = 1.5; ctx.stroke();

      ctx.beginPath();
      for (let x = 8; x < vw - 8; x += 2) {
        const t = x * 0.065;
        const s1 = st.playing ? (Math.sin(t + wavePhaseWeb) * Math.cos(t * 0.35 - wavePhaseWeb * 0.5)) : 0;
        const y1 = Math.max(18, Math.min(vh - 6, midY + s1 * 24 * ampScale));
        if (x === 8) ctx.moveTo(x, y1); else ctx.lineTo(x, y1);
      }
      ctx.strokeStyle = '#00e5ff'; ctx.lineWidth = 2; ctx.stroke();
    } else {
      // CHẾ ĐỘ 2: VU METER 16 THANH (HẠ VỀ 0 KHI PAUSE/STOP)
      ctx.fillStyle = '#facc15';
      ctx.font = 'bold 10px monospace';
      ctx.fillText(`VU METER 16-BAR [${st.playing ? 'PLAYING' : 'PAUSED'}]`, 10, 14);

      const barW = 10, gap = 3, startX = 10, bottomY = vh - 7, maxH = 52;
      for (let i = 0; i < 16; i++) {
        let targetH = 2;
        if (st.playing) {
          const realH = ((st.spectrum && st.spectrum[i]) ? st.spectrum[i] : 0) * maxH / 100;
          const wave = Math.abs(Math.sin(wavePhaseWeb * 1.1 + i * 0.45) * Math.cos(wavePhaseWeb * 0.6 - i * 0.2));
          const simH = wave * ((effMic >= 4 || webAudioActive) ? 12 : 36) * ampScale;
          targetH = Math.min(maxH, Math.max(2, Math.max(realH, simH)));
        }

        if (targetH >= vuPeaksWeb[i]) vuPeaksWeb[i] = targetH;
        else if (vuPeaksWeb[i] > 2) vuPeaksWeb[i] = Math.max(2, vuPeaksWeb[i] - 2.2);

        const bx = startX + i * (barW + gap);
        for (let h = 0; h < targetH; h += 4) {
          ctx.fillStyle = !st.playing ? '#475569' : ((h < 26) ? '#10b981' : ((h < 40) ? '#facc15' : '#f43f5e'));
          ctx.fillRect(bx, bottomY - h - 3, barW, 3);
        }
        ctx.fillStyle = st.playing ? '#ffffff' : '#64748b';
        ctx.fillRect(bx, bottomY - vuPeaksWeb[i] - 2, barW, 1.5);
      }
    }
  }

  // Gửi lệnh điều khiển từ Web xuống ESP32
  window.sendMediaCmd = async function(action, value = 0, name = '') {
    try {
      const res = await fetch('/api/media/control', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action, value, name })
      });
      if (res.ok) {
        const st = await res.json();
        Object.assign(window.mediaState, st);
        const audioEl = document.getElementById('web-sd-audio-player');
        if (audioEl && audioEl.src) {
          if (st.playing && audioEl.paused) audioEl.play().catch(() => {});
          else if (!st.playing && !audioEl.paused) audioEl.pause();
        }
        updateMediaDomFromState();
      }
    } catch (e) {}
  };

  // Phát 1 bài hát từ danh sách (Đồng bộ ESP32 + Phát trên trình duyệt nếu là file từ /Musics SD)
  window.playTrackFromList = async function(index, name, fromSd) {
    await window.sendMediaCmd('select', index, name);
    const audioEl = document.getElementById('web-sd-audio-player');
    if (audioEl && fromSd) {
      setupWebAudioAnalyser();
      if (audioCtx && audioCtx.state === 'suspended') audioCtx.resume();
      audioEl.src = '/api/media/stream?name=' + encodeURIComponent(name);
      audioEl.play().catch(() => {});
    } else if (audioEl) {
      audioEl.pause();
    }
  };

  // Xóa file nhạc trong thư mục /Musics trên Thẻ nhớ SD
  window.deleteMusicFileOnSd = async function(name) {
    if (!confirm(`Bạn có chắc muốn xóa bài hát "${name}" khỏi thư mục /Musics trên Thẻ nhớ SD không?`)) return;
    try {
      const res = await fetch('/api/media/delete?name=' + encodeURIComponent(name), { method: 'POST' });
      const data = await res.json();
      if (typeof showToast === 'function') showToast(data.message || "Đã xóa bài hát khỏi /Musics!");
      await window.loadMusicPlaylist(true);
    } catch (e) {
      if (typeof showToast === 'function') showToast("Lỗi khi xóa file nhạc!", true);
    }
  };

  // Tải danh sách bài hát trong /Musics
  window.loadMusicPlaylist = async function(forceRefresh = false) {
    if (forceRefresh) {
      await window.sendMediaCmd('refresh', 0);
    }
    try {
      const res = await fetch('/api/media/list', { cache: 'no-store' });
      if (!res.ok) return;
      const data = await res.json();
      window.mediaPlaylist = data.tracks || [];

      const countBadge = document.getElementById('tab-music-count');
      const sdSongs = window.mediaPlaylist.filter(t => t.from_sd).length;
      if (countBadge) countBadge.innerText = sdSongs > 0 ? `${sdSongs} SD` : `${window.mediaPlaylist.length}`;

      const sdStatus = document.getElementById('music-sd-status-badge');
      const wmSd = document.getElementById('wm-sd-badge');
      if (sdStatus) {
        sdStatus.innerText = data.sd_mounted
          ? `Thẻ SD (/Musics): ${sdSongs} bài (${data.sd_used_mb || 0}/${data.sd_total_mb || 0} MB)`
          : `Chưa nhận Thẻ SD (Chế độ Demo)`;
        sdStatus.style.color = data.sd_mounted ? '#10b981' : '#f59e0b';
      }
      if (wmSd) wmSd.innerText = data.sd_mounted ? 'SD /Musics' : 'DEMO';

      const container = document.getElementById('music-playlist-container');
      if (!container) return;
      if (window.mediaPlaylist.length === 0) {
        container.innerHTML = '<div style="color:#64748b;padding:12px;">Chưa có bài hát nào trong /Musics. Hãy chọn file .mp3 / .wav ở trên để tải lên nhé!</div>';
        return;
      }

      container.innerHTML = window.mediaPlaylist.map((t, idx) => {
        const isCur = (idx === window.mediaState.track_idx);
        const sizeStr = t.size_kb >= 1024 ? (t.size_kb / 1024).toFixed(2) + ' MB' : t.size_kb + ' KB';
        const safeName = (t.name || '').replace(/'/g, "\\'");
        return `
          <div class="music-track-row ${isCur ? 'active' : ''}">
            <div style="display:flex;align-items:center;gap:10px;min-width:0;flex:1;">
              <div style="width:28px;height:28px;border-radius:6px;background:${t.from_sd ? '#065f46' : '#1e293b'};color:${t.from_sd ? '#34d399' : '#38bdf8'};display:flex;align-items:center;justify-content:center;font-weight:800;font-size:0.76rem;flex-shrink:0;">
                ${String(idx + 1).padStart(2, '0')}
              </div>
              <div style="min-width:0;flex:1;">
                <div style="color:#f8fafc;font-weight:700;font-size:0.84rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;">${t.name}</div>
                <div style="font-size:0.72rem;color:#94a3b8;display:flex;gap:10px;margin-top:2px;">
                  <span style="color:${t.from_sd ? '#10b981' : '#f43f5e'};font-weight:700;">[${t.from_sd ? 'THẺ SD /Musics' : 'BÀI MẪU DEMO'}]</span>
                  <span>⏱️ ${fmtMMSS(t.duration)}</span>
                  <span>💾 ${sizeStr}</span>
                </div>
              </div>
            </div>
            <div style="display:flex;gap:6px;flex-shrink:0;align-items:center;">
              <button class="btn-primary" style="width:auto;white-space:nowrap;padding:6px 12px;font-size:0.76rem;background:${isCur ? '#0284c7' : '#334155'};" onclick="window.playTrackFromList(${idx}, '${safeName}', ${t.from_sd})">
                ${isCur && window.mediaState.playing ? '🔊 Đang Phát' : '▶ Phát'}
              </button>
              ${t.from_sd ? `<button class="btn-secondary" style="width:auto;white-space:nowrap;padding:6px 10px;font-size:0.76rem;background:#7f1d1d;border-color:#ef4444;color:#fecaca;" onclick="window.deleteMusicFileOnSd('${safeName}')" title="Xóa khỏi /Musics">🗑️</button>` : ''}
            </div>
          </div>
        `;
      }).join('');
    } catch (e) {}
  };

  // Tải 1 tệp hoặc nhiều tệp .MP3 / .WAV lên thư mục /Musics trên Thẻ SD
  window.uploadSelectedMusicFiles = async function() {
    const input = document.getElementById('music-upload-input');
    if (!input || !input.files || input.files.length === 0) {
      return alert("Vui lòng chọn ít nhất 1 tệp nhạc (.mp3 hoặc .wav) từ máy tính để tải lên /Musics!");
    }
    const files = Array.from(input.files);
    const wrap = document.getElementById('music-upload-progress-wrap');
    const statusTxt = document.getElementById('music-upload-status-txt');
    const pctTxt = document.getElementById('music-upload-pct-txt');
    const bar = document.getElementById('music-upload-bar');
    const btn = document.getElementById('btn-upload-musics');

    if (wrap) wrap.style.display = 'block';
    if (btn) btn.disabled = true;

    let successCount = 0;
    for (let i = 0; i < files.length; i++) {
      const file = files[i];
      if (statusTxt) statusTxt.innerText = `Đang tải (${i + 1}/${files.length}): ${file.name} (${(file.size / 1024 / 1024).toFixed(2)} MB)...`;

      await new Promise((resolve) => {
        const xhr = new XMLHttpRequest();
        const formData = new FormData();
        formData.append('music', file, file.name);

        xhr.upload.onprogress = function(ev) {
          if (ev.lengthComputable) {
            const filePct = (ev.loaded / ev.total) * 100;
            const totalPct = Math.round(((i + (ev.loaded / ev.total)) / files.length) * 100);
            if (pctTxt) pctTxt.innerText = `${totalPct}% (Tệp ${i + 1}/${files.length}: ${Math.round(filePct)}%)`;
            if (bar) bar.style.width = totalPct + '%';
          }
        };
        xhr.onload = function() {
          if (xhr.status >= 200 && xhr.status < 300) successCount++;
          resolve();
        };
        xhr.onerror = function() { resolve(); };
        xhr.open('POST', '/api/media/upload', true);
        xhr.send(formData);
      });
    }

    if (btn) btn.disabled = false;
    input.value = '';
    if (statusTxt) statusTxt.innerText = `✅ Hoàn tất tải lên ${successCount}/${files.length} tệp vào thư mục /Musics trên Thẻ SD!`;
    if (pctTxt) pctTxt.innerText = '100%';
    if (bar) bar.style.width = '100%';
    if (typeof showToast === 'function') {
      showToast(`🎵 Đã lưu ${successCount}/${files.length} bài nhạc vào /Musics trên Thẻ SD!`);
    }
    await window.loadMusicPlaylist(true);
    await window.pollMediaStatus();
  };

  // Đồng bộ định kỳ trạng thái Trình phát nhạc từ ESP32 -> Web (1.2 giây/lần)
  window.pollMediaStatus = async function() {
    try {
      const res = await fetch('/api/media/status', { cache: 'no-store' });
      if (!res.ok) return;
      const st = await res.json();
      const prevCount = window.mediaState.track_count;
      Object.assign(window.mediaState, st);
      updateMediaDomFromState();
      if (st.track_count !== prevCount) {
        window.loadMusicPlaylist();
      }
    } catch (e) {}
  };

  // Tải thông số phần cứng & phần mềm của hệ thống
  window.loadSystemInfo = async function() {
    try {
      let res = await fetch('/api/system_info');
      if (!res.ok) res = await fetch('/api/system/status');
      if (!res.ok) return;
      const d = await res.json();
      const el = id => document.getElementById(id);
      if (el('sys-os-name')) el('sys-os-name').innerText = d.os_name || 'Space OS';
      if (el('sys-fw-ver')) el('sys-fw-ver').innerText = 'Phiên bản: ' + (d.firmware_version || 'v3.1.0');
      if (el('sys-build-date')) el('sys-build-date').innerText = 'Build: ' + (d.build_date || '--');
      if (el('sys-running-part')) el('sys-running-part').innerText = 'Phân vùng: ' + (d.running_partition || 'ota_0');
      if (el('sys-next-part')) el('sys-next-part').innerText = 'Dự phòng: ' + (d.next_partition || 'ota_1');
      if (el('sys-chip')) el('sys-chip').innerText = `${d.chip_model || 'ESP32-S3'} (${d.cpu_freq_mhz || 240}MHz, Rev ${d.chip_revision || 0})`;
      if (el('sys-flash')) {
        let flMb = d.flash_size_bytes ? (d.flash_size_bytes / (1024*1024)).toFixed(0) : 16;
        let psMb = d.total_psram_bytes ? (d.total_psram_bytes / (1024*1024)).toFixed(0) : 8;
        el('sys-flash').innerText = `Flash: ${flMb}MB • PSRAM: ${psMb}MB`;
      }
      if (el('sys-heap')) {
        let fH = d.free_heap_bytes ? (d.free_heap_bytes/1024).toFixed(0) : (d.free_heap_kb || '--');
        let tH = d.total_heap_bytes ? (d.total_heap_bytes/1024).toFixed(0) : '320';
        el('sys-heap').innerText = `Heap: ${fH}KB / ${tH}KB`;
      }
      if (el('sys-psram')) {
        let fP = d.free_psram_bytes ? (d.free_psram_bytes/(1024*1024)).toFixed(1) : (d.free_psram_mb ? Number(d.free_psram_mb).toFixed(1) : '--');
        let tP = d.total_psram_bytes ? (d.total_psram_bytes/(1024*1024)).toFixed(0) : '8';
        el('sys-psram').innerText = `PSRAM: ${fP}MB / ${tP}MB`;
      }
      if (el('sys-wifi')) el('sys-wifi').innerText = `${d.wifi_ssid || d.current_ssid || 'Chưa nối'} (${d.wifi_rssi || 0} dBm)`;
      if (el('sys-ip')) el('sys-ip').innerText = 'IP: ' + (d.wifi_ip || d.ip || '--');
      const up = d.uptime_seconds || 0;
      const upH = Math.floor(up / 3600);
      const upM = Math.floor((up % 3600) / 60);
      const upS = up % 60;
      if (el('sys-uptime')) el('sys-uptime').innerText = `${upH}h ${upM}m ${upS}s`;
    } catch (e) {
      console.error(e);
    }
  };

  // Kiểm tra phiên bản mới từ máy chủ từ xa
  window.checkCloudUpdate = async function() {
    const box = document.getElementById('ota-check-result');
    if (!box) return;
    box.style.display = 'block';
    box.innerHTML = '<div style="color:#94a3b8;font-size:0.82rem;">⏳ Đang kết nối máy chủ kiểm tra phiên bản mới...</div>';
    const urlInput = document.getElementById('ota-manifest-url');
    const manifestUrl = urlInput ? urlInput.value.trim() : '';

    try {
      const res = await fetch('/api/ota_check?url=' + encodeURIComponent(manifestUrl));
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const d = await res.json();
      if (!d.success) {
        box.innerHTML = '<div style="color:#ef4444;font-size:0.82rem;">❌ Không thể kết nối tới máy chủ kiểm tra cập nhật. Vui lòng kiểm tra lại Wi-Fi hoặc URL manifest.</div>';
        return;
      }
      if (d.has_update) {
        window.cloudOtaBinUrl = d.bin_url;
        box.innerHTML = `
          <div style="border-left:3px solid #10b981;padding-left:10px;">
            <div style="color:#10b981;font-weight:700;font-size:0.95rem;">🎉 Phát Hiện Bản Cập Nhật Mới: ${d.latest_version}</div>
            <div style="color:#cbd5e1;font-size:0.8rem;margin-top:4px;">Phiên bản hiện tại: <strong>${d.current_version}</strong></div>
            <div style="color:#94a3b8;font-size:0.8rem;margin-top:6px;background:#1e293b;padding:8px;border-radius:4px;">
              <strong>Nhật ký thay đổi:</strong><br>${(d.changelog || 'Nâng cấp tính năng và cải thiện hiệu năng.').replace(/\n/g, '<br>')}
            </div>
            <button class="btn-primary" style="margin-top:10px;background:#10b981;width:auto;padding:8px 18px;font-size:0.85rem;" onclick="window.startCloudOtaNow()">🚀 Nâng Cấp Ngay (1-Click OTA)</button>
          </div>
        `;
      } else {
        box.innerHTML = `<div style="color:#38bdf8;font-size:0.85rem;">✅ Thiết bị đang chạy phiên bản mới nhất (<strong>${d.current_version}</strong>). Không có bản cập nhật mới nào.</div>`;
      }
    } catch (e) {
      box.innerHTML = '<div style="color:#ef4444;font-size:0.82rem;">❌ Lỗi kết nối: ' + e.message + '</div>';
    }
  };

  // Bắt đầu cập nhật Cloud OTA
  window.startCloudOtaNow = async function() {
    if (!window.cloudOtaBinUrl) return;
    if (!confirm('Xác nhận tiến hành nâng cấp firmware từ xa?\\nTrong quá trình nạp, vui lòng KHÔNG ngắt nguồn điện của thiết bị.')) return;
    const box = document.getElementById('ota-check-result');
    if (box) {
      box.innerHTML = '<div style="color:#f59e0b;font-weight:700;font-size:0.9rem;">⏳ Đang ra lệnh cho ESP32 tải và nạp Firmware... Màn hình thiết bị sẽ hiển thị tiến trình. Vui lòng đợi trong giây lát!</div>';
    }
    try {
      await fetch('/api/ota_start', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ bin_url: window.cloudOtaBinUrl })
      });
      setTimeout(() => {
        alert('Thiết bị đang nạp firmware và sẽ tự động khởi động lại sau 1-2 phút!');
      }, 500);
    } catch (e) {
      alert('Lỗi: ' + e.message);
    }
  };

  // Nạp Firmware thủ công qua Web (Local Web OTA)
  window.uploadManualOta = function() {
    const fileInput = document.getElementById('manual-ota-file');
    if (!fileInput || !fileInput.files.length) {
      alert('Vui lòng chọn tệp firmware (.bin) trước!');
      return;
    }
    const file = fileInput.files[0];
    if (!file.name.endsWith('.bin')) {
      alert('Tệp được chọn phải có đuôi .bin!');
      return;
    }
    if (!confirm(`Xác nhận nạp tệp "${file.name}" (${(file.size/1024).toFixed(0)} KB) lên ESP32?\\nVui lòng KHÔNG rút nguồn điện trong quá trình này.`)) return;

    const pBox = document.getElementById('manual-ota-progress-box');
    const pBar = document.getElementById('manual-ota-bar');
    const pPct = document.getElementById('manual-ota-pct');
    const pSt  = document.getElementById('manual-ota-status');
    if (pBox) pBox.style.display = 'block';

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/update', true);

    xhr.upload.onprogress = function(e) {
      if (e.lengthComputable) {
        const pct = Math.round((e.loaded / e.total) * 100);
        if (pBar) pBar.style.width = pct + '%';
        if (pPct) pPct.innerText = pct + '%';
        if (pSt) pSt.innerText = `Đang nạp Flash: ${(e.loaded/1024).toFixed(0)}/${(e.total/1024).toFixed(0)} KB...`;
      }
    };

    xhr.onload = function() {
      if (xhr.status === 200) {
        if (pSt) pSt.innerText = '✅ Nạp thành công 100%! Thiết bị đang khởi động lại...';
        if (pBar) pBar.style.background = '#10b981';
        setTimeout(() => { window.location.reload(); }, 6000);
      } else {
        if (pSt) pSt.innerText = '❌ Nạp thất bại: ' + xhr.responseText;
        if (pBar) pBar.style.background = '#ef4444';
      }
    };

    xhr.onerror = function() {
      if (pSt) pSt.innerText = '❌ Lỗi kết nối mạng trong khi nạp!';
    };

    const formData = new FormData();
    formData.append('update', file);
    xhr.send(formData);
  };

  // Điều khiển hiển thị Mã QR Wi-Fi lên màn hình LCD ST7789
  window.showQrOnDeviceScreen = async function() {
    try {
      if (typeof showToast === 'function') showToast("📱 Đang chuyển màn hình thiết bị sang Mã QR Cài Đặt Wi-Fi...");
      await fetch('/api/screen/state', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: 'qr_wifi' })
      });
    } catch(e) {
      console.error(e);
    }
  };

  function injectWifiQrShortcut() {
    const btnScan = document.getElementById('btn-scan-wifi');
    if (btnScan && !document.getElementById('btn-show-qr-device')) {
      const qrBtn = document.createElement('button');
      qrBtn.id = 'btn-show-qr-device';
      qrBtn.className = 'btn-secondary';
      qrBtn.style.cssText = 'width:100%; margin-bottom:12px; display:flex; align-items:center; justify-content:center; gap:8px; background:linear-gradient(135deg, #0ea5e9, #6366f1); color:white; border:none; padding:10px; border-radius:8px; font-weight:600; cursor:pointer; font-size:0.88rem; box-shadow:0 4px 12px rgba(14,165,233,0.3);';
      qrBtn.innerHTML = '<span>📱</span><span>Hiện Mã QR Wi-Fi Lên Màn Hình Thiết Bị (ST7789)</span>';
      qrBtn.onclick = window.showQrOnDeviceScreen;
      btnScan.parentNode.insertBefore(qrBtn, btnScan);
    }
  }

  setTimeout(() => {
    injectMusicTabAndPage();
    injectSystemTabAndPage();
    injectWifiQrShortcut();
    window.loadMusicPlaylist();
    window.pollMediaStatus();
    window.loadSystemInfo();
    setInterval(window.pollMediaStatus, 1200);
    setInterval(window.loadSystemInfo, 3000);
    setInterval(renderWebMediaVisualizerFrame, 50);

    // Tự động chuyển sang Tab "Cài Đặt Wi-Fi" và quét mạng nếu đang truy cập SoftAP (192.168.4.1) hoặc hash #wifi
    if (window.location.hash === '#wifi' || window.location.hostname === '192.168.4.1') {
      setTimeout(() => {
        if (typeof switchTab === 'function') {
          switchTab('wifi');
        }
        setTimeout(() => {
          if (typeof scanWifi === 'function') {
            scanWifi();
          }
        }, 600);
      }, 350);
    }
  }, 250);
})();
</script>
)rawliteral";

static void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    uint8_t buf[1024];
    while (file.available()) {
      size_t n = file.read(buf, sizeof(buf));
      if (n > 0) server.sendContent((const char*)buf, n);
    }
    file.close();
    server.sendContent_P(SD_CONVERTER_INJECT_SCRIPT);
    server.sendContent_P(MEDIA_PAGE_INJECT_SCRIPT);
    server.sendContent("");
  } else {
    server.send(404, "text/plain", "Chưa tìm thấy index.html trên LittleFS!");
  }
}

static void handleWifiScan() {
  JsonDocument doc;
  WifiManager::scanNetworks(doc);

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

static void handleGetSavedWifi() {
  JsonDocument doc;
  WifiManager::loadSavedWifiList(doc);
  JsonArray arr = doc.as<JsonArray>();

  JsonDocument resDoc;
  JsonArray resArr = resDoc.to<JsonArray>();
  for (JsonObject item : arr) {
    JsonObject obj = resArr.add<JsonObject>();
    String s = item["ssid"].as<String>();
    obj["ssid"] = s;
    obj["connected"] = (WifiManager::isConnected() && WifiManager::getCurrentSsid() == s);
    obj["has_password"] = (item["password"].as<String>().length() > 0);
  }
  String res;
  serializeJson(resDoc, res);
  server.send(200, "application/json", res);
}

static void handleDeleteSavedWifi() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  String ssid = doc["ssid"] | "";
  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"SSID không hợp lệ\"}");
    return;
  }
  bool deleted = WifiManager::deleteSavedWifiNetwork(ssid);
  if (deleted) {
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đã xóa Wi-Fi khỏi danh sách!\"}");
  } else {
    server.send(404, "application/json", "{\"error\":\"Không tìm thấy Wi-Fi trong danh sách đã lưu\"}");
  }
}

static void handleWifiConnect() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  String ssid = doc["ssid"] | "";
  String pass = doc["password"] | "";
  bool save = doc["save"] | true;

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Tên Wi-Fi không được để trống\"}");
    return;
  }

  bool ok = WifiManager::connectDirect(ssid, pass, save);
  if (ok) {
    // Tự động kiểm tra OTA khi kết nối thành công
    XiaoZhiClient::queryOTA(true);

    JsonDocument resDoc;
    resDoc["status"] = "ok";
    resDoc["connected"] = true;
    resDoc["ssid"] = ssid;
    resDoc["ip"] = WifiManager::getIp();
    resDoc["message"] = "Đã kết nối Wi-Fi thành công!";
    String res;
    serializeJson(resDoc, res);
    server.send(200, "application/json", res);
  } else {
    server.send(200, "application/json", "{\"status\":\"error\",\"connected\":false,\"message\":\"Không thể kết nối (Sai mật khẩu hoặc sóng yếu)\"}");
  }
}

static void handleSystemStatus() {
  JsonDocument doc;
  doc["device_name"] = OtaManager::getDeviceName();
  doc["os_name"] = OtaManager::getOsName();
  doc["firmware_version"] = OtaManager::getFirmwareVersion();
  doc["build_date"] = OtaManager::getBuildDateTime();
  doc["running_partition"] = OtaManager::getRunningPartitionName();
  doc["next_partition"] = OtaManager::getNextPartitionName();
  doc["free_heap_kb"] = HardwareManager::getFreeHeapKb();
  doc["free_psram_mb"] = HardwareManager::getFreePsramMb();
  doc["fs_used_mb"] = HardwareManager::getFsUsedMb();
  doc["fs_total_mb"] = HardwareManager::getFsTotalMb();
  doc["ip"] = WifiManager::getIp();
  doc["mac"] = WiFi.macAddress();
  doc["connected"] = WifiManager::isConnected();
  doc["current_ssid"] = WifiManager::getCurrentSsid();
  doc["wifi_rssi"] = WifiManager::getRssi();
  doc["wifi_notification"] = WifiManager::getLastNotification();
  doc["active_img"] = ImageManager::getActiveImage();
  doc["screen_mode"] = TestDisplay::getScreenModeName();
  doc["emoji_state"] = TestDisplay::getEmojiStateName();
  doc["emoji_subtitle"] = TestDisplay::getSubtitle();
  doc["hud_style"] = TestDisplay::getHudStyleName();
  doc["sht31_online"] = TestSensors::isSht31Connected();
  doc["temperature_c"] = serialized(String(TestSensors::getTemperatureC(), 1));
  doc["humidity_pct"] = (int)roundf(TestSensors::getHumidityPct());
  doc["touch_pressed"] = TestSensors::isTouchPressed();
  doc["last_button"] = TestButtons::getLastButtonName();

  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

// ---------------- REST APIS QUẢN LÝ ẢNH ----------------

#include <SD.h>
#include "tests/TestSDCard.h"

static void handleListImages() {
  JsonDocument doc;
  ImageManager::listImages(doc);
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void handleImageUploadRoute() {
  bool forceSd = server.hasArg("target") && (server.arg("target") == "sd");
  ImageManager::handleUpload(server.upload(), uploadAllowed, forceSd);
}

static void handleSdUploadRoute() {
  ImageManager::handleUpload(server.upload(), uploadAllowed, true);
}

static void handleSdFileRoute() {
  if (!server.hasArg("path") || !TestSDCard::isMounted()) {
    server.send(404, "text/plain", "SD file not found");
    return;
  }
  String path = server.arg("path");
  if (!path.startsWith("/")) path = "/" + path;
  if (!SD.exists(path)) {
    server.send(404, "text/plain", "SD file does not exist");
    return;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Cannot open SD file");
    return;
  }
  String contentType = "application/octet-stream";
  String low = path;
  low.toLowerCase();
  if (low.endsWith(".png")) contentType = "image/png";
  else if (low.endsWith(".jpg") || low.endsWith(".jpeg")) contentType = "image/jpeg";
  else if (low.endsWith(".bmp")) contentType = "image/bmp";
  server.streamFile(f, contentType);
  f.close();
}

static void handleDeleteImage() {
  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"error\":\"Missing name\"}");
    return;
  }
  String filename = server.arg("name");
  if (ImageManager::deleteImage(filename)) {
    server.send(200, "application/json", "{\"status\":\"success\"}");
  } else {
    server.send(404, "application/json", "{\"error\":\"File not found\"}");
  }
}

static void handleSelectImage() {
  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"error\":\"Missing name\"}");
    return;
  }
  String filename = server.arg("name");
  if (!filename.startsWith("sd:") && !filename.startsWith("/")) filename = "/" + filename;
  ImageManager::selectActiveImage(filename);

  // Đồng bộ luôn vào /standby_config.json và đẩy lên màn hình ST7789
  JsonDocument doc;
  if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
    File rFile = LittleFS.open(FILE_STANDBY_CONFIG, "r");
    if (rFile) {
      deserializeJson(doc, rFile);
      rFile.close();
    }
  }
  doc["bg_mode"] = "image";
  doc["bg_image"] = filename;
  File wFile = LittleFS.open(FILE_STANDBY_CONFIG, "w");
  if (wFile) {
    serializeJson(doc, wFile);
    wFile.close();
  }
  TestDisplay::reloadStandbyConfig();

  server.send(200, "application/json", "{\"status\":\"success\"}");
}

// ---------------- REST APIS CẤU HÌNH MÀN HÌNH CHỜ ----------------

static void handleGetStandbyConfig() {
  if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
    File file = LittleFS.open(FILE_STANDBY_CONFIG, "r");
    server.streamFile(file, "application/json");
    file.close();
  } else {
    server.send(200, "application/json", "{\"theme\":\"cyberpunk\",\"clock_style\":\"digital\",\"clock_color\":\"#38bdf8\",\"clock_format\":\"24h\",\"show_seconds\":true,\"clock_pos\":\"center\",\"show_date\":true,\"date_format\":\"vi\",\"show_weather\":true,\"temp\":28.5,\"humidity\":65,\"custom_text\":\"Trạm Decor Vũ Trụ ✨\",\"text_color\":\"#94a3b8\",\"bg_mode\":\"gradient\",\"bg_color\":\"#0a0f1d\",\"bg_image\":\"\"}");
  }
}

static void handleSaveStandbyConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  File file = LittleFS.open(FILE_STANDBY_CONFIG, "w");
  if (!file) {
    server.send(500, "application/json", "{\"error\":\"Failed to open config file for write\"}");
    return;
  }
  file.print(body);
  file.close();

  // Đồng bộ ảnh nền nếu có khai báo bg_image
  if (doc["bg_image"].is<const char*>()) {
    String bgImg = doc["bg_image"].as<String>();
    if (bgImg.length() > 0) {
      ImageManager::selectActiveImage(bgImg);
    }
  }

  // Đẩy cấu hình mới trực tiếp ra màn hình ST7789 rời
  TestDisplay::reloadStandbyConfig();

  Serial.println("✅ Đã cập nhật cấu hình màn hình chờ vào LittleFS & đồng bộ lên màn hình ST7789!");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Đã lưu và đẩy cấu hình lên màn hình chờ ST7789!\"}");
}

static void handleScreenStatePost() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  if (doc["mode"].is<const char*>()) {
    String mode = doc["mode"].as<String>();
    TestDisplay::setScreenMode(mode);
  }

  if (doc["emoji"].is<const char*>()) {
    String emoji = doc["emoji"].as<String>();
    String subtitle = doc["subtitle"] | "";
    TestDisplay::setEmojiState(emoji, subtitle);
  }

  if (doc["hud_style"].is<const char*>()) {
    String style = doc["hud_style"].as<String>();
    TestDisplay::setHudStyle(style);
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ---------------- REST APIS CẤU HÌNH XIAOZHI AI ----------------

static void handleGetAIConfig() {
  JsonDocument doc;
  XiaoZhiClient::getAIConfig(doc);
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void handleSaveAIConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  bool ok = XiaoZhiClient::saveAIConfig(server.arg("plain"));
  if (ok) {
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Đã lưu cấu hình XiaoZhi AI thành công!\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Failed to save AI config\"}");
  }
}

static void handleXiaoZhiOTA() {
  if (!WifiManager::isConnected()) {
    server.send(400, "application/json", "{\"error\":\"ESP32 chưa nối Wi-Fi để truy cập XiaoZhi Cloud trực tiếp\",\"need_wifi\":true}");
    return;
  }
  String payload = XiaoZhiClient::queryOTA(true);
  if (payload.length() > 0) {
    server.send(200, "application/json", payload);
  } else {
    server.send(500, "application/json", "{\"error\":\"Lỗi kết nối máy chủ XiaoZhi\"}");
  }
}

// ---------------- REST APIS THÔNG SỐ PC HUD ----------------

static void handlePcMetricsGet() {
  JsonDocument doc;
  PcStatsManager::serializeMetrics(doc);
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void handlePcMetricsPost() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  bool ok = PcStatsManager::updateMetrics(server.arg("plain"));
  if (ok) {
    server.send(200, "application/json", "{\"status\":\"ok\",\"is_live\":true}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
  }
}

// ---------------- REST APIS HỆ THỐNG & OTA ----------------

static void handleSystemInfo() {
  JsonDocument doc;
  doc["device_name"]        = OtaManager::getDeviceName();
  doc["os_name"]            = OtaManager::getOsName();
  doc["firmware_version"]   = OtaManager::getFirmwareVersion();
  doc["build_date"]         = OtaManager::getBuildDateTime();
  doc["running_partition"]  = OtaManager::getRunningPartitionName();
  doc["next_partition"]     = OtaManager::getNextPartitionName();
  doc["chip_model"]         = ESP.getChipModel();
  doc["chip_revision"]      = ESP.getChipRevision();
  doc["cpu_freq_mhz"]       = ESP.getCpuFreqMHz();
  doc["flash_size_bytes"]   = ESP.getFlashChipSize();
  doc["free_heap_bytes"]    = ESP.getFreeHeap();
  doc["total_heap_bytes"]   = ESP.getHeapSize();
  doc["free_psram_bytes"]   = ESP.getFreePsram();
  doc["total_psram_bytes"]  = ESP.getPsramSize();
  doc["wifi_connected"]     = (WiFi.status() == WL_CONNECTED);
  doc["wifi_ssid"]          = WiFi.SSID();
  doc["wifi_rssi"]          = WiFi.RSSI();
  doc["wifi_ip"]            = WiFi.localIP().toString();
  doc["uptime_seconds"]     = millis() / 1000UL;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleOtaCheck() {
  String customUrl = server.hasArg("url") ? server.arg("url") : "";
  OtaUpdateInfo info;
  bool ok = OtaManager::checkCloudUpdate(info, customUrl);

  JsonDocument doc;
  doc["success"]          = ok;
  doc["current_version"]  = OtaManager::getFirmwareVersion();
  doc["has_update"]       = info.hasUpdate;
  doc["latest_version"]   = info.latestVersion;
  doc["bin_url"]          = info.downloadUrl;
  doc["changelog"]        = info.changelog;
  doc["size"]             = info.binSize;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

static void handleOtaStart() {
  String binUrl = "";
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    binUrl = doc["bin_url"] | "";
  } else if (server.hasArg("bin_url")) {
    binUrl = server.arg("bin_url");
  }

  if (binUrl.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Missing bin_url\"}");
    return;
  }

  server.send(200, "application/json", "{\"status\":\"updating\"}");
  OtaManager::startCloudUpdate(binUrl);
}

static void handleLocalUpdate() {
  server.sendHeader("Connection", "close");
  if (!Update.hasError()) {
    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;text-align:center;padding:50px;background:#0f172a;color:#fff;'>"
      "<h2>✅ CẬP NHẬT FIRMWARE THÀNH CÔNG!</h2>"
      "<p>Hệ thống đang khởi động lại vào firmware mới... Vui lòng đợi 5 giây rồi tải lại trang.</p>"
      "<script>setTimeout(function(){ window.location.href='/'; }, 6000);</script>"
      "</body></html>");
    delay(1500);
    ESP.restart();
  } else {
    server.send(500, "text/html",
      "<html><body style='font-family:sans-serif;text-align:center;padding:50px;background:#0f172a;color:#fff;'>"
      "<h2>❌ CẬP NHẬT THẤT BẠI!</h2>"
      "<p>Lỗi: " + String(Update.errorString()) + "</p>"
      "<p><a href='/' style='color:#38bdf8;'>Quay lại trang chủ</a></p>"
      "</body></html>");
  }
}

static void handleLocalUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("⬆️ [WEB OTA] Bắt đầu nhận file: %s\n", upload.filename.c_str());
    OtaManager::drawMinimalOtaProgress("Nhan file qua Web...", 0);

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
    if (upload.totalSize > 0) {
      int pct = (upload.currentSize * 100 / upload.totalSize);
      OtaManager::drawMinimalOtaProgress("Dang ghi Flash...", pct);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("✅ [WEB OTA] Nhận file xong: %u bytes! Đang khởi động lại...\n", upload.totalSize);
      OtaManager::drawMinimalOtaProgress("Hoan tat! Dang reboot...", 100);
    } else {
      Update.printError(Serial);
      OtaManager::drawMinimalOtaProgress("Loi ghi Flash!", 0);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    Serial.println("⚠️ [WEB OTA] Quá trình tải lên bị hủy!");
    OtaManager::drawMinimalOtaProgress("Da huy tai len!", 0);
  }
}

// ================================================================
//     REST APIS ĐA PHƯƠNG TIỆN & THƯ MỤC /Musics TRÊN THẺ NHỚ SD
// ================================================================

static File mediaUploadFile;
static String lastUploadedMusicName = "";
static bool mediaUploadSuccess = false;

static void handleMediaStatus() {
  String trackName;
  int trackIdx = 0, trackCount = 0, volumePct = 75;
  bool playing = true, fromSd = false;
  uint8_t visMode = 0, repeatMode = 0;
  uint16_t curSec = 0, totalSec = 180;

  TestDisplay::getMediaPlayerState(trackName, trackIdx, trackCount, playing,
                                   visMode, repeatMode, curSec, totalSec, volumePct, fromSd);

  JsonDocument doc;
  doc["playing"]     = playing;
  doc["track_idx"]   = trackIdx;
  doc["track_name"]  = trackName;
  doc["track_count"] = trackCount;
  doc["cur_sec"]     = curSec;
  doc["total_sec"]   = totalSec;
  doc["volume"]      = volumePct;
  doc["vis_mode"]    = visMode;
  doc["repeat_mode"] = repeatMode;
  doc["from_sd"]     = fromSd;
  doc["sd_mounted"]  = TestSDCard::isMounted();
  doc["mic_level"]   = TestAudio::getLastMicLevelPct();
  doc["ui_theme"]    = TestDisplay::getUiThemeIdx();

  JsonArray specArr = doc["spectrum"].to<JsonArray>();
  const uint8_t* spec16 = TestAudio::getMicSpectrum16();
  for (int i = 0; i < 16; i++) {
    specArr.add(spec16 ? spec16[i] : 0);
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleMediaList() {
  String names[24];
  uint16_t durs[24];
  bool fromSd[24];
  size_t sizes[24];
  int cnt = TestDisplay::getMediaPlaylistItems(names, durs, fromSd, sizes, 24);

  JsonDocument doc;
  doc["sd_mounted"]  = TestSDCard::isMounted();
  doc["sd_total_mb"] = (uint32_t)TestSDCard::getTotalMB();
  doc["sd_used_mb"]  = (uint32_t)TestSDCard::getUsedMB();

  JsonArray arr = doc["tracks"].to<JsonArray>();
  for (int i = 0; i < cnt; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["index"]    = i;
    obj["name"]     = names[i];
    obj["duration"] = durs[i];
    obj["from_sd"]  = fromSd[i];
    obj["size_kb"]  = (uint32_t)(sizes[i] / 1024);
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleMediaControl() {
  if (server.hasArg("plain")) {
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain"))) {
      // 1. Nếu trình duyệt Web gửi phổ âm thanh thực FFT (spectrum[16]) từ file MP3/WAV đang phát
      if (doc["spectrum"].is<JsonArray>()) {
        JsonArray sp = doc["spectrum"].as<JsonArray>();
        uint8_t bands[16] = {0};
        for (int i = 0; i < 16 && i < (int)sp.size(); i++) {
          bands[i] = (uint8_t)constrain(sp[i].as<int>(), 0, 100);
        }
        int lv = doc["mic_level"] | 25;
        TestAudio::setExternalAudioSpectrum(bands, lv);
      }

      // 2. Nếu có lệnh điều khiển trình phát (play, pause, toggle, next, prev, select, seek, volume, vis, repeat, open_on_esp)
      if (!doc["action"].isNull()) {
        String action = doc["action"].as<String>();
        int val       = doc["value"] | 0;
        String name   = doc["name"] | "";
        TestDisplay::controlMediaPlayer(action, val, name);
      }
    }
  }
  handleMediaStatus();
}

static void handleMediaUploadChunk() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    mediaUploadSuccess = false;
    lastUploadedMusicName = upload.filename;
    int sl = lastUploadedMusicName.lastIndexOf('/');
    if (sl >= 0) lastUploadedMusicName = lastUploadedMusicName.substring(sl + 1);
    int bsl = lastUploadedMusicName.lastIndexOf('\\');
    if (bsl >= 0) lastUploadedMusicName = lastUploadedMusicName.substring(bsl + 1);

    if (TestSDCard::isMounted()) {
      if (!SD.exists("/Musics")) {
        SD.mkdir("/Musics");
      }
      String fullPath = "/Musics/" + lastUploadedMusicName;
      if (SD.exists(fullPath)) SD.remove(fullPath);
      mediaUploadFile = SD.open(fullPath, FILE_WRITE);
      Serial.printf("🎵 [SD /Musics] Đang nhận file nhạc: %s\n", fullPath.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (mediaUploadFile && upload.currentSize > 0) {
      mediaUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (mediaUploadFile) {
      mediaUploadFile.close();
      mediaUploadSuccess = true;
      Serial.printf("✅ [SD /Musics] Đã lưu xong: /Musics/%s (%u bytes)\n",
                    lastUploadedMusicName.c_str(), (unsigned)upload.totalSize);
      TestDisplay::controlMediaPlayer("select", 0, lastUploadedMusicName);
    }
  }
}

static void handleMediaDelete() {
  String name = server.arg("name");
  int sl = name.lastIndexOf('/');
  if (sl >= 0) name = name.substring(sl + 1);
  if (name.length() > 0 && TestSDCard::isMounted()) {
    String p1 = "/Musics/" + name;
    String p2 = "/" + name;
    bool removed = false;
    if (SD.exists(p1)) removed = SD.remove(p1);
    else if (SD.exists(p2)) removed = SD.remove(p2);
    TestDisplay::controlMediaPlayer("refresh", 0);
    if (removed) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đã xóa bài hát khỏi thư mục /Musics trên Thẻ SD!\"}");
      return;
    }
  }
  server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Không tìm thấy tệp nhạc trên Thẻ SD!\"}");
}

static void handleMediaStream() {
  String name = server.arg("name");
  int sl = name.lastIndexOf('/');
  if (sl >= 0) name = name.substring(sl + 1);
  if (name.length() > 0 && TestSDCard::isMounted()) {
    String path = "/Musics/" + name;
    if (!SD.exists(path)) path = "/" + name;
    if (SD.exists(path)) {
      File f = SD.open(path, FILE_READ);
      if (f) {
        String low = name;
        low.toLowerCase();
        String mime = "audio/mpeg";
        if (low.endsWith(".wav")) mime = "audio/wav";
        else if (low.endsWith(".ogg")) mime = "audio/ogg";
        else if (low.endsWith(".flac")) mime = "audio/flac";
        server.streamFile(f, mime);
        f.close();
        return;
      }
    }
  }
  server.send(404, "text/plain", "Music file not found on SD");
}

// ================================================================
//                     KHỞI CHẠY WEB SERVER & ROUTES
// ================================================================

void WebRoutes::begin() {
  // 1. Phục vụ trang chủ "/"
  server.on("/", HTTP_GET, handleRoot);

  // 2. Các REST API Mạng & Hệ thống
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/saved", HTTP_GET, handleGetSavedWifi);
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/api/wifi/delete", HTTP_POST, handleDeleteSavedWifi);
  server.on("/api/system/status", HTTP_GET, handleSystemStatus);

  // 3. Các REST API Quản Lý Ảnh (Hỗ trợ cả Thẻ nhớ SD /sd_images + /sd_rgb565 và LittleFS)
  server.on("/api/images", HTTP_GET, handleListImages);
  server.on("/api/sd/file", HTTP_GET, handleSdFileRoute);
  auto finishUploadSync = []() {
    if (uploadAllowed) {
      String activeImg = ImageManager::getActiveImage();
      if (activeImg.length() > 0) {
        JsonDocument doc;
        if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
          File rFile = LittleFS.open(FILE_STANDBY_CONFIG, "r");
          if (rFile) {
            deserializeJson(doc, rFile);
            rFile.close();
          }
        }
        doc["bg_mode"] = "image";
        doc["bg_image"] = activeImg;
        File wFile = LittleFS.open(FILE_STANDBY_CONFIG, "w");
        if (wFile) {
          serializeJson(doc, wFile);
          wFile.close();
        }
      }
      TestDisplay::reloadStandbyConfig();
      server.send(200, "application/json", "{\"status\":\"success\",\"active_img\":\"" + activeImg + "\",\"message\":\"Đã convert & lưu ảnh vào Thẻ nhớ SD (/sd_images + /sd_rgb565) và đẩy lên màn hình ST7789!\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Đã đạt giới hạn tối đa 15 ảnh trên LittleFS. Hãy bật lưu sang Thẻ nhớ SD hoặc xóa bớt ảnh cũ!\"}");
    }
  };
  server.on("/api/images/upload", HTTP_POST, finishUploadSync, handleImageUploadRoute);
  server.on("/api/sd/upload", HTTP_POST, finishUploadSync, handleSdUploadRoute);
  server.on("/api/images/delete", HTTP_POST, handleDeleteImage);
  server.on("/api/images/select", HTTP_POST, handleSelectImage);

  // 4. Các REST API Cấu hình Màn hình chờ & Chuyển đổi Chế độ Màn hình ST7789
  server.on("/api/screen/standby", HTTP_GET, handleGetStandbyConfig);
  server.on("/api/screen/standby", HTTP_POST, handleSaveStandbyConfig);
  server.on("/api/screen/state", HTTP_POST, handleScreenStatePost);

  // 4b. Các REST API Đa Phương Tiện & Trình Phát Nhạc (/Musics trên Thẻ SD)
  server.on("/api/media/status", HTTP_GET, handleMediaStatus);
  server.on("/api/media/list", HTTP_GET, handleMediaList);
  server.on("/api/media/control", HTTP_POST, handleMediaControl);
  server.on("/api/media/upload", HTTP_POST, []() {
    if (mediaUploadSuccess) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"file\":\"" + lastUploadedMusicName + "\",\"message\":\"Đã lưu vào /Musics trên Thẻ SD!\"}");
    } else {
      server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Không thể ghi vào Thẻ nhớ SD (/Musics)!\"}");
    }
  }, handleMediaUploadChunk);
  server.on("/api/media/delete", HTTP_POST, handleMediaDelete);
  server.on("/api/media/stream", HTTP_GET, handleMediaStream);

  // 5. Các REST API Cấu hình XiaoZhi AI & OTA
  server.on("/api/config/ai", HTTP_GET, handleGetAIConfig);
  server.on("/api/config/ai", HTTP_POST, handleSaveAIConfig);
  server.on("/api/xiaozhi/ota", HTTP_GET, handleXiaoZhiOTA);
  server.on("/api/xiaozhi/ota", HTTP_POST, handleXiaoZhiOTA);

  // 6. Các REST API Thông số PC (PC Status HUD)
  server.on("/api/pc/metrics", HTTP_GET, handlePcMetricsGet);
  server.on("/api/pc/metrics", HTTP_POST, handlePcMetricsPost);

  // 6b. Các REST API Cập Nhật Firmware OTA & Thông Tin Hệ Thống
  server.on("/api/system_info", HTTP_GET, handleSystemInfo);
  server.on("/api/ota_check", HTTP_GET, handleOtaCheck);
  server.on("/api/ota_start", HTTP_POST, handleOtaStart);
  server.on("/update", HTTP_POST, handleLocalUpdate, handleLocalUpdateUpload);

  // 6b2. REST API phát thử các âm báo S40 (Nokia Tune, OK, Delete, SMS Morse, Beep)
  server.on("/api/audio/play_tune", HTTP_POST, []() {
    String tune = server.hasArg("tune") ? server.arg("tune") : "nokia";
    if (server.hasArg("plain")) {
      StaticJsonDocument<128> doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("tune")) tune = doc["tune"].as<String>();
    }
    if (tune == "nokia" || tune == "1")       TestAudio::playNokiaTune();
    else if (tune == "ok")                     TestAudio::playOkChime();
    else if (tune == "delete" || tune == "del") TestAudio::playDeleteChime();
    else if (tune == "sms" || tune == "2")     TestAudio::playSmsSpecialTone();
    else if (tune == "beep" || tune == "0")    TestAudio::playKeyBeep();
    else if (tune == "chime" || tune == "3")   TestAudio::playStartupChime();
    else TestAudio::playAlarmTuneStep(tune.toInt(), 0);

    StaticJsonDocument<64> res;
    res["success"] = true;
    res["played"] = tune;
    String out;
    serializeJson(res, out);
    server.send(200, "application/json", out);
  });

  // 6c. Captive Portal Probes cho iOS / Android / Windows (Tự động mở trang Cấu hình Wi-Fi)
  auto handleCaptiveRedirect = []() {
    server.sendHeader("Location", "http://192.168.4.1/#wifi", true);
    server.send(302, "text/plain", "");
  };
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect); // Apple iOS / macOS
  server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);        // Google Android
  server.on("/gen_204", HTTP_GET, handleCaptiveRedirect);             // Chrome / Android
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);            // Windows
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);     // Windows
  server.on("/canonical.html", HTTP_GET, handleCaptiveRedirect);

  // 7. Phục vụ file tĩnh từ cả Thẻ nhớ SD (/sd_images/...) và LittleFS
  server.serveStatic("/", LittleFS, "/");
  server.onNotFound([handleCaptiveRedirect]() {
    String uri = server.uri();

    // 1. Kiểm tra nếu là Captive Portal Probes từ các hệ điều hành điện thoại
    if (uri.indexOf("hotspot-detect") >= 0 ||
        uri.indexOf("generate_204") >= 0 ||
        uri.indexOf("gen_204") >= 0 ||
        uri.indexOf("ncsi.txt") >= 0 ||
        uri.indexOf("connecttest") >= 0 ||
        uri.indexOf("canonical") >= 0 ||
        uri.indexOf("mobile/status") >= 0) {
      handleCaptiveRedirect();
      return;
    }

    if (uri.startsWith("/sd:")) uri = uri.substring(4);
    else if (uri.startsWith("sd:")) uri = uri.substring(3);
    if (!uri.startsWith("/")) uri = "/" + uri;

    if (TestSDCard::isMounted() && SD.exists(uri)) {
      File f = SD.open(uri, FILE_READ);
      if (f) {
        String contentType = "application/octet-stream";
        String low = uri;
        low.toLowerCase();
        if (low.endsWith(".png")) contentType = "image/png";
        else if (low.endsWith(".jpg") || low.endsWith(".jpeg")) contentType = "image/jpeg";
        else if (low.endsWith(".bmp")) contentType = "image/bmp";
        server.streamFile(f, contentType);
        f.close();
        return;
      }
    }
    if (LittleFS.exists(uri)) {
      File f = LittleFS.open(uri, "r");
      if (f) {
        String contentType = "application/octet-stream";
        String low = uri;
        low.toLowerCase();
        if (low.endsWith(".png")) contentType = "image/png";
        else if (low.endsWith(".jpg") || low.endsWith(".jpeg")) contentType = "image/jpeg";
        server.streamFile(f, contentType);
        f.close();
        return;
      }
    }

    // 2. Chuyển hướng Captive Portal nếu người dùng truy cập domain bất kỳ khi kết nối SoftAP
    String host = server.hostHeader();
    if (WifiManager::isApActive() && host.length() > 0 &&
        host != "192.168.4.1" &&
        (WiFi.status() != WL_CONNECTED || host != WiFi.localIP().toString()) &&
        host != (String(MDNS_HOSTNAME) + ".local")) {
      handleCaptiveRedirect();
      return;
    }

    server.send(404, "text/plain", "Not Found: " + uri);
  });

  server.begin();
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("🌐 mDNS ready: http://%s.local\n", MDNS_HOSTNAME);
  }
}

void WebRoutes::handleClient() {
  server.handleClient();
}

