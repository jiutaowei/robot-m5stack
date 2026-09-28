"""
米宝服务器 UDP 自动发现响应器。

背景：Mac（服务器）的局域网 IP 由 DHCP 分配，可能变化。设备端固件里保存的
服务器地址一旦对不上就连不上，而设备又无法通过 OTA 自我修复（鸡生蛋问题）。

方案：设备启动后向局域网广播 UDP 探测包（MIBAO_DISCOVER），本模块收到后
回复服务器当前可用的 IP 与端口，设备据此更新自己的服务器地址。

协议（纯文本，便于设备端 sscanf 解析）：
    设备 -> 服务器: "MIBAO_DISCOVER"
    服务器 -> 设备: "MIBAO_SERVER|<ip>|<ws_port>|<http_port>"
"""

import asyncio
import socket

TAG = "discovery"

# 发现服务端口（设备端固件需与此一致）
DISCOVERY_PORT = 8004
PROBE_MAGIC = "MIBAO_DISCOVER"


class DiscoveryProtocol(asyncio.DatagramProtocol):
    def __init__(self, logger, ws_port: int, http_port: int):
        self.logger = logger
        self.ws_port = ws_port
        self.http_port = http_port
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def _resolve_local_ip(self, peer_ip: str) -> str:
        """返回本机局域网 IP（设备将用它作为服务器地址）。"""
        # 优先按路由到该设备的源地址（多网卡时更准确）
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                s.connect((peer_ip, 9))  # UDP connect 不实际发包，仅让内核选路
                ip = s.getsockname()[0]
            finally:
                s.close()
            # 排除回环（跨主机场景不会出现，防御性处理）
            if ip and not ip.startswith("127."):
                return ip
        except Exception:
            pass
        try:
            from core.utils.util import get_local_ip

            return get_local_ip()
        except Exception:
            return "127.0.0.1"

    def datagram_received(self, data: bytes, addr):
        try:
            text = data.decode("utf-8", "replace").strip()
        except Exception:
            return
        if PROBE_MAGIC not in text:
            return

        ip = self._resolve_local_ip(addr[0])
        reply = f"MIBAO_SERVER|{ip}|{self.ws_port}|{self.http_port}".encode()
        try:
            # 无线链路易丢包/抖动，连发 3 次提高到达率
            for _ in range(3):
                self.transport.sendto(reply, addr)
            if self.logger:
                self.logger.bind(tag=TAG).info(
                    f"设备 {addr[0]}:{addr[1]} 请求发现，回复服务器地址 {ip}"
                )
        except Exception as e:
            if self.logger:
                self.logger.bind(tag=TAG).warning(f"回复发现请求失败: {e}")


async def start_discovery_responder(logger, config: dict):
    """启动 UDP 发现响应器，返回 transport（调用方可忽略）。"""
    server_cfg = config.get("server", {}) or {}
    ws_port = int(server_cfg.get("port", 8001))
    http_port = int(server_cfg.get("http_port", 8003))

    loop = asyncio.get_running_loop()
    try:
        transport, _ = await loop.create_datagram_endpoint(
            lambda: DiscoveryProtocol(logger, ws_port, http_port),
            local_addr=("0.0.0.0", DISCOVERY_PORT),
            allow_broadcast=True,
        )
        logger.bind(tag=TAG).info(
            f"UDP 自动发现服务已启动，监听 0.0.0.0:{DISCOVERY_PORT} "
            f"(回复 ws:{ws_port} http:{http_port})"
        )
        return transport
    except OSError as e:
        logger.bind(tag=TAG).warning(
            f"UDP 自动发现服务启动失败（端口 {DISCOVERY_PORT} 可能被占用）: {e}"
        )
        return None
