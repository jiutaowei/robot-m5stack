import asyncio

import httpx
from bs4 import BeautifulSoup
from config.logger import setup_logging
from plugins_func.register import register_function, ToolType, ActionResponse, Action
from core.utils.util import get_ip_info
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from core.connection import ConnectionHandler

TAG = __name__
logger = setup_logging()

# ===== Open-Meteo：免费、无需 key、无需注册（2026-10-14 接入，替代失效的和风 key）=====
_OPEN_METEO_GEO = "https://geocoding-api.open-meteo.com/v1/search"
_OPEN_METEO_FORECAST = "https://api.open-meteo.com/v1/forecast"

# WMO 天气代码 → 中文（https://open-meteo.com/en/docs）
_WMO_ZH = {
    0: "晴", 1: "晴间多云", 2: "多云", 3: "阴",
    45: "有雾", 48: "雾凇",
    51: "小毛毛雨", 53: "毛毛雨", 55: "大毛毛雨",
    56: "冻毛毛雨", 57: "强冻毛毛雨",
    61: "小雨", 63: "中雨", 65: "大雨",
    66: "冻雨", 67: "强冻雨",
    71: "小雪", 73: "中雪", 75: "大雪", 77: "雪粒",
    80: "阵雨", 81: "强阵雨", 82: "暴雨",
    85: "阵雪", 86: "强阵雪",
    95: "雷阵雨", 96: "雷阵雨伴小冰雹", 99: "雷阵雨伴大冰雹",
}


def _wmo_zh(code) -> str:
    try:
        return _WMO_ZH.get(int(code), f"未知天气({code})")
    except (TypeError, ValueError):
        return "未知天气"


def _geocode_variants(location: str):
    """中文地名补「市」再查：实测查「桂林」只返回同名村庄（城市不在前 8 个结果里），
    查「桂林市」才拿到广西桂林市（人口 157 万，PPLA2）。"""
    variants = [location]
    if location and location[-1] not in "市县区州盟":
        variants.append(location + "市")
    return variants


def _geocode_score(rec: dict):
    """给候选地点打分：行政级别越高越优先，同级比人口。

    注意 PPLA4 是村级（安徽也有个叫「桂林」的 PPLA4 村，人口为空），
    不能只按 PPLA 前缀就当成城市。
    """
    code = rec.get("feature_code") or ""
    if code in ("PPLA", "PPLA2"):
        rank = 4
    elif code == "PPLA3":
        rank = 3
    elif code.startswith("PPL"):
        rank = 2
    else:
        rank = 1
    return (rank, rec.get("population") or 0)


async def fetch_open_meteo(location: str):
    """按地名查天气：先地理编码拿经纬度，再取实况 + 7 天预报。

    实测坑：
    1) 同名地点多，且不带「市」时城市可能不在结果里 → 按「市级优先 > 人口多」打分，
       必要时补「市」再查一次；
    2) Open-Meteo 偶发连接超时（实测南宁一次 ConnectTimeout）→ 每个请求重试 2 次。
    """
    async def _get(client, url, params):
        last = None
        for attempt in range(2):
            try:
                r = await client.get(url, params=params)
                r.raise_for_status()
                return r
            except Exception as e:  # noqa: BLE001
                last = e
                if attempt == 0:
                    await asyncio.sleep(0.6)
        raise last

    async with httpx.AsyncClient(timeout=httpx.Timeout(8.0, connect=4.0)) as client:
        hit = None
        for variant in _geocode_variants(location):
            geo = await _get(
                client,
                _OPEN_METEO_GEO,
                {"name": variant, "count": 8, "language": "zh", "format": "json"},
            )
            for rec in (geo.json() or {}).get("results") or []:
                if hit is None or _geocode_score(rec) > _geocode_score(hit):
                    hit = rec
        if not hit:
            return None

        name = hit.get("name") or location
        admin = hit.get("admin1") or ""
        if admin and name and admin != name:
            if name in admin:
                city = admin
            elif admin in name:
                city = name
            else:
                city = f"{admin}{name}"
        else:
            city = name or location

        fc = await _get(
            client,
            _OPEN_METEO_FORECAST,
            {
                "latitude": hit["latitude"],
                "longitude": hit["longitude"],
                "current": "temperature_2m,relative_humidity_2m,apparent_temperature,"
                           "weather_code,wind_speed_10m,precipitation",
                "daily": "weather_code,temperature_2m_max,temperature_2m_min,"
                         "precipitation_probability_max",
                "timezone": "Asia/Shanghai",
                "forecast_days": 7,
                "wind_speed_unit": "kmh",
            },
        )
        return {"city": city, "data": fc.json()}


def format_open_meteo(info: dict, lang: str = "zh_CN") -> str:
    """把 Open-Meteo 的 JSON 拼成一段适合播报的中文天气报告。"""
    d = info.get("data") or {}
    cur = d.get("current") or {}
    daily = d.get("daily") or {}

    report = f"您查询的位置是：{info.get('city', '')}\n\n"
    if cur:
        report += (
            f"当前天气：{_wmo_zh(cur.get('weather_code'))}，"
            f"气温 {cur.get('temperature_2m')}℃"
            f"（体感 {cur.get('apparent_temperature')}℃），"
            f"湿度 {cur.get('relative_humidity_2m')}%，"
            f"风速 {cur.get('wind_speed_10m')} 公里/小时\n"
        )

    dates = daily.get("time") or []
    codes = daily.get("weather_code") or []
    highs = daily.get("temperature_2m_max") or []
    lows = daily.get("temperature_2m_min") or []
    rains = daily.get("precipitation_probability_max") or []
    if dates:
        report += "\n未来几天预报：\n"
        for i, day in enumerate(dates):
            desc = _wmo_zh(codes[i] if i < len(codes) else None)
            hi = highs[i] if i < len(highs) else "-"
            lo = lows[i] if i < len(lows) else "-"
            rain = rains[i] if i < len(rains) else None
            rain_txt = f"，降水概率 {rain}%" if rain is not None else ""
            report += f"{day}：{desc}，气温 {lo}~{hi}℃{rain_txt}\n"

    report += "\n（如需某一天的具体天气，请告诉我日期）"
    return report


GET_WEATHER_FUNCTION_DESC = {
    "type": "function",
    "function": {
        "name": "get_weather",
        "description": (
            "查询某个地点的天气（含实况与未来几天预报）。用户应提供一个位置，"
            "比如用户说杭州天气，参数为：杭州。如果用户说的是省份，默认用省会城市；"
            "如果用户说的不是省份或城市而是一个地名，默认用该地所在省份的省会城市。"
            "注意：只有「设备所在地」的天气已在上下文中提供；用户问的是其他城市时，"
            "必须调用本工具查询，不要凭空回答「查不到天气」。"
        ),
        "parameters": {
            "type": "object",
            "properties": {
                "location": {
                    "type": "string",
                    "description": "地点名，例如杭州。可选参数，如果不提供则不传",
                },
                "lang": {
                    "type": "string",
                    "description": "返回用户使用的语言code，例如zh_CN/zh_HK/en_US/ja_JP等，默认zh_CN",
                },
            },
            "required": ["lang"],
        },
    },
}

HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/92.0.4515.107 Safari/537.36"
    )
}

# 天气代码 https://dev.qweather.com/docs/resource/icons/#weather-icons
WEATHER_CODE_MAP = {
    "100": "晴",
    "101": "多云",
    "102": "少云",
    "103": "晴间多云",
    "104": "阴",
    "150": "晴",
    "151": "多云",
    "152": "少云",
    "153": "晴间多云",
    "300": "阵雨",
    "301": "强阵雨",
    "302": "雷阵雨",
    "303": "强雷阵雨",
    "304": "雷阵雨伴有冰雹",
    "305": "小雨",
    "306": "中雨",
    "307": "大雨",
    "308": "极端降雨",
    "309": "毛毛雨/细雨",
    "310": "暴雨",
    "311": "大暴雨",
    "312": "特大暴雨",
    "313": "冻雨",
    "314": "小到中雨",
    "315": "中到大雨",
    "316": "大到暴雨",
    "317": "暴雨到大暴雨",
    "318": "大暴雨到特大暴雨",
    "350": "阵雨",
    "351": "强阵雨",
    "399": "雨",
    "400": "小雪",
    "401": "中雪",
    "402": "大雪",
    "403": "暴雪",
    "404": "雨夹雪",
    "405": "雨雪天气",
    "406": "阵雨夹雪",
    "407": "阵雪",
    "408": "小到中雪",
    "409": "中到大雪",
    "410": "大到暴雪",
    "456": "阵雨夹雪",
    "457": "阵雪",
    "499": "雪",
    "500": "薄雾",
    "501": "雾",
    "502": "霾",
    "503": "扬沙",
    "504": "浮尘",
    "507": "沙尘暴",
    "508": "强沙尘暴",
    "509": "浓雾",
    "510": "强浓雾",
    "511": "中度霾",
    "512": "重度霾",
    "513": "严重霾",
    "514": "大雾",
    "515": "特强浓雾",
    "900": "热",
    "901": "冷",
    "999": "未知",
}


async def fetch_city_info(location, api_key, api_host):
    url = f"https://{api_host}/geo/v2/city/lookup?key={api_key}&location={location}&lang=zh"
    async with httpx.AsyncClient(timeout=httpx.Timeout(5.0, connect=3.0)) as client:
        response = await client.get(url, headers=HEADERS)
    data = response.json()
    if data.get("error") is not None:
        logger.bind(tag=TAG).error(
            f"获取天气失败，原因：{data.get('error', {}).get('detail')}"
        )
        return None
    return data.get("location", [])[0] if data.get("location") else None


async def fetch_weather_page(url):
    async with httpx.AsyncClient(timeout=httpx.Timeout(10.0, connect=3.0)) as client:
        response = await client.get(url, headers=HEADERS)
    return BeautifulSoup(response.text, "html.parser") if response.status_code == 200 else None


def parse_weather_info(soup):
    city_name = soup.select_one("h1.c-submenu__location").get_text(strip=True)

    current_abstract = soup.select_one(".c-city-weather-current .current-abstract")
    current_abstract = (
        current_abstract.get_text(strip=True) if current_abstract else "未知"
    )

    current_basic = {}
    for item in soup.select(
        ".c-city-weather-current .current-basic .current-basic___item"
    ):
        parts = item.get_text(strip=True, separator=" ").split(" ")
        if len(parts) == 2:
            key, value = parts[1], parts[0]
            current_basic[key] = value

    temps_list = []
    for row in soup.select(".city-forecast-tabs__row")[:7]:  # 取前7天的数据
        date = row.select_one(".date-bg .date").get_text(strip=True)
        weather_code = (
            row.select_one(".date-bg .icon")["src"].split("/")[-1].split(".")[0]
        )
        weather = WEATHER_CODE_MAP.get(weather_code, "未知")
        temps = [span.get_text(strip=True) for span in row.select(".tmp-cont .temp")]
        high_temp, low_temp = (temps[0], temps[-1]) if len(temps) >= 2 else (None, None)
        temps_list.append((date, weather, high_temp, low_temp))

    return city_name, current_abstract, current_basic, temps_list


@register_function("get_weather", GET_WEATHER_FUNCTION_DESC, ToolType.SYSTEM_CTL)
async def get_weather(conn: "ConnectionHandler", location: str = None, lang: str = "zh_CN"):
    from core.utils.cache.manager import cache_manager, CacheType

    weather_config = conn.config.get("plugins", {}).get("get_weather", {})
    api_host = weather_config.get("api_host", "mj7p3y7naa.re.qweatherapi.com")
    api_key = weather_config.get("api_key", "a861d0d5e7bf4ee1a83d9a9e4f96d4da")
    default_location = weather_config.get("default_location", "广州")
    client_ip = conn.client_ip

    # 优先使用用户提供的location参数
    if not location:
        # 通过客户端IP解析城市
        if client_ip:
            # 先从缓存获取IP对应的城市信息
            cached_ip_info = cache_manager.get(CacheType.IP_INFO, client_ip)
            if cached_ip_info:
                location = cached_ip_info.get("city")
            else:
                # 缓存未命中，调用API获取
                ip_info = get_ip_info(client_ip, logger)
                if ip_info:
                    cache_manager.set(CacheType.IP_INFO, client_ip, ip_info)
                    location = ip_info.get("city")

            if not location:
                location = default_location
        else:
            # 若无IP，使用默认位置
            location = default_location
    # 尝试从缓存获取完整天气报告
    weather_cache_key = f"full_weather_{location}_{lang}"
    cached_weather_report = cache_manager.get(CacheType.WEATHER, weather_cache_key)
    if cached_weather_report:
        return ActionResponse(Action.REQLLM, cached_weather_report, None)

    # 缓存未命中，获取实时天气数据
    # 2026-10-14：改用 Open-Meteo（免费、无需 key、国内外均可直连）。
    # 原实现依赖和风天气：代码里写死的示例 key 早已失效，报
    # 「Authentication failed, check your KEY/Token or Host」，用户侧表现就是
    # 「问天气查不出来」；而且它还要抓取网页 HTML，网站一改版就废。
    try:
        info = await fetch_open_meteo(location)
    except Exception as e:  # noqa: BLE001
        logger.bind(tag=TAG).error(f"Open-Meteo 查询失败: {type(e).__name__}: {e}")
        info = None
    if not info:
        return ActionResponse(
            Action.REQLLM, f"未找到相关的城市: {location}，请确认地点是否正确", None
        )
    weather_report = format_open_meteo(info, lang)

    # 缓存完整的天气报告
    cache_manager.set(CacheType.WEATHER, weather_cache_key, weather_report)

    return ActionResponse(Action.REQLLM, weather_report, None)
