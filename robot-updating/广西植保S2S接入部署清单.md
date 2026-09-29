# 广西植保 S2S 工具 · 接入与部署清单

> 目标：把 hezor「广西省级植保」数字员工接进米宝的 AI 对话（**方案 A：作为服务端知识工具**），
> 在跑 `xiaozhi-server` 的那台机器（局域网内 `192.168.1.225`）上生效。
>
> 本次改动**只影响服务端插件**，不改固件、不改协议、不影响现有对话与语音控制。
> 未配置私钥时工具自动降级，服务器照常运行。
>
> 涉及文件：`robot-updating/xiaozhi-server/` 下 4 个文件 + 1 个新脚本（见 §1）

---

## 0. 时间与风险

| 项 | 说明 |
|---|---|
| 预计耗时 | 15–30 分钟（含单测），私钥/白名单问题可能追加 |
| 停机窗口 | 仅重启 `xiaozhi-server` 一次（约 10–20 秒），期间机器人无法对话 |
| 风险 | 低。配置错误最多导致该工具不可用；把函数名从列表里去掉即可回滚 |
| 不需要动 | 固件、设备端、树莓派「硅基一号」、`mibao-server` |

---

## 1. 本次要同步的文件

| 文件 | 类型 | 说明 |
|---|---|---|
| `xiaozhi-server/plugins_func/functions/ask_guangxi_phyto.py` | 🆕 新增 | 插件本体：Ed25519 签 `X-META-INFO` → `POST /api/v1/widget/chat` → SSE 聚合 |
| `xiaozhi-server/scripts/test_hezor_widget.py` | 🆕 新增 | 自检/联调脚本（`--check-config` / `--fingerprint` / `--self-test` / 直接提问） |
| `xiaozhi-server/requirements.txt`（及 `requirements-mibao.txt`） | ✏️ 修改 | 新增 `cryptography>=42.0.0`（PyJWT 做 EdDSA 必需） |
| `xiaozhi-server/.config.yaml.template` | ✏️ 修改 | 插件配置块示例（不含密钥） |
| `xiaozhi-server/data/.config.yaml` | ✏️ **本地手改，不入库** | 服务器上的真实配置：插件块 + 启用函数列表 |

> 安全红线：`data/.config.yaml`、`.keys/private_key.pem`、私钥密码、`hzr_` API Key
> **一律不得提交、不得贴进文档/日志/群聊**。`*.pem`、`**/.keys/`、`**/data/.config.yaml` 已在 `.gitignore` 中。

---

## 1.5 官方接口要点（据《S2S 连接文档》核对，2026-09-29）

| 项 | 官方要求 | 我们的实现 |
|---|---|---|
| 端点 | `POST https://hezor.com/api/v1/widget/chat`（文档 curl 示例里的 `https://hezor.com/widget/chat` 实测 **405**，以 API 参考的 `/api/v1/...` 为准） | ✅ 用 `base_url + /widget/chat` |
| 请求头 | `X-META-INFO`（MetaInfo JWT）、`X-APP-NAME`（与 Casdoor 注册一致）、`Content-Type: application/json` | ✅ |
| JWT 载荷 | `subject`、`subject_code`、`caller_id`、`creation_slug`、`creation_name`（**注意不是标准 JWT 的 `sub`**） | ✅ 已按文档修正 |
| 签名算法 | Ed25519（EdDSA）；私钥为**加密 PKCS#8**，用 `client_secret` 解密 | ✅ PyJWT + cryptography |
| Token 有效期 | 默认 1 小时，建议 ≤1h，提前 5 分钟刷新 | ✅ 3600s + 300s 提前重签 |
| 自定义透传 | 写在 payload 的 `extras`，键名用 `var_*` 前缀 | ✅ `meta_extras` 配置项 |
| 请求体 | `message`(必填)、`conversationId`、`mode`(默认 '快速研究'，S2S 推荐 `'widget'`)、`stream`、`model`、`systemPrompt`、`workerId`、`kbIds`、`history`、`retry` | ✅ 支持 message/conversationId/mode/stream/workerId/systemPrompt/model/kbIds |
| 响应 | **恒为 SSE**（`stream` 不生效）；`text`(增量)/`done`/`status`/`tool_call`/`client_action`；`done.metadata` 是 snake_case（`conversation_id`/`message_id`/`model`） | ✅ 逐行聚合，`done` 取 conversation_id |
| 终止判定 | 出错时可能**只发 `status(type:error)` 不补 `done`**，只等 done 会误判卡住 | ✅ 把 status error 当终止 |
| 域名白名单 | **S2S 不做 Origin 校验**（只作用于浏览器 iframe）；`affiliation_url` 无需配置 | ✅ 已注明 |
| 错误码 | 401 签名/有效期、403 `allowed_worker_ids`、422 Schema、503 员工未连接或 `mode=widget` 未配路由 | ✅ 已做中文提示映射 |
| 凭证获取 | `GET /app-certs` 拿 `cert_content` + `client_secret`；**只存服务端**，可用环境变量或密钥管理服务 | ✅ 支持环境变量/文件/PEM 三种来源 |

> 说明：官方文档为外部资料，**未复制进本仓库**（本仓库是公开仓库）。
> 本地提取的纯文本仅用于阅读，位于 `.tools/tmp/S2S.txt`（已 gitignore，不入库）。

### 1.6 实测记录（2026-09-29，开发机 Windows）

| 步骤 | 结果 |
|---|---|
| 私钥加载（加密 PKCS#8 + client_secret） | ✅ `Ed25519PrivateKey`，公钥 SHA-256 `be:41:58:ca:3a:d7:c9:54:…` |
| 签发 `X-META-INFO`（EdDSA） | ✅ JWT Header `{'alg':'EdDSA','typ':'JWT'}`，claims 用 `subject` |
| `POST /api/v1/widget/chat` | ✅ **HTTP 200**（签名与 app_name 均通过，不再是 401） |
| 返回内容 | ⛔ `data: {"type":"status","content":{"message":"余额不足，请充值后继续使用","type":"error"}}` 后跟一个 `done` |
| 结论 | **代码侧已打通**；唯一阻塞是 Hezor 账号余额。充值后可直接复测，无需改代码 |

#### 1.6.1 双 worker × 双 mode 对照（2026-09-29 复测）

| 组合 | 结果 | 含义 |
|---|---|---|
| worker `ea532b59-…dcc67` + `mode=widget` | HTTP 200 → `余额不足，请充值后继续使用` | 该 worker **已在白名单**，被卡在计费 |
| worker `87e25025-…9635` + `mode=widget` | **HTTP 403** | 该 worker **不在 `allowed_worker_ids`**；如要使用需先授权 |
| 不指定 worker（`mode` 路由）+ `widget` | HTTP 200 → 同样`余额不足` | 与路由无关 |
| 不指定 worker + `mode=快速研究`（文档默认值） | HTTP 200 → 同样`余额不足` | 换 mode 也不解决 |

**判定**：403（白名单）先于计费校验返回，说明第一条请求本身完全合法，**唯一问题是账号/租户余额**。
换 worker、换 mode 都不能绕过；必须由凭证持有人充值，或改用有额度的账号重新签发一套应用凭证。

**平台可用性对照**（排除平台故障）：同一平台、另一把 key 的 LLM 路径正常 ——
`POST /api/v1/openai/latest/chat/completions` → HTTP 200，正常返回内容。
因此不是 hezor 平台故障，而是 **`phyto_province_gx_app` 这个应用/账号名下的计费项没有额度**
（LLM 网关与 Silicon 数字员工很可能是分开计费的）。响应头里没有账户/余额字段可挖，只能由平台侧查询。

#### 1.6.2 `caller_id`（用户）决定路由与额度（2026-09-29 实测）

JWT 里的 `caller_id` 就是平台侧的"用户"。平台会**按 caller_id 自动建号**，但**路由与额度要按用户另行配置**：

| `caller_id` | 返回 | 含义 |
|---|---|---|
| `guangxi_ai1234@phyto_province_gx_app.local`（`.env.local` 原值） | 10:51 ✅ HTTP 200 + 完整答案 → **11:08 起 `余额不足，请充值后继续使用`** | 路由已配好，但**额度被测试消耗完**（见下方额度说明） |
| `guangxi_ai_robot_m5stack@phyto_province_gx_app.local` | ✅ 全程 HTTP 200 + 完整答案 | 机器人专用用户，**自带额度**，当前仍可用 → 生产用它 |
| `guangxi_ai123@phyto_province_gx_app.local`（少一个 4 的老号） | `余额不足，请充值后继续使用` | 已配好 Widget 数字员工路由，卡在计费 |
| `mibao@…` / `mibao01@…` / `test01@…` / `admin@…`（同域新 caller） | `当前应用尚未配置 Widget 数字员工，请联系管理员在后台为该应用配置对应路由` | 平台已按新 caller 建号，但**新号未分配数字员工路由** |
| `mibao01@example.com`（任意域名邮箱） | 同上（未配置路由） | 建号不区分域名 |
| `guangxi_ai123`（缺域名/格式非法） | **HTTP 401**（无 body） | caller_id 必须是 `xx@yy` 形式 |

**结论（据此定性）**：调用侧能触发的只有"自动建号"；**"分配数字员工路由 + 额度"是应用/用户级的后台配置，调用方无法自动完成**。
因此要么由管理员为指定 caller 配置，要么 Hezor 提供「用户/路由管理」API（需管理权限凭证）后由我们自动注册。

> ⚠️ **额度是消耗品，且按 caller 独立计算**：`guangxi_ai1234@` 在约 17 分钟的连续测试后就被扣到
> `余额不足`；同样的测试量没有耗尽 `guangxi_ai_robot_m5stack@`。数据类问题一次会触发十几次内部
> 工具调用（`datahub_execute_tool` 等），比普通问答贵得多。
> **上线前务必让平台侧给生产 caller 充值/提额，并避免用生产 caller 做压测。**

#### 1.6.3 ✅ 最终可用配置（2026-09-29 实测通过）

| 参数 | 取值 | 说明 |
|---|---|---|
| `caller_id` | `guangxi_ai_robot_m5stack@phyto_province_gx_app.local` | 机器人专用用户；平台自动建号且**自带额度**（当前仍可用）。`.env.local` 原值 `guangxi_ai1234@…` 起初也可用，但已被测试耗到 `余额不足`，**不建议用于生产** |
| `worker_id` | `ea532b59-6311-4d5b-99fb-2553d088cc67` | **必须传**：该用户没有配置 `mode` 路由，靠 workerId 直连该数字员工（UUID 已在 `allowed_worker_ids`） |
| `X-APP-NAME` | `phyto_province_gx_app` | 与 Casdoor 注册一致 |
| 私钥 / 密码 | `.keys/private_key.pem` / `client_secret` | 环境变量 `HEZOR2_HEADER_PK_FILEPATH` + `HEZOR2_HEADER_PK_PASSWORD` |

实测对照（同一把私钥、同一应用）：

| 组合 | 结果 |
|---|---|
| 新 caller `guangxi_ai_robot_m5stack@…` **+ workerId** | ✅ **HTTP 200 + done + 655 字专业答案**（"稻飞虱防治应采取综合防治（IPM）策略…"） |
| `.env.local` 原值 `guangxi_ai1234@…` + workerId | 10:51 ✅ 完整答案 → 11:08 起 ⛔ `余额不足`（额度被测试耗尽） |
| 新 caller，不传 workerId | ⛔ `当前应用尚未配置 Widget 数字员工…路由` |
| 老 caller `guangxi_ai123@…` + workerId | ⛔ `余额不足，请充值后继续使用` |

**关键结论**：
1. 传 `workerId` 可以**绕过"该用户未配置 mode 路由"**的限制；
2. **额度是按用户（caller_id)计的、且会被消耗完**——`guangxi_ai1234@…` 起初可用，连续测试后转为 `余额不足`；
   `guangxi_ai_robot_m5stack@…` 仍有额度，故生产默认用它；
3. 因此无需等管理员配路由：**机器人专用 caller + 显式 workerId** 即可直接工作，但要给生产 caller 备足额度。

启用所需的 5 个环境变量（与 `guangxi_ai_chat` 的 `.env` 命名一致，插件已支持）：

```bash
export HEZOR2_HEADER_PK_FILEPATH=.keys/private_key.pem
export HEZOR2_HEADER_PK_PASSWORD='<client_secret>'
export HEZOR2_APP_NAME=phyto_province_gx_app
export HEZOR2_META_CALLER_ID=guangxi_ai_robot_m5stack@phyto_province_gx_app.local
export WORKER_ID=ea532b59-6311-4d5b-99fb-2553d088cc67
```

#### 1.6.4 配置的三个落点与优先级（本节回答"配在哪个目录"）

| 落点 | 路径 | 入库 | 用途 |
|---|---|---|---|
| 模板（团队共享） | `xiaozhi-server/.config.yaml.template` | ✅ | 插件参数块 + 启用片段，占位值 |
| 真配置（本机/服务器） | `xiaozhi-server/data/.config.yaml` → `plugins.ask_guangxi_phyto` | ❌（`/data/` 已忽略） | 实际生效的取值 |
| 私钥文件 | `xiaozhi-server/.keys/private_key.pem` | ❌（`**/.keys/` 已忽略） | Ed25519 加密私钥 |
| 环境变量 | 由启动器/`launchd` 注入，或 `.env.local` | ❌（`.env` / `.env.local` 已忽略） | 兜底与覆盖 |

解析优先级（`load_plugin_config()`，见插件 164–200 行）：
**默认值 `DEFAULT_CLAIMS`/`DEFAULT_PLUGIN_CONFIG` → `data/.config.yaml` 里的 `plugins.ask_guangxi_phyto` → 环境变量（最高）**；
claim 逐字段生效，环境变量只要有值就覆盖配置。
私钥路径是**配置优先于环境变量**（`_cert_path_candidates`：先配置的 `private_key_path`，再 `HEZOR2_HEADER_PK_FILEPATH`）。
相对路径（`.keys/private_key.pem`）一律以 `xiaozhi-server/` 目录为基准解析。

> 文件名统一约定：**`.keys/private_key.pem`**（2026-09-29 起仓库内所有文档/模板/默认值均由旧的 `private_key.gx.pem` 改为该名）。

#### 1.6.5 Widget SSE 流式语义与已修的 4 个坑（2026-09-29 实测）

抓了三条真实数据流（102~600+ 个 `text` 事件）后确认平台行为，插件已按此加固：

| 现象 | 实测证据 | 插件处理 |
|---|---|---|
| **两条 `done`**：第一条 `metadata` 为 `null`，`client_action` 之后第二条才带 `conversation_id` | 收到第一条就 `break` → 会话记忆永远拿不到 id，追问全部变成新会话 | 收到 `done` 后继续读，拿到 `conversation_id` 才收流（`widget_chat`） |
| **整篇答案从头重发**：数字员工重新生成时把已输出内容再发一遍 | 第 1 段到 `2026\~08` 截断，紧接着重发 `## 查询结果…2026-08-30 `；直接拼接会出现 2~3 份重复 | 按「与当前文档开头的公共前缀 ≥16 字」判定为快照并**替换**（`_looks_like_document_restart`） |
| **长度不能当判据** | 同一天抓到正常片段长 2~495 字，也抓到 1 个 26787 字的（**以 `<tool_call>` 开头**） | 故阈值仅作下限，必须叠加"开头重复"判据 |
| **工具调用过程被当正文流出** | 单个 `text` 事件 26787 字，内容为 `<tool_call><function=datahub_execute_tool>…` | `_clean_answer_text` 剥离 `<tool_call>…</tool_call>` / `<function=…>`，剥空则明确报"未生成答案" |
| **数据类问题超过 60s** | 一次追问跑到 `httpx.ReadTimeout`，整段已收到的正文被丢弃 | 默认 `timeout_seconds` 提到 **120**，且流中断时**保留已聚合内容**而不是报错丢弃 |

> 影响：数据类问题（"广西近期稻飞虱发生情况"）内部要跑 `get_current_datetime` → `read_file` →
> `skill` → `datahub_search_tools` → 多次 `datahub_execute_tool`，单轮明显比普通问答慢且贵，
> 机器人侧建议把这类问题的等待提示做出来（"正在查询省级测报数据…"）。

#### 1.6.6 实测能拿到的真实数据（验收样例）

`guangxi_ai_robot_m5stack@…` 实测返回（机器人问"广西近期稻飞虱发生情况"→ 追问"最近一个月"）：

- **水稻病虫周报表**（第 34~38 期，2026-08-24~09-27）：当前发生面积 75→155 万亩、累计 1078→1433 万亩、
  发生程度 3 级（中等）、平均密度 237→545 头/百丛、最高密度最高 198017 头/百丛、褐飞虱比例；
- **智能虫情测报灯**（灯诱监测）：按监测点给出虫数合计/最高/最小/平均值（如来宾市象州县石龙镇 590 头）；
- **按市/县明细**：桂林、贵港、玉林、钦北区、浦北县等 20 条记录，含主要发生区域。

数字员工会先反问**时段**与**市/县范围**（有时还问**哪种病虫**），所以机器人侧第一句更有效的问法是：
「广西 **最近一个月** 稻飞虱 **全区** 发生面积」，可少一轮往返。

**生产建议**：给米宝一个专用 `caller_id`（例如 `mibao-01@phyto_province_gx_app.local`），
让管理员在后台为它 **① 配置 Widget 数字员工路由 ② 分配额度**；
之后我们只改一行 `claims.caller_id` 即可，**代码无需改动**。

---

## 2. 服务器侧部署（192.168.1.225）

### 2.1 前置条件

- [ ] 能登录服务器，并定位 `xiaozhi-server` 目录（含 `app.py`、`data/.config.yaml`）
- [ ] 拿到**应用凭证**：`cert_content`（Ed25519 加密私钥 PEM）+ `client_secret`（私钥密码）。
      由 Hezor 管理员在 Casdoor 创建，可在平台调用 `GET /app-certs` 获取
- [ ] 应用后台已把 worker UUID `ea532b59-6311-4d5b-99fb-2553d088cc67`
      加进 **OAuth 应用的 `allowed_worker_ids` 白名单**，否则接口返回 403
- [ ] 确认应用名（`phyto_province_gx_app`）与 Casdoor 中注册一致（`X-APP-NAME` 用它）

> **不需要**配置域名白名单：官方文档明确 S2S 调用不做 Origin/域名校验，
> `affiliation_url` 白名单只作用于浏览器 iframe 场景。

### 2.2 同步代码（三选一）

```bash
# A. 服务器上就是本仓库的克隆（推荐）
cd /path/to/robot-m5stack
git pull                      # 或先 git status 确认无本地改动
# 若还没提交，用 scp/rsync 传这两个新文件与两个 requirements 文件

# B. 从开发机 rsync 单文件
rsync -av robot-updating/xiaozhi-server/plugins_func/functions/ask_guangxi_phyto.py \
          robot-updating/xiaozhi-server/scripts/test_hezor_widget.py \
          user@192.168.1.225:/path/to/robot-m5stack/robot-updating/xiaozhi-server/<对应目录>/

# C. 只传文件（Windows 开发机 → 服务器）
scp robot-updating\xiaozhi-server\plugins_func\functions\ask_guangxi_phyto.py user@192.168.1.225:/path/.../plugins_func/functions/
```

同步后确认：

```bash
ls -l plugins_func/functions/ask_guangxi_phyto.py scripts/test_hezor_widget.py
grep -n cryptography requirements.txt        # 应看到 cryptography>=42.0.0
```

### 2.3 安装新增依赖

```bash
cd /path/to/robot-m5stack/robot-updating/xiaozhi-server
source .venv/bin/activate            # 服务器既有虚拟环境
pip install "cryptography>=42.0.0"
python -c "import jwt, cryptography; print('deps ok')"
```

> `cryptography` 是 Ed25519 签名的硬依赖；缺它会在 `build_meta_info_token` 里报
> `ModuleNotFoundError`。PyJWT 本身已在 requirements 里。

### 2.4 提供签名私钥（三种方式任选，插件都支持）

官方文档推荐把 `cert_content` 放进环境变量：

```bash
# 方式一（文档推荐）：环境变量放 PEM 内容 + 私钥密码
export HEZOR_CERT_CONTENT="-----BEGIN ENCRYPTED PRIVATE KEY-----\n...\n-----END ENCRYPTED PRIVATE KEY-----\n"
export HEZOR_CLIENT_SECRET='******'          # 也认 HEZOR2_HEADER_PK_PASSWORD
# 写入启动脚本 / launchd plist / systemd unit，不要写进 yaml

# 方式二：放文件（本项目 .env 风格，HEZOR2_HEADER_PK_FILEPATH）
mkdir -p .keys
scp private_key.pem user@192.168.1.225:/path/.../xiaozhi-server/.keys/
chmod 600 .keys/private_key.pem

# 方式三：直接把 PEM 写进 data/.config.yaml 的 private_key_pem（不推荐，容易误提交）
```

验证（会打印私钥来源，不泄密）：

```bash
python scripts/test_hezor_widget.py --check-config
#   私钥来源 = 环境变量 HEZOR_CERT_CONTENT（PEM 内容）  或  文件 …/.keys/private_key.pem（存在 ✓）
```

> **文件名不强制**：若私钥文件叫 `private_key.pem`，用环境变量
> `HEZOR2_HEADER_PK_FILEPATH=/abs/path/private_key.pem` 指向它即可
> （插件同时支持「配置里的 private_key_path」和「环境变量路径」两种，先命中存在的那个）。

> `cert_content` 是**加密的 PKCS#8**（`-----BEGIN ENCRYPTED PRIVATE KEY-----`），
> 必须用 `client_secret` 解密；两者不匹配就是文档里 401 的头号原因。

### 2.5 修改 `data/.config.yaml`（本地文件，勿入库）

```yaml
# ① 插件配置：私钥走环境变量时这里几乎不用改（其余项都有默认值）
plugins:
  ask_guangxi_phyto:
    private_key_path: .keys/private_key.pem   # 用环境变量 cert_content 时可留默认
    # 可选：private_key_pem_env: HEZOR_CERT_CONTENT      # 自定义环境变量名
    # 可选：private_key_password_env: HEZOR_CLIENT_SECRET # 留空则自动依次尝试两个常见名字
    # 可选：worker_id: ea532b59-6311-4d5b-99fb-2553d088cc67
    # 可选：token_ttl_seconds: 3600 / token_refresh_margin_seconds: 300（默认即文档建议值）
    # claims 默认已按官方文档（subject / subject_code / caller_id / creation_slug / creation_name）

# ② 启用工具：把 ask_guangxi_phyto 加进函数列表
Intent:
  function_call:
    functions:
      - change_role
      - web_search
      - get_weather
      - get_news_from_newsnow
      - play_music
      - ask_guangxi_phyto          # ← 新增
```

> 说明：`data/.config.yaml` 与 `config.yaml` 是**深合并**关系，只写需要覆盖的键即可。
> 若暂时不想启用，先别加 `ask_guangxi_phyto` 那一行——代码同步后不加也不会有任何副作用。

### 2.6 先在服务器上单测（**通过后再重启**）

```bash
cd /path/to/robot-m5stack/robot-updating/xiaozhi-server
source .venv/bin/activate

# 1) 配置是否齐备
python scripts/test_hezor_widget.py --check-config
#    期望：私钥来源 = 环境变量 HEZOR_CERT_CONTENT（PEM 内容） 或 文件 …（存在 ✓）

# 1.5) 打印公钥指纹与 JWT 头（401 排查用，不泄密）
python scripts/test_hezor_widget.py --fingerprint

# 1.8) 模拟框架调用插件，验证 ActionResponse 契约与降级路径
python scripts/test_hezor_widget.py --tool-test

# 2) 不依赖真私钥的链路自检（临时密钥，期望 401）
python scripts/test_hezor_widget.py --self-test
#    期望：[3/4] 签名自验通过  +  [4/4] HTTP 401 MetaInfo verification failed  → ✓ 请求链路正确
#    （若已配置 worker_id，白名单校验可能先返回 403，同样算链路正确）

# 3) 真实调用（这一步才会用到真私钥）
python scripts/test_hezor_widget.py "稻飞虱怎么防治"
#    期望：打印一段植保专业回答 + ✓ 成功，conversation_id=conv_xxx

# 4) 需要把服务端返回原文交给 Hezor 排查时：打印原始 SSE
python scripts/test_hezor_widget.py --raw "稻飞虱怎么防治"
```

三种失败的含义与处理：

| 现象 | 含义 | 处理 |
|---|---|---|
| `--self-test` 就返回非 401（如连接失败） | 网络/base_url 问题 | 检查出网与 `base_url`（应为 `https://hezor.com/api/v1`） |
| 真实调用 401 `MetaInfo verification failed` | **官方故障表**：Token 过期，或 `cert_content` 与 `client_secret` 不匹配 / 签名不对 | ① 跑 `--fingerprint` 拿到**公钥 SHA-256**，发 Hezor 管理员核对是否是应用那一把；② 确认私钥与密码成对（加密 PEM 的密码就是 client_secret）；③ 确认 claims 用 `subject`（不是 `sub`）；④ 确认 token 未过期（默认 1 小时，自动提前 5 分钟重签） |
| 真实调用 403 | `workerId` 不在白名单，或 app_name 与 Casdoor 不一致 | 后台把 UUID 加进 `allowed_worker_ids`；核对 `X-APP-NAME`（S2S 不校验域名，可排除域名白名单因素） |
| 真实调用 422 | 请求体字段校验失败（响应体含 JSON Schema） | 把响应体原文发我，我按 Schema 调整请求体 |
| 真实调用 503 | 数字员工未连接 / `mode=widget` 没配路由 | 到 platform `/silicon` 页面确认该员工在线；或改用 `worker_id` 直连 |
| **HTTP 200 但 `status(type:error)` 报「余额不足，请充值后继续使用」** | **账号/租户余额不足**（签名与路由都是通过的） | 找 Hezor 管理员充值/开通额度；`workerId` 直连与 `mode` 路由返回同一错误，说明与白名单/路由无关 |

### 2.7 重启服务

```bash
# macOS（launchd，本项目的既有部署方式）
launchctl unload -w ~/Library/LaunchAgents/com.mibao.ai-server.plist
sleep 2
launchctl load -w ~/Library/LaunchAgents/com.mibao.ai-server.plist
sleep 12
lsof -iTCP:8001 -sTCP:LISTEN -P        # 应看到监听

# 或前台手动跑（调试用）
cd xiaozhi-server && source .venv/bin/activate && python app.py
```

日志位置：`xiaozhi-server/tmp/server.log`；launchd 标准输出 `/tmp/xiaozhi_server_daemon.log`。

启动自检（应无 Traceback）：

```bash
grep -iE "ask_guangxi_phyto|Traceback|Error" xiaozhi-server/tmp/server.log | tail -20
```

### 2.8 端到端验证

1. 对机器人喊「米宝米宝」，问一个植保问题，例如：**「稻飞虱怎么防治？」**
2. 观察服务器日志，应出现插件调用与回答摘要：

```bash
tail -f xiaozhi-server/tmp/server.log | grep -E "ask_guangxi_phyto|植保数字员工|widget chat"
# 期望：已签发 MetaInfo token → 植保数字员工回答 N 字：…
```

3. 机器人应先用米宝的语气复述植保答案（本地 LLM 会按农业专家 prompt 精简到 120 字内）。
4. **反向验证**：问一句与植保无关的话（如「现在几点」），确认 `ask_guangxi_phyto` **没有被调用**，
   且原有功能（`get_time`、天气、MCP 设备控制）正常。

### 2.9 回滚（1 分钟）

```yaml
# data/.config.yaml：删掉/注释 ask_guangxi_phyto 这一行
Intent:
  function_call:
    functions: [ ... 原有列表 ... ]
```
然后重启服务。插件文件留着不影响任何事；也可以 `rm plugins_func/functions/ask_guangxi_phyto.py` 彻底移除。

---

## 3. 设备固件侧（本机已完成并烧录，仓库待提交）

> 与本插件无关，但同一批改动，提交时请一并处理，避免仓库与实际不一致。

| 文件 | 类型 | 内容 |
|---|---|---|
| `fw/main/apps/app_setup/workers/on_screen_wifi.cpp` | 🆕 新增 | 屏幕直连 Wi-Fi（扫描列表 + 虚拟键盘输密码 + 连接 + 手机配网回退） |
| `fw/main/apps/app_setup/workers/workers.h` | ✏️ 修改 | `OnScreenWifiWorker` 声明 + `Pending` 待办机制 |
| `fw/main/apps/app_setup/app_setup.cpp` | ✏️ 修改 | 菜单：切换 Wi-Fi → 屏幕直连；保留 M5 App 配网 / 手机配网 |
| `fw/main/apps/app_launcher/app_launcher.cpp` | ✏️ 修改 | 首启无 SSID → 进「屏幕直连 Wi-Fi」页 |
| `fw/main/main.cpp` | ✏️ 修改 | 进 xiaozhi 前服务器发现改为 3 次重试（修「配网后回落公网小云导致英文回答」） |
| `fw/components/esp-now/src/debug/**`、`docs/**/debug/**` | 🆕 恢复 | 原缺失源码（`fw/.gitignore` 里未锚定的 `debug/` 规则误删，导致克隆后 cmake 失败） |
| `fw/.gitignore` | ✏️ 修改 | `Debug/debug/Release/release/out/bin/obj` 改为锚定根目录 |
| `.gitignore` | ✏️ 修改 | 新增 `.tools/`、`**/.keys/` |

编译/烧录（本机已就绪，详见 `.tools/build-firmware.ps1`）：

```powershell
pwsh -File .tools\build-firmware.ps1 build     # 编译（约 1–3 分钟增量）
pwsh -File .tools\build-firmware.ps1 flash     # 烧录 COM3（5 个分区，含校验）
```

> 待定（未决策，不影响本清单）：电池供电时「空闲 10 分钟自动关机」是否改为永不关机。

---

## 4. 安全红线（每次部署都检查）

- [ ] `data/.config.yaml` 未被 `git add`（`git status` 里不应出现）
- [ ] `.keys/private_key.pem` 权限 600，且 `git status` 干净
- [ ] 私钥密码只通过环境变量传入，不写在 yaml / 启动脚本的明文注释里
- [ ] 任何日志、截图、文档、对话里出现的密钥一律打码为 `hzr_sk_***` / `sk-***`
- [ ] 本插件**不需要** `HEZOR2_API_KEY`（那条走 OpenAI 兼容路径）；不要把 API Key 贴进插件配置

---

## 5. 验收清单

- [ ] `python scripts/test_hezor_widget.py --check-config` → 私钥就位 ✓
- [ ] `python scripts/test_hezor_widget.py --self-test` → 签名自验通过 + 401（链路正确）
- [ ] `python scripts/test_hezor_widget.py "稻飞虱怎么防治"` → 拿到专业回答
- [ ] 服务器重启后无 Traceback，8001 端口在监听
- [ ] 机器人问植保问题 → 服务器日志出现工具调用 → 回答专业且为中文
- [ ] 机器人问非植保问题 → 工具未被调用，原有功能正常
- [ ] 断网/接口异常场景 → 米宝仍能正常聊天（工具降级，不阻塞对话）
- [ ] 回滚演练一次（注释函数名 → 重启 → 恢复正常）
