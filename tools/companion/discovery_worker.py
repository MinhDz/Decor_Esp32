#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - PC Companion UDP Discovery & Sync Service
Tự động đồng bộ IP hai chiều giữa PC và Trạm Decor qua UDP Broadcast (cổng 8767).
Giải quyết triệt để lỗi đổi IP DHCP khi PC hoặc Trạm chuyển đổi băng tần 2.4GHz <-> 5GHz.
"""

import socket
import json
import time
import threading
import logging

logger = logging.getLogger("DiscoveryWorker")
DISCOVERY_PORT = 8767

def get_local_ip():
    """Lấy địa chỉ IP mạng LAN hiện tại của cạc mạng đang kết nối Internet/LAN."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip

class DiscoveryWorker(threading.Thread):
    def __init__(self, current_esp_ip="192.168.1.82", gateway_port=8765, transcoder_port=5050, on_station_discovered=None):
        super().__init__(name="DiscoveryWorker", daemon=True)
        self.current_esp_ip = current_esp_ip
        self.gateway_port = gateway_port
        self.transcoder_port = transcoder_port
        self.on_station_discovered = on_station_discovered
        self._running = False
        self._lock = threading.Lock()
        self.last_pc_ip = ""

    def set_esp_ip(self, ip):
        with self._lock:
            self.current_esp_ip = str(ip).strip()

    def stop(self):
        self._running = False

    def run(self):
        self._running = True
        logger.info(f"DiscoveryWorker đã khởi động (UDP Broadcast Port {DISCOVERY_PORT}).")

        # Socket lắng nghe UDP Broadcast
        recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            recv_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            recv_sock.bind(('', DISCOVERY_PORT))
            recv_sock.settimeout(1.0)
        except Exception as e:
            logger.warning(f"Không thể bind UDP port {DISCOVERY_PORT}: {e}")
            recv_sock = None

        # Socket gửi UDP Broadcast
        send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        send_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

        last_beacon_time = 0

        while self._running:
            now = time.time()

            # 1. Định kỳ mỗi 4 giây: Bắn broadcast PC Companion Beacon
            if now - last_beacon_time >= 4.0:
                last_beacon_time = now
                pc_ip = get_local_ip()
                if pc_ip != "127.0.0.1":
                    if pc_ip != self.last_pc_ip:
                        logger.info(f"🌐 [NETWORK CHANGE] PC đổi IP mạng LAN sang: {pc_ip}")
                        self.last_pc_ip = pc_ip

                    beacon_data = {
                        "service": "decor_companion",
                        "ip": pc_ip,
                        "gateway_port": self.gateway_port,
                        "transcoder_port": self.transcoder_port,
                        "timestamp": int(now)
                    }
                    try:
                        raw = json.dumps(beacon_data).encode('utf-8')
                        send_sock.sendto(raw, ('255.255.255.255', DISCOVERY_PORT))
                    except Exception as e:
                        logger.debug(f"Lỗi gửi beacon: {e}")

            # 2. Lắng nghe beacon từ Trạm Decor
            if recv_sock:
                try:
                    data, addr = recv_sock.recvfrom(2048)
                    if data:
                        doc = json.loads(data.decode('utf-8', errors='ignore'))
                        device = doc.get("device")
                        if device in ("tramvutru", "DecorStation"):
                            discovered_ip = doc.get("ip") or addr[0]
                            with self._lock:
                                old_ip = self.current_esp_ip
                            if discovered_ip and discovered_ip != old_ip:
                                logger.info(f"🎯 [DISCOVERY] Phát hiện Trạm Decor tại IP mới: {discovered_ip} (cũ: {old_ip})")
                                with self._lock:
                                    self.current_esp_ip = discovered_ip
                                if self.on_station_discovered:
                                    self.on_station_discovered(discovered_ip)
                except socket.timeout:
                    pass
                except Exception as e:
                    logger.debug(f"Lỗi nhận beacon: {e}")
            else:
                time.sleep(1.0)

        if recv_sock:
            recv_sock.close()
        send_sock.close()
        logger.info("DiscoveryWorker đã dừng.")
