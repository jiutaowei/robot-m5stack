/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "mibao_config.h"
#include <mooncake_log.h>
#include <mcp_server.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>
#include <board.h>
#include <cJSON.h>
#include <settings.h>

using namespace stackchan;

static const std::string_view _tag = "HAL-MCP";

namespace {

constexpr int kGreenhouseRequestTimeoutMs         = 5000;
constexpr int kMaxActuatorDurationSeconds         = 3600;

bool isAllowedActuator(const std::string& actuator)
{
    return actuator == "fan" || actuator == "pump" || actuator == "light" || actuator == "heat";
}

bool isAllowedAction(const std::string& action)
{
    return action == "on" || action == "off";
}

const char* actuatorDisplayName(const std::string& actuator)
{
    if (actuator == "fan") return "硅基一号的风扇";
    if (actuator == "pump") return "硅基一号的水泵";
    if (actuator == "light") return "硅基一号的生长灯";
    return "硅基一号的加热垫";
}

}  // namespace

// MCP 工具与物联网触屏 UI 共用的执行器控制入口（声明在 hal.h）。
// URL 来自 NVS 配置 mibao.iot_url（默认 http://10.51.1.205:5000），拼 /actuator_control。
std::string controlGreenhouseActuator(const std::string& actuator, const std::string& action, int durationSeconds)
{
    if (!isAllowedActuator(actuator) || !isAllowedAction(action)) {
        return R"({"success":false,"error":"Unsupported actuator or action"})";
    }

    if (durationSeconds < 0 || durationSeconds > kMaxActuatorDurationSeconds) {
        return fmt::format(R"({{"success":false,"error":"duration_seconds must be between 0 and {}}})",
                           kMaxActuatorDurationSeconds);
    }

    // The actuator service treats duration=0 as continuous operation. A duration only applies to turn-on commands.
    if (action == "off") {
        durationSeconds = 0;
    }

    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "actuator_id", actuator.c_str());
    cJSON_AddStringToObject(request, "action", action.c_str());
    cJSON_AddNumberToObject(request, "duration", durationSeconds);
    char* requestText = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (requestText == nullptr) {
        return R"({"success":false,"error":"Failed to create actuator request"})";
    }

    std::string payload(requestText);
    cJSON_free(requestText);

    // 基础地址可在设置页配置；去掉末尾斜杠后拼固定接口路径
    std::string base_url = mibao::getIotUrl();
    while (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    const std::string actuator_url = base_url + "/actuator_control";

    auto network = Board::GetInstance().GetNetwork();
    auto http    = network ? network->CreateHttp(0) : nullptr;
    if (!http) {
        return R"({"success":false,"error":"Failed to create HTTP client"})";
    }

    http->SetTimeout(kGreenhouseRequestTimeoutMs);
    http->SetHeader("Content-Type", "application/json");
    http->SetContent(std::move(payload));
    if (!http->Open("POST", actuator_url)) {
        mclog::tagError(_tag, "greenhouse request connection failed");
        return R"({"success":false,"error":"Unable to reach greenhouse controller"})";
    }

    const int statusCode = http->GetStatusCode();
    std::string response = http->ReadAll();
    http->Close();

    if (statusCode != 200) {
        mclog::tagError(_tag, "greenhouse request failed: status={}", statusCode);
        return fmt::format(R"({{"success":false,"error":"Greenhouse controller returned HTTP {}"}})", statusCode);
    }

    cJSON* responseJson = cJSON_Parse(response.c_str());
    cJSON* success      = responseJson ? cJSON_GetObjectItemCaseSensitive(responseJson, "success") : nullptr;
    const bool accepted = cJSON_IsTrue(success);
    cJSON_Delete(responseJson);

    mclog::tagInfo(_tag, "greenhouse control: actuator={}, action={}, duration={}s, accepted={}", actuator, action,
                   durationSeconds, accepted);
    return fmt::format(R"({{"success":{},"actuator":"{}","target":"{}","action":"{}","duration_seconds":{}}})",
                       accepted ? "true" : "false", actuator, actuatorDisplayName(actuator), action, durationSeconds);
}

void Hal::xiaozhi_mcp_init()
{
    mclog::tagInfo(_tag, "init");

    // https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage.md
    auto& mcp_server = McpServer::GetInstance();

    // System Prompt：
    // You can control the robot's head. Use get_yaw and get_pitch to sense current position. Use set_yaw for horizontal
    // movement and set_pitch for vertical movement. All angles are in degrees.

    mclog::tagInfo(_tag, "add robot.get_head_angles tool");
    mcp_server.AddTool("self.robot.get_head_angles",
                       "Returns current yaw/pitch in degrees. Neutral position is {yaw:0, pitch:0}.",
                       std::vector<Property>{}, [this](const PropertyList& properties) -> ReturnValue {
                           LvglLockGuard lock;  // StackChan motion update is under the lvgl lock

                           auto& motion      = GetStackChan().motion();
                           int current_yaw   = motion.yawServo().getCurrentAngle() / 10;
                           int current_pitch = motion.pitchServo().getCurrentAngle() / 10;

                           auto result = fmt::format(R"({{"yaw": {}, "pitch": {}}})", current_yaw, current_pitch);
                           mclog::tagInfo(_tag, "get_head_angles: {}", result);
                           return result;
                       });

    mclog::tagInfo(_tag, "add robot.set_head_angles tool");
    mcp_server.AddTool("self.robot.set_head_angles",
                       "Adjust head position. GUIDELINES: "
                       "1. For natural interaction, stay within +/- 45 degrees. "
                       "2. Only use values > 70 if the user explicitly asks to look far away/behind. "
                       "3. Max ranges: Yaw(-128 to 128, -128 as your left), Pitch(0 to 90, 90 as your up). "
                       "Speed(100-1000, 150 is natural).",
                       PropertyList({Property("yaw", kPropertyTypeInteger, -9999, -9999, 128),
                                     Property("pitch", kPropertyTypeInteger, -9999, -9999, 90),
                                     Property("speed", kPropertyTypeInteger, 150, 100, 1000)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int speed = properties["speed"].value<int>();
                           int yaw   = properties["yaw"].value<int>();
                           int pitch = properties["pitch"].value<int>();

                           mclog::tagInfo(_tag, "motion set_angles: yaw: {}, pitch: {}, speed: {}", yaw, pitch, speed);

                           LvglLockGuard lock;

                           auto& motion = GetStackChan().motion();
                           if (pitch != -9999) {
                               motion.pitchServo().moveWithSpeed(pitch * 10, speed);
                           }
                           if (yaw != -9999) {
                               motion.yawServo().moveWithSpeed(yaw * 10, speed);
                           }

                           return true;
                       });

    mclog::tagInfo(_tag, "add robot.set_led_color tool");
    mcp_server.AddTool(
        "self.robot.set_led_color",
        "Set the color of the robot's INTERNAL onboard LED. This is NOT for room lights. "
        "Values: 0-168 (safe range). Red=168,0,0; Green=0,168,0; Blue=0,0,168; White=100,100,100; Off=0,0,0.",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 0, 168),
                      Property("green", kPropertyTypeInteger, 0, 0, 168),
                      Property("blue", kPropertyTypeInteger, 0, 0, 168)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int r = properties["red"].value<int>();
            int g = properties["green"].value<int>();
            int b = properties["blue"].value<int>();

            mclog::tagInfo(_tag, "set_led_color: r={}, g={}, b={}", r, g, b);

            LvglLockGuard lock;

            GetStackChan().leftNeonLight().setColor(r, g, b);
            GetStackChan().rightNeonLight().setColor(r, g, b);

            return true;
        });

    mclog::tagInfo(_tag, "add robot.create_reminder tool");
    mcp_server.AddTool("self.robot.create_reminder",
                       "Create a reminder. Duration is in seconds. Message is what to say when time is up. Set repeat "
                       "to true to repeat the reminder.",
                       PropertyList({Property("duration_seconds", kPropertyTypeInteger, 60, 1, 86400),
                                     Property("message", kPropertyTypeString, std::string("Time's up!")),
                                     Property("repeat", kPropertyTypeBoolean, false)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int duration_seconds = properties["duration_seconds"].value<int>();
                           std::string message  = properties["message"].value<std::string>();
                           bool repeat          = properties["repeat"].value<bool>();

                           // Default message
                           if (message.empty()) {
                               message = "Time's up!";
                           }

                           mclog::tagInfo(_tag, "create_reminder: duration={}s, message={}, repeat={}",
                                          duration_seconds, message, repeat);

                           int id = tools::create_reminder(duration_seconds * 1000, message, repeat);

                           return id;
                       });

    mclog::tagInfo(_tag, "add robot.get_reminders tool");
    mcp_server.AddTool("self.robot.get_reminders", "Get list of active reminders.", std::vector<Property>{},
                       [this](const PropertyList& properties) -> ReturnValue {
                           mclog::tagInfo(_tag, "get_reminders");
                           auto reminders          = tools::get_active_reminders();
                           std::string result_json = "[";
                           for (size_t i = 0; i < reminders.size(); ++i) {
                               const auto& r = reminders[i];
                               result_json +=
                                   fmt::format(R"({{"id": {}, "duration_ms": {}, "message": "{}", "repeat": {}}})",
                                               r.id, r.durationMs, r.message, r.repeat ? "true" : "false");
                               if (i < reminders.size() - 1) {
                                   result_json += ", ";
                               }
                           }
                           result_json += "]";
                           mclog::tagInfo(_tag, "get_reminders result: {}", result_json);
                           return result_json;
                       });

    mclog::tagInfo(_tag, "add robot.stop_reminder tool");
    mcp_server.AddTool("self.robot.stop_reminder", "Stop a reminder by ID.",
                       PropertyList({Property("id", kPropertyTypeInteger, -1)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int id = properties["id"].value<int>();
                           mclog::tagInfo(_tag, "stop_reminder: id={}", id);
                           tools::stop_reminder(id);
                           return true;
                       });

    mclog::tagInfo(_tag, "add greenhouse.control_actuator tool");
    mcp_server.AddTool(
        "greenhouse.control_actuator",
        "Control ONLY Silicon One (硅基一号) greenhouse actuators for Mibao. "
        "Always understand and refer to them as: fan=硅基一号的风扇, pump=硅基一号的水泵, "
        "light=硅基一号的生长灯, heat=硅基一号的加热垫. "
        "Use this only after the user has clearly named a Silicon One actuator and requested on/off. "
        "actuator must be fan, pump, light, or heat; action must be on or off. "
        "duration_seconds is 0 for continuous operation and 1-3600 for a timed ON operation. "
        "Never use this tool for the robot arm, servos, claw, or any other device. "
        "Ask a clarification question when the target or requested duration is ambiguous.",
        PropertyList({Property("actuator", kPropertyTypeString, std::string("")),
                      Property("action", kPropertyTypeString, std::string("")),
                      Property("duration_seconds", kPropertyTypeInteger, 0, 0, kMaxActuatorDurationSeconds)}),
        [](const PropertyList& properties) -> ReturnValue {
            return controlGreenhouseActuator(properties["actuator"].value<std::string>(),
                                             properties["action"].value<std::string>(),
                                             properties["duration_seconds"].value<int>());
        });

    // 米宝：语音打开会议录制。
    // 大模型识别到用户说"打开会议录制/开始会议/我要开会"等意图时调用本工具。
    // 设备会保存目标 app 索引并重启，重启后 launcher 自动打开会议录制 app，
    // 由 app 内部播报"已进入会议录制，是否开启？"并等待语音确认，全程无需触摸。
    // 注意：调用后设备会立即重启，请在回复用户前调用。
    mclog::tagInfo(_tag, "add mibao.open_meeting tool");
    mcp_server.AddTool("self.mibao.open_meeting",
                       "Open the Mibao meeting recording app (会议录制). "
                       "Call this when the user asks to start/open a meeting recording, e.g. "
                       "\"打开会议录制\", \"开始会议\", \"我要开会\", \"记录会议\". "
                       "The device will reboot and open the meeting app, then voice-confirm "
                       "whether to start recording. After calling this tool, reply to the user "
                       "that the meeting recorder is opening.",
                       std::vector<Property>{},
                       [this](const PropertyList& properties) -> ReturnValue {
                           mclog::tagInfo(_tag, "open_meeting: request warm reboot to meeting app");
                           // 标记"语音打开会议"，会议 app 打开时据此启动语音确认
                           {
                               Settings settings("mibao", true);
                               settings.SetInt("voice_meeting", 1);
                           }
                           // launcher 列表索引：Setup=0, Meeting=1（main.cpp 安装顺序去掉 launcher）
                           GetHAL().requestWarmReboot(1);
                           // 延迟重启，让 MCP 调用结果先回传服务器
                           GetHAL().delay(300);
                           GetHAL().reboot();
                           return true;
                       });

    // 米宝：语音打开会议录像。
    // 大模型识别到用户说"打开录像/开始录像/我要录像/录像会议"等意图时调用本工具。
    // 设备重启后 launcher 自动打开会议录像 app 并直接开始录像（调用摄像头）。
    mclog::tagInfo(_tag, "add mibao.open_video tool");
    mcp_server.AddTool("self.mibao.open_video",
                       "Open the Mibao meeting video recording app (会议录像). "
                       "Call this when the user asks to start/open a video recording, e.g. "
                       "\"打开录像\", \"开始录像\", \"我要录像\", \"录像会议\". "
                       "The device will reboot and open the video app, then start recording "
                       "with the camera automatically. After calling this tool, reply to the user "
                       "that the video recorder is opening.",
                       std::vector<Property>{},
                       [this](const PropertyList& properties) -> ReturnValue {
                           mclog::tagInfo(_tag, "open_video: request warm reboot to video app");
                           {
                               Settings settings("mibao", true);
                               settings.SetInt("video_meeting", 1);
                           }
                           // launcher 列表索引：Setup=0, Meeting=1, Video=2（main.cpp 安装顺序去掉 launcher）
                           GetHAL().requestWarmReboot(2);
                           GetHAL().delay(300);
                           GetHAL().reboot();
                           return true;
                       });
}
