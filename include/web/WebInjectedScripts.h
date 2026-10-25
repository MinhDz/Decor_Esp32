#pragma once
#include <Arduino.h>

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
      ['screen-standby', 'chat-screen-standby', 'sim-view-standby'].forEach(id => {
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
            <div id="sys-fw-ver" style="font-size:0.8rem;color:#e2e8f0;margin-top:2px;">Phiên bản: Đang tải...</div>
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

      <!-- 1b. DỌN RÁC BỘ NHỚ & CHẾ ĐỘ NGHIÊM NGẶT (MEMORY SCAVENGER & STRICT MODE) -->
      <div class="designer-panel" style="margin-top:16px;width:100%;max-width:100%;">
        <div class="designer-title">
          <span>🧹 Dọn Rác Bộ Nhớ & Chế Độ Nghiêm Ngặt (Memory Scavenger)</span>
          <button class="btn-primary" style="width:auto;padding:5px 14px;font-size:0.76rem;background:#10b981;" onclick="window.triggerMemoryScavenge()">🧹 Dọn RAM Ngay</button>
        </div>
        <div style="font-size:0.78rem;color:#94a3b8;margin-bottom:12px;line-height:1.5;">
          Tự động giám sát thời gian nhàn rỗi và giải phóng các bộ đệm lớn (Audio Rec 256KB, Stream Jitter Ring 384KB, MJPEG Video 64KB, XiaoZhi Opus Decoder 25KB) khi không sử dụng. Tự cấp phát lại ngay lập tức khi ứng dụng cần.
        </div>
        <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-bottom:12px;">
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Chế Độ Nghiêm Ngặt (Strict Mode)</div>
            <select id="mem-strict-select" onchange="window.setMemoryStrictMode(this.value)" style="width:100%;background:#1e293b;color:#38bdf8;border:1px solid #334155;padding:6px;border-radius:6px;font-size:0.8rem;margin-top:6px;font-weight:600;">
              <option value="0">0: Tắt (Chỉ dọn khi khẩn cấp/thủ công)</option>
              <option value="1" selected>1: Cân bằng (Dọn sau 3 phút idle)</option>
              <option value="2">2: Nghiêm ngặt (Dọn sau 60s / khi tắt màn)</option>
            </select>
          </div>
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Lần Dọn Gần Nhất</div>
            <div id="mem-last-freed" style="font-size:1.05rem;font-weight:700;color:#10b981;margin-top:4px;">-- KB</div>
            <div id="mem-last-reason" style="font-size:0.7rem;color:#64748b;margin-top:2px;">Lý do: --</div>
          </div>
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Tổng Dung Lượng Đã Thu Hồi</div>
            <div id="mem-total-freed" style="font-size:1.05rem;font-weight:700;color:#f59e0b;margin-top:4px;">0 KB</div>
            <div id="mem-total-runs" style="font-size:0.7rem;color:#64748b;margin-top:2px;">Số lần dọn: 0 lần</div>
          </div>
          <div style="background:#0f172a;padding:12px;border-radius:8px;border:1px solid #1e293b;">
            <div style="font-size:0.75rem;color:#94a3b8;">Trạng Thái Bộ Đệm Ứng Dụng</div>
            <div id="mem-buffers-badge" style="font-size:0.78rem;font-weight:600;color:#e2e8f0;margin-top:6px;line-height:1.6;">Đang tải...</div>
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
        <div id="manual-ota-progress-box" style="display:none;margin-top:14px;border:1px solid #0284c7;background:#032b43;padding:14px;border-radius:8px;">
          <div style="display:flex;justify-content:space-between;align-items:center;font-size:0.85rem;color:#e2e8f0;margin-bottom:6px;">
            <span id="manual-ota-status" style="font-weight:600;color:#38bdf8;">Đang nạp firmware...</span>
            <span id="manual-ota-pct" style="font-weight:700;font-size:1.0rem;color:#10b981;">0%</span>
          </div>
          <div style="width:100%;height:14px;background:#0f172a;border-radius:6px;overflow:hidden;border:1px solid #1e293b;">
            <div id="manual-ota-bar" style="width:0%;height:100%;background:linear-gradient(90deg,#0284c7,#38bdf8);transition:width 0.2s;box-shadow:0 0 10px #38bdf8;"></div>
          </div>
          <div style="margin-top:6px;font-size:0.74rem;color:#f59e0b;">⚠️ Đang nạp trực tiếp vào Flash ESP32. Vui lòng giữ nguyên nguồn điện!</div>
        </div>
      </div>

      <!-- 4. KHÔI PHỤC CÀI ĐẶT GỐC (FACTORY RESET - MÈO GOME CHIBI) -->
      <div class="designer-panel" style="margin-top:16px;width:100%;max-width:100%;border-color:#f43f5e;box-shadow:0 0 15px rgba(244,63,94,0.15);">
        <div class="designer-title" style="color:#f43f5e;">
          <span>⚠️ Khôi Phục Cài Đặt Ban Đầu (Factory Reset)</span>
          <span style="font-size:0.74rem;color:#f43f5e;font-weight:700;background:rgba(244,63,94,0.15);padding:2px 8px;border-radius:4px;border:1px solid rgba(244,63,94,0.3);">MÈO CHIBI DEFAULT</span>
        </div>
        <div style="font-size:0.78rem;color:#94a3b8;margin-bottom:14px;line-height:1.6;">
          Khôi phục toàn bộ cài đặt của trạm về nguyên bản ban đầu: Xóa toàn bộ tùy chỉnh người dùng, đặt lại âm lượng thông báo (70%), hiệu ứng đèn LED, mật khẩu quản trị Web về mặc định (<strong>admin / admin</strong>), và tự động kích hoạt giao diện mặc định <strong>MÈO GOME CHIBI</strong> trên màn hình trạm.
        </div>
        <div style="display:flex;justify-content:flex-end;">
          <button class="btn-primary" style="background:#e11d48;border-color:#f43f5e;width:auto;padding:8px 20px;font-size:0.82rem;font-weight:700;box-shadow:0 0 12px rgba(225,29,72,0.4);" onclick="window.performFactoryResetWeb()">
            🔄 Khôi Phục Cài Đặt Gốc (Mèo Chibi)
          </button>
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

  window.rawLogLines = [];
  window.currentLogTheme = 'cyberpunk';
  let logPollTimer = null;

  function injectLogsTabAndPage() {
    const navTabs = document.getElementById('nav-tabs');
    if (!navTabs || document.getElementById('tab-btn-logs')) return;

    // Nút Tab "📜 Nhật Ký (Logs)"
    const btn = document.createElement('button');
    btn.id = 'tab-btn-logs';
    btn.className = 'tab-btn';
    btn.innerHTML = '📜 Nhật Ký (Logs)';
    btn.onclick = function() { window.switchTab('logs'); };
    navTabs.appendChild(btn);

    // Section Page "#tab-logs"
    const sec = document.createElement('div');
    sec.id = 'tab-logs';
    sec.className = 'section';
    sec.innerHTML = `
      <style>
        .terminal-theme-matrix {
          --term-bg: #020d06;
          --term-text: #22c55e;
          --term-accent: #4ade80;
          --term-border: #15803d;
          --term-glow: 0 0 12px rgba(34,197,94,0.3);
          --term-subtle: #166534;
        }
        .terminal-theme-cyberpunk {
          --term-bg: #050b14;
          --term-text: #38bdf8;
          --term-accent: #00f0ff;
          --term-border: #0369a1;
          --term-glow: 0 0 12px rgba(0,240,255,0.3);
          --term-subtle: #075985;
        }
        .terminal-theme-pipboy {
          --term-bg: #0e0700;
          --term-text: #fbbf24;
          --term-accent: #ffb000;
          --term-border: #b45309;
          --term-glow: 0 0 12px rgba(251,191,36,0.35);
          --term-subtle: #78350f;
        }
        .crt-scanlines {
          position: relative;
        }
        .crt-scanlines::after {
          content: " ";
          display: block;
          position: absolute;
          top: 0; left: 0; bottom: 0; right: 0;
          background: linear-gradient(rgba(18, 16, 16, 0) 50%, rgba(0, 0, 0, 0.22) 50%), linear-gradient(90deg, rgba(255, 0, 0, 0.03), rgba(0, 255, 0, 0.01), rgba(0, 0, 255, 0.03));
          z-index: 2;
          background-size: 100% 3px, 4px 100%;
          pointer-events: none;
          border-radius: 8px;
        }
        @keyframes pulse-dot {
          0%, 100% { transform: scale(1); opacity: 1; }
          50% { transform: scale(1.3); opacity: 0.6; }
        }
        @keyframes blink-cursor {
          0%, 49% { opacity: 1; }
          50%, 100% { opacity: 0; }
        }
        .term-cursor {
          display: inline-block;
          animation: blink-cursor 0.9s infinite;
          font-weight: 700;
        }
        .live-dot {
          display: inline-block;
          width: 8px;
          height: 8px;
          border-radius: 50%;
          background: #10b981;
          animation: pulse-dot 1.5s infinite;
          box-shadow: 0 0 8px #10b981;
        }
        .cli-chip {
          background: #0f172a;
          border: 1px solid #334155;
          color: #94a3b8;
          border-radius: 12px;
          padding: 3px 10px;
          font-size: 0.68rem;
          font-family: Consolas, monospace;
          cursor: pointer;
          transition: all 0.2s;
        }
        .cli-chip:hover {
          background: #1e293b;
          color: var(--term-accent, #38bdf8);
          border-color: var(--term-accent, #38bdf8);
          transform: translateY(-1px);
        }
        .cli-suggest-popup {
          position: absolute;
          bottom: calc(100% + 6px);
          left: 0;
          right: 0;
          max-height: 250px;
          overflow-y: auto;
          background: rgba(11, 19, 36, 0.98);
          backdrop-filter: blur(12px);
          border: 1px solid var(--term-border, #0284c7);
          box-shadow: 0 10px 30px rgba(0,0,0,0.85), 0 0 15px rgba(56, 189, 248, 0.25);
          border-radius: 8px;
          z-index: 1000;
          display: none;
        }
        .cli-item {
          padding: 6px 12px;
          display: flex;
          align-items: center;
          gap: 10px;
          cursor: pointer;
          border-bottom: 1px solid rgba(255,255,255,0.05);
          transition: background 0.15s;
          font-family: Consolas, monospace;
          font-size: 0.74rem;
        }
        .cli-item:hover, .cli-item.selected {
          background: rgba(2, 132, 199, 0.25);
          border-left: 3px solid var(--term-accent, #38bdf8);
        }
        .cli-badge {
          padding: 2px 6px;
          border-radius: 4px;
          font-size: 0.62rem;
          font-weight: 700;
          letter-spacing: 0.5px;
          flex-shrink: 0;
        }
        .badge-cmd { background: #0369a1; color: #bae6fd; }
        .badge-mod { background: #065f46; color: #a7f3d0; }
        .badge-act { background: #92400e; color: #fde68a; }
        .badge-set { background: #581c87; color: #e9d5ff; }
        .cli-syntax {
          margin-left: auto;
          font-size: 0.66rem;
          color: #64748b;
          font-style: italic;
          flex-shrink: 0;
        }
      </style>

      <div class="designer-panel" style="width:100%;max-width:100%;">
        <!-- THANH TIÊU ĐỀ & TRẠNG THÁI REAL-TIME -->
        <div class="designer-title" style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;">
          <div style="display:flex;align-items:center;gap:10px;flex-wrap:wrap;">
            <span style="display:flex;align-items:center;gap:6px;font-weight:700;">
              <span class="live-dot" id="log-live-indicator"></span>
              <span>TERMINAL REAL-TIME LOGS</span>
            </span>
            <span id="log-sd-badge" style="padding:3px 9px;border-radius:12px;font-size:0.72rem;background:#064e3b;color:#34d399;font-weight:600;">💾 Thẻ nhớ: Đang kết nối...</span>
            <span id="log-stat-badge" style="padding:3px 9px;border-radius:12px;font-size:0.72rem;background:#1e293b;color:#94a3b8;font-weight:600;">0 dòng</span>
            <span id="log-err-badge" style="padding:3px 9px;border-radius:12px;font-size:0.72rem;background:#7f1d1d;color:#fca5a5;font-weight:600;display:none;">0 lỗi</span>
            <span id="log-fps-badge" style="padding:3px 9px;border-radius:12px;font-size:0.72rem;background:#1e1b4b;color:#a5b4fc;font-weight:600;">Độ trễ: --</span>
          </div>

          <!-- THANH ĐIỀU KHIỂN & CÔNG CỤ -->
          <div style="display:flex;gap:6px;align-items:center;flex-wrap:wrap;">
            <!-- Bộ chọn Theme Sci-Fi -->
            <select id="log-theme-select" onchange="window.setLogTerminalTheme(this.value)" style="background:#0f172a;color:#e2e8f0;border:1px solid #334155;border-radius:6px;padding:4px 8px;font-size:0.75rem;">
              <option value="cyberpunk">🔵 Cyberpunk Cyan</option>
              <option value="matrix">🟢 Matrix Green</option>
              <option value="pipboy">🟡 Fallout Pip-Boy</option>
            </select>

            <!-- Lọc Cấp Độ -->
            <select id="log-level-filter" onchange="window.fetchSystemLogs()" style="background:#0f172a;color:#e2e8f0;border:1px solid #334155;border-radius:6px;padding:4px 8px;font-size:0.75rem;">
              <option value="all">Tất cả cấp độ</option>
              <option value="info">INFO trở lên</option>
              <option value="warn">WARN trở lên</option>
              <option value="error">Chỉ ERROR</option>
            </select>

            <button class="btn-secondary" style="width:auto;padding:5px 10px;font-size:0.74rem;" onclick="window.takeLogSnapshot()">📸 Snapshot</button>
            <button class="btn-secondary" style="width:auto;padding:5px 10px;font-size:0.74rem;" onclick="window.downloadSysLogs()">📥 Tải File (.txt)</button>
            <button class="btn-secondary" style="width:auto;padding:5px 10px;font-size:0.74rem;" onclick="window.clearSysLogs()">🧹 Xóa</button>
            <button class="btn-secondary" style="width:auto;padding:5px 10px;font-size:0.74rem;" onclick="window.fetchSystemLogs()">🔄 Làm mới</button>
          </div>
        </div>

        <!-- THANH TÌM KIẾM TỪ KHÓA & TÙY CHỌN HIỂN THỊ -->
        <div style="display:flex;justify-content:space-between;align-items:center;margin-top:8px;margin-bottom:8px;font-size:0.75rem;color:#94a3b8;flex-wrap:wrap;gap:8px;">
          <!-- Hộp tìm kiếm tức thì -->
          <div style="display:flex;align-items:center;gap:6px;flex:1;min-width:240px;max-width:420px;">
            <input type="text" id="log-search-input" placeholder="🔍 Lọc nhanh (VD: WIFI, SHT31, SNAPSHOT, SD, ERROR)..." oninput="window.applyLogSearchFilter()" style="width:100%;background:#0f172a;border:1px solid #334155;color:#f8fafc;padding:5px 10px;border-radius:6px;font-size:0.76rem;" />
          </div>

          <div id="log-file-desc" style="color:#64748b;font-size:0.72rem;">Đang kết nối...</div>

          <div style="display:flex;gap:12px;align-items:center;">
            <label style="cursor:pointer;display:flex;align-items:center;gap:4px;">
              <input type="checkbox" id="log-scanlines" onchange="window.toggleCrtScanlines(this.checked)" checked style="accent-color:#38bdf8;"> CRT Scanlines
            </label>
            <label style="cursor:pointer;display:flex;align-items:center;gap:4px;">
              <input type="checkbox" id="log-auto-scroll" checked style="accent-color:#38bdf8;"> Tự động cuộn
            </label>
            <label style="cursor:pointer;display:flex;align-items:center;gap:4px;">
              <input type="checkbox" id="log-auto-refresh" checked style="accent-color:#38bdf8;"> Real-time (1.5s)
            </label>
          </div>
        </div>

        <!-- MÀN HÌNH TERMINAL DOS / SCI-FI CONSOLE CHÍNH -->
        <div id="log-terminal-wrap" class="terminal-theme-cyberpunk crt-scanlines" style="position:relative;width:100%;border-radius:8px;box-shadow:var(--term-glow);">
          <div id="log-terminal" style="width:100%;height:480px;background:var(--term-bg);border:1px solid var(--term-border);border-radius:8px;padding:14px;box-sizing:border-box;overflow-y:auto;overflow-x:auto;font-family:Consolas, 'Cascadia Code', 'Fira Code', Menlo, Monaco, monospace;font-size:0.76rem;line-height:1.45;color:var(--term-text);white-space:pre-wrap;box-shadow:inset 0 0 25px rgba(0,0,0,0.85);transition:background 0.3s, color 0.3s, border-color 0.3s;">
            <div style="color:#64748b;">Đang kết nối tới trạm Decor ESP32 và nạp nhật ký hoạt động...</div>
          </div>
        </div>

        <!-- THANH NHẬP LỆNH TƯƠNG TÁC (SPACE OS COMMAND PROMPT & INTELLISENSE) -->
        <div style="margin-top:10px;position:relative;">
          <!-- POPUP GỢI Ý TỰ ĐỘNG INTELLISENSE (CODE EDITOR STYLE) -->
          <div id="cli-suggest-popup" class="cli-suggest-popup"></div>

          <!-- INPUT KHUNG LỆNH -->
          <div style="display:flex;align-items:center;background:#090d16;border:1px solid var(--term-border, #0284c7);border-radius:8px;padding:4px 8px;gap:8px;box-shadow:inset 0 0 10px rgba(0,0,0,0.6);">
            <span style="font-family:Consolas, monospace;font-size:0.8rem;color:var(--term-accent, #38bdf8);font-weight:700;display:flex;align-items:center;gap:4px;user-select:none;flex-shrink:0;">
              <span style="color:#10b981;">root@spaceos</span><span style="color:#94a3b8;">:</span><span style="color:#38bdf8;">~#</span>
            </span>
            <input type="text" id="cli-input"
              placeholder="Nhập lệnh hoặc gõ 'help' (Gợi ý tự động như Code Editor, Enter để chạy)..."
              autocomplete="off" spellcheck="false"
              style="flex:1;background:transparent;border:none;outline:none;color:var(--term-text, #f8fafc);font-family:Consolas, 'Cascadia Code', monospace;font-size:0.82rem;padding:6px 0;"
              onkeydown="window.handleCliKeyDown(event)"
              oninput="window.handleCliInput()"
              onfocus="window.handleCliFocus()" />
            <button onclick="window.execCliInput()" class="btn-primary" style="width:auto;padding:6px 14px;font-size:0.75rem;background:linear-gradient(135deg, #0284c7, #0369a1);border:none;border-radius:6px;font-weight:600;display:flex;align-items:center;gap:4px;cursor:pointer;">
              <span>▶</span><span>Chạy</span>
            </button>
            <button onclick="window.clearCliTerminal()" class="btn-secondary" style="width:auto;padding:6px 10px;font-size:0.75rem;border-radius:6px;" title="Xóa màn hình Terminal">
              🧹 Xóa
            </button>
          </div>

          <!-- GỢI Ý PHÍM TẮT NHANH (QUICK CHIPS) -->
          <div style="display:flex;align-items:center;gap:6px;flex-wrap:wrap;margin-top:8px;">
            <span style="font-size:0.7rem;color:#64748b;font-weight:600;">Gợi ý nhanh:</span>
            <button class="cli-chip" onclick="window.runQuickCli('modules')">⚡ modules</button>
            <button class="cli-chip" onclick="window.runQuickCli('status sht31')">🌡️ status sht31</button>
            <button class="cli-chip" onclick="window.runQuickCli('status sd')">💾 status sd</button>
            <button class="cli-chip" onclick="window.runQuickCli('status mem')">🧠 status mem</button>
            <button class="cli-chip" onclick="window.runQuickCli('clean')">🧹 clean (RAM)</button>
            <button class="cli-chip" onclick="window.runQuickCli('tune nokia')">🎵 tune nokia</button>
            <button class="cli-chip" onclick="window.runQuickCli('snapshot')">📸 snapshot</button>
            <button class="cli-chip" onclick="window.runQuickCli('help')">❓ help</button>
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

  function escapeHtml(text) {
    if (!text) return '';
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  window.setLogTerminalTheme = function(themeName) {
    window.currentLogTheme = themeName;
    const wrap = document.getElementById('log-terminal-wrap');
    if (wrap) {
      wrap.classList.remove('terminal-theme-matrix', 'terminal-theme-cyberpunk', 'terminal-theme-pipboy');
      wrap.classList.add('terminal-theme-' + themeName);
    }
    const wrap3d = document.getElementById('hud-3d-terminal-wrap');
    if (wrap3d) {
      wrap3d.classList.remove('terminal-theme-matrix', 'terminal-theme-cyberpunk', 'terminal-theme-pipboy');
      wrap3d.classList.add('terminal-theme-' + themeName);
    }
    window.renderLogLines();
  };

  window.toggleCrtScanlines = function(enable) {
    const wrap = document.getElementById('log-terminal-wrap');
    if (wrap) {
      if (enable) wrap.classList.add('crt-scanlines');
      else wrap.classList.remove('crt-scanlines');
    }
  };

  window.formatSingleLogLine = function(lineStr) {
    if (!lineStr) return '';

    if (lineStr.includes('====')) {
      return '<div style="color:var(--term-accent);font-weight:700;margin:6px 0;opacity:0.95;text-shadow:0 0 8px var(--term-accent);">' +
             escapeHtml(lineStr) + '</div>';
    }

    let m = lineStr.match(/^(\S+(?:\s+\S+)?)\s+\[(D|I|W|E|F)\]\s+\[([A-Za-z0-9_]+)\]\s*(.*)$/);
    if (!m) {
      return '<div style="color:var(--term-text);opacity:0.9;">' + escapeHtml(lineStr) + '</div>';
    }

    let timePart = m[1];
    let lvlChar  = m[2];
    let tagPart  = m[3];
    let msgPart  = m[4];

    let lvlStyle = 'background:#1e293b;color:#94a3b8;';
    let lvlText = 'INFO';
    if (lvlChar === 'I') {
      lvlStyle = 'background:#034d65;color:#38bdf8;font-weight:600;';
      lvlText = 'INFO';
    } else if (lvlChar === 'W') {
      lvlStyle = 'background:#78350f;color:#fbbf24;font-weight:700;';
      lvlText = 'WARN';
    } else if (lvlChar === 'E' || lvlChar === 'F') {
      lvlStyle = 'background:#7f1d1d;color:#fca5a5;font-weight:700;box-shadow:0 0 6px #ef4444;';
      lvlText = (lvlChar === 'F') ? 'FATAL' : 'ERROR';
    } else if (lvlChar === 'D') {
      lvlStyle = 'background:#0f172a;color:#64748b;';
      lvlText = 'DEBUG';
    }

    let tagColor = '#94a3b8';
    if (tagPart === 'WIFI') tagColor = '#38bdf8';
    else if (tagPart === 'SD') tagColor = '#34d399';
    else if (tagPart === 'WEATHER') tagColor = '#facc15';
    else if (tagPart === 'SNAPSHOT') tagColor = '#c084fc';
    else if (tagPart === 'UI') tagColor = '#fb7185';
    else if (tagPart === 'MEM') tagColor = '#f87171';
    else if (tagPart === 'SYS') tagColor = '#60a5fa';

    let safeMsg = escapeHtml(msgPart);
    safeMsg = safeMsg.replace(/\b(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\b/g, '<span style="color:#60a5fa;text-decoration:underline;">$1</span>');
    safeMsg = safeMsg.replace(/\b(THANH CONG|OK)\b/g, '<span style="color:#34d399;font-weight:bold;">$1</span>');
    safeMsg = safeMsg.replace(/\b(THAT BAI|FAIL|FAILED|LOI)\b/g, '<span style="color:#f87171;font-weight:bold;">$1</span>');
    safeMsg = safeMsg.replace(/(\d+(?:\.\d+)?\s*(?:KB|MB|%|\*C|°C|V|FPS|km\/h))/gi, '<span style="color:var(--term-accent);font-weight:600;">$1</span>');

    return '<div style="display:flex;align-items:baseline;gap:8px;padding:2px 0;line-height:1.4;">' +
             '<span style="color:#64748b;font-size:0.70rem;flex-shrink:0;">' + escapeHtml(timePart) + '</span>' +
             '<span style="padding:1px 5px;border-radius:4px;font-size:0.65rem;flex-shrink:0;' + lvlStyle + '">' + lvlText + '</span>' +
             '<span style="color:' + tagColor + ';font-weight:700;font-size:0.72rem;flex-shrink:0;">[' + escapeHtml(tagPart) + ']</span>' +
             '<span style="color:var(--term-text);word-break:break-word;flex:1;">' + safeMsg + '</span>' +
           '</div>';
  };

  window.applyLogSearchFilter = function() {
    window.renderLogLines();
  };

  window.renderLogLines = function() {
    const term = document.getElementById('log-terminal');
    if (!term) return;

    const filterInput = document.getElementById('log-search-input');
    const keyword = filterInput ? filterInput.value.trim().toLowerCase() : '';

    let html = '';
    let displayedCount = 0;

    for (let i = 0; i < window.rawLogLines.length; i++) {
      let l = window.rawLogLines[i];
      if (keyword.length > 0 && !l.toLowerCase().includes(keyword)) {
        continue;
      }
      html += window.formatSingleLogLine(l);
      displayedCount++;
    }

    if (displayedCount === 0 && (!window.cliSessionOutputs || window.cliSessionOutputs.length === 0)) {
      if (keyword.length > 0) {
        html = '<div style="color:#64748b;padding:8px;">(Không tìm thấy dòng nhật ký nào chứa từ khóa "' + escapeHtml(keyword) + '")</div>';
      } else {
        html = '<div style="color:#64748b;padding:8px;">(Chưa có bản ghi nhật ký phù hợp)</div>';
      }
    }

    // HIỂN THỊ CÁC KHỐI LỆNH CLI ĐÃ THỰC THI (CLI SESSIONS & RESPONSES)
    if (window.cliSessionOutputs && window.cliSessionOutputs.length > 0) {
      html += '<div style="margin:12px 0 6px 0;border-top:1px dashed var(--term-border, #334155);opacity:0.6;"></div>';
      for (let cli of window.cliSessionOutputs) {
        let borderCol = cli.success ? 'var(--term-accent, #38bdf8)' : '#ef4444';
        html += '<div style="margin:8px 0;padding:6px 10px;background:rgba(15, 23, 42, 0.55);border-radius:6px;border-left:3px solid ' + borderCol + ';">' +
          '<div style="display:flex;align-items:center;gap:6px;font-weight:700;">' +
            '<span style="color:#64748b;font-size:0.7rem;">[' + escapeHtml(cli.time) + ']</span>' +
            '<span style="color:#10b981;">root@spaceos</span><span style="color:#94a3b8;">:</span><span style="color:#38bdf8;">~#</span>' +
            '<span style="color:#f8fafc;">' + escapeHtml(cli.cmd) + '</span>' +
            (cli.pending ? '<span style="color:#fbbf24;font-size:0.7rem;margin-left:auto;">⏳ Đang thực thi...</span>' : '') +
          '</div>' +
          (cli.output ? '<div style="margin-top:6px;white-space:pre-wrap;font-family:Consolas, monospace;font-size:0.74rem;line-height:1.45;color:' + (cli.success ? 'var(--term-text, #e2e8f0)' : '#fca5a5') + ';">' + escapeHtml(cli.output) + '</div>' : '') +
        '</div>';
      }
    }

    // CON TRỎ PROMPT CUỐI MÀN HÌNH TERMINAL
    html += '<div style="margin-top:8px;color:var(--term-accent);"><span style="color:#10b981;">root@spaceos</span><span style="color:#94a3b8;">:</span><span style="color:#38bdf8;">~# </span><span class="term-cursor">█</span></div>';

    term.innerHTML = html;

    const autoScroll = document.getElementById('log-auto-scroll');
    if (autoScroll && autoScroll.checked) {
      term.scrollTop = term.scrollHeight;
    }

    // Nếu có 3D terminal, đồng bộ luôn
    const term3d = document.getElementById('hud-3d-terminal-term');
    if (term3d) {
      let last3d = window.rawLogLines.slice(-60);
      let h3d = '';
      last3d.forEach(l => { h3d += window.formatSingleLogLine(l); });
      h3d += '<div style="color:var(--term-accent);"><span class="term-cursor">█</span></div>';
      term3d.innerHTML = h3d;
      term3d.scrollTop = term3d.scrollHeight;
    }
  };

  // ================================================================
  //          SPACE OS WEB CLI TERMINAL & INTELLISENSE ENGINE
  // ================================================================
  window.cliSessionOutputs = [];
  try {
    window.cliHistory = JSON.parse(localStorage.getItem('spaceos_cli_history') || '[]');
  } catch(e) {
    window.cliHistory = [];
  }
  window.cliHistoryIdx = -1;
  window.cliSuggestActiveIdx = -1;
  window.currentSuggestions = [];

  const CLI_COMMANDS = [
    { cmd: "modules", display: "modules", type: "CMD", badge: "badge-cmd", desc: "Liệt kê toàn bộ module ngoại vi và trạng thái hoạt động", syntax: "modules" },
    { cmd: "list", display: "list", type: "CMD", badge: "badge-cmd", desc: "Bí danh của 'modules' - Xem danh sách ngoại vi", syntax: "list" },
    { cmd: "status", display: "status <module>", type: "CMD", badge: "badge-cmd", desc: "Xem báo cáo chẩn đoán chi tiết một module cụ thể", syntax: "status [module]" },
    { cmd: "status sht31", display: "status sht31", type: "MOD", badge: "badge-mod", desc: "Cảm biến SHT31: Nhiệt độ (°C), độ ẩm (%), bus I2C & chân", syntax: "status sht31" },
    { cmd: "status sd", display: "status sd", type: "MOD", badge: "badge-mod", desc: "Thẻ nhớ SD: Dung lượng MB, tình trạng mount, cấu hình SPI", syntax: "status sd" },
    { cmd: "status audio", display: "status audio", type: "MOD", badge: "badge-mod", desc: "Âm thanh MAX98357A: Âm lượng loa, trình phát nhạc, bus I2S", syntax: "status audio" },
    { cmd: "status mic", display: "status mic", type: "MOD", badge: "badge-mod", desc: "Microphone INMP441: Tần số lấy mẫu 16kHz, mức âm thanh VU", syntax: "status mic" },
    { cmd: "status wifi", display: "status wifi", type: "MOD", badge: "badge-mod", desc: "Mạng WiFi: SSID, IP, Gateway, MAC, sóng RSSI dBm", syntax: "status wifi" },
    { cmd: "status mem", display: "status mem", type: "MOD", badge: "badge-mod", desc: "Bộ nhớ RAM: Heap tự do, PSRAM 8MB, chế độ dọn rác nghiêm ngặt", syntax: "status mem" },
    { cmd: "status motor", display: "status motor", type: "MOD", badge: "badge-mod", desc: "Động cơ cối xay gió: Tốc độ %, chế độ quay, chân cầu H L298N", syntax: "status motor" },
    { cmd: "status led", display: "status led", type: "MOD", badge: "badge-mod", desc: "Hệ thống đèn: Bóng Edison trái/phải, dải LED COB nước chảy", syntax: "status led" },
    { cmd: "status xiaozhi", display: "status xiaozhi", type: "MOD", badge: "badge-mod", desc: "Trợ lý ảo XiaoZhi AI: Kết nối WebSocket, hội thoại giọng nói", syntax: "status xiaozhi" },
    { cmd: "status display", display: "status display", type: "MOD", badge: "badge-mod", desc: "Màn hình ST7789: Chế độ hiển thị 0-19, độ sáng đèn nền", syntax: "status display" },
    { cmd: "status sys", display: "status sys", type: "MOD", badge: "badge-mod", desc: "Thông số ESP32-S3: Core rev, CPU 240MHz, bản phân vùng, Uptime", syntax: "status sys" },
    { cmd: "status profiler", display: "status profiler", type: "MOD", badge: "badge-mod", desc: "Giám sát hiệu năng: FPS, thời gian chu kỳ (µs), tác vụ CPU", syntax: "status profiler" },
    { cmd: "clean", display: "clean", type: "ACT", badge: "badge-act", desc: "Thu hồi & dọn rác bộ nhớ RAM ngay lập tức (MemoryScavenger)", syntax: "clean" },
    { cmd: "scavenge", display: "scavenge", type: "ACT", badge: "badge-act", desc: "Bí danh của 'clean' - Dọn dẹp bộ đệm giải phóng Heap/PSRAM", syntax: "scavenge" },
    { cmd: "strict", display: "strict <0|1|2>", type: "SET", badge: "badge-set", desc: "Cấu hình dọn RAM (0: Tắt, 1: Tiêu chuẩn, 2: Nghiêm ngặt)", syntax: "strict [0|1|2]" },
    { cmd: "strict 0", display: "strict 0", type: "SET", badge: "badge-set", desc: "Tắt chế độ dọn dẹp nghiêm ngặt", syntax: "strict 0" },
    { cmd: "strict 1", display: "strict 1", type: "SET", badge: "badge-set", desc: "Bật chế độ dọn RAM TIÊU CHUẨN (Dọn sau 120-180s nhàn rỗi)", syntax: "strict 1" },
    { cmd: "strict 2", display: "strict 2", type: "SET", badge: "badge-set", desc: "Bật chế độ dọn RAM NGHIÊM NGẶT (Dọn sau 30-45s / Sleep)", syntax: "strict 2" },
    { cmd: "vol", display: "vol <0-100>", type: "SET", badge: "badge-set", desc: "Cài đặt âm lượng loa chính MAX98357A (0 - 100%)", syntax: "vol [0-100]" },
    { cmd: "vol 70", display: "vol 70", type: "SET", badge: "badge-set", desc: "Đặt âm lượng loa ở mức 70%", syntax: "vol 70" },
    { cmd: "beep", display: "beep [freq] [ms]", type: "ACT", badge: "badge-act", desc: "Phát âm thanh kiểm tra loa (Ví dụ: beep 1000 200)", syntax: "beep [f] [ms]" },
    { cmd: "tune nokia", display: "tune nokia", type: "ACT", badge: "badge-act", desc: "Phát nhạc chuông huyền thoại Nokia Tune (Grande Valse)", syntax: "tune nokia" },
    { cmd: "tune ok", display: "tune ok", type: "ACT", badge: "badge-act", desc: "Phát âm thanh báo thành công / OK Chime", syntax: "tune ok" },
    { cmd: "tune del", display: "tune del", type: "ACT", badge: "badge-act", desc: "Phát âm thanh cảnh báo / Delete Chime", syntax: "tune del" },
    { cmd: "tune sms", display: "tune sms", type: "ACT", badge: "badge-act", desc: "Phát âm báo tin nhắn Morse SMS kinh điển (... -- ...)", syntax: "tune sms" },
    { cmd: "toast", display: "toast <nội_dung>", type: "ACT", badge: "badge-act", desc: "Hiện thông báo Toast nổi lên màn hình LCD", syntax: "toast [text]" },
    { cmd: "mode", display: "mode <0-19>", type: "SET", badge: "badge-set", desc: "Chuyển giao diện màn hình (0: Chờ, 1: Đồng hồ tròn, 11: AI...)", syntax: "mode [0-19]" },
    { cmd: "motor", display: "motor <0-100|off>", type: "SET", badge: "badge-set", desc: "Điều khiển tốc độ động cơ cối xay gió", syntax: "motor [0-100|off]" },
    { cmd: "motor off", display: "motor off", type: "SET", badge: "badge-set", desc: "Dừng quay động cơ cối xay gió", syntax: "motor off" },
    { cmd: "motor 50", display: "motor 50", type: "SET", badge: "badge-set", desc: "Quay cối xay gió tốc độ 50%", syntax: "motor 50" },
    { cmd: "led left 80", display: "led left 80", type: "SET", badge: "badge-set", desc: "Đặt độ sáng bóng Edison Trái 80%", syntax: "led left [0-100]" },
    { cmd: "led right 80", display: "led right 80", type: "SET", badge: "badge-set", desc: "Đặt độ sáng bóng Edison Phải 80%", syntax: "led right [0-100]" },
    { cmd: "led cob next", display: "led cob next", type: "ACT", badge: "badge-act", desc: "Chuyển chế độ LED COB nước chảy kế tiếp", syntax: "led cob next" },
    { cmd: "snapshot", display: "snapshot", type: "ACT", badge: "badge-act", desc: "Ghi bản chụp nhật ký & phần cứng ra thẻ nhớ SD", syntax: "snapshot" },
    { cmd: "clear", display: "clear", type: "CMD", badge: "badge-cmd", desc: "Xóa sạch nội dung hiển thị trên màn hình Terminal Web", syntax: "clear" },
    { cmd: "reboot", display: "reboot", type: "CMD", badge: "badge-cmd", desc: "Khởi động lại vi điều khiển ESP32-S3 từ xa", syntax: "reboot" },
    { cmd: "factory-reset", display: "factory-reset", type: "ACT", badge: "badge-act", desc: "Khôi phục cài đặt gốc ban đầu (Theme Mèo Chibi và admin/admin)", syntax: "factory-reset" },
    { cmd: "help", display: "help", type: "CMD", badge: "badge-cmd", desc: "Xem hướng dẫn chi tiết và danh mục toàn bộ lệnh CLI", syntax: "help" }
  ];

  window.extractCliPrefix = function(val) {
    if (!val) return "";
    let m = val.match(/^(?:sudo|spaceos|space|decor)\s+/i);
    return m ? m[0] : "";
  };

  window.handleCliInput = function() {
    const input = document.getElementById('cli-input');
    if (!input) return;
    const val = input.value;
    const prefix = window.extractCliPrefix(val);
    const query = val.substring(prefix.length).trimStart();

    if (query.length === 0) {
      window.currentSuggestions = CLI_COMMANDS.slice(0, 8);
    } else {
      const q = query.toLowerCase();
      window.currentSuggestions = CLI_COMMANDS.filter(function(item) {
        return item.cmd.toLowerCase().startsWith(q) || item.cmd.toLowerCase().includes(q) || item.display.toLowerCase().includes(q);
      });
    }
    window.cliSuggestActiveIdx = window.currentSuggestions.length > 0 ? 0 : -1;
    window.renderCliSuggestions(window.currentSuggestions, prefix, query);
  };

  window.handleCliFocus = function() {
    window.handleCliInput();
  };

  window.renderCliSuggestions = function(list, prefix, query) {
    const popup = document.getElementById('cli-suggest-popup');
    if (!popup) return;

    if (!list || list.length === 0) {
      popup.style.display = 'none';
      popup.innerHTML = '';
      return;
    }

    let html = '';
    for (let idx = 0; idx < list.length; idx++) {
      let item = list[idx];
      let isSel = (idx === window.cliSuggestActiveIdx);
      let selClass = isSel ? 'selected' : '';
      let disp = escapeHtml(item.display);
      if (query && query.length > 0) {
        let regex = new RegExp('(' + query.replace(/[-\/\\^$*+?.()|[\]{}]/g, '\\$&') + ')', 'gi');
        disp = disp.replace(regex, '<span style="color:var(--term-accent, #38bdf8);font-weight:700;text-decoration:underline;">$1</span>');
      }
      html += '<div class="cli-item ' + selClass + '" onmousedown="window.selectCliSuggestion(' + idx + ', event)">' +
        '<span class="cli-badge ' + item.badge + '">' + item.type + '</span>' +
        '<span style="font-weight:600;color:#f8fafc;">' + disp + '</span>' +
        '<span style="color:#94a3b8;font-size:0.7rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:320px;">' + escapeHtml(item.desc) + '</span>' +
        '<span class="cli-syntax">' + escapeHtml(item.syntax) + '</span>' +
      '</div>';
    }

    popup.innerHTML = html;
    popup.style.display = 'block';

    const selEl = popup.querySelector('.cli-item.selected');
    if (selEl) {
      selEl.scrollIntoView({ block: 'nearest' });
    }
  };

  window.selectCliSuggestion = function(idx, evt) {
    if (evt) evt.preventDefault();
    if (idx < 0 || idx >= window.currentSuggestions.length) return;
    const item = window.currentSuggestions[idx];
    const input = document.getElementById('cli-input');
    if (!input) return;

    const prefix = window.extractCliPrefix(input.value);
    input.value = prefix + item.cmd;

    const popup = document.getElementById('cli-suggest-popup');
    if (popup) popup.style.display = 'none';
    window.cliSuggestActiveIdx = -1;

    // Nếu lệnh là lệnh có đối số cần nhập thêm thì thêm khoảng trắng và mở lại gợi ý
    if (item.cmd === 'status' || item.cmd === 'strict' || item.cmd === 'vol' || item.cmd === 'mode' || item.cmd === 'toast' || item.cmd === 'beep') {
      input.value = input.value + ' ';
      input.focus();
      window.handleCliInput();
    } else {
      input.focus();
      // Nếu là thao tác nhấp chuột (click) vào 1 lệnh hoàn chỉnh (không phải gõ phím), thực thi luôn!
      if (evt && evt.type === 'mousedown') {
        window.execCliInput();
      }
    }
  };

  window.handleCliKeyDown = function(e) {
    const popup = document.getElementById('cli-suggest-popup');
    const isPopupOpen = popup && popup.style.display === 'block';

    if (isPopupOpen) {
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        if (window.currentSuggestions.length > 0) {
          window.cliSuggestActiveIdx = (window.cliSuggestActiveIdx + 1) % window.currentSuggestions.length;
          const prefix = window.extractCliPrefix(e.target.value);
          const query = e.target.value.substring(prefix.length).trimStart();
          window.renderCliSuggestions(window.currentSuggestions, prefix, query);
        }
        return;
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault();
        if (window.currentSuggestions.length > 0) {
          window.cliSuggestActiveIdx = (window.cliSuggestActiveIdx - 1 + window.currentSuggestions.length) % window.currentSuggestions.length;
          const prefix = window.extractCliPrefix(e.target.value);
          const query = e.target.value.substring(prefix.length).trimStart();
          window.renderCliSuggestions(window.currentSuggestions, prefix, query);
        }
        return;
      }
      if (e.key === 'Tab') {
        e.preventDefault();
        if (window.cliSuggestActiveIdx >= 0 && window.cliSuggestActiveIdx < window.currentSuggestions.length) {
          window.selectCliSuggestion(window.cliSuggestActiveIdx);
        }
        return;
      }
      if (e.key === 'Escape') {
        e.preventDefault();
        popup.style.display = 'none';
        window.cliSuggestActiveIdx = -1;
        return;
      }
      if (e.key === 'Enter') {
        if (window.cliSuggestActiveIdx >= 0 && window.cliSuggestActiveIdx < window.currentSuggestions.length) {
          const item = window.currentSuggestions[window.cliSuggestActiveIdx];
          const inputVal = e.target.value.trim();
          const prefix = window.extractCliPrefix(inputVal);
          const rawQ = inputVal.substring(prefix.length).trim();
          if (rawQ !== item.cmd) {
            e.preventDefault();
            window.selectCliSuggestion(window.cliSuggestActiveIdx);
            return;
          }
        }
      }
    } else {
      if (e.key === 'ArrowUp') {
        e.preventDefault();
        if (window.cliHistory && window.cliHistory.length > 0) {
          if (window.cliHistoryIdx === -1) window.cliHistoryIdx = window.cliHistory.length - 1;
          else if (window.cliHistoryIdx > 0) window.cliHistoryIdx--;
          e.target.value = window.cliHistory[window.cliHistoryIdx];
        }
        return;
      }
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        if (window.cliHistory && window.cliHistory.length > 0 && window.cliHistoryIdx >= 0) {
          if (window.cliHistoryIdx < window.cliHistory.length - 1) {
            window.cliHistoryIdx++;
            e.target.value = window.cliHistory[window.cliHistoryIdx];
          } else {
            window.cliHistoryIdx = -1;
            e.target.value = '';
          }
        }
        return;
      }
    }

    if (e.key === 'Enter') {
      e.preventDefault();
      window.execCliInput();
    }
  };

  document.addEventListener('click', function(e) {
    const popup = document.getElementById('cli-suggest-popup');
    const input = document.getElementById('cli-input');
    if (popup && popup.style.display === 'block') {
      if (!popup.contains(e.target) && e.target !== input) {
        popup.style.display = 'none';
        window.cliSuggestActiveIdx = -1;
      }
    }
  });

  window.execCliInput = async function() {
    const input = document.getElementById('cli-input');
    if (!input) return;
    const cmd = input.value.trim();
    if (cmd.length === 0) return;

    if (!window.cliHistory) window.cliHistory = [];
    if (!window.cliHistory.includes(cmd)) {
      window.cliHistory.push(cmd);
      if (window.cliHistory.length > 50) window.cliHistory.shift();
      try { localStorage.setItem('spaceos_cli_history', JSON.stringify(window.cliHistory)); } catch(e){}
    }
    window.cliHistoryIdx = -1;

    const popup = document.getElementById('cli-suggest-popup');
    if (popup) popup.style.display = 'none';
    window.cliSuggestActiveIdx = -1;

    await window.runQuickCli(cmd);
  };

  window.runQuickCli = async function(cmdStr) {
    if (!cmdStr || cmdStr.trim().length === 0) return;
    const cmd = cmdStr.trim();

    // Cập nhật lên ô nhập lệnh để người dùng quan sát trực quan
    const input = document.getElementById('cli-input');
    if (input) input.value = cmd;

    const popup = document.getElementById('cli-suggest-popup');
    if (popup) popup.style.display = 'none';
    window.cliSuggestActiveIdx = -1;

    // Thêm bản ghi pending vào terminal
    const timeStr = new Date().toLocaleTimeString();
    const entry = { cmd: cmd, output: "Đang truyền tải lệnh tới trạm ESP32-S3...", pending: true, time: timeStr, success: true };
    if (!window.cliSessionOutputs) window.cliSessionOutputs = [];
    window.cliSessionOutputs.push(entry);
    window.renderLogLines();

    try {
      let res = await fetch('/api/terminal/exec', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd: cmd })
      });
      let data = await res.json();

      if (data.clear) {
        window.cliSessionOutputs = [];
        window.rawLogLines = [];
        window.renderLogLines();
        return;
      }

      entry.output = data.output || '(Không có kết xuất)';
      entry.pending = false;
      entry.success = (data.success !== false);
      window.renderLogLines();
    } catch (err) {
      entry.output = 'Lỗi kết nối Web Server: ' + err.message;
      entry.pending = false;
      entry.success = false;
      window.renderLogLines();
    }
  };

  window.clearCliTerminal = function() {
    window.cliSessionOutputs = [];
    window.rawLogLines = [];
    window.renderLogLines();
  };

  window.downloadSysLogs = function() {
    window.open('/api/logs/download', '_blank');
  };

  window.takeLogSnapshot = async function() {
    try {
      await fetch('/api/logs/snapshot', { method: 'POST' });
      setTimeout(window.fetchSystemLogs, 300);
    } catch(e) { console.error(e); }
  };

  window.clearSysLogs = async function() {
    if (!confirm("Bạn có chắc chắn muốn xóa bộ đệm nhật ký hệ thống không?")) return;
    try {
      await fetch('/api/logs/clear', { method: 'POST' });
      window.rawLogLines = [];
      window.renderLogLines();
      setTimeout(window.fetchSystemLogs, 400);
    } catch(e) { console.error(e); }
  };

  window.fetchSystemLogs = async function() {
    try {
      let lvl = document.getElementById('log-level-filter') ? document.getElementById('log-level-filter').value : 'all';
      let t0 = performance.now();
      let [resLogs, resStats] = await Promise.all([
        fetch('/api/logs?format=json&limit=150&level=' + lvl),
        fetch('/api/logs/stats')
      ]);
      let t1 = performance.now();

      let logData = await resLogs.json();
      let statData = await resStats.json();

      const sdB = document.getElementById('log-sd-badge');
      if (sdB) {
        if (statData.sd_active) {
          sdB.style.background = '#064e3b';
          sdB.style.color = '#34d399';
          sdB.textContent = '💾 Thẻ SD: Đang ghi (' + (statData.written_kb || 0) + ' KB)';
        } else if (statData.sd_ready) {
          sdB.style.background = '#1e3a8a';
          sdB.style.color = '#60a5fa';
          sdB.textContent = '💾 Thẻ SD: Sẵn sàng';
        } else {
          sdB.style.background = '#78350f';
          sdB.style.color = '#fbbf24';
          sdB.textContent = '⚠️ Thẻ SD: Chưa cắm (Đệm PSRAM)';
        }
      }

      const statB = document.getElementById('log-stat-badge');
      if (statB) statB.textContent = (statData.total || 0) + ' dòng';

      const errB = document.getElementById('log-err-badge');
      if (errB) {
        let errCount = (statData.error || 0) + (statData.fatal || 0);
        if (errCount > 0) {
          errB.style.display = 'inline-block';
          errB.textContent = errCount + ' lỗi';
        } else {
          errB.style.display = 'none';
        }
      }

      const fpsB = document.getElementById('log-fps-badge');
      if (fpsB) {
        fpsB.textContent = 'Trễ: ' + Math.round(t1 - t0) + 'ms';
      }

      const desc = document.getElementById('log-file-desc');
      if (desc && statData.current_file) {
        desc.textContent = 'Tệp: ' + statData.current_file + ' | Chờ ghi: ' + (statData.pending_bytes || 0) + ' B';
      }

      if (logData.logs && Array.isArray(logData.logs)) {
        window.rawLogLines = logData.logs.map(item => item.line || '');
        window.renderLogLines();
      }
    } catch(e) {
      console.error("fetchSystemLogs error:", e);
    }
  };

  // HÀM TÁI SỬ DỤNG CHO 3D COMMAND CENTER HUD (Dựng màn hình Terminal bên cạnh Model 3D)
  window.createSciFiLogTerminal = function(targetContainerId, options = {}) {
    const container = document.getElementById(targetContainerId);
    if (!container) return;

    let theme = options.theme || window.currentLogTheme || 'matrix';
    let height = options.height || '400px';

    container.innerHTML = `
      <div id="${targetContainerId}-wrap" class="terminal-theme-${theme} crt-scanlines" style="position:relative;width:100%;height:${height};border-radius:6px;box-shadow:var(--term-glow);">
        <div id="${targetContainerId}-term" style="width:100%;height:100%;background:var(--term-bg);border:1px solid var(--term-border);border-radius:6px;padding:10px;box-sizing:border-box;overflow-y:auto;overflow-x:auto;font-family:Consolas, monospace;font-size:0.74rem;line-height:1.4;color:var(--term-text);white-space:pre-wrap;">
        </div>
      </div>
    `;

    if (window.rawLogLines && window.rawLogLines.length > 0) {
      const term3d = document.getElementById(targetContainerId + '-term');
      if (term3d) {
        let last3d = window.rawLogLines.slice(-60);
        let h3d = '';
        last3d.forEach(l => { h3d += window.formatSingleLogLine(l); });
        h3d += '<div style="color:var(--term-accent);"><span class="term-cursor">█</span></div>';
        term3d.innerHTML = h3d;
        term3d.scrollTop = term3d.scrollHeight;
      }
    }
  };

  // Mở rộng hàm switchTab để hỗ trợ cả tab 'music', 'system' và 'logs'
  const origSwitchTab = window.switchTab;
  window.switchTab = function(tabId) {
    injectMusicTabAndPage();
    injectSystemTabAndPage();
    injectLogsTabAndPage();
    if (tabId === 'music') {
      if (logPollTimer) { clearInterval(logPollTimer); logPollTimer = null; }
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
      if (logPollTimer) { clearInterval(logPollTimer); logPollTimer = null; }
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      const sSec = document.getElementById('tab-system');
      const sBtn = document.getElementById('tab-btn-system');
      if (sSec) sSec.classList.add('active');
      if (sBtn) sBtn.classList.add('active');
      window.loadSystemInfo();
      return;
    }
    if (tabId === 'logs') {
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      const lSec = document.getElementById('tab-logs');
      const lBtn = document.getElementById('tab-btn-logs');
      if (lSec) lSec.classList.add('active');
      if (lBtn) lBtn.classList.add('active');
      window.fetchSystemLogs();
      if (!logPollTimer) {
        logPollTimer = setInterval(function() {
          const autoRef = document.getElementById('log-auto-refresh');
          if (autoRef && autoRef.checked && document.getElementById('tab-logs') && document.getElementById('tab-logs').classList.contains('active')) {
            window.fetchSystemLogs();
          }
        }, 1500);
      }
      return;
    }
    if (logPollTimer) { clearInterval(logPollTimer); logPollTimer = null; }
    const mSec = document.getElementById('tab-music');
    const mBtn = document.getElementById('tab-btn-music');
    if (mSec) mSec.classList.remove('active');
    if (mBtn) mBtn.classList.remove('active');
    const sSec = document.getElementById('tab-system');
    const sBtn = document.getElementById('tab-btn-system');
    if (sSec) sSec.classList.remove('active');
    if (sBtn) sBtn.classList.remove('active');
    const lSec = document.getElementById('tab-logs');
    const lBtn = document.getElementById('tab-btn-logs');
    if (lSec) lSec.classList.remove('active');
    if (lBtn) lBtn.classList.remove('active');
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
      if (el('sys-fw-ver')) el('sys-fw-ver').innerText = 'Phiên bản: ' + (d.firmware_version || '--');
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
        let minH = d.min_free_heap_bytes ? ` (Đáy: ${(d.min_free_heap_bytes/1024).toFixed(0)}KB)` : '';
        let loopStr = d.loop_fps ? ` • Vòng lặp: ${Number(d.loop_fps).toFixed(0)} FPS` : '';
        el('sys-heap').innerText = `Heap: ${fH}/${tH}KB${minH}${loopStr}`;
      }
      if (el('sys-psram')) {
        let fP = d.free_psram_bytes ? (d.free_psram_bytes/(1024*1024)).toFixed(1) : (d.free_psram_mb ? Number(d.free_psram_mb).toFixed(1) : '--');
        let tP = d.total_psram_bytes ? (d.total_psram_bytes/(1024*1024)).toFixed(0) : '8';
        let uP = Math.max(0, Number(tP) - Number(fP)).toFixed(1);
        let minP = d.min_free_psram_bytes ? ` (Đáy: ${(d.min_free_psram_bytes/(1024*1024)).toFixed(1)}MB)` : '';
        el('sys-psram').innerText = `PSRAM: Trống ${fP}/${tP}MB (Dùng ~${uP}M)${minP}`;
      }
      if (el('sys-wifi')) el('sys-wifi').innerText = `${d.wifi_ssid || d.current_ssid || 'Chưa nối'} (${d.wifi_rssi || 0} dBm)`;
      if (el('sys-ip')) el('sys-ip').innerText = 'IP: ' + (d.wifi_ip || d.ip || '--');
      const up = d.uptime_seconds || 0;
      const upH = Math.floor(up / 3600);
      const upM = Math.floor((up % 3600) / 60);
      const upS = up % 60;
      if (el('sys-uptime')) el('sys-uptime').innerText = `${upH}h ${upM}m ${upS}s`;
      if (typeof window.fetchMemoryStatus === 'function') {
        window.fetchMemoryStatus();
      }
    } catch (e) {
      console.error(e);
    }
  };

  // Lấy trạng thái Memory Scavenger & Bộ đệm
  window.fetchMemoryStatus = async function() {
    try {
      const res = await fetch('/api/memory/status');
      if (!res.ok) return;
      const d = await res.json();
      const el = id => document.getElementById(id);
      if (el('mem-strict-select') && document.activeElement !== el('mem-strict-select')) {
        el('mem-strict-select').value = String(d.strict_mode !== undefined ? d.strict_mode : 1);
      }
      if (el('mem-last-freed')) {
        if (d.last_scavenge && d.last_scavenge.freed_kb > 0) {
          el('mem-last-freed').innerText = `+${d.last_scavenge.freed_kb} KB`;
        } else {
          el('mem-last-freed').innerText = `0 KB (Tối ưu)`;
        }
      }
      if (el('mem-last-reason')) {
        el('mem-last-reason').innerText = `Lý do: ${d.last_scavenge ? d.last_scavenge.reason : 'Chưa chạy'}`;
      }
      if (el('mem-total-freed')) {
        el('mem-total-freed').innerText = `${d.cumulative_freed_kb || 0} KB`;
      }
      if (el('mem-total-runs')) {
        el('mem-total-runs').innerText = `Số lần dọn: ${d.total_scavenge_runs || 0} lần`;
      }
      if (el('mem-buffers-badge')) {
        const audioBadge = d.audio_has_buf ? '<span style="color:#f59e0b;">● Rec 256K</span>' : '<span style="color:#10b981;">○ Rec Trống</span>';
        const streamBadge = d.stream_has_buf ? '<span style="color:#f59e0b;">● Stream 448K</span>' : '<span style="color:#10b981;">○ Stream Trống</span>';
        const uploadBadge = d.upload_has_buf ? '<span style="color:#f59e0b;">● Upload 64K</span>' : '<span style="color:#10b981;">○ Upload Trống</span>';
        const xzBadge = d.xiaozhi_has_buf ? '<span style="color:#f59e0b;">● Opus 25K</span>' : '<span style="color:#10b981;">○ Opus Trống</span>';
        el('mem-buffers-badge').innerHTML = `${audioBadge} • ${streamBadge}<br>${uploadBadge} • ${xzBadge}`;
      }
    } catch (e) {
      console.warn("fetchMemoryStatus:", e);
    }
  };

  // Kích hoạt Dọn RAM thủ công
  window.triggerMemoryScavenge = async function() {
    try {
      const res = await fetch('/api/memory/scavenge', { method: 'POST' });
      if (!res.ok) return;
      const d = await res.json();
      alert(`🧹 Dọn RAM thành công!\nĐã thu hồi: ${d.freed_kb} KB\nHeap hiện tại: ${(d.free_heap/1024).toFixed(0)} KB\nPSRAM trống: ${(d.free_psram/(1024*1024)).toFixed(1)} MB`);
      window.fetchMemoryStatus();
      window.loadSystemInfo();
    } catch (e) {
      alert("Lỗi gọi Dọn RAM: " + e.message);
    }
  };

  // Cấu hình Chế độ Nghiêm ngặt
  window.setMemoryStrictMode = async function(val) {
    try {
      await fetch('/api/memory/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ strict_mode: parseInt(val) })
      });
      window.fetchMemoryStatus();
    } catch (e) {
      console.warn("setMemoryStrictMode error:", e);
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

  // Bắt đầu cập nhật Cloud OTA với thanh tiến trình trực quan thời gian thực
  window.startCloudOtaNow = async function() {
    if (!window.cloudOtaBinUrl) return;
    if (!confirm('Xác nhận tiến hành nâng cấp firmware từ xa?\\nTrong quá trình nạp, vui lòng KHÔNG ngắt nguồn điện của thiết bị.')) return;
    const box = document.getElementById('ota-check-result');
    if (box) {
      box.innerHTML = `
        <div style="border:1px solid #0284c7;background:#032b43;padding:16px;border-radius:8px;">
          <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
            <div style="font-weight:700;font-size:0.95rem;color:#38bdf8;">🚀 TIẾN TRÌNH CẬP NHẬT TỪ XA (OTA)</div>
            <div id="cloud-ota-pct" style="font-size:1.1rem;font-weight:700;color:#10b981;">0%</div>
          </div>
          <div style="width:100%;height:14px;background:#0f172a;border-radius:6px;overflow:hidden;border:1px solid #1e293b;">
            <div id="cloud-ota-bar" style="width:0%;height:100%;background:linear-gradient(90deg,#0284c7,#38bdf8);transition:width 0.3s;box-shadow:0 0 10px #38bdf8;"></div>
          </div>
          <div id="cloud-ota-status" style="margin-top:8px;font-size:0.82rem;color:#94a3b8;">Đang kết nối máy chủ GitHub & tải firmware...</div>
          <div style="margin-top:6px;font-size:0.75rem;color:#f59e0b;">⚠️ Màn hình thiết bị và trang web đang đồng bộ tiến trình. Vui lòng giữ nguyên nguồn điện!</div>
        </div>
      `;
    }

    try {
      const res = await fetch('/api/ota_start', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ bin_url: window.cloudOtaBinUrl })
      });
      if (!res.ok) throw new Error('HTTP ' + res.status);

      const pBar = document.getElementById('cloud-ota-bar');
      const pPct = document.getElementById('cloud-ota-pct');
      const pSt  = document.getElementById('cloud-ota-status');

      let pollInterval = setInterval(async () => {
        try {
          const sRes = await fetch('/api/ota_status');
          if (sRes.ok) {
            const sData = await sRes.json();
            const pct = sData.progress || 0;
            if (pBar) pBar.style.width = pct + '%';
            if (pPct) pPct.innerText = pct + '%';
            if (pSt && sData.status) pSt.innerText = sData.status;

            if (pct >= 100) {
              clearInterval(pollInterval);
              if (pBar) pBar.style.background = '#10b981';
              if (pSt)  pSt.innerHTML = '<span style="color:#10b981;font-weight:700;">✅ NẠP HOÀN TẤT 100%! Thiết bị đang khởi động lại...</span>';
              setTimeout(() => { window.location.reload(); }, 6000);
            }
          }
        } catch (err) {
          console.log('Polling OTA:', err);
        }
      }, 500);

    } catch (e) {
      if (box) box.innerHTML += `<div style="color:#ef4444;font-size:0.82rem;margin-top:8px;">❌ Lỗi khởi động OTA: ${e.message}</div>`;
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
    xhr.open('POST', '/update?size=' + file.size, true);

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
    injectLogsTabAndPage();
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

// ---------------- INJECT SCRIPT: ĐỒNG BỘ TRÒ CHUYỆN XIAOZHI AI & NÚT MICRO KÍCH HOẠT PHẦN CỨNG ----------------
static const char XIAOZHI_CHAT_INJECT_SCRIPT[] PROGMEM = R"rawliteral(
<style>
/* VOICE INTERACTION DOCK - TRÒ CHUYỆN XIAOZHI AI */
.voice-dock {
  background: linear-gradient(180deg, rgba(15, 23, 42, 0.92) 0%, rgba(10, 15, 29, 0.98) 100%);
  border: 1px solid #1e293b;
  border-top: 1px solid rgba(56, 189, 248, 0.35);
  border-radius: 14px;
  padding: 14px 16px;
  margin-top: 12px;
  box-shadow: 0 -4px 20px rgba(0, 0, 0, 0.4);
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.voice-status-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 8px;
  padding-bottom: 8px;
  border-bottom: 1px dashed rgba(255, 255, 255, 0.08);
}
.voice-status-info {
  display: flex;
  align-items: center;
  gap: 8px;
}
.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #10b981;
  box-shadow: 0 0 8px #10b981;
  transition: all 0.25s ease;
}
.status-dot.listening {
  background: #ef4444;
  box-shadow: 0 0 12px #ef4444;
  animation: pulse-dot 0.8s infinite alternate;
}
.status-dot.thinking {
  background: #f59e0b;
  box-shadow: 0 0 12px #f59e0b;
  animation: pulse-dot 0.6s infinite alternate;
}
.status-dot.replying {
  background: #00e5ff;
  box-shadow: 0 0 12px #00e5ff;
  animation: pulse-dot 0.8s infinite alternate;
}
@keyframes pulse-dot {
  0% { transform: scale(0.85); opacity: 0.6; }
  100% { transform: scale(1.35); opacity: 1; }
}
.status-label {
  font-size: 0.78rem;
  font-weight: 700;
  letter-spacing: 0.5px;
  color: #e2e8f0;
  text-transform: uppercase;
}
.voice-vu-wrap {
  display: flex;
  align-items: center;
  gap: 6px;
  background: rgba(15, 23, 42, 0.65);
  padding: 3px 8px;
  border-radius: 20px;
  border: 1px solid rgba(255, 255, 255, 0.08);
}
.vu-label {
  font-size: 0.68rem;
  font-weight: 800;
  color: #38bdf8;
}
.vu-bar-track {
  width: 70px;
  height: 6px;
  background: #0f172a;
  border-radius: 3px;
  overflow: hidden;
}
.vu-bar-fill {
  height: 100%;
  background: linear-gradient(90deg, #10b981, #f59e0b, #ef4444);
  width: 0%;
  transition: width 0.08s ease;
}
.vu-val {
  font-size: 0.68rem;
  font-family: monospace;
  color: #94a3b8;
  width: 28px;
  text-align: right;
}
.voice-vol-dock {
  display: flex;
  align-items: center;
  gap: 5px;
  background: rgba(15, 23, 42, 0.7);
  padding: 3px 8px;
  border-radius: 20px;
  border: 1px solid rgba(255, 255, 255, 0.08);
}
.vol-btn {
  background: #1e293b;
  color: #e2e8f0;
  border: 1px solid #334155;
  border-radius: 4px;
  width: 22px;
  height: 20px;
  font-size: 0.75rem;
  font-weight: bold;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
}
.vol-btn:hover {
  background: #38bdf8;
  color: #0f172a;
}
.vol-val {
  font-size: 0.72rem;
  font-family: monospace;
  font-weight: 700;
  color: #38bdf8;
  min-width: 32px;
  text-align: center;
}
.mic-button-zone {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 8px 0;
  position: relative;
}
.xz-mic-button {
  width: 78px;
  height: 78px;
  border-radius: 50%;
  border: 2px solid #38bdf8;
  background: radial-gradient(circle, #1e293b 0%, #0b1120 100%);
  color: #38bdf8;
  cursor: pointer;
  outline: none;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  transition: all 0.25s ease;
  box-shadow: 0 0 20px rgba(56, 189, 248, 0.25);
}
.xz-mic-button:hover {
  transform: scale(1.05);
  box-shadow: 0 0 30px rgba(56, 189, 248, 0.5);
  border-color: #7dd3fc;
}
.mic-wave-ring {
  position: absolute;
  top: 50%;
  left: 50%;
  width: 100%;
  height: 100%;
  border-radius: 50%;
  transform: translate(-50%, -50%);
  pointer-events: none;
  opacity: 0;
}
.xz-mic-button.listening {
  border-color: #ef4444;
  color: #ffffff;
  background: radial-gradient(circle, #b91c1c 0%, #450a0a 100%);
  box-shadow: 0 0 32px rgba(239, 68, 68, 0.65);
  animation: pulse-mic 1.1s infinite alternate;
}
.xz-mic-button.listening .mic-wave-ring.r1 {
  border: 2px solid rgba(239, 68, 68, 0.7);
  animation: ripple 1.6s infinite;
}
.xz-mic-button.listening .mic-wave-ring.r2 {
  border: 2px solid rgba(239, 68, 68, 0.45);
  animation: ripple 1.6s infinite 0.5s;
}
.xz-mic-button.listening .mic-wave-ring.r3 {
  border: 2px solid rgba(239, 68, 68, 0.2);
  animation: ripple 1.6s infinite 1.0s;
}
.xz-mic-button.thinking {
  border-color: #f59e0b;
  color: #f59e0b;
  background: radial-gradient(circle, #78350f 0%, #1c1917 100%);
  box-shadow: 0 0 25px rgba(245, 158, 11, 0.4);
  animation: pulse-mic 1.5s infinite alternate;
}
.xz-mic-button.replying {
  border-color: #00e5ff;
  color: #00e5ff;
  background: radial-gradient(circle, #083344 0%, #030712 100%);
  box-shadow: 0 0 25px rgba(0, 229, 255, 0.45);
  animation: pulse-mic 1.8s infinite alternate;
}
@keyframes ripple {
  0% { width: 78px; height: 78px; opacity: 0.95; }
  100% { width: 150px; height: 150px; opacity: 0; }
}
@keyframes pulse-mic {
  0% { transform: scale(1); }
  100% { transform: scale(1.06); }
}
.mic-sub-hint {
  margin-top: 10px;
  font-size: 0.82rem;
  font-weight: 500;
  color: #94a3b8;
  text-align: center;
  transition: color 0.2s ease;
}
.voice-suggest-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  overflow: hidden;
  padding-top: 6px;
  border-top: 1px dashed rgba(255, 255, 255, 0.06);
}
.suggest-lead {
  font-size: 0.72rem;
  color: #64748b;
  white-space: nowrap;
}
.suggest-scroll {
  display: flex;
  gap: 6px;
  overflow-x: auto;
  scrollbar-width: none;
  -webkit-overflow-scrolling: touch;
}
.suggest-scroll::-webkit-scrollbar { display: none; }
.suggest-chip {
  font-size: 0.72rem;
  color: #38bdf8;
  background: rgba(56, 189, 248, 0.1);
  border: 1px solid rgba(56, 189, 248, 0.2);
  padding: 3px 8px;
  border-radius: 12px;
  white-space: nowrap;
}

/* Kiểu bong bóng tin nhắn chat người dùng & trợ lý */
.chat-msg.user .chat-bubble {
  background: linear-gradient(135deg, #065f46 0%, #064e3b 100%) !important;
  border: 1px solid #10b981 !important;
  color: #ecfdf5 !important;
  box-shadow: 0 2px 10px rgba(16, 185, 129, 0.25) !important;
}
.chat-msg.user .chat-avatar {
  background: #10b981 !important;
  color: #064e3b !important;
  font-weight: bold;
}
.chat-msg.bot .chat-bubble {
  background: linear-gradient(135deg, #1e1b4b 0%, #0f172a 100%) !important;
  border: 1px solid #6366f1 !important;
  color: #f8fafc !important;
  box-shadow: 0 2px 10px rgba(99, 102, 241, 0.25) !important;
}
.chat-emotion-tag {
  display: inline-block;
  font-size: 0.68rem;
  font-weight: 700;
  color: #38bdf8;
  background: rgba(56, 189, 248, 0.15);
  border: 1px solid rgba(56, 189, 248, 0.3);
  padding: 2px 6px;
  border-radius: 6px;
  margin-bottom: 5px;
}
#card-xiaozhi-auth {
  display: none !important;
}
</style>

<script>
(function() {
  let xzPollTimer = null;
  let xzLastTurnId = 0;
  let xzLastState = "";
  let xzCurVol = 80;
  let xzActiveUserBubble = null;
  let xzActiveBotBubble = null;
  let xzRenderedTurnIds = new Set();

  function initXiaoZhiVoiceUI() {
    // 0. Ẩn hoàn toàn thẻ Mã Xác Thực thêm thiết bị vì đã kết nối Hub thành công
    const authCard = document.getElementById('card-xiaozhi-auth');
    if (authCard) authCard.style.display = 'none';

    // 1. Tìm hoặc ẩn khung nhập bàn phím cũ
    const oldInputBar = document.querySelector('#tab-chat .chat-input-bar');
    if (oldInputBar) oldInputBar.style.display = 'none';
    const oldQuickPrompts = document.querySelector('#tab-chat .quick-prompts-wrapper');
    if (oldQuickPrompts) oldQuickPrompts.style.display = 'none';

    // 2. Chèn Voice Dock mới nếu chưa có
    if (!document.getElementById('xiaozhi-voice-dock')) {
      const chatPanel = document.querySelector('#tab-chat .chat-panel') || document.getElementById('tab-chat');
      const dock = document.createElement('div');
      dock.id = 'xiaozhi-voice-dock';
      dock.className = 'voice-dock';
      dock.innerHTML = `
        <div class="voice-status-bar">
          <div class="voice-status-info">
            <span id="xz-status-icon" class="status-dot"></span>
            <span id="xz-status-text" class="status-label">SẴN SÀNG</span>
          </div>
          <div class="voice-vu-wrap" id="xz-vu-container" style="display:none;">
            <span class="vu-label">MIC</span>
            <div class="vu-bar-track">
              <div id="xz-vu-level" class="vu-bar-fill"></div>
            </div>
            <span id="xz-vu-val" class="vu-val">0%</span>
          </div>
          <div class="voice-vol-dock">
            <span title="Âm lượng thoại XiaoZhi AI" style="font-size:0.75rem;">🔊</span>
            <button class="vol-btn" onclick="window.adjustXiaoZhiVolume(-5)">-</button>
            <span id="xz-vol-label" class="vol-val">80%</span>
            <button class="vol-btn" onclick="window.adjustXiaoZhiVolume(5)">+</button>
          </div>
        </div>

        <div class="mic-button-zone">
          <button id="btn-xz-mic-trigger" class="xz-mic-button idle" onclick="window.triggerXiaoZhiMicWeb()">
            <div class="mic-wave-ring r1"></div>
            <div class="mic-wave-ring r2"></div>
            <div class="mic-wave-ring r3"></div>
            <div class="mic-icon-center">
              <svg viewBox="0 0 24 24" width="34" height="34" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M12 1a3 3 0 0 0-3 3v8a3 3 0 0 0 6 0V4a3 3 0 0 0-3-3z"></path>
                <path d="M19 10v2a7 7 0 0 1-14 0v-2"></path>
                <line x1="12" y1="19" x2="12" y2="23"></line>
                <line x1="8" y1="23" x2="16" y2="23"></line>
              </svg>
            </div>
          </button>
          <div id="xz-mic-sub-hint" class="mic-sub-hint">Chạm nút để kích hoạt Micro trên ESP32</div>
        </div>

        <div class="voice-suggest-bar">
          <span class="suggest-lead">💡 Câu mẫu nói vào Mic:</span>
          <div class="suggest-scroll">
            <span class="suggest-chip">"Phát nhạc đi"</span>
            <span class="suggest-chip">"Mấy giờ rồi?"</span>
            <span class="suggest-chip">"Thời tiết hôm nay thế nào?"</span>
            <span class="suggest-chip">"Giới thiệu về bạn đi"</span>
            <span class="suggest-chip">"Đổi sang màn hình PC HUD"</span>
            <span class="suggest-chip">"Kể chuyện cười đi"</span>
          </div>
        </div>
      `;
      chatPanel.appendChild(dock);
    }

    // 3. Cập nhật header trạng thái
    const badge = document.getElementById('chat-status-badge');
    if (badge) {
      badge.className = 'chat-status-tag cloud';
      badge.innerText = '🌐 XiaoZhi Hub Trực Tiếp';
    }
    const modeToggle = document.querySelector('#tab-chat .ai-mode-toggle-group');
    if (modeToggle) modeToggle.style.display = 'none';
  }

  window.triggerXiaoZhiMicWeb = async function() {
    const btn = document.getElementById('btn-xz-mic-trigger');
    const hint = document.getElementById('xz-mic-sub-hint');
    if (btn) {
      if (xzLastState === 'listening') {
        hint.innerText = "Đang gửi âm thanh tới XiaoZhi...";
      } else {
        btn.className = 'xz-mic-button listening';
        hint.innerText = "🎙️ Đang mở Micro trên ESP32... Hãy nói!";
      }
    }
    try {
      const res = await fetch('/api/xiaozhi/action', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'listen' })
      });
      const data = await res.json();
      updateXiaoZhiChatUI(data);
    } catch(e) {
      console.warn("trigger mic err:", e);
    }
  };

  window.adjustXiaoZhiVolume = async function(delta) {
    let nextVol = Math.max(0, Math.min(100, xzCurVol + delta));
    xzCurVol = nextVol;
    const lbl = document.getElementById('xz-vol-label');
    if (lbl) lbl.innerText = nextVol + '%';
    try {
      await fetch('/api/xiaozhi/action', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'volume', volume: nextVol })
      });
    } catch(e) {}
  };

  function formatTime(d = new Date()) {
    return String(d.getHours()).padStart(2,'0') + ':' + String(d.getMinutes()).padStart(2,'0');
  }

  function appendChatBubble(type, text, emotion = '') {
    const box = document.getElementById('chat-messages-box');
    if (!box) return null;
    const div = document.createElement('div');
    div.className = 'chat-msg ' + (type === 'user' ? 'user' : 'bot');
    const time = formatTime();

    if (type === 'user') {
      div.innerHTML = `
        <div class="chat-avatar">👤</div>
        <div class="chat-bubble">
          <div class="msg-content">${escapeHtml(text)}</div>
          <div class="chat-time">${time}</div>
        </div>
      `;
    } else {
      const emoHtml = emotion ? `<div class="chat-emotion-tag">${escapeHtml(emotion)}</div>` : '';
      div.innerHTML = `
        <div class="chat-avatar">🤖</div>
        <div class="chat-bubble">
          ${emoHtml}
          <div class="msg-content">${escapeHtml(text).replace(/\\n/g, '<br>')}</div>
          <div class="chat-time">${time}</div>
        </div>
      `;
    }
    box.appendChild(div);
    box.scrollTop = box.scrollHeight;
    return div;
  }

  function showTypingDot() {
    let el = document.getElementById('chat-typing-indicator');
    if (!el) {
      const box = document.getElementById('chat-messages-box');
      if (!box) return;
      el = document.createElement('div');
      el.id = 'chat-typing-indicator';
      el.className = 'chat-msg bot';
      el.innerHTML = `
        <div class="chat-avatar">🤖</div>
        <div class="chat-bubble">
          <div style="font-size:0.75rem; color:#a5b4fc; margin-bottom:4px;">XiaoZhi đang suy nghĩ...</div>
          <div class="typing-dots">
            <div class="typing-dot"></div>
            <div class="typing-dot"></div>
            <div class="typing-dot"></div>
          </div>
        </div>
      `;
      box.appendChild(el);
      box.scrollTop = box.scrollHeight;
    }
  }

  function removeTypingDot() {
    const el = document.getElementById('chat-typing-indicator');
    if (el) el.remove();
  }

  function updateXiaoZhiChatUI(data) {
    if (!data) return;
    const state = data.state || 'idle';
    const userText = (data.user_text || '').trim();
    const aiReply = (data.ai_reply || '').trim();
    const emotion = data.emotion || 'happy';
    const vol = (data.volume_pct !== undefined) ? data.volume_pct : 80;
    const mic = data.mic_level || 0;
    const turnId = data.turn_id || 0;

    xzCurVol = vol;
    const volLbl = document.getElementById('xz-vol-label');
    if (volLbl) volLbl.innerText = vol + '%';

    const btn = document.getElementById('btn-xz-mic-trigger');
    const hint = document.getElementById('xz-mic-sub-hint');
    const statusIcon = document.getElementById('xz-status-icon');
    const statusText = document.getElementById('xz-status-text');
    const vuBox = document.getElementById('xz-vu-container');
    const vuFill = document.getElementById('xz-vu-level');
    const vuVal = document.getElementById('xz-vu-val');

    if (btn) btn.className = 'xz-mic-button ' + state;
    if (statusIcon) statusIcon.className = 'status-dot ' + state;

    if (state === 'listening') {
      if (statusText) statusText.innerText = 'ĐANG LẮNG NGHE...';
      if (hint) hint.innerText = '🎙️ Micro đang thu âm... Hãy nói câu hỏi vào thiết bị!';
      if (vuBox) vuBox.style.display = 'flex';
      if (vuFill) vuFill.style.width = Math.min(100, mic) + '%';
      if (vuVal) vuVal.innerText = mic + '%';
    } else if (state === 'thinking') {
      if (statusText) statusText.innerText = 'ĐANG XỬ LÝ...';
      if (hint) hint.innerText = '⏳ XiaoZhi đang suy nghĩ câu trả lời...';
      if (vuBox) vuBox.style.display = 'none';
      showTypingDot();
    } else if (state === 'replying') {
      if (statusText) statusText.innerText = 'ĐANG PHÁT ÂM THANH';
      if (hint) hint.innerText = '🔊 XiaoZhi đang trả lời qua loa...';
      if (vuBox) vuBox.style.display = 'none';
      removeTypingDot();
    } else if (state === 'binding') {
      if (statusText) statusText.innerText = 'CHỜ LIÊN KẾT HUB';
      if (hint) hint.innerText = '🔑 Cần nhập mã OTP xác thực trên trang Hub!';
      if (vuBox) vuBox.style.display = 'none';
      removeTypingDot();
    } else {
      if (statusText) statusText.innerText = 'SẴN SÀNG // CHỜ NÓI';
      if (hint) hint.innerText = 'Chạm nút để kích hoạt Micro trên ESP32';
      if (vuBox) vuBox.style.display = 'none';
      removeTypingDot();
    }

    // ĐỒNG BỘ NỘI DUNG HỘI THOẠI VÀO CHAT BOX
    if (turnId > 0 && turnId !== xzLastTurnId) {
      xzLastTurnId = turnId;
      xzActiveUserBubble = null;
      xzActiveBotBubble = null;
    }

    // 1. Thêm/Cập nhật bong bóng của User
    if (userText && userText.length > 0 && !userText.startsWith("Xin chào XiaoZhi") && !userText.startsWith("Dang gui") && !userText.startsWith("(Da nhan am thanh)")) {
      if (!xzActiveUserBubble) {
        xzActiveUserBubble = appendChatBubble('user', userText);
      } else {
        const mc = xzActiveUserBubble.querySelector('.msg-content');
        if (mc && mc.innerText !== userText) mc.innerText = userText;
      }
    }

    // 2. Thêm/Cập nhật bong bóng của XiaoZhi
    if (aiReply && aiReply.length > 0 && !aiReply.startsWith("Nhấn phím [OK]") && !aiReply.startsWith("Chua phat hien tieng noi") && !aiReply.startsWith("Xin chao!")) {
      removeTypingDot();
      let cleanReply = aiReply;
      if (cleanReply.indexOf('XiaoZhi: ') >= 0) {
        cleanReply = cleanReply.substring(cleanReply.indexOf('XiaoZhi: ') + 9);
      }
      if (!xzActiveBotBubble) {
        xzActiveBotBubble = appendChatBubble('bot', cleanReply, '✨ ' + emotion);
      } else {
        const mc = xzActiveBotBubble.querySelector('.msg-content');
        if (mc && mc.innerText !== cleanReply) mc.innerHTML = escapeHtml(cleanReply).replace(/\\n/g, '<br>');
        const et = xzActiveBotBubble.querySelector('.chat-emotion-tag');
        if (et && emotion) et.innerText = '✨ ' + emotion;
      }
    }

    // 3. Đồng bộ biểu cảm sang màn hình Live Preview nếu có hàm
    if (typeof updateChatPreviewEyes === 'function') {
      updateChatPreviewEyes(data.eye_state, emotion);
    }

    xzLastState = state;
  }

  async function pollXiaoZhiStatus() {
    const chatTab = document.getElementById('tab-chat');
    if (chatTab && chatTab.classList.contains('active')) {
      try {
        const res = await fetch('/api/xiaozhi/chat_status');
        if (res.ok) {
          const data = await res.json();
          updateXiaoZhiChatUI(data);
        }
      } catch(e) {}
    }
  }

  setTimeout(() => {
    initXiaoZhiVoiceUI();
    pollXiaoZhiStatus();
    if (!xzPollTimer) {
      xzPollTimer = setInterval(pollXiaoZhiStatus, 380);
    }
  }, 300);

})();
</script>
)rawliteral";

static const char STREAM_PAGE_INJECT_SCRIPT[] PROGMEM = R"rawliteral(
<style>
  #tab-stream { width: 100%; max-width: 100%; overflow: hidden; }
  .stream-grid-wrap {
    display: grid;
    grid-template-columns: 274px minmax(0, 1fr);
    gap: 16px;
    align-items: start;
    width: 100%;
    max-width: 100%;
  }
  @media (max-width: 780px) {
    .stream-grid-wrap { grid-template-columns: minmax(0, 1fr); }
  }
  .stream-bezel {
    margin: 0 auto;
    width: 274px;
    max-width: 100%;
    box-sizing: border-box;
    background: linear-gradient(145deg, #1e293b, #0f172a);
    border: 2px solid #38bdf8;
    border-radius: 16px;
    padding: 14px;
    box-shadow: 0 10px 30px rgba(0,0,0,0.7), 0 0 15px rgba(56,189,248,0.25);
  }
  .stream-screen-preview {
    width: 240px;
    height: 320px;
    background: #000;
    border-radius: 8px;
    overflow: hidden;
    position: relative;
    border: 1px solid #1e293b;
    box-shadow: inset 0 0 20px rgba(0,0,0,0.9);
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
  }
  .stream-preview-canvas {
    width: 240px;
    height: 320px;
    display: block;
    background: #060913;
  }
  .stream-card {
    background: #0f172a;
    border: 1px solid #1e293b;
    border-radius: 12px;
    padding: 14px;
    margin-bottom: 14px;
    box-shadow: 0 4px 15px rgba(0,0,0,0.3);
  }
  .stream-card-title {
    font-size: 0.88rem;
    font-weight: 700;
    color: #38bdf8;
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 10px;
    border-bottom: 1px solid #1e293b;
    padding-bottom: 6px;
  }
  .stream-btn-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
    gap: 8px;
    margin-bottom: 10px;
  }
  .stream-btn {
    background: #1e293b;
    border: 1px solid #334155;
    color: #f8fafc;
    padding: 8px 10px;
    border-radius: 8px;
    font-size: 0.76rem;
    font-weight: 700;
    cursor: pointer;
    transition: all 0.2s;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    text-align: center;
  }
  .stream-btn:hover {
    background: #0284c7;
    border-color: #38bdf8;
    color: #fff;
    transform: translateY(-1px);
    box-shadow: 0 4px 12px rgba(56,189,248,0.3);
  }
  .stream-btn.btn-stop {
    background: rgba(239,68,68,0.15);
    border-color: #ef4444;
    color: #f87171;
  }
  .stream-btn.btn-stop:hover {
    background: #ef4444;
    color: #fff;
  }
  .stream-btn.active {
    background: linear-gradient(135deg, #0284c7, #10b981);
    border-color: #38bdf8;
    color: #fff;
    box-shadow: 0 0 10px rgba(16,185,129,0.4);
  }
  .stream-param-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-size: 0.74rem;
    color: #94a3b8;
    margin-top: 6px;
  }
  .stream-input {
    width: 100%;
    background: #0b0f19;
    border: 1px solid #334155;
    color: #fff;
    padding: 8px 10px;
    border-radius: 6px;
    font-size: 0.76rem;
    margin-bottom: 8px;
    outline: none;
    box-sizing: border-box;
  }
  .stream-input:focus {
    border-color: #38bdf8;
    box-shadow: 0 0 6px rgba(56,189,248,0.3);
  }
  .stream-preset-pill {
    background: #161f30;
    border: 1px solid #334155;
    color: #cbd5e1;
    padding: 4px 8px;
    border-radius: 6px;
    font-size: 0.7rem;
    cursor: pointer;
    transition: 0.15s;
  }
  .stream-preset-pill:hover {
    border-color: #38bdf8;
    color: #38bdf8;
  }
</style>

<script>
(function(){
  let s_streamVideoEl = null;
  let s_streamOffCanvas = null;
  let s_streamOffCtx = null;
  let s_previewCanvas = null;
  let s_previewCtx = null;
  let s_mediaStream = null;
  let s_videoInterval = null;
  let s_isSendingVideo = false;

  let s_audioCtx = null;
  let s_audioSourceNode = null;
  let s_audioProcessor = null;
  let s_micStream = null;
  let s_isSendingAudio = false;

  let s_streamFps = 12;
  let s_jpegQuality = 0.40;
  let s_frameCount = 0;
  let s_lastFpsCalcTime = Date.now();
  let s_currentLiveFps = 0;

  function injectStreamTabAndPage() {
    const navTabs = document.getElementById('nav-tabs');
    if (!navTabs || document.getElementById('tab-btn-stream')) return;

    // 1. Thêm nút Tab "📡 Stream Đa Phương Tiện"
    const btn = document.createElement('button');
    btn.id = 'tab-btn-stream';
    btn.className = 'tab-btn';
    btn.innerHTML = '📡 Web Streamer';
    btn.onclick = function() { window.switchTab('stream'); };

    const wifiTab = Array.from(navTabs.children).find(c => (c.textContent || '').indexOf('Wi-Fi') >= 0);
    if (wifiTab) navTabs.insertBefore(btn, wifiTab);
    else navTabs.appendChild(btn);

    // 2. Tạo Section Page "#tab-stream"
    const sec = document.createElement('div');
    sec.id = 'tab-stream';
    sec.className = 'section';
    sec.innerHTML = `
      <div class="stream-grid-wrap">
        <!-- CỘT TRÁI: MÀN HÌNH PREVIEW 240x320 MÔ PHỎNG ST7789 -->
        <div class="stream-bezel">
          <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;font-size:0.68rem;color:#64748b;font-weight:700;">
            <span>ESP32-S3 // 240x320 STREAM</span>
            <span id="st-live-dot" style="color:#64748b;">● IDLE</span>
          </div>

          <div class="stream-screen-preview">
            <canvas id="stream-live-canvas" class="stream-preview-canvas" width="240" height="320"></canvas>
            <img id="stream-live-mjpg" class="stream-preview-canvas" style="display:none; width:240px; height:320px; object-fit:contain; background:#000;" alt="Live Stream">
          </div>

          <div style="margin-top:10px;background:#0b0f19;border:1px solid #1e293b;border-radius:8px;padding:8px;font-size:0.72rem;color:#94a3b8;">
            <div style="display:flex;justify-content:space-between;margin-bottom:4px;">
              <span>Tốc độ: <strong id="st-stat-fps" style="color:#00e5ff;">0.0 FPS</strong></span>
              <span>Đã gửi: <strong id="st-stat-frames" style="color:#10b981;">0 frames</strong></span>
            </div>
            <div style="display:flex;justify-content:space-between;">
              <span>Âm thanh: <strong id="st-stat-audio" style="color:#f59e0b;">Chưa bật</strong></span>
              <span>Độ trễ: <strong id="st-stat-rtt" style="color:#cbd5e1;">~35ms</strong></span>
            </div>
          </div>
        </div>

        <!-- CỘT PHẢI: BẢNG ĐIỀU KHIỂN STREAM VIDEO & AUDIO -->
        <div>
          <!-- THẺ 1: STREAM VIDEO TRỰC TIẾP TỪ PC -->
          <div class="stream-card">
            <div class="stream-card-title">
              <span>🖥️ Stream Video Từ PC Xuống ST7789</span>
              <span style="font-size:0.72rem;color:#10b981;">Hardware DMA SPI 80MHz</span>
            </div>

            <div class="stream-btn-grid">
              <button class="stream-btn" id="btn-stream-screen" style="border-color:#38bdf8;background:linear-gradient(135deg, rgba(2,132,199,0.3), rgba(6,182,212,0.3));font-weight:700;" onclick="window.startScreenStream()">
                ▶️ Phát Tab YouTube (Trực Tiếp Từ Trình Duyệt)
              </button>
              <button class="stream-btn" id="btn-stream-file" onclick="document.getElementById('stream-file-selector').click()">
                📁 Phát Video MP4 / WebM
              </button>
              <button class="stream-btn" id="btn-stream-cam" onclick="window.startWebcamStream()">
                📷 Bật Webcam Máy Tính
              </button>
              <button class="stream-btn btn-stop" id="btn-stream-stop-video" onclick="window.stopVideoStream()">
                ⏹️ Dừng Video Stream
              </button>
            </div>
            <input type="file" id="stream-file-selector" accept="video/*" style="display:none;" onchange="window.handleVideoFileSelected(event)">

            <div style="background:#0b0f19;border-radius:8px;padding:10px;margin-top:8px;">
              <div class="stream-param-row">
                <span>Tốc độ khung hình (FPS):</span>
                <div style="display:flex;gap:6px;">
                  <button class="stream-preset-pill" onclick="window.setStreamFps(10)">10 FPS</button>
                  <button class="stream-preset-pill" style="border-color:#38bdf8;color:#38bdf8;" onclick="window.setStreamFps(15)">15 FPS (Chuẩn)</button>
                  <button class="stream-preset-pill" onclick="window.setStreamFps(20)">20 FPS (Tối đa)</button>
                </div>
              </div>
              <div class="stream-param-row" style="margin-top:8px;">
                <span>Chất lượng nén JPEG:</span>
                <input type="range" id="stream-jpeg-qual" min="30" max="80" value="45" style="width:120px;" onchange="window.setStreamQuality(this.value)">
                <span id="stream-qual-txt" style="color:#38bdf8;font-weight:700;">45% (Mượt)</span>
              </div>
            </div>
          </div>

          <!-- THẺ 2: STREAM ÂM THANH XUỐNG LOA MAX98357A -->
          <div class="stream-card">
            <div class="stream-card-title">
              <span>🎵 Stream Âm Thanh Ra Loa I2S DAC</span>
              <span style="font-size:0.72rem;color:#facc15;">MAX98357A (16kHz PCM)</span>
            </div>

            <div class="stream-btn-grid">
              <button class="stream-btn" id="btn-stream-mic" onclick="window.startMicAudioStream()">
                🎙️ Bật Micro / Audio PC
              </button>
              <button class="stream-btn btn-stop" id="btn-stream-stop-audio" onclick="window.stopAudioStream()">
                ⏹️ Dừng Audio Stream
              </button>
            </div>

            <div style="display:flex;align-items:center;justify-content:space-between;background:#0b0f19;border-radius:8px;padding:10px;margin-top:8px;">
              <span style="font-size:0.74rem;color:#94a3b8;">Âm lượng Loa ESP32:</span>
              <input type="range" id="stream-spk-vol" min="0" max="100" value="75" style="width:140px;" oninput="window.setSpeakerVolume(this.value)">
              <span id="stream-vol-val" style="color:#10b981;font-weight:700;font-size:0.74rem;">75%</span>
            </div>
          </div>

          <!-- THẺ 4: TRẠM CHUYỂN MÃ PC (PC TRANSCODER FOR YOUTUBE & WEB LINK) -->
          <div class="stream-card" style="border:1px solid rgba(6,182,212,0.4);background:rgba(6,182,212,0.03);">
            <div class="stream-card-title">
              <span style="color:#38bdf8;">⚡ Trạm Chuyển Mã PC (YouTube / TikTok / Web Link)</span>
              <span id="transcoder-badge" style="font-size:0.72rem;background:rgba(16,185,129,0.2);color:#10b981;border:1px solid #10b981;padding:2px 6px;border-radius:10px;">yt-dlp & FFmpeg</span>
            </div>

            <div style="font-size:0.74rem;color:#94a3b8;margin-bottom:8px;line-height:1.4;">
              PC giải mã link YouTube/Web, băm nhỏ thành <strong>MJPEG 240x240 @ 15 FPS</strong> và <strong>PCM 16kHz</strong>, stream qua mạng LAN xuống ESP32 mượt mà không nóng vi xử lý.
            </div>

            <div style="margin-bottom:8px;">
              <input type="text" id="transcoder-url-input" class="stream-input" placeholder="Dán link YouTube, TikTok, Facebook hoặc tệp video trên PC...">
              <div style="display:flex;gap:6px;flex-wrap:wrap;margin-top:6px;">
                <span class="stream-preset-pill" onclick="document.getElementById('transcoder-url-input').value='https://www.youtube.com/watch?v=dQw4w9WgXcQ'">🕺 Rickroll</span>
                <span class="stream-preset-pill" onclick="document.getElementById('transcoder-url-input').value='https://www.youtube.com/watch?v=5qap5aO4i9A'">🎶 Lofi Hip Hop</span>
                <span class="stream-preset-pill" onclick="document.getElementById('transcoder-url-input').value='https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4'">🐰 Big Buck Bunny</span>
              </div>
            </div>

            <div style="display:flex;align-items:center;gap:8px;margin-bottom:8px;">
              <span style="font-size:0.72rem;color:#94a3b8;white-space:nowrap;">IP Máy Tính (Transcoder):</span>
              <input type="text" id="pc-transcoder-host-input" class="stream-input" style="margin-bottom:0;padding:4px 8px;font-size:0.72rem;" value="localhost" placeholder="localhost (hoặc 192.168.1.xxx)" onchange="localStorage.setItem('pc_transcoder_host', this.value.trim());">
            </div>

            <div style="display:grid;grid-template-columns:2fr 1fr;gap:8px;">
              <button class="stream-btn" id="btn-pc-transcode" style="background:linear-gradient(135deg, #0284c7, #06b6d4);color:#fff;font-weight:700;" onclick="window.startPcTranscoderStream()">
                🚀 Chuyển Mã & Stream Xuống ESP32
              </button>
              <button class="stream-btn btn-stop" onclick="window.stopPcTranscoderStream()">
                ⏹️ Dừng Stream
              </button>
            </div>

            <div style="display:flex;justify-content:space-between;align-items:center;margin-top:8px;padding-top:8px;border-top:1px dashed rgba(255,255,255,0.08);font-size:0.72rem;">
              <span id="transcoder-status-txt" style="color:#64748b;">● PC Transcoder: http://localhost:5050</span>
              <a href="http://localhost:5050" target="_blank" style="color:#38bdf8;text-decoration:none;">Mở Bảng Điều Khiển PC ↗</a>
            </div>
          </div>
        </div>
      </div>
    `;

    const container = document.querySelector('.container');
    if (container) container.appendChild(sec);

    initStreamElements();
  }

  function initStreamElements() {
    s_previewCanvas = document.getElementById('stream-live-canvas');
    if (s_previewCanvas) {
      s_previewCtx = s_previewCanvas.getContext('2d');
      drawIdlePreviewScreen();
    }

    s_streamOffCanvas = document.createElement('canvas');
    s_streamOffCanvas.width = 240;
    s_streamOffCanvas.height = 320;
    s_streamOffCtx = s_streamOffCanvas.getContext('2d');

    s_streamVideoEl = document.createElement('video');
    s_streamVideoEl.muted = false;
    s_streamVideoEl.volume = 1.0;
    s_streamVideoEl.playsInline = true;
    s_streamVideoEl.autoplay = true;
    s_streamVideoEl.style.cssText = 'position:fixed;top:-9999px;left:-9999px;width:240px;height:320px;opacity:0.01;pointer-events:none;';
    document.body.appendChild(s_streamVideoEl);
  }

  function drawIdlePreviewScreen() {
    if (!s_previewCtx) return;
    s_previewCtx.fillStyle = '#060913';
    s_previewCtx.fillRect(0, 0, 240, 320);

    s_previewCtx.strokeStyle = '#1e293b';
    s_previewCtx.strokeRect(10, 10, 220, 300);

    s_previewCtx.fillStyle = '#38bdf8';
    s_previewCtx.font = 'bold 12px sans-serif';
    s_previewCtx.textAlign = 'center';
    s_previewCtx.fillText('ESP32-S3 STREAM PREVIEW', 120, 110);

    s_previewCtx.fillStyle = '#64748b';
    s_previewCtx.font = '11px sans-serif';
    s_previewCtx.fillText('240 x 320 Portrait', 120, 135);
    s_previewCtx.fillText('Chưa có luồng dữ liệu', 120, 160);

    s_previewCtx.fillStyle = '#10b981';
    s_previewCtx.fillText('Bấm nút bên phải để bắt đầu!', 120, 210);
  }

  window.setStreamFps = function(fps) {
    s_streamFps = fps;
    if (s_videoInterval) {
      clearInterval(s_videoInterval);
      startVideoPump();
    }
  };

  window.setStreamQuality = function(q) {
    s_jpegQuality = q / 100.0;
    const txt = document.getElementById('stream-qual-txt');
    if (txt) txt.innerText = q + '%';
  };

  window.setSpeakerVolume = async function(vol) {
    const valEl = document.getElementById('stream-vol-val');
    if (valEl) valEl.innerText = vol + '%';
    await fetch('/api/media/control', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'volume', value: parseInt(vol) })
    }).catch(()=>{});
  };

  function updateLiveDot(active, text) {
    const el = document.getElementById('st-live-dot');
    if (el) {
      el.style.color = active ? '#10b981' : '#64748b';
      el.innerText = active ? ('● ' + text) : '● IDLE';
    }
  }

  let s_videoPumpActive = false;
  let s_videoPumpTimeout = null;

  function scheduleNextVideoFrame(delayMs) {
    if (!s_videoPumpActive) return;
    if (s_videoPumpTimeout) clearTimeout(s_videoPumpTimeout);
    s_videoPumpTimeout = setTimeout(sendNextVideoFrame, Math.max(5, Math.round(delayMs)));
  }

  function sendNextVideoFrame() {
    if (!s_videoPumpActive) return;
    if (!s_streamVideoEl || s_streamVideoEl.paused || s_streamVideoEl.ended || s_streamVideoEl.readyState < 2) {
      if (s_streamVideoEl && s_streamVideoEl.paused && (s_streamVideoEl.src || s_streamVideoEl.srcObject)) {
        s_streamVideoEl.play().catch(()=>{});
      }
      scheduleNextVideoFrame(50);
      return;
    }

    const vw = s_streamVideoEl.videoWidth || 240;
    const vh = s_streamVideoEl.videoHeight || 320;

    // Scale và canh giữa tỉ lệ 240x320
    s_streamOffCtx.fillStyle = '#000000';
    s_streamOffCtx.fillRect(0, 0, 240, 320);

    const targetAspect = 240.0 / 320.0;
    const videoAspect = vw / vh;
    let drawW = 240, drawH = 320, drawX = 0, drawY = 0;

    if (videoAspect > targetAspect) {
      drawW = 240;
      drawH = Math.round(240 / videoAspect);
      drawY = Math.round((320 - drawH) / 2);
    } else {
      drawH = 320;
      drawW = Math.round(320 * videoAspect);
      drawX = Math.round((240 - drawW) / 2);
    }

    s_streamOffCtx.drawImage(s_streamVideoEl, drawX, drawY, drawW, drawH);

    // Cập nhật live preview trên trang Web
    if (s_previewCtx) {
      s_previewCtx.drawImage(s_streamOffCanvas, 0, 0);
    }

    const targetInterval = Math.round(1000 / s_streamFps);
    const t0 = performance.now();

    s_streamOffCanvas.toBlob(blob => {
      if (!blob || !s_videoPumpActive) {
        scheduleNextVideoFrame(targetInterval);
        return;
      }

      // AbortController 650ms: Đủ thời gian cho mạng Wi-Fi và ESP32 giải mã, tránh abort sớm gây nghẽn socket
      const ctrl = new AbortController();
      const tid = setTimeout(() => ctrl.abort(), 650);

      const fd = new FormData();
      fd.append('f', blob);

      fetch('/api/stream/video_frame', {
        method: 'POST',
        body: fd,
        signal: ctrl.signal
      })
      .then(() => {
        clearTimeout(tid);
        s_frameCount++;
        const now = Date.now();
        if (now - s_lastFpsCalcTime >= 1000) {
          s_currentLiveFps = ((s_frameCount * 1000.0) / (now - s_lastFpsCalcTime)).toFixed(1);
          s_frameCount = 0;
          s_lastFpsCalcTime = now;
          const fpsEl = document.getElementById('st-stat-fps');
          if (fpsEl) fpsEl.innerText = s_currentLiveFps + ' FPS';
        }
        const framesEl = document.getElementById('st-stat-frames');
        if (framesEl) framesEl.innerText = s_frameCount + ' frames';

        const rtt = Math.round(performance.now() - t0);
        const rttEl = document.getElementById('st-stat-rtt');
        if (rttEl) rttEl.innerText = '~' + rtt + 'ms';

        // Nhịp gửi thích ứng (Adaptive Pacing): Bù trừ độ trễ mạng để luôn đạt FPS mục tiêu
        // Khi audio đang hoạt động, duy trì "khoảng thở vàng" tối thiểu 35ms để ESP32 ưu tiên nhận Audio PCM
        const minGap = s_audioProcessor ? 35 : 10;
        const remainingDelay = Math.max(minGap, targetInterval - rtt);
        scheduleNextVideoFrame(remainingDelay);
      })
      .catch(() => {
        clearTimeout(tid);
        // Nếu bị nghẽn mạng: dừng nhẹ 80ms để ESP32 giải phóng kết nối TCP trước khi gửi frame mới
        scheduleNextVideoFrame(80);
      });
    }, 'image/jpeg', s_jpegQuality);
  }

  function startVideoPump() {
    stopVideoPump();
    const mjpgEl = document.getElementById('stream-live-mjpg');
    const cvEl = document.getElementById('stream-live-canvas');
    if (mjpgEl && cvEl) {
      mjpgEl.removeAttribute('src');
      mjpgEl.style.display = 'none';
      cvEl.style.display = 'block';
    }
    s_videoPumpActive = true;
    s_frameCount = 0;
    s_lastFpsCalcTime = Date.now();
    updateLiveDot(true, 'LIVE VIDEO');
    fetch('/api/screen/state', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mode: 'stream' })
    }).catch(()=>{});
    sendNextVideoFrame();
  }

  function stopVideoPump() {
    s_videoPumpActive = false;
    if (s_videoPumpTimeout) {
      clearTimeout(s_videoPumpTimeout);
      s_videoPumpTimeout = null;
    }
  }

  window.startScreenStream = async function() {
    try {
      if (!navigator.mediaDevices || !navigator.mediaDevices.getDisplayMedia) {
        if (confirm("💡 Trình duyệt Cốc Cốc / Chrome yêu cầu bảo mật Localhost để cho phép chia sẻ Tab YouTube.\n\nBấm [OK] để mở trang Bảng Điều Khiển: http://localhost:5050 (đang chạy trên máy tính) để bắt Tab YouTube mượt mà không bị chặn!")) {
          window.open('http://localhost:5050', '_blank');
        }
        return;
      }
      window.stopVideoStream();
      s_mediaStream = await navigator.mediaDevices.getDisplayMedia({
        video: { frameRate: { ideal: s_streamFps } },
        audio: true
      });
      s_streamVideoEl.srcObject = s_mediaStream;
      await s_streamVideoEl.play();
      startVideoPump();

      const audioTracks = s_mediaStream.getAudioTracks();
      if (audioTracks && audioTracks.length > 0) {
        initWebAudio(s_mediaStream);
        const audEl = document.getElementById('st-stat-audio');
        if (audEl) { audEl.innerText = 'Audio Kèm Màn Hình'; audEl.style.color = '#10b981'; }
      }

      s_mediaStream.getVideoTracks()[0].onended = () => { window.stopVideoStream(); };
    } catch(e) {
      if (e.name !== 'NotAllowedError') {
        alert("Không thể bắt đầu chia sẻ màn hình: " + e.message);
      }
    }
  };

  window.startWebcamStream = async function() {
    try {
      window.stopVideoStream();
      s_mediaStream = await navigator.mediaDevices.getUserMedia({
        video: { width: { ideal: 640 }, height: { ideal: 480 } },
        audio: false
      });
      s_streamVideoEl.srcObject = s_mediaStream;
      await s_streamVideoEl.play();
      startVideoPump();
    } catch(e) {
      alert("Không thể mở webcam: " + e.message);
    }
  };

  let s_videoAudioSourceConnected = false;
  let s_videoAudioGainNode = null;

  function connectVideoElementToWebAudio() {
    if (!s_audioCtx) {
      try {
        s_audioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 16000 });
      } catch(e) {
        s_audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      }
    }
    if (s_audioCtx.state === 'suspended') {
      s_audioCtx.resume();
    }

    if (!s_videoAudioSourceConnected && s_streamVideoEl) {
      try {
        s_audioSourceNode = s_audioCtx.createMediaElementSource(s_streamVideoEl);
        s_videoAudioSourceConnected = true;
      } catch(e) {
        console.warn("createMediaElementSource error:", e);
      }
    }

    if (s_audioProcessor) {
      try { s_audioProcessor.disconnect(); } catch(e){}
      s_audioProcessor = null;
    }

    try {
      s_audioProcessor = s_audioCtx.createScriptProcessor(2048, 1, 1);
    } catch(e) {
      console.warn("createScriptProcessor error:", e);
      return;
    }

    if (!s_videoAudioGainNode && s_audioCtx) {
      s_videoAudioGainNode = s_audioCtx.createGain();
      s_videoAudioGainNode.gain.value = 0.0;
      s_videoAudioGainNode.connect(s_audioCtx.destination);
    }

    if (s_audioSourceNode && s_audioProcessor) {
      try { s_audioSourceNode.disconnect(); } catch(e){}
      s_audioSourceNode.connect(s_audioProcessor);
      s_audioProcessor.connect(s_videoAudioGainNode || s_audioCtx.destination);
    }

    let sampleBuffer = [];
    const targetHz = 16000;
    const inputHz = s_audioCtx.sampleRate;
    let phaseAcc = 0.0;
    const isDirect16k = (inputHz === targetHz);
    const step = inputHz / targetHz;

    s_audioProcessor.onaudioprocess = function(e) {
      if (!s_streamVideoEl || s_streamVideoEl.paused || s_streamVideoEl.ended) return;
      const input = e.inputBuffer.getChannelData(0);
      const inputLen = input.length;

      if (isDirect16k) {
        for (let i = 0; i < inputLen; i++) {
          let s = Math.max(-1, Math.min(1, input[i]));
          sampleBuffer.push(Math.round(s < 0 ? s * 32768 : s * 32767));
        }
      } else {
        while (phaseAcc < inputLen - 1) {
          const idx = Math.floor(phaseAcc);
          const frac = phaseAcc - idx;
          const s = input[idx] * (1.0 - frac) + input[idx + 1] * frac;
          const clamped = Math.max(-1, Math.min(1, s));
          sampleBuffer.push(Math.round(clamped < 0 ? clamped * 32768 : clamped * 32767));
          phaseAcc += step;
        }
        phaseAcc -= inputLen;
        if (phaseAcc < 0) phaseAcc = 0;
      }

      // Tuyệt đối không drop audio: Cho phép buffer tích lũy tới 32,000 mẫu (~2.0s) khi mạng bận
      if (sampleBuffer.length > 32000) {
        sampleBuffer = sampleBuffer.slice(sampleBuffer.length - 32000);
      }

      function tryFlushAudio() {
        if (s_isSendingAudio || sampleBuffer.length < 1600) return;
        s_isSendingAudio = true;
        const sendCount = Math.min(sampleBuffer.length, 3200);
        const toSend = sampleBuffer.splice(0, sendCount);
        const int16Arr = new Int16Array(toSend);
        const blob = new Blob([int16Arr.buffer], { type: 'application/octet-stream' });
        const fd = new FormData();
        fd.append('a', blob, 'stream.pcm');

        const ctrl = new AbortController();
        const tid = setTimeout(() => ctrl.abort(), 1200);

        fetch('/api/stream/audio_pcm', { method: 'POST', body: fd, signal: ctrl.signal })
          .catch(()=>{})
          .finally(() => {
            clearTimeout(tid);
            s_isSendingAudio = false;
            if (sampleBuffer.length >= 1600) {
              setTimeout(tryFlushAudio, 5);
            }
          });
      }

      tryFlushAudio();
    };
  }

  window.handleVideoFileSelected = function(e) {
    const file = e.target.files[0];
    if (!file) return;

    // 1. Kích hoạt AudioContext ngay lập tức trong User Gesture của trình duyệt
    if (!s_audioCtx) {
      s_audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    }
    if (s_audioCtx.state === 'suspended') {
      s_audioCtx.resume();
    }

    window.stopVideoStream();
    if (typeof showToast === 'function') {
      showToast("🎬 Đang tải video: " + file.name + "... Vui lòng đợi trong giây lát!", false);
    }
    updateLiveDot(true, 'DANG TAI VIDEO');

    s_streamVideoEl.srcObject = null;
    s_streamVideoEl.src = URL.createObjectURL(file);
    s_streamVideoEl.loop = true;
    s_streamVideoEl.muted = false;
    s_streamVideoEl.volume = 1.0;
    s_streamVideoEl.playsInline = true;

    connectVideoElementToWebAudio();

    let hasStarted = false;
    const triggerStart = async () => {
      if (hasStarted) return;
      hasStarted = true;
      try {
        if (s_audioCtx && s_audioCtx.state === 'suspended') {
          await s_audioCtx.resume();
        }
        await s_streamVideoEl.play();
      } catch(err) {
        console.warn("Video play:", err);
      }
      startVideoPump();
      const audEl = document.getElementById('st-stat-audio');
      if (audEl) { audEl.innerText = 'MP4 PCM 16kHz LIVE'; audEl.style.color = '#10b981'; }
      if (typeof showToast === 'function') {
        showToast("▶ Đang stream video sang ESP32!");
      }
    };

    s_streamVideoEl.onloadedmetadata = triggerStart;
    s_streamVideoEl.oncanplay = triggerStart;
    s_streamVideoEl.onplay = () => {
      if (!s_videoPumpActive) startVideoPump();
    };
    s_streamVideoEl.load();
    setTimeout(() => {
      if (!hasStarted) triggerStart();
    }, 250);
    e.target.value = '';
  };

  window.stopVideoStream = function() {
    stopVideoPump();
    if (s_videoInterval) {
      clearInterval(s_videoInterval);
      s_videoInterval = null;
    }
    if (s_mediaStream) {
      s_mediaStream.getTracks().forEach(t => t.stop());
      s_mediaStream = null;
    }
    if (s_streamVideoEl) {
      s_streamVideoEl.pause();
      s_streamVideoEl.srcObject = null;
      s_streamVideoEl.removeAttribute('src');
    }
    if (s_audioProcessor) {
      try { s_audioProcessor.disconnect(); } catch(e){}
      s_audioProcessor = null;
    }
    fetch('/api/stream/stop', { method: 'POST' }).catch(()=>{});
    updateLiveDot(false, '');
    drawIdlePreviewScreen();
    const fpsEl = document.getElementById('st-stat-fps');
    if (fpsEl) fpsEl.innerText = '0.0 FPS';
    const audEl = document.getElementById('st-stat-audio');
    if (audEl) { audEl.innerText = 'Đã dừng'; audEl.style.color = '#f59e0b'; }
  };

  // ---------------- STREAM ÂM THANH QUA WEB AUDIO API (16kHz PCM) ----------------

  window.startMicAudioStream = async function() {
    try {
      window.stopAudioStream();
      s_micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
      initWebAudio(s_micStream);
      const audEl = document.getElementById('st-stat-audio');
      if (audEl) { audEl.innerText = '16kHz PCM LIVE'; audEl.style.color = '#10b981'; }
    } catch(e) {
      alert("Không thể kích hoạt Micro: " + e.message);
    }
  };

  function initWebAudio(stream) {
    s_audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    s_audioSourceNode = s_audioCtx.createMediaStreamSource(stream);
    s_audioProcessor = s_audioCtx.createScriptProcessor(2048, 1, 1);

    s_audioSourceNode.connect(s_audioProcessor);
    s_audioProcessor.connect(s_audioCtx.destination);

    let sampleBuffer = [];
    const targetHz = 16000;
    const inputHz = s_audioCtx.sampleRate;
    const ratio = targetHz / inputHz;

    s_audioProcessor.onaudioprocess = function(e) {
      const channelData = e.inputBuffer.getChannelData(0);
      for (let i = 0; i < channelData.length; i += (1 / ratio)) {
        const idx = Math.floor(i);
        if (idx < channelData.length) {
          let s = Math.max(-1, Math.min(1, channelData[idx]));
          let val16 = Math.round(s < 0 ? s * 32768 : s * 32767);
          sampleBuffer.push(val16);
        }
      }
      function tryFlushAudio() {
        if (s_isSendingAudio || sampleBuffer.length < 1600) return;
        s_isSendingAudio = true;
        const sendCount = Math.min(sampleBuffer.length, 3200);
        const toSend = sampleBuffer.splice(0, sendCount);
        const int16Arr = new Int16Array(toSend);
        const blob = new Blob([int16Arr.buffer], { type: 'application/octet-stream' });
        const fd = new FormData();
        fd.append('a', blob, 'stream.pcm');

        const ctrl = new AbortController();
        const tid = setTimeout(() => ctrl.abort(), 1200);

        fetch('/api/stream/audio_pcm', { method: 'POST', body: fd, signal: ctrl.signal })
          .catch(()=>{})
          .finally(() => {
            clearTimeout(tid);
            s_isSendingAudio = false;
            if (sampleBuffer.length >= 1600) {
              setTimeout(tryFlushAudio, 5);
            }
          });
      }

      tryFlushAudio();
    };
  }


  window.stopAudioStream = function() {
    if (s_audioProcessor) {
      try { s_audioProcessor.disconnect(); } catch(e){}
      s_audioProcessor = null;
    }
    if (s_audioSourceNode) {
      try { s_audioSourceNode.disconnect(); } catch(e){}
      s_audioSourceNode = null;
    }
    if (s_audioCtx) {
      try { s_audioCtx.close(); } catch(e){}
      s_audioCtx = null;
      s_videoAudioSourceConnected = false;
      s_videoAudioGainNode = null;
    }
    if (s_micStream) {
      s_micStream.getTracks().forEach(t => t.stop());
      s_micStream = null;
    }
    if (s_webRelayAudioEl) {
      s_webRelayAudioEl.pause();
      s_webRelayAudioEl.src = "";
    }
    if (!s_videoInterval) {
      fetch('/api/stream/stop', { method: 'POST' }).catch(()=>{});
    }
    const audEl = document.getElementById('st-stat-audio');
    if (audEl) { audEl.innerText = 'Đã dừng'; audEl.style.color = '#f59e0b'; }
  };

  let s_webRelayAudioEl = null;

  function initWebAudioFromElement(mediaEl) {
    if (!s_audioCtx) s_audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    if (s_audioSourceNode) try { s_audioSourceNode.disconnect(); } catch(e){}
    if (s_audioProcessor) try { s_audioProcessor.disconnect(); } catch(e){}

    try {
      s_audioSourceNode = s_audioCtx.createMediaElementSource(mediaEl);
    } catch(e) {}
    s_audioProcessor = s_audioCtx.createScriptProcessor(2048, 1, 1);

    if (s_audioSourceNode) s_audioSourceNode.connect(s_audioProcessor);
    s_audioProcessor.connect(s_audioCtx.destination);

    let sampleBuffer = [];
    const targetHz = 16000;
    const inputHz = s_audioCtx.sampleRate;
    const ratio = targetHz / inputHz;

    s_audioProcessor.onaudioprocess = function(e) {
      const channelData = e.inputBuffer.getChannelData(0);
      for (let i = 0; i < channelData.length; i += (1 / ratio)) {
        const idx = Math.floor(i);
        if (idx < channelData.length) {
          let s = Math.max(-1, Math.min(1, channelData[idx]));
          let val16 = Math.round(s < 0 ? s * 32768 : s * 32767);
          sampleBuffer.push(val16);
        }
      }
      function tryFlushAudio() {
        if (s_isSendingAudio || sampleBuffer.length < 1600) return;
        s_isSendingAudio = true;
        const sendCount = Math.min(sampleBuffer.length, 3200);
        const toSend = sampleBuffer.splice(0, sendCount);
        const int16Arr = new Int16Array(toSend);
        const blob = new Blob([int16Arr.buffer], { type: 'application/octet-stream' });
        const fd = new FormData();
        fd.append('a', blob, 'stream.pcm');

        const ctrl = new AbortController();
        const tid = setTimeout(() => ctrl.abort(), 1200);

        fetch('/api/stream/audio_pcm', { method: 'POST', body: fd, signal: ctrl.signal })
          .catch(()=>{})
          .finally(() => {
            clearTimeout(tid);
            s_isSendingAudio = false;
            if (sampleBuffer.length >= 1600) {
              setTimeout(tryFlushAudio, 5);
            }
          });
      }

      tryFlushAudio();
    };
  }


  // ---------------- PHÁT STREAM URL QUA TRÌNH DUYỆT HOẶC ESP32 ----------------

  window.fillUrl = function(url, type) {
    const input = document.getElementById('transcoder-url-input') || document.getElementById('stream-url-input');
    if (input) input.value = url;
  };

  function isWebVideoPlatform(url) {
    return /youtube\.com|youtu\.be|tiktok\.com|bilibili\.com|facebook\.com|fb\.watch/i.test(url);
  }

  window.playUrlStreamWebRelay = function(type) {
    const input = document.getElementById('transcoder-url-input') || document.getElementById('stream-url-input');
    const url = (input ? input.value : '').trim();
    if (!url) return alert("Vui lòng nhập đường link stream!");

    if (isWebVideoPlatform(url)) {
      const pcInput = document.getElementById('transcoder-url-input') || document.getElementById('stream-url-input');
      if (pcInput) pcInput.value = url;
      alert("ℹ️ Đây là link trang web video (YouTube / TikTok), vi xử lý ESP32 không thể tự phân tích HTML.\n\nHệ thống đang tự động kích hoạt Trạm Chuyển Mã PC (FFmpeg) để giải mã mượt mà!");
      return window.startPcTranscoderStream();
    }

    if (type === 'audio') {
      window.stopAudioStream();
      try {
        if (!s_webRelayAudioEl) {
          s_webRelayAudioEl = new Audio();
          s_webRelayAudioEl.crossOrigin = "anonymous";
        }
        s_webRelayAudioEl.src = url;
        s_webRelayAudioEl.play().then(() => {
          initWebAudioFromElement(s_webRelayAudioEl);
          const audEl = document.getElementById('st-stat-audio');
          if (audEl) { audEl.innerText = 'Radio Web Relay'; audEl.style.color = '#10b981'; }
          updateLiveDot(true, 'WEB AUDIO RELAY');
        }).catch(err => {
          alert("Trình duyệt không thể phát trực tiếp link này (do CORS). Đang chuyển sang để ESP32 tự tải...");
          window.playUrlStream('audio');
        });
      } catch(e) {
        window.playUrlStream('audio');
      }
    } else if (type === 'video') {
      window.stopVideoStream();
      s_streamVideoEl.srcObject = null;
      s_streamVideoEl.crossOrigin = "anonymous";
      s_streamVideoEl.src = url;
      s_streamVideoEl.muted = false;
      s_streamVideoEl.onloadedmetadata = async () => {
        try {
          await s_streamVideoEl.play();
        } catch(err) {}
        startVideoPump();
        connectVideoElementToWebAudio();
        const audEl = document.getElementById('st-stat-audio');
        if (audEl) { audEl.innerText = 'Video PCM (16kHz)'; audEl.style.color = '#10b981'; }
      };
      s_streamVideoEl.onerror = () => {
        alert("Video không hỗ trợ phát qua trình duyệt, đang chuyển sang để ESP32 tải...");
        window.playUrlStream('video');
      };
    }
  };

  window.playUrlStream = async function(type) {
    const input = document.getElementById('transcoder-url-input') || document.getElementById('stream-url-input');
    const url = (input ? input.value : '').trim();
    if (!url) return alert("Vui lòng nhập đường link stream HTTP/HTTPS!");

    if (isWebVideoPlatform(url)) {
      const pcInput = document.getElementById('transcoder-url-input') || document.getElementById('stream-url-input');
      if (pcInput) pcInput.value = url;
      alert("ℹ️ Đây là link trang web video (YouTube / TikTok), vi xử lý ESP32 không thể tự phân tích HTML.\n\nHệ thống đang tự động kích hoạt Trạm Chuyển Mã PC (FFmpeg) để giải mã mượt mà!");
      return window.startPcTranscoderStream();
    }

    const res = await fetch('/api/stream/start_url', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ type: type, url: url })
    });
    const data = await res.json();
    if (data.status === 'ok') {
      alert("Đã gửi yêu cầu kết nối luồng " + type.toUpperCase() + " tới ESP32!");
      updateLiveDot(true, 'ESP32 ' + type.toUpperCase());
    } else {
      alert("Lỗi kết nối URL stream: " + (data.error || 'Thất bại'));
    }
  };

  window.stopAllStreams = async function() {
    window.stopVideoStream();
    window.stopAudioStream();
    await fetch('/api/stream/stop', { method: 'POST' }).catch(()=>{});
    updateLiveDot(false, '');
    drawIdlePreviewScreen();
  };

  // ---------------- TRẠM CHUYỂN MÃ PC (PC TRANSCODER) ----------------
  window.startPcTranscoderStream = async function() {
    const input = document.getElementById('transcoder-url-input') || document.getElementById('stream-url-input');
    const url = (input ? input.value : '').trim();
    if (!url) return alert("Vui lòng dán link YouTube hoặc video cần chuyển mã!");

    const hostInput = document.getElementById('pc-transcoder-host-input');
    let host = (hostInput ? hostInput.value.trim() : '') || localStorage.getItem('pc_transcoder_host') || 'localhost';
    if (!host) host = 'localhost';
    localStorage.setItem('pc_transcoder_host', host);
    if (hostInput) hostInput.value = host;

    const transcoderBase = 'http://' + host + ':5050';
    const esp_ip = window.location.hostname || '192.168.1.233';

    const btn = document.getElementById('btn-pc-transcode');
    if (btn) {
      btn.disabled = true;
      btn.innerHTML = '⏳ Đang phân tích link & nạp luồng...';
    }

    const statusTxt = document.getElementById('transcoder-status-txt');
    if (statusTxt) {
      statusTxt.innerText = "⏳ Đang kết nối Trạm Chuyển Mã PC (" + transcoderBase + ")...";
      statusTxt.style.color = "#f59e0b";
    }

    try {
      let data = null;
      let startOk = false;

      // 1. Gửi trực tiếp từ Trình duyệt tới Transcoder (Ưu tiên số 1: chạy trực tiếp loopback localhost, 0ms proxy, không tải ESP32, không bị tường lửa Windows chặn)
      try {
        const res = await fetch(transcoderBase + '/api/start', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ url: url, esp_ip: esp_ip, fps: 15, resolution: '240x240', volume: 100 })
        });
        if (res.ok) {
          data = await res.json();
          if (data && data.status === 'ok') {
            startOk = true;
          }
        }
      } catch(eDirect) {
        console.warn("Direct fetch to transcoder failed:", eDirect);
      }

      // 2. Nếu gọi trực tiếp chưa thành công (ví dụ mở web từ điện thoại), thử fallback qua ESP32 proxy
      if (!startOk) {
        try {
          const pRes = await fetch('/api/stream/proxy_start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ url: url, host: host, fps: 15, resolution: '240x240', volume: 100 })
          });
          if (pRes.ok) {
            data = await pRes.json();
            if (data && data.status === 'ok') {
              startOk = true;
            }
          }
        } catch(eProxy) {
          console.warn("Proxy start failed:", eProxy);
        }
      }

      if (startOk && data) {
        if (typeof showToast === 'function') {
          showToast("✅ Đang stream: " + (data.title || "Video"), false);
        }
        if (statusTxt) {
          statusTxt.innerText = "🟢 Đang stream: " + (data.title || url);
          statusTxt.style.color = "#10b981";
        }
        updateLiveDot(true, 'PC TRANSCODER');

        // Bật Live Preview thời gian thực trực tiếp trên màn hình ảo ST7789
        const mjpgEl = document.getElementById('stream-live-mjpg');
        const cvEl = document.getElementById('stream-live-canvas');
        if (mjpgEl && cvEl) {
          mjpgEl.src = transcoderBase + '/api/stream.mjpg';
          mjpgEl.style.display = 'block';
          cvEl.style.display = 'none';
        }

        if (window.s_transcoderStatInterval) clearInterval(window.s_transcoderStatInterval);
        window.s_transcoderStatInterval = setInterval(async () => {
          try {
            let st = null;
            try {
              const sres = await fetch(transcoderBase + '/api/status');
              if (sres.ok) st = await sres.json();
            } catch(e) {}
            if (!st) {
              const sres2 = await fetch('/api/stream/status');
              if (sres2.ok) st = await sres2.json();
            }
            if (!st || (!st.session_active && !st.running)) {
              window.stopPcTranscoderStream();
              return;
            }
            const fpsEl = document.getElementById('st-stat-fps');
            if (fpsEl) fpsEl.innerText = (st.live_fps || '15.0') + ' FPS';
            const fEl = document.getElementById('st-stat-frames');
            if (fEl) fEl.innerText = (st.video_frames || st.frames_sent || '0') + ' frames';
            const aEl = document.getElementById('st-stat-audio');
            if (aEl) { aEl.innerText = (st.audio_chunks || st.chunks_sent || '0') + ' KB (UDP)'; aEl.style.color = '#10b981'; }
            const rttEl = document.getElementById('st-stat-rtt');
            if (rttEl) rttEl.innerText = '< 2ms (UDP)';
          } catch(e) {}
        }, 1000);
      } else {
        const errMsg = (data && data.error) ? data.error : ("Không thể kết nối tới " + transcoderBase);
        alert("⚠️ Không thể kết nối tới Trạm Chuyển Mã (" + transcoderBase + ")!\n\n" +
              "1. Kiểm tra bạn đã bật file 'start_transcoder.bat' trên máy tính chưa.\n" +
              "2. Nếu mở web từ điện thoại, vui lòng nhập IP máy tính thay vì localhost.\n\nChi tiết lỗi: " + errMsg);
        if (statusTxt) {
          statusTxt.innerText = "🔴 " + errMsg;
          statusTxt.style.color = "#ef4444";
        }
      }
    } catch(err) {
      console.error("Transcoder error:", err);
      alert("⚠️ Không thể kết nối tới Trạm Chuyển Mã (" + transcoderBase + ")!\n\nHãy kiểm tra xem bạn đã chạy tệp 'start_transcoder.bat' trên máy tính chưa.");
      if (statusTxt) {
        statusTxt.innerText = "🔴 Chưa bật start_transcoder.bat hoặc sai IP";
        statusTxt.style.color = "#ef4444";
      }
    } finally {
      if (btn) {
        btn.disabled = false;
        btn.innerHTML = '🚀 Chuyển Mã & Stream Xuống ESP32';
      }
    }
  };

  window.stopPcTranscoderStream = async function() {
    if (window.s_transcoderStatInterval) {
      clearInterval(window.s_transcoderStatInterval);
      window.s_transcoderStatInterval = null;
    }
    const mjpgEl = document.getElementById('stream-live-mjpg');
    const cvEl = document.getElementById('stream-live-canvas');
    if (mjpgEl && cvEl) {
      mjpgEl.removeAttribute('src');
      mjpgEl.style.display = 'none';
      cvEl.style.display = 'block';
      drawIdlePreviewScreen();
    }
    const hostInput = document.getElementById('pc-transcoder-host-input');
    let host = (hostInput ? hostInput.value.trim() : '') || localStorage.getItem('pc_transcoder_host') || 'localhost';
    try {
      await fetch('http://' + host + ':5050/api/stop', { method: 'POST', mode: 'cors' });
    } catch(e) {}
    try {
      await fetch('/api/stream/proxy_stop', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ host: host })
      });
    } catch(e) {}
    try {
      await fetch('/api/stream/stop', { method: 'POST' });
    } catch(e) {}
    window.stopAllStreams();
    const statusTxt = document.getElementById('transcoder-status-txt');
    if (statusTxt) {
      statusTxt.innerText = "⚪ Đã dừng stream";
      statusTxt.style.color = "#64748b";
    }
  };

  // Mở rộng hàm switchTab để hỗ trợ và kích hoạt Tab 'stream'
  const origSwitchTab_Stream = window.switchTab;
  window.switchTab = function(tabId) {
    injectStreamTabAndPage();
    if (tabId === 'stream') {
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      const stSec = document.getElementById('tab-stream');
      const stBtn = document.getElementById('tab-btn-stream');
      if (stSec) stSec.classList.add('active');
      if (stBtn) {
        stBtn.classList.add('active');
        stBtn.scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'center' });
      }
      setTimeout(() => {
        if (!s_previewCanvas) initStreamElements();
        drawIdlePreviewScreen();
      }, 50);
      return;
    }
    const stSec = document.getElementById('tab-stream');
    const stBtn = document.getElementById('tab-btn-stream');
    if (stSec) stSec.classList.remove('active');
    if (stBtn) stBtn.classList.remove('active');

    if (typeof origSwitchTab_Stream === 'function') origSwitchTab_Stream(tabId);
  };

  setTimeout(injectStreamTabAndPage, 250);
})();
</script>
)rawliteral";

// ---------------- INJECT SCRIPT GIAO DIỆN ĐIỀU KHIỂN DECOR & NGUỒN PIN ----------------
static const char DECOR_PAGE_INJECT_SCRIPT[] PROGMEM = R"rawliteral(
<style>
  .decor-card { background:#0f172a; border:1px solid #1e293b; border-radius:12px; padding:16px; margin-bottom:16px; box-shadow:0 4px 12px rgba(0,0,0,0.25); }
  .decor-title { font-size:0.95rem; font-weight:700; color:#38bdf8; display:flex; justify-content:space-between; align-items:center; margin-bottom:12px; border-bottom:1px solid #1e293b; padding-bottom:8px; }
  .decor-grid-2 { display:grid; grid-template-columns:1fr 1fr; gap:14px; }
  @media(max-width:600px) { .decor-grid-2 { grid-template-columns:1fr; } }
  .decor-tube-card { background:#162032; border:1px solid #334155; border-radius:10px; padding:12px; }
  .decor-preset-grid { display:grid; grid-template-columns:repeat(auto-fit, minmax(130px, 1fr)); gap:8px; margin-bottom:14px; }
  .btn-decor-preset { background:linear-gradient(135deg, #1e293b, #0f172a); border:1px solid #334155; color:#f1f5f9; padding:8px 10px; border-radius:8px; font-size:0.78rem; font-weight:600; cursor:pointer; transition:all 0.15s; display:flex; align-items:center; justify-content:center; gap:6px; }
  .btn-decor-preset:hover { background:linear-gradient(135deg, #0284c7, #0369a1); border-color:#38bdf8; color:#fff; transform:translateY(-1px); box-shadow:0 0 10px rgba(56,189,248,0.3); }
  .decor-slider-row { display:flex; align-items:center; gap:10px; margin-top:8px; }
  .decor-slider-row input[type=range] { flex:1; accent-color:#38bdf8; cursor:pointer; }
  .decor-badge-pill { padding:3px 8px; border-radius:12px; font-size:0.7rem; font-weight:700; }
  .bat-bar-wrap { width:100%; height:14px; background:#1e293b; border-radius:7px; overflow:hidden; position:relative; margin:8px 0; border:1px solid #334155; }
  .bat-bar-fill { height:100%; width:0%; transition:width 0.4s ease, background 0.3s ease; }
</style>
<script>
(function(){
  function injectDecorTabAndPage() {
    const navTabs = document.getElementById('nav-tabs');
    if (!navTabs || document.getElementById('tab-btn-decor')) return;

    // 1. Cập nhật thanh Status Bar ở đầu trang web để hiển thị thông số Pin & Sạc
    const statusBar = document.querySelector('.status-bar .status-left');
    if (statusBar && !document.getElementById('st-bat-box')) {
      const batSpan = document.createElement('span');
      batSpan.id = 'st-bat-box';
      batSpan.innerHTML = 'Nguồn: <strong id="st-bat-txt" style="color:#10b981;">🔋 --V (--%)</strong>';
      statusBar.appendChild(batSpan);
    }

    // 2. Thêm nút Tab "✨ Hiệu Ứng Decor & Nguồn"
    const btn = document.createElement('button');
    btn.id = 'tab-btn-decor';
    btn.className = 'tab-btn';
    btn.innerHTML = '✨ Hiệu Ứng Decor & Nguồn (<span id="tab-decor-bat">--%</span>)';
    btn.onclick = function() { window.switchTab('decor'); };

    const streamTab = document.getElementById('tab-btn-stream');
    const wifiTab = Array.from(navTabs.children).find(c => (c.textContent || '').indexOf('Wi-Fi') >= 0);
    if (streamTab) navTabs.insertBefore(btn, streamTab);
    else if (wifiTab) navTabs.insertBefore(btn, wifiTab);
    else navTabs.appendChild(btn);

    // 3. Tạo Section Page "#tab-decor"
    const sec = document.createElement('div');
    sec.id = 'tab-decor';
    sec.className = 'section';
    sec.innerHTML = `
      <!-- CARD 1: GIÁM SÁT NGUỒN PIN 18650 & BO SẠC MH-CD42 -->
      <div class="decor-card">
        <div class="decor-title">
          <span>🔋 Giám Sát Nguồn & Pin MH-CD42</span>
          <span id="dec-chg-badge" class="decor-badge-pill" style="background:#059669;color:#ecfdf5;">Đang kiểm tra...</span>
        </div>
        <div class="decor-grid-2">
          <div>
            <div style="display:flex;align-items:baseline;gap:8px;">
              <span id="dec-bat-v" style="font-size:1.7rem;font-weight:700;color:#38bdf8;">--</span>
              <span style="font-size:1rem;color:#94a3b8;font-weight:600;">V</span>
              <span id="dec-bat-mv" style="font-size:0.8rem;color:#64748b;margin-left:6px;">(-- mV)</span>
            </div>
            <div class="bat-bar-wrap">
              <div id="dec-bat-fill" class="bat-bar-fill" style="width:0%;background:#10b981;"></div>
            </div>
            <div style="display:flex;justify-content:space-between;font-size:0.75rem;color:#94a3b8;">
              <span>Dung lượng: <strong id="dec-bat-pct" style="color:#f8fafc;">--%</strong></span>
              <span>Kênh: <strong>GPIO 2 (ADC1_CH1)</strong></span>
            </div>
          </div>
          <div style="background:#162032;padding:10px 14px;border-radius:8px;border:1px solid #334155;font-size:0.8rem;">
            <div style="margin-bottom:6px;display:flex;justify-content:space-between;">
              <span style="color:#94a3b8;">Nguồn cung cấp:</span>
              <strong id="dec-src-txt" style="color:#facc15;">--</strong>
            </div>
            <div style="margin-bottom:6px;display:flex;justify-content:space-between;">
              <span style="color:#94a3b8;">Cờ nhận diện sạc:</span>
              <span>GPIO 46 (Vin High/Low)</span>
            </div>
            <div style="display:flex;justify-content:space-between;align-items:center;">
              <span style="color:#94a3b8;">Cảm biến chạm TTP223:</span>
              <span id="dec-touch-status" class="decor-badge-pill" style="background:#334155;color:#e2e8f0;">GPIO 20 (Nhả)</span>
            </div>
          </div>
        </div>
      </div>

      <!-- CARD 2: CỤM ĐÈN ỐNG EDISON FILAMENT (SƯỜN TRÁI & PHẢI) -->
      <div class="decor-card">
        <div class="decor-title">
          <span>💡 Cụm Đèn Ống Edison Sườn Trạm (Filament Tubes)</span>
          <label style="font-size:0.75rem;color:#94a3b8;display:flex;align-items:center;gap:6px;cursor:pointer;">
            <input type="checkbox" id="chk-sync-edison" checked /> Đồng bộ cả 2 ống
          </label>
        </div>

        <div style="font-size:0.76rem;color:#94a3b8;margin-bottom:8px;">Preset 1 Chạm (Áp dụng nhanh cho 2 ống Edison):</div>
        <div class="decor-preset-grid">
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('reactor')">⚡ Lò Phản Ứng</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('vumeter')">🎵 Nhịp Nhạc VU</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('voice')">🤖 Giọng Nói AI</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('smart')">✨ Tự Động Auto</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('candle')">🕯️ Ngọn Nến</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('heartbeat')">💓 Nhịp Tim</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('lightning')">🌩️ Sấm Sét</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('dual_breath')">🌊 Thở Lệch Pha</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('charging')">🔋 Dòng Năng Lượng</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('max')">☀️ Sáng Tối Đa</button>
          <button class="btn-decor-preset" onclick="window.applyDecorPreset('off')">🌑 Tắt Đèn</button>
        </div>

        <div class="decor-grid-2">
          <!-- ỐNG TRÁI -->
          <div class="decor-tube-card">
            <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;">
              <strong style="color:#facc15;font-size:0.85rem;">Ống Sườn TRÁI (GPIO 47)</strong>
              <span id="txt-edl-pct" style="color:#38bdf8;font-size:0.78rem;font-weight:700;">70%</span>
            </div>
            <label style="font-size:0.74rem;color:#94a3b8;">Chế độ hiệu ứng:</label>
            <select id="sel-edl-mode" style="width:100%;padding:6px 10px;background:#0f172a;color:#f8fafc;border:1px solid #334155;border-radius:6px;font-size:0.8rem;margin:4px 0 8px 0;" onchange="window.onEdisonModeChange('edison_left', this.value)">
              <option value="0">0: Tắt (0%)</option>
              <option value="1">1: Dịu nhẹ (25%)</option>
              <option value="2">2: Vừa phải (50%)</option>
              <option value="3">3: Sáng rõ (75%)</option>
              <option value="4">4: Tối đa (100%)</option>
              <option value="5" selected>5: Lò phản ứng (Reactor & PC)</option>
              <option value="6">6: Ngọn nến lung linh (Candle)</option>
              <option value="7">7: Nhịp đập Cyberpunk (Heartbeat)</option>
              <option value="8">8: Sấm sét hồ quang (Lightning)</option>
              <option value="9">9: Thở đối xứng lệch pha (Dual Breath)</option>
              <option value="10">10: Dòng năng lượng sạc (Power Flow)</option>
              <option value="11">11: Nhịp nhạc VU Meter (Stereo Spectrum)</option>
              <option value="12">12: Giọng nói AI (XiaoZhi Voice Reactive)</option>
              <option value="13">13: Tự động thông minh (Smart Fusion)</option>
            </select>
            <div class="decor-slider-row">
              <span style="font-size:0.72rem;color:#64748b;">0%</span>
              <input type="range" id="rng-edl-bright" min="0" max="100" value="70" oninput="window.onEdisonSliderInput('edison_left', this.value)" onchange="window.onEdisonSliderChange('edison_left', this.value)" />
              <span style="font-size:0.72rem;color:#64748b;">100%</span>
            </div>
          </div>

          <!-- ỐNG PHẢI -->
          <div class="decor-tube-card">
            <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;">
              <strong style="color:#facc15;font-size:0.85rem;">Ống Sườn PHẢI (GPIO 21)</strong>
              <span id="txt-edr-pct" style="color:#38bdf8;font-size:0.78rem;font-weight:700;">70%</span>
            </div>
            <label style="font-size:0.74rem;color:#94a3b8;">Chế độ hiệu ứng:</label>
            <select id="sel-edr-mode" style="width:100%;padding:6px 10px;background:#0f172a;color:#f8fafc;border:1px solid #334155;border-radius:6px;font-size:0.8rem;margin:4px 0 8px 0;" onchange="window.onEdisonModeChange('edison_right', this.value)">
              <option value="0">0: Tắt (0%)</option>
              <option value="1">1: Dịu nhẹ (25%)</option>
              <option value="2">2: Vừa phải (50%)</option>
              <option value="3">3: Sáng rõ (75%)</option>
              <option value="4">4: Tối đa (100%)</option>
              <option value="5" selected>5: Lò phản ứng (Reactor & PC)</option>
              <option value="6">6: Ngọn nến lung linh (Candle)</option>
              <option value="7">7: Nhịp đập Cyberpunk (Heartbeat)</option>
              <option value="8">8: Sấm sét hồ quang (Lightning)</option>
              <option value="9">9: Thở đối xứng lệch pha (Dual Breath)</option>
              <option value="10">10: Dòng năng lượng sạc (Power Flow)</option>
              <option value="11">11: Nhịp nhạc VU Meter (Stereo Spectrum)</option>
              <option value="12">12: Giọng nói AI (XiaoZhi Voice Reactive)</option>
              <option value="13">13: Tự động thông minh (Smart Fusion)</option>
            </select>
            <div class="decor-slider-row">
              <span style="font-size:0.72rem;color:#64748b;">0%</span>
              <input type="range" id="rng-edr-bright" min="0" max="100" value="70" oninput="window.onEdisonSliderInput('edison_right', this.value)" onchange="window.onEdisonSliderChange('edison_right', this.value)" />
              <span style="font-size:0.72rem;color:#64748b;">100%</span>
            </div>
          </div>
        </div>
      </div>

      <!-- CARD 3 & 4: CỐI XAY GIÓ & LED NƯỚC CHẢY COB -->
      <div class="decor-grid-2">
        <!-- CỐI XAY GIÓ L298N -->
        <div class="decor-card">
          <div class="decor-title">
            <span>🎡 Cối Xay Gió (GPIO 1)</span>
            <span id="txt-wm-speed" style="font-size:0.78rem;color:#38bdf8;font-weight:700;">0%</span>
          </div>
          <div style="display:flex;gap:6px;flex-wrap:wrap;margin-bottom:10px;">
            <button class="btn-secondary" style="padding:4px 8px;font-size:0.72rem;" onclick="window.sendDecorControl({target:'windmill',mode:0})">Tắt (0%)</button>
            <button class="btn-secondary" style="padding:4px 8px;font-size:0.72rem;" onclick="window.sendDecorControl({target:'windmill',mode:1})">Êm (35%)</button>
            <button class="btn-secondary" style="padding:4px 8px;font-size:0.72rem;" onclick="window.sendDecorControl({target:'windmill',mode:2})">Vừa (60%)</button>
            <button class="btn-secondary" style="padding:4px 8px;font-size:0.72rem;" onclick="window.sendDecorControl({target:'windmill',mode:3})">Nhanh (85%)</button>
            <button class="btn-secondary" style="padding:4px 8px;font-size:0.72rem;" onclick="window.sendDecorControl({target:'windmill',mode:4})">Thở</button>
          </div>
          <div class="decor-slider-row">
            <span style="font-size:0.72rem;color:#64748b;">0%</span>
            <input type="range" id="rng-wm-speed" min="0" max="100" value="0" oninput="document.getElementById('txt-wm-speed').innerText = this.value + '%'" onchange="window.sendDecorControl({target:'windmill',speed:parseInt(this.value)})" />
            <span style="font-size:0.72rem;color:#64748b;">100%</span>
          </div>
        </div>

        <!-- LED NƯỚC CHẢY COB MẶT LƯNG -->
        <div class="decor-card">
          <div class="decor-title">
            <span>🌊 LED Nước Chảy COB</span>
            <span id="dec-flow-status" class="decor-badge-pill" style="background:#059669;color:#ecfdf5;">Đang BẬT</span>
          </div>
          <div style="font-size:0.78rem;color:#94a3b8;margin-bottom:10px;">
            Vị trí: 2 thanh mặt lưng (Trái: GPIO 38, Phải: GPIO 18). IC quét tự động.
          </div>
          <div style="display:flex;gap:8px;">
            <button class="btn-secondary" style="flex:1;padding:8px;" onclick="window.sendDecorControl({target:'flowing',action:'toggle'})">🔘 Bật / Tắt Nguồn</button>
            <button class="btn-primary" style="flex:1;padding:8px;background:linear-gradient(135deg,#0284c7,#0369a1);" onclick="window.sendDecorControl({target:'flowing',action:'next_mode'})">⚡ Chuyển Mode IC</button>
          </div>
        </div>
      </div>
    `;
    const tabImages = document.getElementById('tab-images');
    if (tabImages && tabImages.parentNode) {
      tabImages.parentNode.insertBefore(sec, tabImages);
    } else {
      document.querySelector('.container').appendChild(sec);
    }
  }

  window.sendDecorControl = async function(payload) {
    try {
      const res = await fetch('/api/decor/control', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      if (res.ok) {
        const data = await res.json();
        renderDecorState(data);
      }
    } catch(e) {
      console.warn("Decor control failed:", e);
    }
  };

  window.applyDecorPreset = function(preset) {
    window.sendDecorControl({ preset: preset });
  };

  window.onEdisonModeChange = function(target, modeVal) {
    const isSync = document.getElementById('chk-sync-edison')?.checked;
    const finalTarget = isSync ? 'edison_both' : target;
    window.sendDecorControl({ target: finalTarget, mode: parseInt(modeVal) });
  };

  window.onEdisonSliderInput = function(target, val) {
    const isSync = document.getElementById('chk-sync-edison')?.checked;
    if (target === 'edison_left' || isSync) {
      const el = document.getElementById('txt-edl-pct');
      if (el) el.innerText = val + '%';
      const rng = document.getElementById('rng-edl-bright');
      if (rng && rng.value != val) rng.value = val;
    }
    if (target === 'edison_right' || isSync) {
      const el = document.getElementById('txt-edr-pct');
      if (el) el.innerText = val + '%';
      const rng = document.getElementById('rng-edr-bright');
      if (rng && rng.value != val) rng.value = val;
    }
  };

  window.onEdisonSliderChange = function(target, val) {
    const isSync = document.getElementById('chk-sync-edison')?.checked;
    const finalTarget = isSync ? 'edison_both' : target;
    window.sendDecorControl({ target: finalTarget, brightness: parseInt(val) });
  };

  function renderDecorState(d) {
    if (!d) return;
    // 1. Cụm Pin & Sạc
    if (d.battery) {
      const b = d.battery;
      const vEl = document.getElementById('dec-bat-v');
      const mvEl = document.getElementById('dec-bat-mv');
      const pctEl = document.getElementById('dec-bat-pct');
      const fillEl = document.getElementById('dec-bat-fill');
      const chgBadge = document.getElementById('dec-chg-badge');
      const srcTxt = document.getElementById('dec-src-txt');
      const tabBat = document.getElementById('tab-decor-bat');
      const stBat = document.getElementById('st-bat-txt');

      if (vEl) vEl.innerText = Number(b.voltage || 0).toFixed(2);
      if (mvEl) mvEl.innerText = '(' + (b.millivolts || 0) + ' mV)';
      if (pctEl) pctEl.innerText = (b.percent || 0) + '%';
      if (tabBat) tabBat.innerText = (b.percent || 0) + '%';
      if (fillEl) {
        fillEl.style.width = (b.percent || 0) + '%';
        if (b.percent > 50) fillEl.style.background = '#10b981';
        else if (b.percent > 20) fillEl.style.background = '#f59e0b';
        else fillEl.style.background = '#ef4444';
      }
      if (chgBadge) {
        if (b.is_charging) {
          chgBadge.className = 'decor-badge-pill';
          chgBadge.style.background = '#059669';
          chgBadge.style.color = '#ecfdf5';
          chgBadge.innerText = '⚡ ĐANG SẠC PIN (Vin = HIGH)';
        } else {
          chgBadge.className = 'decor-badge-pill';
          chgBadge.style.background = '#334155';
          chgBadge.style.color = '#cbd5e1';
          chgBadge.innerText = '🔋 DÙNG PIN LITHIUM (Vin = LOW)';
        }
      }
      if (srcTxt) srcTxt.innerText = b.charging_source || (b.is_charging ? 'Vin MH-CD42' : 'Pin 18650');
      if (stBat) {
        stBat.innerText = (b.is_charging ? '⚡ ' : '🔋 ') + Number(b.voltage || 0).toFixed(2) + 'V (' + (b.percent || 0) + '%)';
        stBat.style.color = b.is_charging ? '#38bdf8' : (b.percent > 20 ? '#10b981' : '#ef4444');
      }
    }

    // 2. Touch
    if (d.touch) {
      const tEl = document.getElementById('dec-touch-status');
      if (tEl) {
        tEl.innerText = d.touch.pressed ? '🟢 ĐANG CHẠM' : 'GPIO 20 (Nhả)';
        tEl.style.background = d.touch.pressed ? '#059669' : '#334155';
      }
    }

    // 3. Edison
    if (d.edison_left) {
      const edL = d.edison_left;
      const selL = document.getElementById('sel-edl-mode');
      const rngL = document.getElementById('rng-edl-bright');
      const txtL = document.getElementById('txt-edl-pct');
      if (selL) selL.value = edL.mode;
      if (rngL) rngL.value = edL.brightness;
      if (txtL) txtL.innerText = edL.brightness + '%';
    }
    if (d.edison_right) {
      const edR = d.edison_right;
      const selR = document.getElementById('sel-edr-mode');
      const rngR = document.getElementById('rng-edr-bright');
      const txtR = document.getElementById('txt-edr-pct');
      if (selR) selR.value = edR.mode;
      if (rngR) rngR.value = edR.brightness;
      if (txtR) txtR.innerText = edR.brightness + '%';
    }

    // 4. Windmill
    if (d.windmill) {
      const wm = d.windmill;
      const rngWm = document.getElementById('rng-wm-speed');
      const txtWm = document.getElementById('txt-wm-speed');
      if (rngWm) rngWm.value = wm.speed;
      if (txtWm) txtWm.innerText = wm.speed + '%';
    }

    // 5. Flowing Leds
    if (d.flowing_leds) {
      const fl = d.flowing_leds;
      const flBadge = document.getElementById('dec-flow-status');
      if (flBadge) {
        flBadge.innerText = fl.is_on ? 'Đang BẬT (' + (fl.mode_name || '') + ')' : 'Đang TẮT';
        flBadge.style.background = fl.is_on ? '#059669' : '#475569';
      }
    }
  }

  window.fetchDecorStatus = async function() {
    try {
      const res = await fetch('/api/decor/status');
      if (res.ok) {
        const d = await res.json();
        renderDecorState(d);
      }
    } catch(e) {}
  };

  // Mở rộng window.switchTab cho tab 'decor'
  const origSwitchTab_Decor = window.switchTab;
  window.switchTab = function(tabId) {
    injectDecorTabAndPage();
    if (tabId === 'decor') {
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      const decSec = document.getElementById('tab-decor');
      const decBtn = document.getElementById('tab-btn-decor');
      if (decSec) decSec.classList.add('active');
      if (decBtn) {
        decBtn.classList.add('active');
        decBtn.scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'center' });
      }
      window.fetchDecorStatus();
      return;
    }
    const decSec = document.getElementById('tab-decor');
    const decBtn = document.getElementById('tab-btn-decor');
    if (decSec) decSec.classList.remove('active');
    if (decBtn) decBtn.classList.remove('active');

    if (typeof origSwitchTab_Decor === 'function') origSwitchTab_Decor(tabId);
  };

  setTimeout(() => {
    injectDecorTabAndPage();
    window.fetchDecorStatus();
    setInterval(window.fetchDecorStatus, 3000);
  }, 350);
})();
</script>
)rawliteral";

static const char KEYPAD_PAGE_INJECT_SCRIPT[] PROGMEM = R"rawliteral(
<script>
(function(){
  const KEYPAD_STYLES = `
    .keypad-sim-container {
      display: flex;
      gap: 24px;
      align-items: flex-start;
      justify-content: center;
      flex-wrap: wrap;
      margin-top: 10px;
    }
    .keypad-screen-col {
      flex: 0 0 310px;
      max-width: 320px;
    }
    .keypad-controls-col {
      flex: 1 1 360px;
      max-width: 520px;
      display: flex;
      flex-direction: column;
      gap: 16px;
    }
    /* Khung Màn hình Bezel Cyberpunk ST7789 */
    .sim-bezel {
      background: linear-gradient(145deg, #0e1726, #090e18);
      border: 3px solid #1e293b;
      border-radius: 18px;
      box-shadow: 0 15px 35px rgba(0,0,0,0.7), inset 0 0 20px rgba(6,182,212,0.12);
      padding: 12px;
      display: flex;
      flex-direction: column;
      user-select: none;
    }
    .sim-bezel-head {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding-bottom: 8px;
      border-bottom: 1px solid rgba(255,255,255,0.06);
      margin-bottom: 8px;
      font-size: 0.68rem;
      font-weight: 700;
      color: #94a3b8;
      letter-spacing: 0.5px;
    }
    .sim-live-led {
      width: 9px;
      height: 9px;
      border-radius: 50%;
      background: #10b981;
      box-shadow: 0 0 10px #10b981;
      transition: all 0.2s ease;
    }
    .sim-live-led.blink {
      background: #38bdf8;
      box-shadow: 0 0 14px #38bdf8;
      transform: scale(1.3);
    }
    /* Vùng hiển thị màn hình ST7789 (Tỉ lệ chuẩn dọc 240x320) */
    .sim-screen-body {
      width: 100%;
      height: 380px;
      background: #000;
      border-radius: 8px;
      overflow: hidden;
      position: relative;
      border: 1px solid #1e293b;
      box-shadow: inset 0 0 15px rgba(0,0,0,0.9);
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    }
    .sim-view {
      width: 100%;
      height: 100%;
      position: absolute;
      top: 0;
      left: 0;
      display: flex;
      flex-direction: column;
      box-sizing: border-box;
      padding: 12px;
    }
    /* 1. STANDBY VIEW - ĐỒNG BỘ 100% VỚI MÀN HÌNH ESP32 VÀ TAB MÀN HÌNH DEMO & THIẾT KẾ */
    #sim-view-standby {
      background-size: cover;
      background-position: center;
      background-repeat: no-repeat;
      color: #fff;
      position: relative;
      padding: 10px 10px 8px 10px;
      overflow: hidden;
    }
    .sim-standby-dim {
      position: absolute;
      top: 0; left: 0; right: 0; bottom: 0;
      background: rgba(0,0,0,0.35);
      pointer-events: none;
      z-index: 1;
      transition: background 0.25s ease;
    }
    .sim-standby-content {
      position: relative;
      z-index: 2;
      width: 100%;
      height: 100%;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      align-items: center;
      box-sizing: border-box;
    }
    .sim-topbar {
      width: 100%;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .sim-pill {
      background: rgba(15, 23, 42, 0.75);
      backdrop-filter: blur(4px);
      border: 1px solid rgba(56, 189, 248, 0.35);
      padding: 3px 8px;
      border-radius: 12px;
      font-size: 0.65rem;
      font-weight: 700;
      color: #cbd5e1;
      display: flex;
      align-items: center;
      gap: 5px;
      box-shadow: 0 2px 6px rgba(0,0,0,0.5);
    }
    .sim-pill-node {
      color: #38bdf8;
      border-color: rgba(56, 189, 248, 0.4);
    }
    .sim-dot-wifi {
      color: #10b981;
      font-size: 0.65rem;
      line-height: 1;
      filter: drop-shadow(0 0 5px #10b981);
    }
    .sim-clock-area {
      text-align: center;
      margin: auto 0;
      width: 100%;
    }
    .sim-clock-time {
      font-size: 2.75rem;
      font-weight: 800;
      color: #38bdf8;
      letter-spacing: 1px;
      text-shadow: 0 0 16px rgba(56, 189, 248, 0.7);
      line-height: 1;
      font-variant-numeric: tabular-nums;
      margin-bottom: 4px;
      transition: all 0.2s ease;
    }
    .sim-clock-sec {
      font-size: 1.15rem;
      color: #94a3b8;
      opacity: 0.85;
      font-weight: 500;
      margin-left: 2px;
    }
    .sim-clock-date {
      font-size: 0.75rem;
      color: #e2e8f0;
      font-weight: 600;
      text-shadow: 0 1px 3px rgba(0,0,0,0.8);
      margin-top: 4px;
    }
    .sim-footer-area {
      width: 100%;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 4px;
    }
    .sim-weather-pill {
      background: rgba(15, 23, 42, 0.75);
      backdrop-filter: blur(4px);
      border: 1px solid rgba(255, 255, 255, 0.2);
      padding: 3px 10px;
      border-radius: 12px;
      font-size: 0.68rem;
      color: #f1f5f9;
      box-shadow: 0 2px 6px rgba(0,0,0,0.4);
    }
    .sim-slogan-text {
      font-size: 0.7rem;
      color: #fbbf24;
      font-style: italic;
      text-shadow: 0 1px 3px #000;
      max-width: 90%;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .sim-power-tag {
      font-size: 0.62rem;
      color: #34d399;
      font-weight: bold;
      background: rgba(16, 185, 129, 0.15);
      border: 1px solid rgba(16, 185, 129, 0.3);
      padding: 2px 8px;
      border-radius: 6px;
    }

    /* 2. S40 MENU VIEW - ĐỒNG BỘ 100% TIÊU ĐỀ FIRMWARE */
    #sim-view-s40 {
      background: #020617;
      padding: 6px;
    }
    .s40-header-bar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      background: linear-gradient(90deg, #1e3a8a, #0369a1);
      padding: 4px 8px;
      border-radius: 4px;
      font-size: 0.75rem;
      font-weight: bold;
      color: #fff;
      margin-bottom: 4px;
    }
    .s40-desc-banner {
      background: #0f172a;
      border: 1px solid #1e293b;
      padding: 4px 6px;
      font-size: 0.68rem;
      color: #fbbf24;
      font-weight: 600;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      margin-bottom: 6px;
      border-radius: 4px;
    }
    .s40-grid-viewport {
      flex: 1;
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 4px;
      overflow-y: hidden;
    }
    .s40-cell {
      background: #111827;
      border: 1px solid #1f2937;
      border-radius: 6px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 4px 2px;
      transition: all 0.15s ease;
      cursor: pointer;
    }
    .s40-cell.selected {
      background: #1e3a8a;
      border: 2px solid #fbbf24;
      box-shadow: 0 0 10px rgba(251, 191, 36, 0.6);
      transform: scale(1.04);
    }
    .s40-cell-icon { font-size: 1.25rem; line-height: 1.1; }
    .s40-cell-name { font-size: 0.58rem; color: #cbd5e1; margin-top: 2px; text-align: center; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 90%; }
    .s40-cell.selected .s40-cell-name { color: #fbbf24; font-weight: bold; }
    .s40-bottom-softkeys {
      display: flex;
      justify-content: space-between;
      padding-top: 4px;
      font-size: 0.68rem;
      font-weight: bold;
      color: #94a3b8;
      border-top: 1px solid #1e293b;
      margin-top: 4px;
    }

    /* 3. PC HUD VIEW */
    #sim-view-pchud {
      background: #020617;
      color: #fff;
    }
    .pchud-header { font-size: 0.72rem; font-weight: 800; color: #38bdf8; border-bottom: 1px solid #1e293b; padding-bottom: 4px; margin-bottom: 8px; }
    .pchud-meter { margin-bottom: 10px; }
    .pchud-meter .m-lbl { font-size: 0.68rem; font-weight: 700; color: #94a3b8; }
    .pchud-meter .m-bar { height: 10px; background: #1e293b; border-radius: 5px; overflow: hidden; margin: 2px 0; }
    .pchud-meter .m-bar div { height: 100%; background: linear-gradient(90deg, #06b6d4, #3b82f6); border-radius: 5px; transition: width 0.3s ease; }
    .pchud-meter .m-val { font-size: 0.75rem; font-weight: 800; color: #38bdf8; float: right; margin-top: -16px; }

    /* 4. XIAOZHI VIEW */
    #sim-view-xiaozhi {
      background: #000;
      color: #fff;
      align-items: center;
      justify-content: space-between;
    }
    .sim-eyes-stage { display: flex; gap: 28px; margin-top: 35px; }
    .sim-eye {
      width: 42px;
      height: 62px;
      background: #00f0ff;
      border-radius: 20px;
      box-shadow: 0 0 25px #00f0ff;
      animation: simEyeBlink 4s infinite;
    }
    @keyframes simEyeBlink {
      0%, 94%, 100% { transform: scaleY(1); }
      96% { transform: scaleY(0.08); }
    }
    .sim-sub-box {
      width: 100%;
      background: #111827;
      border: 1px solid #1f2937;
      border-radius: 8px;
      padding: 8px;
      margin-bottom: 6px;
    }
    .sim-sub-sender { font-size: 0.65rem; color: #38bdf8; font-weight: bold; margin-bottom: 2px; }
    .sim-sub-content { font-size: 0.75rem; color: #f3f4f6; line-height: 1.3; }

    /* 5. MEDIA VIEW */
    #sim-view-media { background: #030712; color: #fff; align-items: center; justify-content: center; }
    .vinyl-disc-spin { font-size: 3.5rem; animation: spinDisc 3s linear infinite; margin: 15px 0; }
    @keyframes spinDisc { 100% { transform: rotate(360deg); } }
    .visualizer-bars { display: flex; gap: 4px; align-items: flex-end; height: 35px; margin-bottom: 15px; }
    .visualizer-bars .vbar { width: 8px; background: #38bdf8; border-radius: 3px; animation: bounceBar 0.6s infinite alternate ease-in-out; }
    .visualizer-bars .vbar:nth-child(2) { animation-delay: 0.1s; }
    .visualizer-bars .vbar:nth-child(3) { animation-delay: 0.2s; }
    .visualizer-bars .vbar:nth-child(4) { animation-delay: 0.3s; }
    .visualizer-bars .vbar:nth-child(5) { animation-delay: 0.15s; }
    .visualizer-bars .vbar:nth-child(6) { animation-delay: 0.25s; }
    @keyframes bounceBar { 0% { height: 6px; } 100% { height: 32px; background: #ec4899; } }

    /* 6. DIAG VIEW */
    #sim-view-diag { background: #050b14; color: #38bdf8; font-family: monospace; }
    .diag-header { font-size: 0.75rem; font-weight: bold; color: #fbbf24; border-bottom: 1px solid #1e293b; padding-bottom: 4px; margin-bottom: 10px; }
    .diag-info-list div { margin-bottom: 8px; font-size: 0.72rem; }

    /* Bezel Foot */
    .sim-bezel-foot {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding-top: 8px;
      margin-top: 6px;
      border-top: 1px solid rgba(255,255,255,0.06);
    }
    .sim-foot-status { display: flex; gap: 6px; align-items: center; }
    .sim-foot-status .mode-tag { background: #0284c7; color: #fff; font-size: 0.62rem; font-weight: 800; padding: 2px 6px; border-radius: 4px; }
    .sim-foot-status .title-tag { font-size: 0.7rem; color: #cbd5e1; }
    .btn-refresh-sim { background: #1e293b; border: 1px solid #334155; color: #38bdf8; border-radius: 6px; padding: 2px 8px; cursor: pointer; }

    /* --- BẢNG THÔNG SỐ ĐO ADC (CỘT PHẢI) --- */
    .adc-matrix-card {
      background: #0f172a;
      border: 1px solid #1e293b;
      border-radius: 12px;
      padding: 12px 16px;
      box-shadow: 0 4px 15px rgba(0,0,0,0.4);
    }
    .adc-matrix-head {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 10px;
      font-size: 0.8rem;
      font-weight: 700;
      color: #e2e8f0;
    }
    .hud-chip {
      background: rgba(56, 189, 248, 0.15);
      border: 1px solid #0284c7;
      color: #38bdf8;
      font-size: 0.65rem;
      padding: 2px 6px;
      border-radius: 4px;
      font-weight: bold;
    }
    .adc-grid-stats {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 10px;
    }
    .adc-cell {
      background: #090e17;
      border: 1px solid #1e293b;
      border-radius: 8px;
      padding: 8px 10px;
      display: flex;
      flex-direction: column;
    }
    .adc-cell .lbl { font-size: 0.68rem; color: #64748b; font-weight: 600; text-transform: uppercase; margin-bottom: 2px; }
    .adc-cell .val { font-size: 0.92rem; font-weight: 800; font-family: monospace; }

    /* --- DÀN PHÍM ẢO MÔ PHỎNG (D-PAD CHỮ THẬP + FUNCTION) --- */
    .virtual-dpad-box {
      background: linear-gradient(145deg, #131c2e, #0a0f1d);
      border: 2px solid #1e293b;
      border-radius: 16px;
      padding: 18px 14px;
      box-shadow: 0 10px 25px rgba(0,0,0,0.5);
      display: flex;
      flex-direction: column;
      gap: 10px;
      user-select: none;
    }
    .dpad-row {
      display: flex;
      justify-content: center;
      align-items: center;
      gap: 10px;
    }
    .dpad-btn {
      background: linear-gradient(145deg, #1e293b, #0f172a);
      border: 2px solid #334155;
      color: #f8fafc;
      border-radius: 12px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      cursor: pointer;
      box-shadow: 0 5px 0 #090d16, 0 8px 15px rgba(0,0,0,0.4);
      transition: all 0.08s ease;
      touch-action: manipulation;
    }
    .dpad-btn:active, .dpad-btn.pressed {
      transform: translateY(4px);
      box-shadow: 0 1px 0 #090d16, 0 0 16px rgba(56, 189, 248, 0.8);
      border-color: #38bdf8;
      background: #0284c7;
      color: #fff;
    }
    .dpad-btn .btn-arrow { font-size: 1.25rem; font-weight: 900; line-height: 1; }
    .dpad-btn .btn-text { font-size: 0.72rem; font-weight: 800; margin-top: 2px; }
    .dpad-btn .btn-sub { font-size: 0.6rem; color: #94a3b8; font-weight: 500; }
    .dpad-btn:active .btn-sub, .dpad-btn.pressed .btn-sub { color: #e0f2fe; }

    /* Kích thước D-Pad chính */
    .btn-up, .btn-down { width: 130px; height: 60px; }
    .btn-left, .btn-right { width: 115px; height: 65px; }
    .btn-ok {
      width: 110px;
      height: 65px;
      background: linear-gradient(145deg, #0369a1, #0c4a6e);
      border-color: #0284c7;
      color: #fff;
      box-shadow: 0 5px 0 #082f49, 0 8px 15px rgba(2, 132, 199, 0.4);
    }
    .btn-ok:active, .btn-ok.pressed {
      background: #0284c7;
      border-color: #38bdf8;
      box-shadow: 0 1px 0 #082f49, 0 0 20px #38bdf8;
    }
    .btn-center-ring { font-size: 1.1rem; color: #38bdf8; }

    /* Phím chức năng Menu & Exit */
    .function-row {
      display: flex;
      gap: 12px;
      justify-content: center;
      margin-top: 6px;
    }
    .btn-func {
      flex: 1;
      height: 52px;
      border-radius: 10px;
      flex-direction: row;
      gap: 8px;
    }
    .btn-func .btn-icon { font-size: 1.1rem; }
    .btn-func .btn-text { font-size: 0.75rem; }
    .btn-menu {
      border-color: #6366f1;
      background: linear-gradient(145deg, #312e81, #1e1b4b);
    }
    .btn-menu:active, .btn-menu.pressed {
      background: #4f46e5;
      border-color: #a5b4fc;
      box-shadow: 0 1px 0 #1e1b4b, 0 0 15px rgba(99, 102, 241, 0.8);
    }
    .btn-exit {
      border-color: #e11d48;
      background: linear-gradient(145deg, #881337, #4c0519);
    }
    .btn-exit:active, .btn-exit.pressed {
      background: #e11d48;
      border-color: #fda4af;
      box-shadow: 0 1px 0 #4c0519, 0 0 15px rgba(225, 29, 72, 0.8);
    }

    /* Phím Chạm Touch TTP223 */
    .touch-row { margin-top: 4px; }
    .btn-touch {
      width: 100%;
      height: 48px;
      background: linear-gradient(145deg, #854d0e, #451a03);
      border-color: #eab308;
      color: #fef08a;
      flex-direction: row;
      gap: 8px;
    }
    .btn-touch:active, .btn-touch.pressed {
      background: #ca8a04;
      border-color: #fef08a;
      box-shadow: 0 1px 0 #451a03, 0 0 18px rgba(234, 179, 8, 0.8);
      color: #fff;
    }

    .keyboard-shortcuts-pill {
      background: #0f172a;
      border: 1px dashed #334155;
      border-radius: 8px;
      padding: 8px 12px;
      font-size: 0.72rem;
      color: #94a3b8;
      line-height: 1.5;
    }
    .keyboard-shortcuts-pill kbd {
      background: #1e293b;
      border: 1px solid #475569;
      color: #38bdf8;
      padding: 1px 5px;
      border-radius: 4px;
      font-weight: bold;
      font-size: 0.7rem;
    }
  `;

  // 15 TIÊU ĐỀ & MÔ TẢ ĐỒNG BỘ CHÍNH XÁC 100% VỚI S40_MENU_TITLES DƯỚI FIRMWARE ESP32
  const S40_APP_LIST = [
    { id: 0,  title: "Hình nền",  icon: "🖼️", desc: "1. Tùy chỉnh Hình nền & Đồng hồ chờ", tab: "screen" },
    { id: 1,  title: "Cài đặt",   icon: "⚙️", desc: "2. Giao diện (6 Chủ đề), Độ sáng & Loa", tab: "screen" },
    { id: 2,  title: "Máy tính",  icon: "📊", desc: "3. Giám sát Hiệu năng Máy tính (PC)", mode: "pchud" },
    { id: 3,  title: "Thư viện",  icon: "🌄", desc: "4. Bộ sưu tập Hình ảnh & Ảnh nền", tab: "images" },
    { id: 4,  title: "Bộ nhớ",    icon: "📁", desc: "5. Quản lý Tệp tin & Bộ nhớ lưu trữ", tab: "images" },
    { id: 5,  title: "Trợ lý AI", icon: "🤖", desc: "6. Trò chuyện Giọng nói XiaoZhi AI", tab: "chat" },
    { id: 6,  title: "Âm nhạc",   icon: "🎵", desc: "7. Trình phát Âm nhạc & Sóng nhạc", tab: "music" },
    { id: 7,  title: "Đồng hồ",   icon: "⏰", desc: "8. Đồng hồ: Báo thức, Bấm giờ & Hẹn giờ", mode: "clock" },
    { id: 8,  title: "Lịch",      icon: "📅", desc: "9. Lịch Vạn Niên & Tra cứu Ngày tháng", mode: "calendar" },
    { id: 9,  title: "Thiết bị",  icon: "ℹ️", desc: "10. Thông tin Thiết bị & Kết nối Mạng", tab: "system" },
    { id: 10, title: "Sóng âm",   icon: "🧪", desc: "11. Biểu đồ Sóng âm, Cảm biến & Thẻ SD", mode: "diag" },
    { id: 11, title: "Trò chơi",  icon: "🎮", desc: "12. Trò chơi: Rắn săn mồi, Flappy & Xếp gạch", mode: "game" },
    { id: 12, title: "Mã QR",     icon: "📱", desc: "13. Quét Mã QR Cài Đặt Wi-Fi & Quản Lý", tab: "wifi" },
    { id: 13, title: "Kết nối",   icon: "📶", desc: "14. Quản Lý Mạng Wi-Fi Đã Lưu & Chuyển Sóng", tab: "wifi", mode: "connect" },
    { id: 14, title: "Hệ thống",  icon: "📊", desc: "15. Giám Sát Hệ Thống Telemetry (1MB PSRAM)", tab: "system", mode: "telemetry" }
  ];

  let currentS40Cursor = 0;

  function injectKeypadStyles() {
    if (document.getElementById('keypad-injected-styles')) return;
    const s = document.createElement('style');
    s.id = 'keypad-injected-styles';
    s.innerHTML = KEYPAD_STYLES;
    document.head.appendChild(s);
  }

  function injectKeypadTabAndPage() {
    const navTabs = document.getElementById('nav-tabs');
    if (!navTabs || document.getElementById('tab-btn-keypad')) return;

    injectKeypadStyles();

    // 1. Tạo nút tab mới
    const btn = document.createElement('button');
    btn.className = 'tab-btn';
    btn.id = 'tab-btn-keypad';
    btn.innerHTML = '🎮 Bàn Phím Ảo (Keypad)';
    btn.onclick = () => window.switchTab('keypad');

    const streamTab = Array.from(navTabs.children).find(c => (c.textContent || '').indexOf('Web Streamer') >= 0);
    const decorTab = Array.from(navTabs.children).find(c => (c.textContent || '').indexOf('Decor') >= 0);
    if (streamTab) navTabs.insertBefore(btn, streamTab);
    else if (decorTab) navTabs.insertBefore(btn, decorTab.nextSibling);
    else navTabs.appendChild(btn);

    // 2. Tạo phần nội dung Tab
    const mainContainer = document.querySelector('.main-content') || document.querySelector('.container') || document.body;
    const sec = document.createElement('div');
    sec.id = 'tab-keypad';
    sec.className = 'section';
    sec.innerHTML = `
      <div class="keypad-sim-container">
        <!-- CỘT TRÁI: MÀN HÌNH DEMO ST7789 ĐỒNG BỘ THỜI GIAN THỰC -->
        <div class="keypad-screen-col">
          <div class="sim-bezel">
            <div class="sim-bezel-head">
              <span>ESP32-S3 // ST7789 LIVE SYNC</span>
              <div class="sim-live-led" id="sim-live-led"></div>
            </div>

            <div class="sim-screen-body" id="sim-screen-body">
              <!-- Mode 0: Standby (Đồng bộ ảnh nền & màu chữ với Tab Màn hình Demo & thiết kế) -->
              <div class="sim-view" id="sim-view-standby">
                <div class="sim-standby-dim" id="sim-standby-dim"></div>
                <div class="sim-standby-content">
                  <!-- Top Bar: Wi-Fi Pill (trái) & ESP32-S3 (phải) giống hệt thiết bị thật -->
                  <div class="sim-topbar">
                    <div class="sim-pill sim-pill-wifi" id="sim-pill-wifi">
                      <svg id="sim-wifi-svg" width="13" height="11" viewBox="0 0 24 20" style="vertical-align:middle;margin-right:2px;fill:none;stroke:#10b981;stroke-width:2.5;stroke-linecap:round;">
                        <path d="M1 5a18.5 18.5 0 0 1 22 0" id="sim-wifi-arc3"/>
                        <path d="M5 9a12.5 12.5 0 0 1 14 0" id="sim-wifi-arc2"/>
                        <path d="M9 13a6.5 6.5 0 0 1 6 0" id="sim-wifi-arc1"/>
                        <circle cx="12" cy="18" r="1.5" fill="#10b981" id="sim-wifi-dot"/>
                      </svg>
                      <span id="sim-wifi-ssid">STA</span>
                    </div>
                    <div class="sim-pill sim-pill-node">
                      <span>ESP32-S3</span>
                    </div>
                  </div>

                  <!-- Đồng hồ số trung tâm (chạy thời gian thực từng giây) -->
                  <div class="sim-clock-area">
                    <div class="sim-clock-time" id="sim-clock">12:00<span class="sim-clock-sec" id="sim-sec">:00</span></div>
                    <div class="sim-clock-date" id="sim-date">Chủ Nhật, 18/10/2026</div>
                  </div>

                  <!-- Footer thời tiết, slogan, pin sạc -->
                  <div class="sim-footer-area">
                    <div class="sim-weather-pill" id="sim-weather">🌡️ 28.5°C | 💧 65%</div>
                    <div class="sim-slogan-text" id="sim-slogan">Trạm Decor Vũ Trụ ✨</div>
                    <div class="sim-power-tag" id="sim-power-tag">🔋 4.05V (88%)</div>
                  </div>
                </div>
              </div>

              <!-- Mode 4: Symbian S40 Menu (Đồng bộ 14 app) -->
              <div class="sim-view" id="sim-view-s40" style="display:none;">
                <div class="s40-header-bar">
                  <span id="s40-head-title">MENU CHÍNH</span>
                  <span>📶 🔋</span>
                </div>
                <div class="s40-desc-banner" id="s40-desc-banner">1. Tùy chỉnh Hình nền & Đồng hồ chờ</div>
                <div class="s40-grid-viewport" id="s40-grid-cells"></div>
                <div class="s40-bottom-softkeys">
                  <span style="color:#38bdf8; cursor:pointer;" onclick="window.triggerS40OpenCurrentApp()">Chọn (OK)</span>
                  <span id="s40-cur-label">1/14</span>
                  <span style="cursor:pointer;" onclick="window.pressKeypadKey(6)">Thoát</span>
                </div>
              </div>

              <!-- Mode 2: PC HUD -->
              <div class="sim-view" id="sim-view-pchud" style="display:none;">
                <div class="pchud-header">PC STATUS TELEMETRY // HUD</div>
                <div class="pchud-meter">
                  <span class="m-lbl">CPU Intel Core</span>
                  <span class="m-val" id="sim-cpu-val">42%</span>
                  <div class="m-bar"><div id="sim-cpu-bar" style="width:42%"></div></div>
                </div>
                <div class="pchud-meter">
                  <span class="m-lbl">GPU Graphics</span>
                  <span class="m-val" id="sim-gpu-val">28%</span>
                  <div class="m-bar"><div id="sim-gpu-bar" style="width:28%; background:linear-gradient(90deg, #ec4899, #8b5cf6);"></div></div>
                </div>
                <div class="pchud-meter">
                  <span class="m-lbl">RAM System</span>
                  <span class="m-val" id="sim-ram-val">65%</span>
                  <div class="m-bar"><div id="sim-ram-bar" style="width:65%; background:linear-gradient(90deg, #f59e0b, #ef4444);"></div></div>
                </div>
                <div class="pchud-meter">
                  <span class="m-lbl">Mạng Internet</span>
                  <span class="m-val" id="sim-net-val">15 MB/s</span>
                  <div class="m-bar"><div id="sim-net-bar" style="width:45%; background:linear-gradient(90deg, #10b981, #06b6d4);"></div></div>
                </div>
              </div>

              <!-- Mode 1 / 11: XiaoZhi AI -->
              <div class="sim-view" id="sim-view-xiaozhi" style="display:none;">
                <div class="sim-eyes-stage">
                  <div class="sim-eye left"></div>
                  <div class="sim-eye right"></div>
                </div>
                <div class="sim-sub-box">
                  <div class="sim-sub-sender">🤖 TRỢ LÝ XIAOZHI AI</div>
                  <div class="sim-sub-content" id="sim-sub-content">Xin chào! Hãy trò chuyện cùng mình nhé.</div>
                </div>
              </div>

              <!-- Mode 12: Media Player -->
              <div class="sim-view" id="sim-view-media" style="display:none;">
                <div style="font-size:0.75rem; color:#38bdf8; font-weight:bold; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;" id="sim-media-title">🎵 Neon_City_Chill.mp3</div>
                <div class="vinyl-disc-spin">💿</div>
                <div class="visualizer-bars">
                  <div class="vbar"></div><div class="vbar"></div><div class="vbar"></div><div class="vbar"></div><div class="vbar"></div><div class="vbar"></div><div class="vbar"></div>
                </div>
                <div style="font-size:0.75rem; color:#34d399; font-weight:bold;">▶ Đang Phát Âm Thanh I2S</div>
              </div>

              <!-- Mode 3: Chẩn đoán -->
              <div class="sim-view" id="sim-view-diag" style="display:none;">
                <div class="diag-header">🧪 CHẨN ĐOÁN DIO & ADC</div>
                <div class="diag-info-list">
                  <div>PIN ADC: <strong id="sim-diag-pin">GPIO 3 (ADC1_CH2)</strong></div>
                  <div>ĐIỆN ÁP: <strong id="sim-diag-volt" style="color:#34d399;">3.30 V</strong></div>
                  <div>ADC 12-BIT: <strong id="sim-diag-raw" style="color:#fbbf24;">4095</strong></div>
                  <div>NÚT NHẬN DIỆN: <strong id="sim-diag-btn" style="color:#38bdf8;">KHÔNG BẤM</strong></div>
                </div>
              </div>

              <!-- Mode 18: Quản lý Kết nối Wi-Fi -->
              <div class="sim-view" id="sim-view-wifi" style="display:none;background:#050b14;color:#fff;flex-direction:column;padding:8px;box-sizing:border-box;">
                <div class="s40-header-bar" style="border-bottom:1px solid #00f0ff;padding-bottom:4px;margin-bottom:6px;display:flex;justify-content:space-between;">
                  <span style="color:#00f0ff;font-weight:700;font-size:0.75rem;">KẾT NỐI WI-FI</span>
                  <span style="color:#10b981;font-size:0.65rem;" id="sim-wificonn-status">● ĐÃ KẾT NỐI</span>
                </div>
                <div style="background:#0a192f;border:1px solid #1e293b;border-radius:6px;padding:6px;margin-bottom:8px;">
                  <div style="font-size:0.65rem;color:#facc15;font-weight:700;" id="sim-wificonn-ssid">ĐANG DÙNG: Wi-Fi Gia Đình</div>
                  <div style="font-size:0.58rem;color:#94a3b8;" id="sim-wificonn-sub">IP: 192.168.1.5 | Sóng: -52 dBm</div>
                </div>
                <div style="font-size:0.62rem;color:#38bdf8;margin-bottom:4px;font-weight:700;">MẠNG ĐÃ LƯU TRÊN TRẠM:</div>
                <div id="sim-wificonn-list" style="display:flex;flex-direction:column;gap:4px;flex:1;overflow-y:auto;">
                  <div style="background:#0f172a;border:1px solid #10b981;border-radius:4px;padding:4px 6px;font-size:0.62rem;display:flex;justify-content:space-between;">
                    <span style="color:#10b981;">📶 [✓] Yen Long</span>
                    <span style="color:#64748b;font-size:0.55rem;">-48 dBm</span>
                  </div>
                </div>
                <div style="border-top:1px solid #1e293b;padding-top:4px;font-size:0.55rem;color:#64748b;text-align:center;">
                  Phím OK: Đổi Wi-Fi | Thoát: Về Menu
                </div>
              </div>

              <!-- Mode 19: Giám sát Hệ Thống Telemetry 1MB PSRAM -->
              <div class="sim-view" id="sim-view-trend" style="display:none;background:#050b14;color:#fff;flex-direction:column;padding:8px;box-sizing:border-box;">
                <div class="s40-header-bar" style="border-bottom:1px solid #ffb000;padding-bottom:4px;margin-bottom:6px;display:flex;justify-content:space-between;align-items:center;">
                  <span style="color:#ffb000;font-weight:700;font-size:0.72rem;">📊 HỆ THỐNG [24h]</span>
                  <span style="color:#38bdf8;font-size:0.6rem;background:#075985;padding:1px 5px;border-radius:4px;">1MB PSRAM</span>
                </div>
                <div style="background:#0a192f;border:1px solid #1e293b;border-radius:6px;padding:6px;margin-bottom:6px;">
                  <div style="display:flex;justify-content:space-between;font-size:0.62rem;color:#94a3b8;">
                    <span>CHỈ SỐ: <strong style="color:#4ade80;">Nhiệt độ phòng</strong></span>
                    <span style="color:#fbbf24;">87,381 MẪU</span>
                  </div>
                  <div style="height:90px;width:100%;margin-top:6px;position:relative;background:#020617;border:1px solid #1e293b;border-radius:4px;overflow:hidden;">
                    <svg viewBox="0 0 200 80" style="width:100%;height:100%;">
                      <defs>
                        <linearGradient id="trendSimGrad" x1="0" y1="0" x2="0" y2="1">
                          <stop offset="0%" stop-color="#00f0ff" stop-opacity="0.45"/>
                          <stop offset="100%" stop-color="#00f0ff" stop-opacity="0.0"/>
                        </linearGradient>
                      </defs>
                      <path d="M 0 55 Q 35 25 70 42 T 130 20 T 170 38 T 200 15 L 200 80 L 0 80 Z" fill="url(#trendSimGrad)" />
                      <path d="M 0 55 Q 35 25 70 42 T 130 20 T 170 38 T 200 15" fill="none" stroke="#00f0ff" stroke-width="2" />
                      <line x1="0" y1="40" x2="200" y2="40" stroke="#334155" stroke-dasharray="3,3" />
                      <circle cx="200" cy="15" r="3" fill="#ffb000" />
                    </svg>
                  </div>
                </div>
                <div style="display:flex;justify-content:space-between;font-size:0.58rem;color:#38bdf8;padding:3px 6px;background:#0f172a;border-radius:4px;">
                  <span>Min: 26.5°C</span>
                  <span style="color:#4ade80;font-weight:700;">Hiện tại: 28.2°C</span>
                  <span>Max: 31.0°C</span>
                </div>
                <div style="border-top:1px solid #1e293b;margin-top:auto;padding-top:4px;font-size:0.55rem;color:#64748b;text-align:center;">
                  Phím ▲/▼: Đổi chỉ số | ◄/►: Khung giờ | Thoát: Menu
                </div>
              </div>
            </div>

            <div class="sim-bezel-foot">
              <div class="sim-foot-status">
                <span class="mode-tag" id="sim-mode-tag">STANDBY</span>
                <span class="title-tag" id="sim-title-tag">Màn Hình Chờ</span>
              </div>
              <button class="btn-refresh-sim" onclick="window.fetchKeypadStatus()" title="Làm mới tức thì">🔄</button>
            </div>
          </div>
        </div>

        <!-- CỘT PHẢI: DÀN PHÍM ẢO MÔ PHỎNG & BẢNG ĐO ADC -->
        <div class="keypad-controls-col">
          <!-- BẢNG LED ĐO ADC THANG ĐIỆN TRỞ -->
          <div class="adc-matrix-card">
            <div class="adc-matrix-head">
              <span>🎛️ BẢNG THÔNG SỐ ĐIỆN TỬ ADC (GPIO 3)</span>
              <span class="hud-chip">12-BIT RESISTOR LADDER</span>
            </div>
            <div class="adc-grid-stats">
              <div class="adc-cell"><span class="lbl">Nút Vừa Bấm</span><span class="val" id="adc-key-name" style="color:#38bdf8;">KHÔNG BẤM</span></div>
              <div class="adc-cell"><span class="lbl">Điện Trở (R)</span><span class="val" id="adc-key-res" style="color:#a78bfa;">∞ (Pull-up 10k)</span></div>
              <div class="adc-cell"><span class="lbl">Điện Áp Đo</span><span class="val" id="adc-key-volt" style="color:#34d399;">3.30 V (3300 mV)</span></div>
              <div class="adc-cell"><span class="lbl">Mã ADC 12-Bit</span><span class="val" id="adc-key-raw" style="color:#fbbf24;">4095 / 4095</span></div>
            </div>
          </div>

          <!-- DÀN PHÍM ẢO MÔ PHỎNG -->
          <div class="virtual-dpad-box">
            <!-- HÀNG 1: UP -->
            <div class="dpad-row">
              <button class="dpad-btn btn-up" id="vbtn-1" onclick="window.pressKeypadKey(1)">
                <span class="btn-arrow">▲</span>
                <span class="btn-text">LÊN (UP)</span>
                <span class="btn-sub">1.0kΩ | 0.30V</span>
              </button>
            </div>

            <!-- HÀNG 2: LEFT - OK - RIGHT -->
            <div class="dpad-row">
              <button class="dpad-btn btn-left" id="vbtn-3" onclick="window.pressKeypadKey(3)">
                <span class="btn-arrow">◄</span>
                <span class="btn-text">TRÁI (LEFT)</span>
                <span class="btn-sub">4.7kΩ | 1.05V</span>
              </button>

              <button class="dpad-btn btn-ok" id="vbtn-0" onclick="window.pressKeypadKey(0)">
                <span class="btn-center-ring">●</span>
                <span class="btn-text">OK (GIỮA)</span>
                <span class="btn-sub">0Ω (GND) | 0.00V</span>
              </button>

              <button class="dpad-btn btn-right" id="vbtn-4" onclick="window.pressKeypadKey(4)">
                <span class="btn-arrow">►</span>
                <span class="btn-text">PHẢI (RIGHT)</span>
                <span class="btn-sub">10kΩ | 1.65V</span>
              </button>
            </div>

            <!-- HÀNG 3: DOWN -->
            <div class="dpad-row">
              <button class="dpad-btn btn-down" id="vbtn-2" onclick="window.pressKeypadKey(2)">
                <span class="btn-arrow">▼</span>
                <span class="btn-text">XUỐNG (DOWN)</span>
                <span class="btn-sub">2.2kΩ | 0.60V</span>
              </button>
            </div>

            <!-- HÀNG 4: 2 PHÍM CHỨC NĂNG HỆ THỐNG MENU & EXIT -->
            <div class="function-row">
              <button class="dpad-btn btn-func btn-menu" id="vbtn-5" onclick="window.pressKeypadKey(5)">
                <span class="btn-icon">☰</span>
                <span class="btn-text">MENU (TRÌNH ĐƠN)</span>
                <span class="btn-sub">20kΩ | 2.18V</span>
              </button>

              <button class="dpad-btn btn-func btn-exit" id="vbtn-6" onclick="window.pressKeypadKey(6)">
                <span class="btn-icon">✕</span>
                <span class="btn-text">EXIT (THOÁT)</span>
                <span class="btn-sub">47kΩ | 2.65V</span>
              </button>
            </div>

            <!-- HÀNG 5: PHÍM CẢM BIẾN CHẠM TTP223 -->
            <div class="touch-row">
              <button class="dpad-btn btn-touch" id="vbtn-touch" onclick="window.pressKeypadKey('touch')">
                <span style="font-size:1.2rem;">👆</span>
                <span class="btn-text">CHẠM CẢM ỨNG TTP223 (GPIO 20)</span>
                <span class="btn-sub">Đổi hiệu ứng Edison & Phản hồi xúc giác</span>
              </button>
            </div>
          </div>

          <!-- BẢNG PHÍM TẮT BÀN PHÍM PC -->
          <div class="keyboard-shortcuts-pill">
            <span>⌨️ <strong>Phím tắt PC:</strong></span>
            <kbd>↑</kbd><kbd>↓</kbd><kbd>←</kbd><kbd>→</kbd> / <kbd>W</kbd><kbd>S</kbd><kbd>A</kbd><kbd>D</kbd> Điều hướng
            • <kbd>Enter</kbd> / <kbd>Space</kbd> OK (Mở tính năng)
            • <kbd>M</kbd> Menu
            • <kbd>Esc</kbd> Exit
            • <kbd>T</kbd> Chạm Touch
          </div>
        </div>
      </div>
    `;
    mainContainer.appendChild(sec);

    renderS40Grid(0);
    window.syncKeypadStandbyTheme();
  }

  // ĐỒNG BỘ ẢNH NỀN VÀ MÀU SẮC MÀN HÌNH CHỜ VỚI TAB MÀN HÌNH DEMO & THIẾT KẾ VÀ ESP32
  window.syncKeypadStandbyTheme = async function(liveData) {
    const simStandby = document.getElementById('sim-view-standby');
    if (!simStandby) return;

    // 1. Thử copy ảnh nền trực tiếp từ Tab 1 (#screen-standby) nếu đã có
    const tab1Screen = document.getElementById('screen-standby');
    if (tab1Screen && tab1Screen.style.backgroundImage && tab1Screen.style.backgroundImage !== 'none') {
      simStandby.style.backgroundImage = tab1Screen.style.backgroundImage;
      simStandby.style.backgroundColor = tab1Screen.style.backgroundColor || '#000000';
    }

    let bgMode = (liveData && liveData.bg_mode) ? liveData.bg_mode : (document.getElementById('cfg-bg-mode')?.value || '');
    let bgImg = (liveData && liveData.bg_image) ? liveData.bg_image : (window.activeImageName || '');
    let dimVal = (liveData && liveData.dim_overlay !== undefined) ? liveData.dim_overlay : (parseInt(document.getElementById('cfg-dim')?.value) || 35);
    let clockColor = (liveData && liveData.clock_color) ? liveData.clock_color : (document.getElementById('cfg-clock-color')?.value || '#38bdf8');
    let slogan = (liveData && liveData.slogan) ? liveData.slogan : (document.getElementById('cfg-slogan')?.value || 'Trạm Decor Vũ Trụ ✨');
    let wifiSsid = (liveData && liveData.wifi_ssid) ? liveData.wifi_ssid : '';
    let wifiConn = (liveData && liveData.wifi_connected !== undefined) ? liveData.wifi_connected : true;
    let temp = liveData ? liveData.temp : undefined;
    let hum = liveData ? liveData.humidity : undefined;

    // 2. Nếu chưa có ảnh nền cụ thể, fetch trực tiếp từ /api/screen/standby
    if (!bgImg && (!tab1Screen || !tab1Screen.style.backgroundImage || tab1Screen.style.backgroundImage === 'none')) {
      try {
        const res = await fetch('/api/screen/standby');
        if (res.ok) {
          const cfg = await res.json();
          if (cfg.bg_mode) bgMode = cfg.bg_mode;
          if (cfg.bg_image) bgImg = cfg.bg_image;
          if (cfg.dim_overlay !== undefined) dimVal = cfg.dim_overlay;
          if (cfg.clock_color) clockColor = cfg.clock_color;
          if (cfg.custom_text) slogan = cfg.custom_text;
          if (cfg.temp !== undefined) temp = cfg.temp;
          if (cfg.humidity !== undefined) hum = cfg.humidity;
        }
      } catch(e){}
    }

    // 3. Áp dụng hình nền lên màn hình chờ ảo của Tab Bàn Phím
    if (bgImg) {
      const cleanImg = bgImg.replace(/^sd:/, '');
      const url = (typeof window.resolveWebImgUrl === 'function')
        ? window.resolveWebImgUrl(cleanImg)
        : (cleanImg.startsWith('/sd_images/') ? '/api/sd/file?path=' + encodeURIComponent(cleanImg) : cleanImg);
      simStandby.style.backgroundImage = `url('${url}')`;
      simStandby.style.backgroundColor = '#000000';
    } else if (bgMode === 'image' && window.activeImageName) {
      const url = (typeof window.resolveWebImgUrl === 'function') ? window.resolveWebImgUrl(window.activeImageName) : window.activeImageName;
      simStandby.style.backgroundImage = `url('${url}')`;
      simStandby.style.backgroundColor = '#000000';
    } else if (bgMode === 'gradient-cyber') {
      simStandby.style.backgroundImage = 'linear-gradient(180deg, #0b132b 0%, #1c2541 50%, #3a0ca3 100%)';
    } else if (bgMode === 'gradient-nebula') {
      simStandby.style.backgroundImage = 'linear-gradient(180deg, #2b0938 0%, #0f172a 60%, #1e1b4b 100%)';
    } else if (bgMode === 'gradient-sunset') {
      simStandby.style.backgroundImage = 'linear-gradient(180deg, #3d1308 0%, #1e1b4b 70%, #0f172a 100%)';
    } else if (bgMode === 'solid-black') {
      simStandby.style.backgroundImage = 'none';
      simStandby.style.backgroundColor = '#000000';
    }

    // 4. Lớp phủ làm tối
    const dimElDom = document.getElementById('sim-standby-dim');
    if (dimElDom) {
      dimElDom.style.background = `rgba(0,0,0,${dimVal / 100})`;
    }

    // 5. Màu chữ đồng hồ
    const clockEl = document.getElementById('sim-clock');
    if (clockEl) {
      clockEl.style.color = clockColor;
      clockEl.style.textShadow = `0 0 15px ${clockColor}`;
    }

    // 6. Slogan
    const sloganDom = document.getElementById('sim-slogan');
    if (sloganDom) sloganDom.innerText = slogan;

    // 7. Cập nhật viên thuốc WiFi SSID (Top Left)
    if (wifiSsid) {
      const ssidEl = document.getElementById('sim-wifi-ssid');
      if (ssidEl) ssidEl.innerText = wifiSsid;
    }
    const dotWifi = document.getElementById('sim-dot-wifi');
    if (dotWifi) {
      dotWifi.style.color = wifiConn ? '#10b981' : '#f59e0b';
      dotWifi.style.filter = wifiConn ? 'drop-shadow(0 0 5px #10b981)' : 'drop-shadow(0 0 5px #f59e0b)';
    }
    const wifiSvg = document.getElementById('sim-wifi-svg');
    const wifiDot = document.getElementById('sim-wifi-dot');
    if (wifiSvg) {
      const strokeCol = wifiConn ? '#10b981' : '#f59e0b';
      wifiSvg.style.stroke = strokeCol;
      if (wifiDot) wifiDot.style.fill = strokeCol;
      wifiSvg.style.filter = wifiConn ? 'drop-shadow(0 0 5px #10b981)' : 'drop-shadow(0 0 5px #f59e0b)';
    }

    // 8. Cập nhật nhiệt độ & độ ẩm (Weather pill)
    if (temp !== undefined && hum !== undefined) {
      const wEl = document.getElementById('sim-weather');
      if (wEl) wEl.innerText = `🌡️ ${Number(temp).toFixed(1)}°C | 💧 ${hum}%`;
    }
  };

  // Đồng hồ chạy thời gian thực từng giây cho màn hình chờ ảo
  function updateSimClockLive() {
    const now = new Date();
    let h = now.getHours();
    const m = String(now.getMinutes()).padStart(2, '0');
    const s = String(now.getSeconds()).padStart(2, '0');

    const fmtEl = document.getElementById('cfg-clock-fmt');
    const is12h = fmtEl && fmtEl.value === '12h';
    let ampm = '';
    if (is12h) {
      ampm = h >= 12 ? ' PM' : ' AM';
      h = h % 12 || 12;
    }
    const hStr = String(h).padStart(2, '0');

    const clkEl = document.getElementById('sim-clock');
    if (clkEl) {
      const showSec = !document.getElementById('cfg-show-sec') || document.getElementById('cfg-show-sec').checked;
      clkEl.innerHTML = `${hStr}:${m}${ampm}<span class="sim-clock-sec" id="sim-sec">${showSec ? ':' + s : ''}</span>`;
    }

    const dateEl = document.getElementById('sim-date');
    if (dateEl) {
      const daysVi = ['Chủ Nhật', 'Thứ Hai', 'Thứ Ba', 'Thứ Tư', 'Thứ Năm', 'Thứ Sáu', 'Thứ Bảy'];
      const dayName = daysVi[now.getDay()];
      const d = String(now.getDate()).padStart(2, '0');
      const mo = String(now.getMonth() + 1).padStart(2, '0');
      const y = now.getFullYear();
      dateEl.innerText = `${dayName}, ${d}/${mo}/${y}`;
    }
  }

  updateSimClockLive();
  setInterval(updateSimClockLive, 1000);

  const origUpdateClock_Keypad = window.updateClock;
  window.updateClock = function() {
    if (typeof origUpdateClock_Keypad === 'function') origUpdateClock_Keypad();
    updateSimClockLive();
  };

  // Hook vào hàm updateLivePreview của Tab 1 để đồng bộ tức thì khi người dùng đổi ảnh nền
  const origUpdateLivePreview_Keypad = window.updateLivePreview;
  window.updateLivePreview = function() {
    if (typeof origUpdateLivePreview_Keypad === 'function') origUpdateLivePreview_Keypad();
    window.syncKeypadStandbyTheme();
  };

  function renderS40Grid(curIdx) {
    const grid = document.getElementById('s40-grid-cells');
    if (!grid) return;
    currentS40Cursor = curIdx;
    grid.innerHTML = '';
    S40_APP_LIST.forEach((app, idx) => {
      const cell = document.createElement('div');
      cell.className = 's40-cell' + (idx === curIdx ? ' selected' : '');
      cell.innerHTML = `
        <div class="s40-cell-icon">${app.icon}</div>
        <div class="s40-cell-name">${app.title}</div>
      `;
      // Bấm vào ô: Nếu chưa chọn thì chọn ô đó, nếu đã chọn thì bấm OK (mở app)
      cell.onclick = () => {
        if (idx !== currentS40Cursor) {
          currentS40Cursor = idx;
          renderS40Grid(idx);
          // Gửi phím điều hướng xuống ESP32
          window.pressKeypadKey(idx > curIdx ? 4 : 3, false);
        } else {
          window.triggerS40OpenCurrentApp();
        }
      };
      grid.appendChild(cell);
    });
    const curLabel = document.getElementById('s40-cur-label');
    if (curLabel) curLabel.innerText = (curIdx + 1) + '/' + S40_APP_LIST.length;
    const banner = document.getElementById('s40-desc-banner');
    if (banner && S40_APP_LIST[curIdx]) banner.innerText = S40_APP_LIST[curIdx].desc;
  }

  // KHI BẤM OK TRONG MENU S40: GỬI LỆNH XUỐNG ESP32 VÀ TỰ ĐỘNG CHUYỂN THẲNG VÀO TAB CHỨC NĂNG TRÊN WEB!
  window.triggerS40OpenCurrentApp = function() {
    const app = S40_APP_LIST[currentS40Cursor];
    if (!app) return;

    // 1. Gửi lệnh phím OK (key 0) xuống ESP32 để chuyển mode phần cứng thật
    window.pressKeypadKey(0);

    // 2. Chuyển thẳng sang Tab tương ứng trên Web nếu có
    if (app.tab && typeof window.switchTab === 'function') {
      setTimeout(() => {
        window.switchTab(app.tab);
        if (typeof showToast === 'function') {
          showToast(`🚀 Đã mở "${app.title}" trên Web & đồng bộ xuống ESP32!`);
        }
      }, 120);
    }
  };

  // Âm thanh click phím cơ nhẹ qua Web Audio API
  function playKeyClickSound() {
    try {
      const AudioCtx = window.AudioContext || window.webkitAudioContext;
      if (!AudioCtx) return;
      const ctx = new AudioCtx();
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(800, ctx.currentTime);
      osc.frequency.exponentialRampToValueAtTime(320, ctx.currentTime + 0.035);
      gain.gain.setValueAtTime(0.18, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.01, ctx.currentTime + 0.035);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start();
      osc.stop(ctx.currentTime + 0.04);
    } catch(e){}
  }

  const BTN_SPECS_INFO = [
    { name: "OK (Giữa)",   res: "0 Ohm (GND)",   volt: "0.00 V (0 mV)",     raw: "0 / 4095" },
    { name: "UP (Lên)",    res: "1.0 kOhm",      volt: "0.30 V (300 mV)",   raw: "372 / 4095" },
    { name: "DOWN (Xuống)",res: "2.2 kOhm",      volt: "0.60 V (600 mV)",   raw: "744 / 4095" },
    { name: "LEFT (Trái)", res: "4.7 kOhm",      volt: "1.05 V (1050 mV)",  raw: "1303 / 4095" },
    { name: "RIGHT (Phải)",res: "10 kOhm",       volt: "1.65 V (1650 mV)",  raw: "2048 / 4095" },
    { name: "MENU",        res: "20 kOhm",       volt: "2.18 V (2180 mV)",  raw: "2705 / 4095" },
    { name: "EXIT",        res: "47 kOhm",       volt: "2.65 V (2650 mV)",  raw: "3288 / 4095" }
  ];

  window.pressKeypadKey = async function(key, allowAppOpen = true) {
    playKeyClickSound();

    // Hiệu ứng nhấp nhả nút ảo trên giao diện
    const btnId = (key === 'touch') ? 'vbtn-touch' : ('vbtn-' + key);
    const btnEl = document.getElementById(btnId);
    if (btnEl) {
      btnEl.classList.add('pressed');
      setTimeout(() => btnEl.classList.remove('pressed'), 120);
    }

    const led = document.getElementById('sim-live-led');
    if (led) {
      led.classList.add('blink');
      setTimeout(() => led.classList.remove('blink'), 150);
    }

    // Cập nhật ngay thông số ADC trên bảng
    if (typeof key === 'number' && BTN_SPECS_INFO[key]) {
      const s = BTN_SPECS_INFO[key];
      const knEl = document.getElementById('adc-key-name');
      const krEl = document.getElementById('adc-key-res');
      const kvEl = document.getElementById('adc-key-volt');
      const kwEl = document.getElementById('adc-key-raw');
      if (knEl) knEl.innerText = s.name;
      if (krEl) krEl.innerText = s.res;
      if (kvEl) kvEl.innerText = s.volt;
      if (kwEl) kwEl.innerText = s.raw;
    } else if (key === 'touch') {
      const knEl = document.getElementById('adc-key-name');
      const krEl = document.getElementById('adc-key-res');
      const kvEl = document.getElementById('adc-key-volt');
      if (knEl) knEl.innerText = "CHẠM TTP223 (GPIO 20)";
      if (krEl) krEl.innerText = "Cảm ứng điện dung";
      if (kvEl) kvEl.innerText = "Active HIGH (3.3V)";
    }

    // Nếu đang ở Menu S40 và bấm phím OK: Mở thẳng Tab Web tương ứng
    const s40View = document.getElementById('sim-view-s40');
    if (key === 0 && allowAppOpen && s40View && s40View.style.display !== 'none') {
      const app = S40_APP_LIST[currentS40Cursor];
      if (app && app.tab && typeof window.switchTab === 'function') {
        setTimeout(() => {
          window.switchTab(app.tab);
          if (typeof showToast === 'function') {
            showToast(`🚀 Đã mở "${app.title}" trên Web & đồng bộ xuống ESP32!`);
          }
        }, 120);
      }
    }

    try {
      const res = await fetch('/api/keypad/press', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key: key })
      });
      const data = await res.json();
      if (data && data.success) {
        updateSimScreenFromData(data);
      }
    } catch(e) {
      console.warn("Lỗi gửi phím bấm ảo:", e);
    }
  };

  function updateSimScreenFromData(d) {
    if (!d) return;

    // 1. Cập nhật thẻ trạng thái footer
    const modeTag = document.getElementById('sim-mode-tag');
    const titleTag = document.getElementById('sim-title-tag');
    if (modeTag) modeTag.innerText = (d.screen_mode || 'STANDBY').toUpperCase();
    if (titleTag) titleTag.innerText = d.screen_title || 'Màn hình ESP32';

    // 2. Chuyển đổi View theo mode_id
    const views = ['standby', 's40', 'pchud', 'xiaozhi', 'media', 'diag', 'wifi', 'trend'];
    views.forEach(v => {
      const el = document.getElementById('sim-view-' + v);
      if (el) el.style.display = 'none';
    });

    const mId = (typeof d.current_mode_id === 'number') ? d.current_mode_id : 0;
    if (mId === 4) {
      // S40 Menu
      const el = document.getElementById('sim-view-s40');
      if (el) el.style.display = 'flex';
      renderS40Grid(d.menu_cursor || 0);
    } else if (mId === 19) {
      // Hệ Thống Telemetry 1MB PSRAM
      const el = document.getElementById('sim-view-trend');
      if (el) el.style.display = 'flex';
    } else if (mId === 18) {
      // Kết nối Wi-Fi
      const el = document.getElementById('sim-view-wifi');
      if (el) el.style.display = 'flex';
      const isConn = d.wifi_connected || false;
      const stEl = document.getElementById('sim-wificonn-status');
      if (stEl) {
        stEl.innerText = isConn ? '● ĐÃ KẾT NỐI' : '● SOFTAP';
        stEl.style.color = isConn ? '#10b981' : '#f59e0b';
      }
      const ssidEl = document.getElementById('sim-wificonn-ssid');
      if (ssidEl) ssidEl.innerText = isConn ? ('ĐANG DÙNG: ' + (d.wifi_ssid || 'Wi-Fi')) : 'CHẾ ĐỘ AP: TramVuTru-Config';
      const subEl = document.getElementById('sim-wificonn-sub');
      if (subEl) subEl.innerText = 'IP: ' + (d.ip || '192.168.4.1') + (isConn ? (' | Sóng: ' + (d.wifi_rssi || 0) + ' dBm') : ' (Tự dò mạng ngầm)');
    } else if (mId === 2) {
      // PC HUD
      const el = document.getElementById('sim-view-pchud');
      if (el) el.style.display = 'flex';
    } else if (mId === 1 || mId === 11) {
      // XiaoZhi
      const el = document.getElementById('sim-view-xiaozhi');
      if (el) el.style.display = 'flex';
      const sub = document.getElementById('sim-sub-content');
      if (sub && d.subtitle) sub.innerText = d.subtitle;
    } else if (mId === 12) {
      // Media Player
      const el = document.getElementById('sim-view-media');
      if (el) el.style.display = 'flex';
    } else if (mId === 3 || mId === 9) {
      // Diag
      const el = document.getElementById('sim-view-diag');
      if (el) el.style.display = 'flex';
    } else {
      // Standby (Mặc định)
      const el = document.getElementById('sim-view-standby');
      if (el) el.style.display = 'flex';
      if (d.battery_v) {
        const pt = document.getElementById('sim-power-tag');
        if (pt) pt.innerText = (d.is_charging ? '⚡ SẠC: ' : '🔋 PIN: ') + d.battery_v.toFixed(2) + 'V (' + (d.battery_pct || 0) + '%)';
      }
      window.syncKeypadStandbyTheme(d);
    }
  }

  window.fetchKeypadStatus = async function() {
    try {
      const res = await fetch('/api/keypad/status');
      const data = await res.json();
      if (data) {
        updateSimScreenFromData(data);
        if (data.adc_mv !== undefined) {
          const kvEl = document.getElementById('adc-key-volt');
          const kwEl = document.getElementById('adc-key-raw');
          if (kvEl && data.adc_voltage) kvEl.innerText = data.adc_voltage.toFixed(2) + ' V (' + data.adc_mv + ' mV)';
          if (kwEl && data.adc_raw) kwEl.innerText = data.adc_raw + ' / 4095';
        }
      }
    } catch(e){}
  };

  // Lắng nghe bàn phím PC
  window.addEventListener('keydown', (e) => {
    // Chỉ kích hoạt khi đang không gõ chữ vào input/textarea
    if (['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName)) return;

    if (e.key === 'ArrowUp' || e.code === 'KeyW') {
      e.preventDefault(); window.pressKeypadKey(1);
    } else if (e.key === 'ArrowDown' || e.code === 'KeyS') {
      e.preventDefault(); window.pressKeypadKey(2);
    } else if (e.key === 'ArrowLeft' || e.code === 'KeyA') {
      e.preventDefault(); window.pressKeypadKey(3);
    } else if (e.key === 'ArrowRight' || e.code === 'KeyD') {
      e.preventDefault(); window.pressKeypadKey(4);
    } else if (e.key === 'Enter' || e.code === 'Space') {
      e.preventDefault(); window.pressKeypadKey(0);
    } else if (e.key === 'm' || e.key === 'M') {
      e.preventDefault(); window.pressKeypadKey(5);
    } else if (e.key === 'Escape' || e.key === 'Backspace') {
      e.preventDefault(); window.pressKeypadKey(6);
    } else if (e.key === 't' || e.key === 'T') {
      e.preventDefault(); window.pressKeypadKey('touch');
    }
  });

  // Hook vào switchTab của hệ thống
  const origSwitchTab_Keypad = window.switchTab;
  window.switchTab = function(tabId) {
    injectKeypadTabAndPage();
    if (tabId === 'keypad') {
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      const sec = document.getElementById('tab-keypad');
      const btn = document.getElementById('tab-btn-keypad');
      if (sec) sec.classList.add('active');
      if (btn) {
        btn.classList.add('active');
        btn.scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'center' });
      }
      window.fetchKeypadStatus();
      window.syncKeypadStandbyTheme();
      return;
    }
    const sec = document.getElementById('tab-keypad');
    const btn = document.getElementById('tab-btn-keypad');
    if (sec) sec.classList.remove('active');
    if (btn) btn.classList.remove('active');

    if (typeof origSwitchTab_Keypad === 'function') origSwitchTab_Keypad(tabId);
  };

  setTimeout(() => {
    injectKeypadTabAndPage();
    window.fetchKeypadStatus();
    window.syncKeypadStandbyTheme();
    setInterval(() => {
      const sec = document.getElementById('tab-keypad');
      if (sec && sec.classList.contains('active')) {
        window.fetchKeypadStatus();
      }
    }, 450);
  }, 400);
})();
</script>
)rawliteral";

static const char TELEMETRY_WEATHER_INJECT_SCRIPT[] PROGMEM = R"rawliteral(
<script>
(function(){
  // 1. CSS cho Weather Pill và Telemetry Dashboard
  const style = document.createElement('style');
  style.textContent = `
    .weather-pill-box {
      display: flex;
      align-items: center;
      gap: 8px;
      background: rgba(15, 23, 42, 0.85);
      border: 1px solid #0284c7;
      border-radius: 8px;
      padding: 4px 10px;
      font-size: 0.78rem;
      margin-top: 6px;
      width: 100%;
      box-shadow: 0 0 10px rgba(2, 132, 199, 0.15);
      flex-wrap: wrap;
    }
    .weather-tag {
      font-weight: 700;
      color: #38bdf8;
      display: inline-flex;
      align-items: center;
      gap: 4px;
    }
    .weather-refresh-btn {
      background: none;
      border: 1px solid #334155;
      color: #38bdf8;
      border-radius: 4px;
      padding: 1px 6px;
      font-size: 0.72rem;
      cursor: pointer;
      margin-left: auto;
      transition: all 0.2s;
    }
    .weather-refresh-btn:hover {
      background: #0284c7;
      color: #fff;
    }
    .tele-stat-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
      gap: 12px;
      margin-bottom: 16px;
    }
    .tele-card {
      background: #0f172a;
      border: 1px solid #1e293b;
      border-radius: 10px;
      padding: 12px;
      box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    }
    .tele-card-title {
      font-size: 0.72rem;
      color: #94a3b8;
      text-transform: uppercase;
      font-weight: 700;
      letter-spacing: 0.5px;
      margin-bottom: 6px;
    }
    .tele-card-val {
      font-size: 1.35rem;
      font-weight: 800;
      color: #38bdf8;
    }
    .tele-card-sub {
      font-size: 0.7rem;
      color: #64748b;
      margin-top: 4px;
    }
    .tele-btn-group {
      display: flex;
      gap: 6px;
      flex-wrap: wrap;
      margin-bottom: 12px;
    }
    .tele-btn {
      background: #1e293b;
      border: 1px solid #334155;
      color: #cbd5e1;
      border-radius: 6px;
      padding: 6px 12px;
      font-size: 0.78rem;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.15s;
    }
    .tele-btn:hover {
      background: #334155;
      color: #fff;
    }
    .tele-btn.active {
      background: #0284c7;
      border-color: #38bdf8;
      color: #fff;
      box-shadow: 0 0 10px rgba(56, 189, 248, 0.4);
    }
    .tele-chart-box {
      background: #070d19;
      border: 1px solid #1e293b;
      border-radius: 12px;
      padding: 14px;
      margin-bottom: 16px;
      position: relative;
    }
    .tele-canvas-wrapper {
      position: relative;
      width: 100%;
      height: 240px;
    }
    #telemetryCanvas {
      width: 100%;
      height: 100%;
      display: block;
    }
  `;
  document.head.appendChild(style);

  // 2. Chèn Weather Pill vào Status Bar
  function injectWeatherPill() {
    const statusBar = document.querySelector('.status-bar');
    if (!statusBar || document.getElementById('st-weather-pill')) return;

    const pill = document.createElement('div');
    pill.id = 'st-weather-pill';
    pill.className = 'weather-pill-box';
    pill.innerHTML = `
      <span class="weather-tag" id="wp-icon">🌤️</span>
      <span style="font-weight:700; color:#38bdf8" id="wp-city">Hà Nội</span>
      <span id="wp-outdoor" style="color:#f8fafc">--°C (--%)</span>
      <span style="color:#475569">|</span>
      <span id="wp-indoor" style="color:#10b981">Phòng: --°C</span>
      <span id="wp-cond" style="color:#94a3b8; font-size:0.72rem">--</span>
      <button class="weather-refresh-btn" id="btn-wp-refresh" onclick="window.refreshWeatherApi()" title="Cập nhật thời tiết Open-Meteo">🔄 Cập nhật</button>
    `;
    statusBar.appendChild(pill);
  }

  // 3. Chèn Tab "Xu Hướng (Telemetry)" và nội dung Section
  function injectTelemetryTabAndPage() {
    const navTabs = document.getElementById('nav-tabs');
    if (!navTabs || document.getElementById('tab-btn-telemetry')) return;

    const btn = document.createElement('button');
    btn.id = 'tab-btn-telemetry';
    btn.className = 'tab-btn';
    btn.innerHTML = '📈 Xu Hướng (1MB PSRAM)';
    btn.onclick = () => window.switchTab('telemetry');
    navTabs.appendChild(btn);

    const container = document.querySelector('.container');
    if (!container || document.getElementById('tab-telemetry')) return;

    const section = document.createElement('div');
    section.id = 'tab-telemetry';
    section.className = 'section';
    section.innerHTML = `
      <div style="background:#0f172a; border:1px solid #1e293b; border-radius:12px; padding:16px; margin-bottom:16px;">
        <div style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:8px; margin-bottom:12px;">
          <div>
            <h2 style="font-size:1.15rem; color:#38bdf8; display:flex; align-items:center; gap:8px;">
              📊 BIỂU ĐỒ XU HƯỚNG TELEMETRY
              <span class="badge" style="background:#0284c7; color:#fff; font-size:0.68rem;">1MB PSRAM</span>
            </h2>
            <p style="font-size:0.75rem; color:#64748b; margin-top:2px;">
              Bộ đệm vòng Circular Buffer 87,381 mẫu lưu liên tục 48.5 giờ | Cơ chế bảo vệ FlashGuard chống chai Flash
            </p>
          </div>
          <div style="display:flex; gap:6px;">
            <button class="tele-btn" onclick="window.fetchTelemetryHistory()" style="background:#0369a1; color:#fff;">🔄 Tải lại</button>
          </div>
        </div>

        <div class="tele-stat-grid">
          <div class="tele-card">
            <div class="tele-card-title">💾 PSRAM Circular Buffer</div>
            <div class="tele-card-val" id="tc-psram-records">--</div>
            <div class="tele-card-sub" id="tc-psram-desc">Dung lượng: 1MB (87,381 mẫu)</div>
          </div>
          <div class="tele-card">
            <div class="tele-card-title">🛡️ FlashGuard Protection</div>
            <div class="tele-card-val" style="color:#10b981;" id="tc-flash-saved">--</div>
            <div class="tele-card-sub">Lần ghi Flash đã bảo vệ (Debounce 2.0s)</div>
          </div>
          <div class="tele-card">
            <div class="tele-card-title">🌡️ Nhiệt độ Min / Max / TB</div>
            <div class="tele-card-val" id="tc-temp-stats">--</div>
            <div class="tele-card-sub" id="tc-temp-sub">Tính theo khung giờ đang chọn</div>
          </div>
          <div class="tele-card">
            <div class="tele-card-title">💧 Độ ẩm Min / Max / TB</div>
            <div class="tele-card-val" style="color:#06b6d4;" id="tc-hum-stats">--</div>
            <div class="tele-card-sub">Đo đạc thực tế từ cảm biến</div>
          </div>
        </div>

        <!-- Điều khiển biểu đồ -->
        <div style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:8px; margin-bottom:10px;">
          <div class="tele-btn-group" id="tele-metric-btns">
            <button class="tele-btn active" data-metric="temp" onclick="window.setTeleMetric('temp')">🌡️ Nhiệt độ (°C)</button>
            <button class="tele-btn" data-metric="hum" onclick="window.setTeleMetric('hum')">💧 Độ ẩm (%)</button>
            <button class="tele-btn" data-metric="cpu" onclick="window.setTeleMetric('cpu')">⚡ Tải CPU (%)</button>
            <button class="tele-btn" data-metric="bat" onclick="window.setTeleMetric('bat')">🔋 Dung lượng Pin (%)</button>
          </div>
          <div class="tele-btn-group" id="tele-range-btns">
            <button class="tele-btn" data-range="1h" onclick="window.setTeleRange('1h')">1 Giờ</button>
            <button class="tele-btn" data-range="6h" onclick="window.setTeleRange('6h')">6 Giờ</button>
            <button class="tele-btn active" data-range="24h" onclick="window.setTeleRange('24h')">24 Giờ</button>
            <button class="tele-btn" data-range="48h" onclick="window.setTeleRange('48h')">48 Giờ</button>
          </div>
        </div>

        <!-- Khung vẽ biểu đồ Canvas -->
        <div class="tele-chart-box">
          <div class="tele-canvas-wrapper">
            <canvas id="telemetryCanvas"></canvas>
          </div>
          <div id="tele-hover-info" style="font-size:0.75rem; color:#94a3b8; margin-top:8px; text-align:center;">
            Biểu đồ đường thời gian thực cập nhật định kỳ từ 1MB Circular Buffer trong PSRAM
          </div>
        </div>
      </div>
    `;
    container.appendChild(section);
  }

  // Quản lý trạng thái
  let curTeleMetric = 'temp';
  let curTeleRange = '24h';
  let cachedTeleData = null;

  window.setTeleMetric = function(m) {
    curTeleMetric = m;
    document.querySelectorAll('#tele-metric-btns .tele-btn').forEach(b => {
      b.classList.toggle('active', b.getAttribute('data-metric') === m);
    });
    drawTelemetryChart();
  };

  window.setTeleRange = function(r) {
    curTeleRange = r;
    document.querySelectorAll('#tele-range-btns .tele-btn').forEach(b => {
      b.classList.toggle('active', b.getAttribute('data-range') === r);
    });
    window.fetchTelemetryHistory();
  };

  window.refreshWeatherApi = function() {
    const btn = document.getElementById('btn-wp-refresh');
    if (btn) btn.innerText = '⏳ Đang lấy...';
    fetch('/api/weather/refresh', { method: 'POST' })
      .then(r => r.json())
      .then(d => {
        setTimeout(window.fetchWeatherStatus, 1500);
      })
      .catch(e => console.error(e));
  };

  window.fetchWeatherStatus = function() {
    fetch('/api/weather')
      .then(r => r.json())
      .then(d => {
        if (!d) return;
        const iconEl = document.getElementById('wp-icon');
        const cityEl = document.getElementById('wp-city');
        const outEl  = document.getElementById('wp-outdoor');
        const condEl = document.getElementById('wp-cond');
        const btn    = document.getElementById('btn-wp-refresh');
        if (btn) btn.innerText = '🔄 Cập nhật';

        if (d.isAvailable) {
          if (iconEl) {
            const icons = ['☀️', '⛅', '☁️', '🌧️', '⛈️', '🌫️', '❄️'];
            iconEl.innerText = icons[d.iconType] || '🌤️';
          }
          if (cityEl) cityEl.innerText = d.city || 'Hà Nội';
          if (outEl)  outEl.innerText = `${d.temp.toFixed(1)}°C (${d.humidity}%)`;
          if (condEl) condEl.innerText = `${d.condition} | Gió: ${d.windSpeed.toFixed(1)} km/h`;
        } else {
          if (cityEl) cityEl.innerText = 'Trạm ESP32';
          if (outEl) outEl.innerText = 'Đang đồng bộ Open-Meteo...';
        }
      })
      .catch(e => console.error(e));
  };

  window.fetchTelemetryHistory = function() {
    fetch(`/api/telemetry/history?range=${curTeleRange}&points=80`)
      .then(r => r.json())
      .then(d => {
        if (!d || d.status !== 'ok') return;
        cachedTeleData = d;

        // Cập nhật thẻ thông số
        const recEl   = document.getElementById('tc-psram-records');
        const saveEl  = document.getElementById('tc-flash-saved');
        const tStatEl = document.getElementById('tc-temp-stats');
        const hStatEl = document.getElementById('tc-hum-stats');

        if (recEl && d.psram) {
          recEl.innerText = `${d.psram.used.toLocaleString()} mẫu`;
        }
        if (saveEl && d.psram) {
          saveEl.innerText = `${d.psram.flashWritesSaved.toLocaleString()} lần`;
        }
        if (tStatEl && d.stats) {
          tStatEl.innerText = `${d.stats.minT.toFixed(1)}° / ${d.stats.maxT.toFixed(1)}° / ${d.stats.avgT.toFixed(1)}°C`;
        }
        if (hStatEl && d.stats) {
          hStatEl.innerText = `${d.stats.minH}% / ${d.stats.maxH}% / ${d.stats.avgH.toFixed(0)}%`;
        }

        drawTelemetryChart();
      })
      .catch(e => console.error(e));
  };

  function drawTelemetryChart() {
    const canvas = document.getElementById('telemetryCanvas');
    if (!canvas || !cachedTeleData || !cachedTeleData.data) return;

    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    if (rect.width === 0 || rect.height === 0) return;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);

    const w = rect.width;
    const h = rect.height;
    ctx.clearRect(0, 0, w, h);

    const items = cachedTeleData.data;
    if (items.length < 2) {
      ctx.fillStyle = '#64748b';
      ctx.font = '12px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Đang thu thập mẫu Telemetry vào 1MB PSRAM...', w / 2, h / 2);
      return;
    }

    // Chọn metric
    let values = [];
    let unit = '°C';
    let strokeColor = '#38bdf8';
    let gradTop = 'rgba(56, 189, 248, 0.4)';
    let gradBot = 'rgba(56, 189, 248, 0.0)';

    if (curTeleMetric === 'temp') {
      values = items.map(p => p.t);
      unit = '°C';
      strokeColor = '#38bdf8';
      gradTop = 'rgba(56, 189, 248, 0.4)';
    } else if (curTeleMetric === 'hum') {
      values = items.map(p => p.h);
      unit = '%';
      strokeColor = '#06b6d4';
      gradTop = 'rgba(6, 182, 212, 0.4)';
    } else if (curTeleMetric === 'cpu') {
      values = items.map(p => p.c);
      unit = '%';
      strokeColor = '#f59e0b';
      gradTop = 'rgba(245, 158, 11, 0.4)';
    } else if (curTeleMetric === 'bat') {
      values = items.map(p => p.b);
      unit = '%';
      strokeColor = '#10b981';
      gradTop = 'rgba(16, 185, 129, 0.4)';
    }

    let minV = Math.min(...values);
    let maxV = Math.max(...values);
    if (minV === maxV) { minV -= 1; maxV += 1; }
    const span = maxV - minV;
    const padL = 44, padR = 20, padT = 20, padB = 25;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;

    // Grid lines
    ctx.strokeStyle = '#1e293b';
    ctx.lineWidth = 1;
    ctx.fillStyle = '#64748b';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'right';

    for (let i = 0; i <= 4; i++) {
      const gy = padT + (plotH / 4) * i;
      const gv = maxV - (span / 4) * i;
      ctx.beginPath();
      ctx.moveTo(padL, gy);
      ctx.lineTo(w - padR, gy);
      ctx.stroke();
      ctx.fillText(gv.toFixed(1) + unit, padL - 6, gy + 3);
    }

    // Path
    ctx.beginPath();
    for (let i = 0; i < values.length; i++) {
      const px = padL + (i / (values.length - 1)) * plotW;
      const py = padT + (1 - (values[i] - minV) / span) * plotH;
      if (i === 0) ctx.moveTo(px, py);
      else ctx.lineTo(px, py);
    }
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 2.5;
    ctx.shadowColor = strokeColor;
    ctx.shadowBlur = 8;
    ctx.stroke();
    ctx.shadowBlur = 0;

    // Gradient fill
    const grad = ctx.createLinearGradient(0, padT, 0, padT + plotH);
    grad.addColorStop(0, gradTop);
    grad.addColorStop(1, gradBot);
    ctx.fillStyle = grad;
    ctx.lineTo(padL + plotW, padT + plotH);
    ctx.lineTo(padL, padT + plotH);
    ctx.closePath();
    ctx.fill();

    // Điểm mốc cuối cùng
    const lastX = padL + plotW;
    const lastY = padT + (1 - (values[values.length - 1] - minV) / span) * plotH;
    ctx.fillStyle = '#fff';
    ctx.beginPath();
    ctx.arc(lastX, lastY, 4, 0, Math.PI * 2);
    ctx.fill();
  }

  // Hook tab switching
  const origSwitchTab_Tele = window.switchTab;
  window.switchTab = function(tabId) {
    if (tabId === 'telemetry') {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      const sec = document.getElementById('tab-telemetry');
      const btn = document.getElementById('tab-btn-telemetry');
      if (sec) sec.classList.add('active');
      if (btn) btn.classList.add('active');
      window.fetchTelemetryHistory();
      return;
    }
    const sec = document.getElementById('tab-telemetry');
    const btn = document.getElementById('tab-btn-telemetry');
    if (sec) sec.classList.remove('active');
    if (btn) btn.classList.remove('active');

    if (typeof origSwitchTab_Tele === 'function') origSwitchTab_Tele(tabId);
  };

  // Khởi động
  setTimeout(() => {
    injectWeatherPill();
    injectTelemetryTabAndPage();
    window.fetchWeatherStatus();
    setInterval(() => {
      window.fetchWeatherStatus();
      const sec = document.getElementById('tab-telemetry');
      if (sec && sec.classList.contains('active')) {
        window.fetchTelemetryHistory();
      }
    }, 15000);
  }, 500);
})();
</script>
)rawliteral";


// ============================================================================
// INJECT SCRIPT: BẢO MẬT ĐĂNG NHẬP (WEB AUTH) & KHÔI PHỤC CÀI ĐẶT GỐC (FACTORY RESET)
// ============================================================================
static const char AUTH_FACTORY_INJECT_SCRIPT[] PROGMEM = R"rawliteral(
<style>
/* SPACE OS AUTHENTICATION MODAL & OVERLAY */
#spaceos-auth-overlay {
  position: fixed;
  inset: 0;
  z-index: 1000000;
  background: radial-gradient(circle at 50% 32%, #0f1f38 0%, #030712 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
  transition: opacity 0.3s ease, visibility 0.3s ease;
}
#spaceos-auth-overlay.hidden {
  opacity: 0;
  visibility: hidden;
  pointer-events: none;
}
.spaceos-auth-card {
  width: 90%;
  max-width: 400px;
  background: rgba(15, 23, 42, 0.95);
  border: 1px solid #0284c7;
  box-shadow: 0 0 35px rgba(56, 189, 248, 0.25), 0 25px 50px -12px rgba(0, 0, 0, 0.9);
  border-radius: 16px;
  padding: 32px 26px;
  text-align: center;
  color: #f8fafc;
  animation: authCardIn 0.35s cubic-bezier(0.16, 1, 0.3, 1);
}
@keyframes authCardIn {
  from { opacity: 0; transform: scale(0.92) translateY(12px); }
  to { opacity: 1; transform: scale(1) translateY(0); }
}
.spaceos-auth-badge {
  display: inline-block;
  padding: 4px 14px;
  border-radius: 9999px;
  background: rgba(56, 189, 248, 0.12);
  border: 1px solid rgba(56, 189, 248, 0.35);
  color: #38bdf8;
  font-size: 0.72rem;
  font-weight: 700;
  letter-spacing: 1.2px;
  margin-bottom: 12px;
}
.spaceos-auth-title {
  font-size: 1.35rem;
  font-weight: 800;
  color: #f8fafc;
  margin-bottom: 6px;
  letter-spacing: 0.5px;
}
.spaceos-auth-sub {
  font-size: 0.8rem;
  color: #94a3b8;
  margin-bottom: 22px;
}
.spaceos-input-group {
  margin-bottom: 14px;
  text-align: left;
}
.spaceos-input-label {
  display: block;
  font-size: 0.75rem;
  font-weight: 600;
  color: #94a3b8;
  margin-bottom: 6px;
}
.spaceos-input-wrap {
  position: relative;
  display: flex;
  align-items: center;
}
.spaceos-input-wrap span.icon {
  position: absolute;
  left: 12px;
  font-size: 0.95rem;
  color: #64748b;
  pointer-events: none;
}
.spaceos-auth-input {
  width: 100%;
  padding: 10px 12px 10px 38px;
  background: #090d16;
  border: 1px solid #334155;
  border-radius: 8px;
  color: #f8fafc;
  font-size: 0.88rem;
  outline: none;
  transition: border-color 0.2s, box-shadow 0.2s;
  box-sizing: border-box;
}
.spaceos-auth-input:focus {
  border-color: #38bdf8;
  box-shadow: 0 0 10px rgba(56, 189, 248, 0.3);
}
.spaceos-auth-btn {
  width: 100%;
  padding: 11px;
  background: linear-gradient(135deg, #0284c7 0%, #0369a1 100%);
  border: 1px solid #38bdf8;
  border-radius: 8px;
  color: white;
  font-size: 0.9rem;
  font-weight: 700;
  cursor: pointer;
  margin-top: 8px;
  transition: all 0.2s;
  box-shadow: 0 4px 15px rgba(2, 132, 199, 0.4);
}
.spaceos-auth-btn:hover {
  background: linear-gradient(135deg, #0ea5e9 0%, #0284c7 100%);
  box-shadow: 0 0 20px rgba(56, 189, 248, 0.6);
  transform: translateY(-1px);
}
.spaceos-auth-err {
  display: none;
  background: rgba(225, 29, 72, 0.15);
  border: 1px solid #f43f5e;
  border-radius: 8px;
  padding: 8px 12px;
  color: #fda4af;
  font-size: 0.78rem;
  margin-bottom: 14px;
  text-align: left;
}
.spaceos-auth-hint {
  font-size: 0.73rem;
  color: #64748b;
  margin-top: 14px;
  line-height: 1.4;
}

/* User pill in status bar */
.auth-user-pill {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  background: #0f172a;
  border: 1px solid #1e293b;
  padding: 3px 10px;
  border-radius: 8px;
  font-size: 0.76rem;
}
.auth-user-pill .auth-name {
  color: #38bdf8;
  font-weight: 700;
}
.auth-action-btn {
  background: none;
  border: none;
  color: #94a3b8;
  font-size: 0.72rem;
  cursor: pointer;
  padding: 2px 6px;
  border-radius: 4px;
  transition: 0.15s;
}
.auth-action-btn:hover {
  color: white;
  background: #334155;
}
.auth-action-btn.logout:hover {
  color: #fda4af;
  background: rgba(225, 29, 72, 0.25);
}

/* Change password modal */
#spaceos-pwd-modal {
  position: fixed;
  inset: 0;
  z-index: 999999;
  background: rgba(0,0,0,0.75);
  display: flex;
  align-items: center;
  justify-content: center;
  backdrop-filter: blur(8px);
}
#spaceos-pwd-modal.hidden {
  display: none;
}
</style>

<!-- FULLSCREEN LOGIN MODAL -->
<div id="spaceos-auth-overlay">
  <div class="spaceos-auth-card">
    <div class="spaceos-auth-badge">SPACE STATION // ESP32-S3</div>
    <div class="spaceos-auth-title">🔐 TRẠM VŨ TRỤ DECOR</div>
    <div class="spaceos-auth-sub">Xác thực quyền quản trị hệ thống điều khiển trạm</div>

    <div id="spaceos-auth-err" class="spaceos-auth-err"></div>

    <form id="spaceos-login-form" onsubmit="window.doSpaceOsLogin(event)">
      <div class="spaceos-input-group">
        <label class="spaceos-input-label">Tên Đăng Nhập</label>
        <div class="spaceos-input-wrap">
          <span class="icon">👤</span>
          <input type="text" id="spaceos-user-input" class="spaceos-auth-input" placeholder="admin" autocomplete="username" required />
        </div>
      </div>

      <div class="spaceos-input-group">
        <label class="spaceos-input-label">Mật Khẩu</label>
        <div class="spaceos-input-wrap">
          <span class="icon">🔒</span>
          <input type="password" id="spaceos-pass-input" class="spaceos-auth-input" placeholder="••••••••" autocomplete="current-password" required />
        </div>
      </div>

      <button type="submit" id="spaceos-login-btn" class="spaceos-auth-btn">🚀 ĐĂNG NHẬP HỆ THỐNG</button>
    </form>

    <div class="spaceos-auth-hint">
      💡 Tài khoản ban đầu: <strong>admin</strong> / <strong>admin</strong><br>
      (Bạn có thể đổi mật khẩu bất kỳ lúc nào sau khi đăng nhập)
    </div>
  </div>
</div>

<!-- CHANGE PASSWORD MODAL -->
<div id="spaceos-pwd-modal" class="hidden">
  <div class="spaceos-auth-card" style="max-width:360px;padding:26px 22px;">
    <div class="spaceos-auth-title" style="font-size:1.15rem;">🔑 ĐỔI MẬT KHẨU</div>
    <div class="spaceos-auth-sub" style="margin-bottom:16px;">Cập nhật mật khẩu quản trị Web Control Center</div>

    <div id="spaceos-pwd-err" class="spaceos-auth-err"></div>

    <form id="spaceos-pwd-form" onsubmit="window.doChangePasswordSubmit(event)">
      <div class="spaceos-input-group">
        <label class="spaceos-input-label">Mật Khẩu Hiện Tại</label>
        <div class="spaceos-input-wrap">
          <span class="icon">🔒</span>
          <input type="password" id="spaceos-old-pwd" class="spaceos-auth-input" required />
        </div>
      </div>
      <div class="spaceos-input-group">
        <label class="spaceos-input-label">Mật Khẩu Mới (≥ 3 ký tự)</label>
        <div class="spaceos-input-wrap">
          <span class="icon">✨</span>
          <input type="password" id="spaceos-new-pwd" class="spaceos-auth-input" required />
        </div>
      </div>
      <div class="spaceos-input-group">
        <label class="spaceos-input-label">Xác Nhận Mật Khẩu Mới</label>
        <div class="spaceos-input-wrap">
          <span class="icon">✔️</span>
          <input type="password" id="spaceos-confirm-pwd" class="spaceos-auth-input" required />
        </div>
      </div>

      <div style="display:flex;gap:8px;margin-top:14px;">
        <button type="button" class="btn-secondary" style="flex:1;padding:9px;" onclick="window.closeChangePasswordModal()">Hủy</button>
        <button type="submit" class="btn-primary" style="flex:1;padding:9px;">Lưu Thay Đổi</button>
      </div>
    </form>
  </div>
</div>

<script>
(function() {
  // 1. Fetch Wrapper: Tự động đính kèm Cookie + Header Token và bắt lỗi 401 Unauthorized
  const origFetch = window.fetch;
  window.fetch = function(url, init) {
    init = init || {};
    init.credentials = 'same-origin';
    const tok = localStorage.getItem('spaceos_token');
    if (tok) {
      init.headers = init.headers || {};
      if (init.headers instanceof Headers) {
        if (!init.headers.has('X-Auth-Token')) init.headers.append('X-Auth-Token', tok);
      } else if (Array.isArray(init.headers)) {
        init.headers.push(['X-Auth-Token', tok]);
      } else {
        if (!init.headers['X-Auth-Token']) init.headers['X-Auth-Token'] = tok;
      }
    }
    return origFetch(url, init).then(res => {
      if (res.status === 401 && !url.includes('/api/auth/login') && !url.includes('/api/auth/status')) {
        window.showLoginOverlay();
      }
      return res;
    });
  };

  // 2. Quản lý trạng thái xác thực
  window.showLoginOverlay = function() {
    const ov = document.getElementById('spaceos-auth-overlay');
    if (ov) ov.classList.remove('hidden');
    const uInput = document.getElementById('spaceos-user-input');
    if (uInput) setTimeout(() => uInput.focus(), 100);
  };

  window.hideLoginOverlay = function() {
    const ov = document.getElementById('spaceos-auth-overlay');
    if (ov) ov.classList.add('hidden');
  };

  window.checkAuthStatus = function() {
    fetch('/api/auth/status')
      .then(r => r.json())
      .then(d => {
        if (d && d.authenticated) {
          window.hideLoginOverlay();
          updateUserBadge(d.username || 'admin');
        } else {
          window.showLoginOverlay();
        }
      })
      .catch(() => {
        window.showLoginOverlay();
      });
  };

  // 3. Đăng nhập
  window.doSpaceOsLogin = function(e) {
    if (e) e.preventDefault();
    const u = (document.getElementById('spaceos-user-input').value || '').trim();
    const p = (document.getElementById('spaceos-pass-input').value || '').trim();
    const btn = document.getElementById('spaceos-login-btn');
    const errBox = document.getElementById('spaceos-auth-err');

    if (!u || !p) return;
    if (btn) btn.innerText = '⏳ Đang xác thực...';
    if (errBox) errBox.style.display = 'none';

    fetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: u, password: p })
    })
    .then(r => r.json().then(data => ({ status: r.status, data })))
    .then(res => {
      if (btn) btn.innerText = '🚀 ĐĂNG NHẬP HỆ THỐNG';
      if (res.status === 200 && res.data.status === 'ok') {
        if (res.data.token) {
          localStorage.setItem('spaceos_token', res.data.token);
          document.cookie = 'spaceos_token=' + res.data.token + '; Path=/; Max-Age=2592000; SameSite=Lax';
        }
        window.hideLoginOverlay();
        updateUserBadge(res.data.username || u);
      } else {
        if (errBox) {
          errBox.innerText = '❌ ' + (res.data.message || 'Sai tên đăng nhập hoặc mật khẩu!');
          errBox.style.display = 'block';
        }
      }
    })
    .catch(err => {
      if (btn) btn.innerText = '🚀 ĐĂNG NHẬP HỆ THỐNG';
      if (errBox) {
        errBox.innerText = '❌ Lỗi kết nối đến thiết bị: ' + err;
        errBox.style.display = 'block';
      }
    });
  };

  // 4. Đăng xuất
  window.doSpaceOsLogout = function() {
    if (!confirm('Bạn có chắc chắn muốn đăng xuất khỏi Trạm Điều Khiển?')) return;
    fetch('/api/auth/logout', { method: 'POST' })
      .finally(() => {
        localStorage.removeItem('spaceos_token');
        document.cookie = 'spaceos_token=; Path=/; Max-Age=0; SameSite=Lax';
        window.showLoginOverlay();
      });
  };

  // 5. Đổi mật khẩu
  window.openChangePasswordModal = function() {
    const modal = document.getElementById('spaceos-pwd-modal');
    if (modal) modal.classList.remove('hidden');
    const err = document.getElementById('spaceos-pwd-err');
    if (err) err.style.display = 'none';
    const oldP = document.getElementById('spaceos-old-pwd');
    if (oldP) { oldP.value = ''; oldP.focus(); }
    const newP = document.getElementById('spaceos-new-pwd');
    if (newP) newP.value = '';
    const cfP = document.getElementById('spaceos-confirm-pwd');
    if (cfP) cfP.value = '';
  };

  window.closeChangePasswordModal = function() {
    const modal = document.getElementById('spaceos-pwd-modal');
    if (modal) modal.classList.add('hidden');
  };

  window.doChangePasswordSubmit = function(e) {
    if (e) e.preventDefault();
    const oldPass = (document.getElementById('spaceos-old-pwd').value || '').trim();
    const newPass = (document.getElementById('spaceos-new-pwd').value || '').trim();
    const confPass = (document.getElementById('spaceos-confirm-pwd').value || '').trim();
    const errBox = document.getElementById('spaceos-pwd-err');

    if (!oldPass || !newPass) return;
    if (newPass !== confPass) {
      if (errBox) {
        errBox.innerText = '❌ Xác nhận mật khẩu mới không khớp!';
        errBox.style.display = 'block';
      }
      return;
    }
    if (newPass.length < 3) {
      if (errBox) {
        errBox.innerText = '❌ Mật khẩu mới phải từ 3 ký tự trở lên!';
        errBox.style.display = 'block';
      }
      return;
    }

    fetch('/api/auth/change_password', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ old_password: oldPass, new_password: newPass })
    })
    .then(r => r.json().then(data => ({ status: r.status, data })))
    .then(res => {
      if (res.status === 200 && res.data.status === 'ok') {
        if (res.data.token) {
          localStorage.setItem('spaceos_token', res.data.token);
          document.cookie = 'spaceos_token=' + res.data.token + '; Path=/; Max-Age=2592000; SameSite=Lax';
        }
        window.closeChangePasswordModal();
        alert('✅ ' + (res.data.message || 'Đổi mật khẩu thành công!'));
      } else {
        if (errBox) {
          errBox.innerText = '❌ ' + (res.data.message || 'Không thể đổi mật khẩu!');
          errBox.style.display = 'block';
        }
      }
    })
    .catch(err => {
      if (errBox) {
        errBox.innerText = '❌ Lỗi kết nối: ' + err;
        errBox.style.display = 'block';
      }
    });
  };

  // 6. Khôi phục cài đặt ban đầu (Factory Reset)
  window.performFactoryResetWeb = function() {
    const cf = confirm('⚠️ CẢNH BÁO KHÔI PHỤC CÀI ĐẶT GỐC:\n\n' +
                     'Toàn bộ tùy chỉnh cá nhân sẽ bị xóa!\n' +
                     '- Giao diện màn hình trạm sẽ chuyển về MÈO GOME CHIBI\n' +
                     '- Tài khoản đăng nhập Web sẽ khôi phục về admin / admin\n' +
                     '- Đặt lại âm lượng (70%) và cấu hình hệ thống ban đầu.\n\n' +
                     'Bạn có chắc chắn muốn thực hiện không?');
    if (!cf) return;

    fetch('/api/system/factory_reset', { method: 'POST' })
      .then(r => r.json().then(data => ({ status: r.status, data })))
      .then(res => {
        if (res.status === 200 && res.data.status === 'ok') {
          alert('✅ ' + (res.data.message || 'Đã khôi phục cài đặt ban đầu thành công!'));
          localStorage.removeItem('spaceos_token');
          document.cookie = 'spaceos_token=; Path=/; Max-Age=0; SameSite=Lax';
          location.reload();
        } else {
          alert('❌ Lỗi: ' + (res.data.message || 'Không thể thực hiện khôi phục cài đặt gốc!'));
        }
      })
      .catch(e => {
        alert('❌ Lỗi kết nối: ' + e);
      });
  };

  // 7. Inject User Badge vào Header Status Bar
  function updateUserBadge(username) {
    let pill = document.getElementById('auth-user-pill');
    if (!pill) {
      const statusBar = document.querySelector('.status-bar');
      const statusLeft = document.querySelector('.status-left') || statusBar;
      if (statusLeft) {
        pill = document.createElement('div');
        pill.id = 'auth-user-pill';
        pill.className = 'auth-user-pill';
        pill.innerHTML = `
          <span>👤 <strong id="auth-pill-username" class="auth-name">${username}</strong></span>
          <button class="auth-action-btn" onclick="window.openChangePasswordModal()" title="Đổi mật khẩu">🔑 Đổi MK</button>
          <button class="auth-action-btn logout" onclick="window.doSpaceOsLogout()" title="Đăng xuất">🚪 Thoát</button>
        `;
        statusLeft.appendChild(pill);
      }
    } else {
      const nameEl = document.getElementById('auth-pill-username');
      if (nameEl) nameEl.innerText = username;
    }
  }

  // Khởi động kiểm tra trạng thái xác thực
  setTimeout(window.checkAuthStatus, 150);
})();
</script>
)rawliteral";

