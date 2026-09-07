import asyncio
from aiohttp import web
from config.logger import setup_logging
from core.api.ota_handler import OTAHandler
from core.api.vision_handler import VisionHandler
from core.api.meeting_transcribe_handler import MeetingTranscribeHandler

TAG = __name__


class SimpleHttpServer:
    def __init__(self, config: dict):
        self.config = config
        self.logger = setup_logging()
        self.ota_handler = OTAHandler(config)
        self.vision_handler = VisionHandler(config)
        self.meeting_transcribe_handler = MeetingTranscribeHandler(config)

    def _get_websocket_url(self, local_ip: str, port: int) -> str:
        """获取websocket地址

        Args:
            local_ip: 本地IP地址
            port: 端口号

        Returns:
            str: websocket地址
        """
        server_config = self.config["server"]
        websocket_config = server_config.get("websocket")

        if websocket_config and "你" not in websocket_config:
            return websocket_config
        else:
            return f"ws://{local_ip}:{port}/xiaozhi/v1/"

    async def _handle_device_config(self, request):
        """设备配置接口：返回 upload_url（会议录音上传地址）等。

        设备端（mibao_config.cpp pullDeviceConfigFromServer）启动时会
        GET /api/device/config 拉取配置，把 upload_url 写入 NVS，
        之后会议录音结束自动上传 WAV 到此地址转会议纪要。
        """
        try:
            import json as _json

            from core.utils.util import get_local_ip

            server_config = self.config["server"]
            http_port = int(server_config.get("http_port", 8003))
            ip = get_local_ip()
            upload_url = f"http://{ip}:{http_port}/mibao/meeting/transcribe"
            ota_url = f"http://{ip}:{http_port}/xiaozhi/ota/"
            config = {"upload_url": upload_url, "ota_url": ota_url}
            response = web.Response(
                text=_json.dumps(config, ensure_ascii=False),
                content_type="application/json",
            )
        except Exception as e:
            self.logger.bind(tag=TAG).error(f"设备配置接口异常: {e}")
            response = web.Response(
                text='{"error":"internal error"}',
                content_type="application/json",
                status=500,
            )
        return response

    async def start(self):
        try:
            server_config = self.config["server"]
            read_config_from_api = self.config.get("read_config_from_api", False)
            host = server_config.get("ip", "0.0.0.0")
            port = int(server_config.get("http_port", 8003))

            if port:
                app = web.Application()

                if not read_config_from_api:
                    # 如果没有开启智控台，只是单模块运行，就需要再添加简单OTA接口，用于下发websocket接口
                    app.add_routes(
                        [
                            web.get("/xiaozhi/ota/", self.ota_handler.handle_get),
                            web.post("/xiaozhi/ota/", self.ota_handler.handle_post),
                            web.options(
                                "/xiaozhi/ota/", self.ota_handler.handle_options
                            ),
                            # 下载接口，仅提供 data/bin/*.bin 下载
                            web.get(
                                "/xiaozhi/ota/download/{filename}",
                                self.ota_handler.handle_download,
                            ),
                            web.options(
                                "/xiaozhi/ota/download/{filename}",
                                self.ota_handler.handle_options,
                            ),
                        ]
                    )
                # 添加路由
                app.add_routes(
                    [
                        web.get("/mcp/vision/explain", self.vision_handler.handle_get),
                        web.post(
                            "/mcp/vision/explain", self.vision_handler.handle_post
                        ),
                        web.options(
                            "/mcp/vision/explain", self.vision_handler.handle_options
                        ),
                        # 会议录音转纪要接口（设备上传 WAV → ASR → LLM 纪要）
                        web.get(
                            "/mibao/meeting/transcribe",
                            self.meeting_transcribe_handler.handle_get,
                        ),
                        web.post(
                            "/mibao/meeting/transcribe",
                            self.meeting_transcribe_handler.handle_post,
                        ),
                        web.options(
                            "/mibao/meeting/transcribe",
                            self.meeting_transcribe_handler.handle_options,
                        ),
                        # 设备启动配置拉取（upload_url / ota_url）
                        web.get(
                            "/api/device/config", self._handle_device_config
                        ),
                    ]
                )

                # 运行服务
                runner = web.AppRunner(app)
                await runner.setup()
                site = web.TCPSite(runner, host, port)
                await site.start()

                # 保持服务运行
                while True:
                    await asyncio.sleep(3600)  # 每隔 1 小时检查一次
        except Exception as e:
            self.logger.bind(tag=TAG).error(f"HTTP服务器启动失败: {e}")
            import traceback

            self.logger.bind(tag=TAG).error(f"错误堆栈: {traceback.format_exc()}")
            raise
