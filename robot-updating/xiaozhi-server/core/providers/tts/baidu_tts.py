"""百度翻译 TTS provider（云端、无需 key、国内可达）。

为什么需要它：微软 Edge TTS（晓晓）在部分大陆出口会被 403，本地 Piper 音色一般。
百度翻译网页用的发音接口在国内稳定可达、无需注册/额度，音色比 Piper 更自然，
适合做「在线优先」链里的一级：

    chain: [EdgeTTS, BaiduTTS, PiperTTS]

配置（data/.config.yaml）::

    TTS:
      BaiduTTS:
        type: baidu_tts
        speed: 5          # 对应接口的 spd（1~15，默认 5）
        output_dir: tmp/

说明：这是网页发音接口（非正式开放 API），可能随对方调整而变化；因此它只作为
链路中的一级，失败会自动落到下一级（本地 Piper 保证一定有声音）。
"""

import asyncio
import os
import urllib.parse
import uuid
from datetime import datetime

from config.logger import setup_logging
from core.providers.tts.base import TTSProviderBase

TAG = __name__
logger = setup_logging()

API = "https://fanyi.baidu.com/gettts"


class TTSProvider(TTSProviderBase):
    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        self.audio_file_type = "mp3"
        self.speed = str(config.get("speed", 5) or 5)
        self.lan = config.get("lan", "zh")
        self.timeout = float(config.get("tts_timeout", 15) or 15)

    def generate_filename(self, extension=".mp3"):
        return os.path.join(
            self.output_file,
            f"tts-{datetime.now().date()}@{uuid.uuid4().hex}{extension}",
        )

    def _fetch(self, text: str) -> bytes:
        import httpx

        params = {
            "lan": self.lan,
            "text": text,
            "spd": self.speed,
            "source": "web",
        }
        url = API + "?" + urllib.parse.urlencode(params)
        headers = {
            "User-Agent": (
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36"
            ),
            "Referer": "https://fanyi.baidu.com/",
        }
        with httpx.Client(timeout=self.timeout) as c:
            r = c.get(url, headers=headers)
        r.raise_for_status()
        data = r.content
        # 接口出错时返回的是很小的文本/JSON，别把它当音频
        if len(data) < 1024 or not data[:3] == b"ID3" and data[:2] != b"\xff\xfb" \
                and data[:2] != b"\xff\xf3" and data[:2] != b"\xff\xf2":
            raise Exception(f"返回内容不是音频（{len(data)} 字节）")
        return data

    async def text_to_speak(self, text, output_file):
        try:
            data = await asyncio.to_thread(self._fetch, text)
        except Exception as e:  # noqa: BLE001
            raise Exception(f"百度 TTS 请求失败: {e}")
        if output_file:
            os.makedirs(os.path.dirname(output_file) or ".", exist_ok=True)
            with open(output_file, "wb") as f:
                f.write(data)
            return None
        return data
