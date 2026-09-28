# data 目录

服务器运行时的**私有配置**放在这里，**不会提交到仓库**（含密钥）。

## 必需文件

| 文件 | 说明 |
|---|---|
| `.config.yaml` | 服务器配置，**含你的 API 密钥**（阿里云 ASR + Hezor LLM）。缺失时服务器会直接报错退出 |

## 怎么得到 `.config.yaml`

- **推荐**：直接拷贝你 Mac/旧电脑上的 `xiaozhi-server/data/.config.yaml`
- 或：从项目根目录的 `.config.yaml.template` 复制过来，填入两个 API Key
  （Windows 部署脚本 `scripts/windows/setup_windows.bat` 会自动帮你生成）

## 其它

- `bin/`：OTA 固件下载目录（可选，服务器需要时会用）
- 本目录下的所有内容都被 `.gitignore` 排除，**不要**尝试提交
