import json
import os
import time
import wave
from aiohttp import web
from config.logger import setup_logging
from core.api.base_handler import BaseHandler
from core.utils.asr import create_instance as create_asr
from core.utils.llm import create_instance as create_llm
from core.providers.asr.base import ASRProviderBase
from core.utils.util import get_vision_url

TAG = __name__

# 最大录音文件 200MB（会议录音可能很长）
MAX_FILE_SIZE = 200 * 1024 * 1024

MEETING_NOTES_SYSTEM_PROMPT = (
    "你是会议纪要整理助手。用户会给你一段会议录音转写出的文字，"
    "请把它整理成结构清晰的中文会议纪要，包含：\n"
    "1. 会议主题（根据内容概括）\n"
    "2. 讨论要点（分条列出）\n"
    "3. 结论/决议\n"
    "4. 待办事项（如有）\n"
    "保持简洁，用条目形式输出，不要遗漏重要信息。"
)


class MeetingTranscribeHandler(BaseHandler):
    """会议录音转纪要接口：接收设备上传的 WAV，ASR 转文字 + LLM 生成纪要"""

    def __init__(self, config: dict):
        super().__init__(config)
        self._asr = None
        self._llm = None
        self._config = config

    def _get_asr(self):
        """懒加载 ASR 实例（复用与对话相同的 Qwen3ASRFlash）"""
        if self._asr is None:
            select_asr = self._config["selected_module"]["ASR"]
            asr_type = (
                select_asr
                if "type" not in self._config["ASR"][select_asr]
                else self._config["ASR"][select_asr]["type"]
            )
            delete_audio = str(self._config.get("delete_audio", True)).lower() in (
                "true",
                "1",
                "yes",
            )
            self._asr = create_asr(asr_type, self._config["ASR"][select_asr], delete_audio)
        return self._asr

    def _get_llm(self):
        """懒加载 LLM 实例（复用与对话相同的 HezorLLM）"""
        if self._llm is None:
            select_llm = self._config["selected_module"]["LLM"]
            llm_type = (
                select_llm
                if "type" not in self._config["LLM"][select_llm]
                else self._config["LLM"][select_llm]["type"]
            )
            self._llm = create_llm(llm_type, self._config["LLM"][select_llm])
        return self._llm

    def _create_error_response(self, message: str) -> dict:
        return {"success": False, "message": message}

    async def handle_post(self, request):
        """处理录音转纪要 POST 请求（multipart/form-data）"""
        response = None
        temp_wav = None
        try:
            # 解析 multipart 请求：type / device_id / file
            # 注意：aiohttp MultipartReader 是流式的，每个 part 必须立即消费，
            # 不能先存引用后面再读（未消费的 part 在 next() 时会被丢弃）。
            reader = await request.multipart()

            fields = {}
            rec_type = "meeting"
            device_id = ""
            filename = None
            upload_dir = "tmp"
            os.makedirs(upload_dir, exist_ok=True)
            temp_wav = os.path.join(
                upload_dir, f"meeting_{int(time.time()*1000)}.wav"
            )
            size = 0
            file_written = False

            while True:
                part = await reader.next()
                if part is None:
                    break
                name = part.name
                if name == "file":
                    # 立即流式读取文件内容
                    filename = part.filename
                    with open(temp_wav, "wb") as f:
                        while True:
                            chunk = await part.read_chunk(64 * 1024)
                            if not chunk:
                                break
                            size += len(chunk)
                            if size > MAX_FILE_SIZE:
                                raise ValueError(
                                    f"录音文件超过大小限制（{MAX_FILE_SIZE//1024//1024}MB）"
                                )
                            f.write(chunk)
                    file_written = True
                else:
                    fields[name] = (await part.read()).decode("utf-8", "ignore")

            rec_type = fields.get("type", rec_type)
            device_id = fields.get("device_id", device_id)
            if filename is None:
                filename = f"rec_{int(time.time())}.wav"

            if not file_written:
                raise ValueError("缺少 file 字段")

            if size == 0:
                raise ValueError("录音文件为空")

            self.logger.bind(tag=TAG).info(
                f"收到录音转写请求: type={rec_type} device={device_id} file={filename} size={size}"
            )

            # ---- 0. 视频(.avi) → 提取音轨转 16k mono wav ----
            # 临时文件初始按 .wav 命名；若是 .avi，用 ffmpeg 抽出音轨覆盖写回。
            if filename.lower().endswith(".avi"):
                if not os.path.exists(temp_wav):
                    raise ValueError("视频文件为空")
                import subprocess

                wav_tmp = temp_wav + ".extract.wav"
                ret = subprocess.run(
                    [
                        "ffmpeg", "-y", "-i", temp_wav,
                        "-vn", "-ac", "1", "-ar", "16000", "-f", "wav", wav_tmp,
                    ],
                    capture_output=True,
                )
                if ret.returncode != 0 or not os.path.exists(wav_tmp):
                    raise ValueError("视频音轨提取失败（可能没有音轨）")
                os.replace(wav_tmp, temp_wav)

            # ---- 1. ASR 转文字 ----
            asr = self._get_asr()
            # Qwen3ASRFlash 使用 artifacts.temp_path（见 speech_to_text 实现），
            # 把上传的 WAV 路径放进 temp_path 即可直接转写。
            artifacts = ASRProviderBase.AudioArtifacts(
                pcm_frames=[],
                pcm_bytes=b"",
                file_path=temp_wav,
                temp_path=temp_wav,
            )
            text, _ = await asr.speech_to_text([], f"meeting_{device_id}", artifacts)
            if not text:
                raise ValueError("语音识别失败或录音中没有语音")

            self.logger.bind(tag=TAG).info(f"转写完成: {text[:100]}...")

            # ---- 2. LLM 生成纪要 ----
            llm = self._get_llm()
            notes = llm.response_no_stream(
                MEETING_NOTES_SYSTEM_PROMPT, text, max_tokens=1500
            )

            return_json = {
                "success": True,
                "type": rec_type,
                "device_id": device_id,
                "transcript": text,
                "notes": notes,
            }
            response = web.Response(
                text=json.dumps(return_json, ensure_ascii=False, separators=(",", ":")),
                content_type="application/json",
            )
        except ValueError as e:
            self.logger.bind(tag=TAG).error(f"录音转写请求异常: {e}")
            response = web.Response(
                text=json.dumps(self._create_error_response(str(e)), ensure_ascii=False),
                content_type="application/json",
                status=400,
            )
        except Exception as e:
            self.logger.bind(tag=TAG).error(f"录音转写请求异常: {e}")
            response = web.Response(
                text=json.dumps(self._create_error_response("处理请求时发生错误"), ensure_ascii=False),
                content_type="application/json",
                status=500,
            )
        finally:
            # 清理临时 WAV
            if temp_wav and os.path.exists(temp_wav):
                try:
                    os.remove(temp_wav)
                except Exception:
                    pass
            if response:
                self._add_cors_headers(response)
            return response

    async def handle_get(self, request):
        """健康检查"""
        try:
            server_config = self._config["server"]
            vision_url = get_vision_url(self._config)
            base = vision_url.replace("/mcp/vision/explain", "") if vision_url else ""
            if not base:
                from core.utils.util import get_local_ip

                base = f"http://{get_local_ip()}:{server_config.get('http_port', 8003)}"
            message = (
                "米宝会议纪要转写接口运行正常。"
                f"接口地址：{base}/mibao/meeting/transcribe"
            )
            response = web.Response(text=message, content_type="text/plain")
        except Exception as e:
            self.logger.bind(tag=TAG).error(f"GET请求异常: {e}")
            response = web.Response(
                text="服务器内部错误", content_type="text/plain", status=500
            )
        self._add_cors_headers(response)
        return response
