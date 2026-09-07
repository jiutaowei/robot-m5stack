# 米宝一号 UI 设计指导文档

> 目的：系统性规避 **文字截断 / 文字与图标重叠 / 字体缺字** 三大问题，避免逐个测试反馈
> 适用：所有米宝 app 的 UI 开发
> 维护：每次新增界面需对照本规范自检

---

## 1. 核心铁律（不可违反）

### 1.1 字体子集铁律

**所有中文字符必须在 `assets/fonts/symbols.txt` 中显式列出**（半角逗号或空格分隔的字符串），否则该字在 LVGL 9 下渲染为透明/占位符，表现为「文字截断」。

| 错误现象 | 真实原因 |
|---|---|
| "已删除" 只显示 "已" | 缺少「删」「除」 |
| "确定" 只显示 "确" | 缺少「定」 |
| "重命名" 只显示 "重" "名" | 缺少「命」 |
| "文件管理" 只显示 "文件" | 缺少「管」「理」 |

**自检流程**：
1. 写新文案 → 把每个中文字单独列出来
2. 与 `symbols.txt` 对比，缺的字补进去
3. 运行 `lv_font_conv` 重新生成 `mibao_zh_font.c` 和 `mibao_zh_font_16.c`
4. 全量重建（`idf.py reconfigure && idf.py build`）

### 1.2 按钮/标签尺寸铁律

**320×240 屏的 UI 元素必须留出最小内边距**：

| 元素 | 最小尺寸 | 推荐尺寸 | 备注 |
|---|---|---|---|
| 中文按钮（3 字） | 88×32 | 96×36 | 创建按钮时显式 set_size |
| 中文按钮（4 字） | 96×32 | 110×40 | "重命名" 这类 |
| 标题条 | 320×28 | 320×32 | 顶到屏幕边 |
| Toast 弹窗宽 | 264 | 280 | 用 264 留左右 6px 边距 |
| Toast 文本宽 | 248 | 256 | 弹窗内边距 16 |

### 1.3 文本与图标不重叠铁律

**图标和下方文字必须留 ≥ 8px 间距**，且图标实际渲染尺寸要小于容器尺寸：

```cpp
// ❌ 错误：图标 150px + 文字 0px 间距
img.setSize(150, 150);
label.setY(0);  // 与图标重叠

// ✅ 正确：图标 110px + 文字 12px 间距
img.setSize(110, 110);
img.setScale(190);  // 视觉 ≈ 74% 缩放
label.setY(img.getBottom() + 12);
```

---

## 2. UI 元素布局规范

### 2.1 屏幕分区（320×240 屏）

```
┌──────────────────────────────────┐ y=0
│         状态栏 / 标题条            │ 高 28
├──────────────────────────────────┤ y=28
│                                  │
│           内容区域                 │ 高 168
│       (上下 24px 安全边距)         │
│                                  │
├──────────────────────────────────┤ y=196
│           底部按钮行               │ 高 36
├──────────────────────────────────┤ y=232
│       Home Indicator（8px）        │
└──────────────────────────────────┘ y=240
```

### 2.2 中文字号对照

| 用途 | 字号 | 字体 |
|---|---|---|
| 标题 | 20-26px | `mibao_zh_font` |
| 正文 | 16-18px | `mibao_zh_font_16` |
| 按钮文字 | 16-18px | `mibao_zh_font_16` |
| 列表项 | 16px | `mibao_zh_font_16` |

**永远不要**用小于 14px 的中文字（小尺寸中文模糊不可读）。

### 2.3 文件名显示规范

**长文件名处理三选一**（按场景选）：

| 场景 | 方案 | 实现 |
|---|---|---|
| 列表项 | 缩字号到 14px + 截断超长（>16 字符加 "..."） | `lv_label_set_text` 前预处理 |
| 详情显示 | 单行省略 + tooltip | `lv_label_set_long_mode(LABEL_LONG_DOT)` |
| 卡片标题 | 缩字号到 14px + 卡片高度留 36px | setSize 后 label 居中 |

**禁止**直接展示完整原始文件名（meeting_20260807_130000.wav 有 28 字符，320px 屏放不下）。

---

## 3. 命名规范（避免 UI 文字过长）

### 3.1 按钮文案

| 不推荐 | 推荐 | 字数 |
|---|---|---|
| 「开始录音」 | 「开始」 | 2 |
| 「结束录音」 | 「结束」 | 2 |
| 「点击删除此项」 | 「删除」 | 2 |
| 「点击进行重命名操作」 | 「重命名」 | 3 |
| 「播放该文件」 | 「播放」 | 2 |
| 「确认提交修改」 | 「确定」 | 2 |

### 3.2 弹窗文案

| 不推荐 | 推荐 |
|---|---|
| 「文件已经成功删除，请继续操作」 | 「已删除」 |
| 「文件已成功修改名称」 | 「已修改」 |
| 「网络连接失败，请检查 Wi-Fi 凭证后重试」 | 「连接失败」 |

### 3.3 文件名前缀

为了避免前缀吃显示空间，会议/个人录音文件名在 UI 中只显示**类型 + 时间**：

| 原始路径 | UI 显示 |
|---|---|
| `/sdcard/meetings/meeting_20260807_130000.wav` | `[会议] 08-07 13:00` |
| `/sdcard/personal/personal_20260807_130000.wav` | `[个人] 08-07 13:00` |

---

## 4. Toast 弹窗规范

### 4.1 文本宽度计算

toast 默认宽度 264px，文字宽 248px。中文字符 16px 字号下每字占 16px，**所以 toast 文案不超过 14 个汉字**。

### 4.2 弹窗对齐

```cpp
// 文字太长时，setLongMode 自动滚动
_msg_label->setLongMode(LABEL_LONG_SCROLL_CIRCULAR);
_msg_label->setWidth(240);
```

### 4.3 颜色映射

| 类型 | 背景 | 边框 | 文字 |
|---|---|---|---|
| Info | #E6F1FE | #AFD1F9 | #005BC4 |
| Success | #E8FAF0 | #A2E8C1 | #12A150 |
| Warning | #FEFCE8 | #DBD38B | #C4841D |
| Error | #FEE7EF | #FCB5CD | #C20E4D |

---

## 5. Home Indicator 兼容性

### 5.1 全屏界面必须避让

任何全屏 lv_obj 都要让出底部 8px 给 HomeIndicator：

```cpp
lv_obj_set_size(_root, 320, 232);  // 不是 240
// 或
lv_obj_align(_root, LV_ALIGN_TOP_MID, 0, 0);
lv_obj_set_height(_root, 232);
```

**禁止**全屏对象覆盖到 y=240，会导致 HomeIndicator 点击无响应，看起来"卡住"。

### 5.2 弹窗位置

弹窗水平居中后，检查是否遮挡 HomeIndicator：

```cpp
lv_obj_center(_dialog);
// 如果弹窗高度 > 200，需要上移
if (lv_obj_get_height(_dialog) > 200) {
    lv_obj_align(_dialog, LV_ALIGN_CENTER, 0, -10);
}
```

---

## 6. WiFi/网络配置规范

### 6.1 频段显示

SoftAP 配网页面必须显示每个网络的频段：

```html
<span class="ssid">${ssid}</span>
<span class="band">${band === 1 ? '2.4G' : '5G'}</span>
```

### 6.2 失败提示

配网失败时显示具体原因，不要只显示"连接失败"：

| 真实原因 | UI 提示 |
|---|---|
| 5GHz 不支持 | 「请选择 2.4G 网络」 |
| 密码错误 | 「密码错误，请重新输入」 |
| 信号弱 | 「信号弱，请靠近路由器」 |

---

## 7. 自检清单（每次改 UI 必跑）

```markdown
- [ ] 新文案所有中文字都在 symbols.txt？
- [ ] 按钮尺寸 ≥ 88×32？
- [ ] 列表项高度 ≥ 28px（容纳 16px 文字 + 上下间距）？
- [ ] 卡片标题与下方内容间距 ≥ 12px？
- [ ] 全屏界面让出底部 8px 给 HomeIndicator？
- [ ] 弹窗不遮挡 HomeIndicator？
- [ ] 长文件名（>16 字符）有省略处理？
- [ ] 中文按钮文字 < 4 字？
- [ ] 编译通过 + 烧录真机验证？
```

---

## 8. 字体子集维护流程

### 8.1 新增文案 → 补字流程

1. 编辑 `main/assets/fonts/symbols.txt`，在末尾追加缺字
2. 执行重新生成命令：
   ```bash
   cd /Users/cobain/Documents/[0]project-JHTech/[2]硬件产品规划/[2]产品规划/[15]Stackstan/robot-updating/fw
   lv_font_conv --no-compress --no-prefilter --force-fast-kern-format \
     --font managed_components/78__xiaozhi-fonts/ttf/puhui-common.ttf \
     --format lvgl --lv-include lvgl.h --bpp 4 --size 16 \
     --range 0x20-0x7e --symbols "$(cat main/assets/fonts/symbols.txt | tr -d '\n')" \
     --output main/assets/fonts/mibao_zh_font_16.c
   
   lv_font_conv ... --size 26 --output main/assets/fonts/mibao_zh_font.c
   ```
3. 在镜像目录编译：`cd /Users/cobain/robot-build/fw && idf.py reconfigure && idf.py build`
4. 真机验证

### 8.2 字符总数预算

| 字体 | 当前字符数 | 预算上限 |
|---|---|---|
| 16px | ~150 | 400（占 ~150KB flash） |
| 26px | ~150 | 300（占 ~300KB flash） |

子集超过 400 字时考虑改用全字符字体（~3MB）或动态加载（复杂度高，不推荐）。

---

## 9. 已知缺陷与后续改进

| 缺陷 | 临时方案 | 永久方案 |
|---|---|---|
| 5GHz WiFi 配网失败 | wifi_station::Start 强制 2.4G band | 替换 wifi-connect 组件支持双频 |
| 文件名长换行显示不全 | 缩字号 + 加省略号 | 缩略图预览 + 详情页 |
| HomeIndicator 偶尔失效 | 弹窗手动上移 | 改用 mooncake 标准的 popup 层 |
| 字体子集手工维护 | 跑脚本扫所有字符串 | 编译期自动 + 编译失败提示 |
| 播放闪退（task 栈溢出） | task 栈 16KB → 32KB + 短持锁 + vTaskDelay | 用现成 audio player 组件 |
| Hotspot home 闪退 | worker 析构同步停 AP，去掉 warm-reboot | 拆 AP 生命周期与 app 退出解耦 |

---

## 10. 补充：2026-08-07 真机反馈的问题与修复记录

### 10.1 文件管理 app 反馈与修复

| # | 用户反馈 | 根因 | 修复 |
|---|---|---|---|
| 1 | 标题"文件管理"显示"文件理" | symbols.txt 缺"管"字 | 扩展 symbols.txt 至 1025 个中文字符，重新生成 mibao_zh_font_16.c（903KB） |
| 2 | 文件名长换行显示不全 | 没有长文本处理 | `lv_label_set_long_mode(LV_LABEL_LONG_DOT)` + 列表行高从 22 调到 28 |
| 3 | 底部按钮无文字 | "播放"缺"播"、"重命名"缺"命"、"确定"缺"定"、"删除"缺"除" | 同 #1，扩展字体子集 |
| 4 | 弹窗只显示"已"而非"已删除" | 缺"删"、"除" | 同 #1 |
| 5 | 录音卡片文件名与"录音"重叠 | 卡片高度 54px 太矮 | 改为 70px，标题/时长/文件名分行布局 |
| 6 | 播放闪退 | task 栈 16KB 不足 + SdCardAccessGuard 跨 codec 操作 | 栈 32KB、每次 fread 短持锁、循环内 vTaskDelay(1) 让出 CPU、循环外 vTaskDelay(200) |

### 10.2 字体子集自检脚本

```python
#!/usr/bin/env python3
# scripts/scan_chinese_chars.py
# 扫描 main/ 下所有 C++ 字符串字面值里的中文字符，
# 对照 symbols.txt 输出缺失列表。
import os, re
str_pattern = re.compile(r'"([^"\\]|\\.)*"')
chars = set()
for root, dirs, files in os.walk('main'):
    dirs[:] = [d for d in dirs if d not in ('build',)]
    for fn in files:
        if not fn.endswith(('.cpp', '.h', '.c', '.cc', '.hpp')):
            continue
        with open(os.path.join(root, fn), encoding='utf-8', errors='ignore') as f:
            for line in f:
                if line.strip().startswith(('//', '*', '/*')):
                    continue
                for m in str_pattern.findall(line):
                    for ch in m:
                        if '\u4e00' <= ch <= '\u9fff':
                            chars.add(ch)
with open('main/assets/fonts/symbols.txt') as f:
    syms = set(f.read())
missing = chars - syms
print(f'缺失 {len(missing)} 字: {"".join(sorted(missing))}')
```

### 10.3 播放 task 标准模式（更新到规范）

```cpp
// 1) 栈 ≥ 32KB
// 2) WAV 头解析放主线程，避免 task 启动后才发现文件无效
// 3) SdCardAccessGuard 绝不可跨越 codec->OutputData 阻塞调用
// 4) 每次 fread 单独持锁，codec 操作在锁外
// 5) 循环内 vTaskDelay(1) 让出 CPU
// 6) 循环结束 vTaskDelay(200) 等 codec DMA 排空

struct PlayTaskArg {
    std::string path;
    uint64_t data_offset;
    uint64_t data_bytes;
    App* self;
};
xTaskCreate(play_task, "play", 32768, task_arg, 4, nullptr);
```

### 10.4 WiFi 2.4G/5G 同 SSID 处理

- 配网页面显示 `2.4G` 频段标签 + `ch N` 信道
- wifi_station::Start 强制 `esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY)`
- HandleScanResult 过滤 `primary >= 36` 的 5G BSSID（双重保险）
- ESP32-S3 不支持 5G，但配网页面统一标注 2.4G 便于用户理解

### 10.5 Home Indicator 与 Worker 退出

`worker` 析构必须清理自己创建的全局资源（WiFi AP、音频 codec 等），
否则 home → onClose → worker.reset() 会导致资源未释放，引起"闪退"感。
worker 析构顺序：先停业务（AP/音频），再 destroy_ui()。

---

*本文档维护人：开发团队；变更需在 IM 群通告；季度评审是否需扩展。*
