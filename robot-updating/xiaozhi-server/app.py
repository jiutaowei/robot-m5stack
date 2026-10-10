import sys
import uuid
import signal
import socket
import asyncio
from aioconsole import ainput
from config.settings import load_config
from config.logger import setup_logging
from core.utils.util import get_local_ip, validate_mcp_endpoint
from core.api.discovery_responder import start_discovery_responder
from core.http_server import SimpleHttpServer
from core.websocket_server import WebSocketServer
from core.utils.util import check_ffmpeg_installed
from core.utils.gc_manager import get_gc_manager

TAG = __name__
logger = setup_logging()


async def wait_for_exit() -> None:
    """
    阻塞直到收到 Ctrl‑C / SIGTERM。
    - Unix: 使用 add_signal_handler
    - Windows: 依赖 KeyboardInterrupt
    """
    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    if sys.platform != "win32":  # Unix / macOS
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, stop_event.set)
        await stop_event.wait()
    else:
        # Windows：await一个永远pending的fut，
        # 让 KeyboardInterrupt 冒泡到 asyncio.run，以此消除遗留普通线程导致进程退出阻塞的问题
        try:
            await asyncio.Future()
        except KeyboardInterrupt:  # Ctrl‑C
            pass


async def monitor_stdin():
    """监控标准输入，消费回车键"""
    while True:
        await ainput()  # 异步等待输入，消费回车


def _ensure_ports_free(ports) -> None:
    """启动前预检端口，避免「重复启动」变成半残服务。

    Windows 上 SO_REUSEADDR 允许第二个进程也绑定 8001，于是两个实例同时"运行"：
    连接会随机落到其中一个（可能正是 HTTP/UDP 没绑上的那个），现象是
    8003/8004 报「端口被占用」+ 机器人时好时坏。这里直接失败退出并说清原因。
    """
    busy = []
    for port in ports:
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.settimeout(0.5)
        try:
            if probe.connect_ex(("127.0.0.1", int(port))) == 0:
                busy.append(int(port))
        finally:
            probe.close()
    if not busy:
        return
    message = (
        f"\n[无法启动] 端口 {', '.join(str(p) for p in busy)} 已被占用："
        "很可能已经有一个米宝服务端正在运行。\n"
        "  1) 屏幕上那个「Mibao-1 AI Server」窗口就是正在跑的服务，不要重复双击；\n"
        "  2) 要重启：先关掉旧窗口，等 3 秒再启动本程序；\n"
        "  3) 若确定没有旧窗口，则是别的程序占了端口，"
        "可改 app\\data\\.config.yaml 里的 port / http_port。\n"
    )
    print(message)
    logger.bind(tag=TAG).error(message)
    sys.exit(1)


async def main():
    # ffmpeg 只在少数路径用到（本地音频文件播放/格式转换，见 core/utils/util.py 的
    # AudioSegment 相关函数）。便携版（米宝服务端.exe）默认不带 ffmpeg，用
    # MIBAO_ALLOW_NO_FFMPEG=1 允许缺省启动并给出提示；开发环境保持原样直接报错。
    try:
        check_ffmpeg_installed()
    except Exception as exc:
        if os.environ.get("MIBAO_ALLOW_NO_FFMPEG") == "1":
            print(f"[警告] 未检测到 ffmpeg（{exc}）")
            print("       语音对话不受影响；仅在播放本地音频文件等少数功能上需要。")
            print("       需要时把 ffmpeg.exe 放进 tools\\ffmpeg\\ 再重启本程序即可。")
        else:
            raise
    config = await load_config()

    # 先做端口预检：重复启动会退化成「两个实例抢连接」的假运行状态
    _ensure_ports_free(
        [
            int(config["server"].get("port", 8000)),
            int(config["server"].get("http_port", 8003)),
        ]
    )

    # auth_key优先级：配置文件server.auth_key > manager-api.secret > 自动生成
    # auth_key用于jwt认证，比如视觉分析接口的jwt认证、ota接口的token生成与websocket认证
    # 获取配置文件中的auth_key
    auth_key = config["server"].get("auth_key", "")
    
    # 验证auth_key，无效则尝试使用manager-api.secret
    if not auth_key or len(auth_key) == 0 or "你" in auth_key:
        auth_key = config.get("manager-api", {}).get("secret", "")
        # 验证secret，无效则生成随机密钥
        if not auth_key or len(auth_key) == 0 or "你" in auth_key:
            auth_key = str(uuid.uuid4().hex)
    
    config["server"]["auth_key"] = auth_key

    # 添加 stdin 监控任务
    stdin_task = asyncio.create_task(monitor_stdin())

    # 启动全局GC管理器（5分钟清理一次）
    gc_manager = get_gc_manager(interval_seconds=300)
    await gc_manager.start()

    # 启动 WebSocket 服务器
    ws_server = WebSocketServer(config)
    ws_task = asyncio.create_task(ws_server.start())
    # 启动 Simple http 服务器
    ota_server = SimpleHttpServer(config)
    ota_task = asyncio.create_task(ota_server.start())

    # 启动 UDP 自动发现服务（设备广播探测 → 回复服务器当前 IP，
    # 解决 Mac IP 由 DHCP 变化后设备连不上的问题）
    discovery_transport = await start_discovery_responder(logger, config)

    read_config_from_api = config.get("read_config_from_api", False)
    port = int(config["server"].get("http_port", 8003))
    host = get_local_ip()
    if not read_config_from_api:
        logger.bind(tag=TAG).info(
            "OTA接口是\t\thttp://{}:{}/xiaozhi/ota/",
            host,
            port,
        )
    logger.bind(tag=TAG).info(
        "视觉分析接口是\thttp://{}:{}/mcp/vision/explain",
        host,
        port,
    )
    mcp_endpoint = config.get("mcp_endpoint", None)
    if mcp_endpoint is not None and "你" not in mcp_endpoint:
        # 校验MCP接入点格式
        if validate_mcp_endpoint(mcp_endpoint):
            logger.bind(tag=TAG).info("mcp接入点是\t{}", mcp_endpoint)
            # 将mcp计入点地址转成调用点
            mcp_endpoint = mcp_endpoint.replace("/mcp/", "/call/")
            config["mcp_endpoint"] = mcp_endpoint
        else:
            logger.bind(tag=TAG).error("mcp接入点不符合规范")
            config["mcp_endpoint"] = "你的接入点 websocket地址"

    # 获取WebSocket配置，使用安全的默认值
    websocket_port = 8000
    server_config = config.get("server", {})
    if isinstance(server_config, dict):
        websocket_port = int(server_config.get("port", 8000))

    logger.bind(tag=TAG).info(
        "Websocket地址是\tws://{}:{}/xiaozhi/v1/",
        host,
        websocket_port,
    )

    logger.bind(tag=TAG).info(
        "=======上面的地址是websocket协议地址，请勿用浏览器访问======="
    )
    logger.bind(tag=TAG).info(
        "如想测试websocket请启动digital-human模块，打开浏览器交互测试"
    )
    logger.bind(tag=TAG).info(
        "=============================================================\n"
    )

    try:
        await wait_for_exit()  # 阻塞直到收到退出信号
    except asyncio.CancelledError:
        print("任务被取消，清理资源中...")
    finally:
        # 停止全局GC管理器
        await gc_manager.stop()

        # 取消所有任务（关键修复点）
        stdin_task.cancel()
        ws_task.cancel()
        if ota_task:
            ota_task.cancel()

        # 等待任务终止（必须加超时）
        await asyncio.wait(
            [stdin_task, ws_task, ota_task] if ota_task else [stdin_task, ws_task],
            timeout=3.0,
            return_when=asyncio.ALL_COMPLETED,
        )
        print("服务器已关闭，程序退出。")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("手动中断，程序终止。")
