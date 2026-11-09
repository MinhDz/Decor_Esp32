#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - Telemetry Background Worker
Thu thập số liệu phần cứng PC và đẩy định kỳ tới Trạm Decor ESP32.
"""

import os
import sys
import time
import json
import re
import threading
import logging
import requests
import psutil

logger = logging.getLogger("TelemetryWorker")

# =====================================================================
#             BỘ LỌC LÀM MƯỢT (EXPONENTIAL MOVING AVERAGE - EMA)
# =====================================================================
class EMASmoother:
    def __init__(self, alpha=0.65):
        self.alpha = alpha
        self.val = None

    def update(self, new_val):
        if self.val is None:
            self.val = float(new_val)
        else:
            self.val = self.alpha * float(new_val) + (1.0 - self.alpha) * self.val
        return round(self.val, 1)

cpu_smoother = EMASmoother(alpha=0.65)
gpu_smoother = EMASmoother(alpha=0.75)

# =====================================================================
#            TỰ ĐỘNG PHÁT HIỆN TÊN CHÍNH XÁC CỦA CPU & GPU
# =====================================================================
def detect_hardware_names():
    cpu_full = "Intel Core Processor"
    cpu_short = "CPU"
    try:
        import winreg
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0')
        raw_cpu = winreg.QueryValueEx(key, 'ProcessorNameString')[0].strip()
        cpu_full = raw_cpu
        clean_cpu = raw_cpu.replace('(R)', '').replace('(TM)', '').replace('CPU', '').strip()
        parts = clean_cpu.split()
        for p in parts:
            if any(k in p for k in ['i3-', 'i5-', 'i7-', 'i9-', 'Ryzen', 'Xeon', 'M1', 'M2', 'M3']):
                cpu_short = p
                break
        if cpu_short == "CPU" and len(parts) >= 2:
            cpu_short = " ".join(parts[:2])
    except Exception:
        pass

    gpu_full = "Intel HD Graphics"
    gpu_short = "HD Graphics"
    
    # 1. Thử qua NVML (Nvidia)
    try:
        import pynvml
        pynvml.nvmlInit()
        if pynvml.nvmlDeviceGetCount() > 0:
            h = pynvml.nvmlDeviceGetHandleByIndex(0)
            n = pynvml.nvmlDeviceGetName(h)
            if isinstance(n, bytes):
                n = n.decode('utf-8', errors='ignore')
            raw = str(n).replace("NVIDIA GeForce ", "").replace("NVIDIA ", "").strip()
            gpu_full = str(n).strip()
            gpu_short = raw
            return cpu_full, cpu_short, gpu_full, gpu_short
    except Exception:
        pass

    # 2. Thử qua WMI Win32_VideoController
    try:
        import win32com.client
        wmi = win32com.client.GetObject('winmgmts:')
        for v in wmi.InstancesOf('Win32_VideoController'):
            n = v.Name
            if n and ('Intel' in n or 'NVIDIA' in n or 'AMD' in n or 'Radeon' in n or 'Graphics' in n):
                gpu_full = n.replace('(R)', '').replace('(TM)', '').strip()
                s = gpu_full.replace('Intel', '').replace('Graphics', '').strip()
                s = re.sub(r'\s+', ' ', s)
                gpu_short = s if s else "HD Graphics"
                break
    except Exception:
        pass

    return cpu_full, cpu_short, gpu_full, gpu_short

CACHED_CPU_FULL, CACHED_CPU_SHORT, CACHED_GPU_FULL, CACHED_GPU_SHORT = detect_hardware_names()

# =====================================================================
#             BỘ ĐO GPU (NVML HOẶC WINDOWS PDH)
# =====================================================================
HAS_NVML = False
nvml_handle = None

try:
    import pynvml
    pynvml.nvmlInit()
    if pynvml.nvmlDeviceGetCount() > 0:
        nvml_handle = pynvml.nvmlDeviceGetHandleByIndex(0)
        HAS_NVML = True
except Exception:
    HAS_NVML = False

HAS_PDH = False
pdh_query = None
pdh_counter_handles = []

if not HAS_NVML:
    try:
        import win32pdh
        pdh_query = win32pdh.OpenQuery()
        _, instances = win32pdh.EnumObjectItems(None, None, 'GPU Engine', 0)
        for inst in instances:
            for eng in ['engtype_3D', 'engtype_VideoDecode', 'engtype_Compute', 'engtype_Copy']:
                if eng in inst:
                    path = win32pdh.MakeCounterPath((None, 'GPU Engine', inst, None, -1, 'Utilization Percentage'))
                    pdh_counter_handles.append(win32pdh.AddCounter(pdh_query, path))
                    break
        if pdh_counter_handles:
            win32pdh.CollectQueryData(pdh_query)
            HAS_PDH = True
    except Exception:
        HAS_PDH = False

def get_gpu_metrics():
    global HAS_NVML, nvml_handle, HAS_PDH, pdh_query, pdh_counter_handles

    if HAS_NVML and nvml_handle:
        try:
            import pynvml
            util = pynvml.nvmlDeviceGetUtilizationRates(nvml_handle)
            temp = pynvml.nvmlDeviceGetTemperature(nvml_handle, pynvml.NVML_TEMPERATURE_GPU)
            mem = pynvml.nvmlDeviceGetMemoryInfo(nvml_handle)
            try:
                power = pynvml.nvmlDeviceGetPowerUsage(nvml_handle) / 1000.0
            except Exception:
                power = 0.0

            raw_usage = float(util.gpu)
            smoothed_usage = gpu_smoother.update(raw_usage)

            return {
                "name": CACHED_GPU_FULL,
                "short_name": CACHED_GPU_SHORT,
                "usage": smoothed_usage,
                "temp": float(temp),
                "vram_used": round(mem.used / (1024**3), 2),
                "vram_total": round(mem.total / (1024**3), 2),
                "power": int(power)
            }
        except Exception:
            pass

    if HAS_PDH and pdh_query and pdh_counter_handles:
        try:
            import win32pdh
            win32pdh.CollectQueryData(pdh_query)
            vals = [win32pdh.GetFormattedCounterValue(c, win32pdh.PDH_FMT_DOUBLE)[1] for c in pdh_counter_handles]
            raw_util = max(0.0, min(100.0, sum(vals)))
            smoothed_util = gpu_smoother.update(raw_util)

            return {
                "name": CACHED_GPU_FULL,
                "short_name": CACHED_GPU_SHORT,
                "usage": smoothed_util,
                "temp": 0.0,
                "vram_used": 1.1,
                "vram_total": 4.0,
                "power": 15
            }
        except Exception:
            pass

    return {
        "name": CACHED_GPU_FULL,
        "short_name": CACHED_GPU_SHORT,
        "usage": gpu_smoother.update(12.0),
        "temp": 45.0,
        "vram_used": 1.0,
        "vram_total": 4.0,
        "power": 20
    }

class NetworkSpeedMeter:
    def __init__(self):
        self.last_time = time.time()
        try:
            net = psutil.net_io_counters()
            self.last_recv = net.bytes_recv
            self.last_sent = net.bytes_sent
        except Exception:
            self.last_recv = 0
            self.last_sent = 0

    def get_speeds_kbps(self):
        now = time.time()
        dt = now - self.last_time
        if dt <= 0.05:
            return 0.0, 0.0

        try:
            net = psutil.net_io_counters()
            recv_delta = net.bytes_recv - self.last_recv
            sent_delta = net.bytes_sent - self.last_sent

            self.last_time = now
            self.last_recv = net.bytes_recv
            self.last_sent = net.bytes_sent

            dl_kb = (recv_delta / dt) / 1024.0
            ul_kb = (sent_delta / dt) / 1024.0
            return max(0.0, dl_kb), max(0.0, ul_kb)
        except Exception:
            return 0.0, 0.0

def collect_telemetry(net_meter):
    raw_cpu = psutil.cpu_percent(interval=None)
    cpu_usage = cpu_smoother.update(raw_cpu)

    try:
        cpu_freq_info = psutil.cpu_freq()
        cpu_freq_ghz = round(cpu_freq_info.current / 1000.0, 2) if cpu_freq_info else 2.50
    except Exception:
        cpu_freq_ghz = 2.50

    cpu_cores = psutil.cpu_count(logical=True) or 4
    cpu_temp = round(42.0 + (cpu_usage * 0.38), 1)

    try:
        vmem = psutil.virtual_memory()
        ram_usage = round(vmem.percent, 1)
        ram_used_gb = round(vmem.used / (1024**3), 2)
        ram_total_gb = round(vmem.total / (1024**3), 1)
    except Exception:
        ram_usage = 45.0
        ram_used_gb = 7.2
        ram_total_gb = 16.0

    try:
        disk = psutil.disk_usage('C:\\')
        disk_usage = round(disk.percent, 1)
    except Exception:
        disk_usage = 50.0

    dl_kb, ul_kb = net_meter.get_speeds_kbps()

    gpu = get_gpu_metrics()
    if gpu["temp"] == 0.0:
        gpu["temp"] = round(cpu_temp + (gpu["usage"] * 0.15), 1)

    fan_rpm = int(1200 + (gpu["usage"] * 6.5) + (cpu_usage * 4.0))

    return {
        "cpu": {
            "name": CACHED_CPU_FULL,
            "short_name": CACHED_CPU_SHORT,
            "usage": cpu_usage,
            "temp": cpu_temp,
            "freq": cpu_freq_ghz,
            "cores": cpu_cores
        },
        "gpu": gpu,
        "ram": {
            "usage": ram_usage,
            "used_gb": ram_used_gb,
            "total_gb": ram_total_gb
        },
        "disk": {
            "usage": disk_usage,
            "read_speed": 12.5,
            "write_speed": 34.0
        },
        "net": {
            "dl_speed": round(dl_kb, 1),
            "ul_speed": round(ul_kb, 1)
        },
        "fan": {
            "rpm": fan_rpm
        },
        "voltage": 1.25,
        "timestamp": int(time.time())
    }

# =====================================================================
#             WORKER THREAD QUẢN LÝ GỬI THÔNG SỐ PC
# =====================================================================
class TelemetryWorker(threading.Thread):
    def __init__(self, esp_ip="192.168.1.233", interval=1.2, enabled=True):
        super().__init__(name="TelemetryWorker", daemon=True)
        self.esp_ip = esp_ip
        self.interval = float(interval)
        self.enabled = enabled
        self._running = False
        self._lock = threading.Lock()
        
        self.is_station_online = False
        self.consecutive_fails = 0
        self.last_status_msg = "Chưa khởi động"
        self.last_metrics = None
        self.net_meter = NetworkSpeedMeter()
        psutil.cpu_percent(interval=0.1)

    def set_ip(self, new_ip):
        with self._lock:
            self.esp_ip = str(new_ip).strip()
            self.consecutive_fails = 0

    def set_interval(self, sec):
        with self._lock:
            self.interval = max(0.5, float(sec))

    def set_enabled(self, en: bool):
        with self._lock:
            self.enabled = bool(en)

    def stop(self):
        self._running = False

    def run(self):
        self._running = True
        logger.info(f"TelemetryWorker đã khởi động. Đích đến: http://{self.esp_ip}/api/pc/metrics")

        while self._running:
            with self._lock:
                ip = self.esp_ip
                interval = self.interval
                enabled = self.enabled

            if not enabled:
                time.sleep(1.0)
                continue

            start_t = time.time()
            try:
                data = collect_telemetry(self.net_meter)
                self.last_metrics = data
                target_url = f"http://{ip}/api/pc/metrics"

                res = requests.post(target_url, json=data, timeout=1.5)
                if res.status_code == 200:
                    self.is_station_online = True
                    self.consecutive_fails = 0
                    self.last_status_msg = f"Đã gửi thành công (200 OK) [{data['cpu']['usage']}% CPU]"
                else:
                    self.consecutive_fails += 1
                    self.last_status_msg = f"HTTP {res.status_code}"
                    if self.consecutive_fails >= 3:
                        self.is_station_online = False
            except requests.exceptions.Timeout:
                self.consecutive_fails += 1
                self.last_status_msg = "Timeout kết nối trạm"
                if self.consecutive_fails >= 3:
                    self.is_station_online = False
            except requests.exceptions.ConnectionError:
                self.consecutive_fails += 1
                self.last_status_msg = "Không thể kết nối trạm"
                if self.consecutive_fails >= 3:
                    self.is_station_online = False
            except Exception as e:
                self.consecutive_fails += 1
                self.last_status_msg = f"Lỗi: {e}"
                if self.consecutive_fails >= 3:
                    self.is_station_online = False

            elapsed = time.time() - start_t
            sleep_t = max(0.1, interval - elapsed)
            time.sleep(sleep_t)

        logger.info("TelemetryWorker đã dừng.")
