# CCHPNetwork

面向 Windows 的 CCHP 原生 DLL SDK，提供账户查询、用量统计、状态快照和实时事件订阅。适用于 C/C++、易语言、Delphi，以及支持 C ABI 的其他宿主。

> [!WARNING]
> 管理员权限 Token 相关功能尚未验证。接口尚不稳定，可能随上游变更而调整。

- 发布文件：`CCHPNetwork.x64.dll` / `CCHPNetwork.x86.dll`，以及对应导入库、导出清单和 `cchp.h`
- 支持架构：x64、x86（Win32）
- 接口格式：UTF-8 JSON；x86 使用 `stdcall`，函数名无修饰
- 运行依赖：Windows 系统组件；Release 静态链接 CRT

## 快速开始

### 使用发布包

选择与宿主架构一致的包，解压后将 DLL 放在可执行文件同目录。x64 使用 `CCHPNetwork.x64.dll`，x86 使用 `CCHPNetwork.x86.dll`。C/C++ 项目添加 `cchp.h` 的包含路径，并链接同名 `.lib` 导入库。

| 文件 | 用途 |
| --- | --- |
| `CCHPNetwork.x64.dll` / `CCHPNetwork.x86.dll` | 运行时库 |
| `CCHPNetwork.x64.lib` / `CCHPNetwork.x86.lib` | MSVC 导入库 |
| `CCHPNetwork.x64.def` / `CCHPNetwork.x86.def` | 导出名称清单 |
| `cchp.h` | 公共 C 头文件 |
| `cchp_status.exe` | 使用配置文件查询状态的 C 示例 |
| `e-language-x86.txt` | 易语言 DLL 声明，仅 x86 包提供 |
| `SHA256SUMS.txt` | 包内文件的 SHA-256 校验值 |

公共头文件与 `cchp_*` 函数名保持兼容。现有宿主需更新 DLL 路径和导入库后重新链接；请保留发布包中的架构后缀。

### 查询状态

准备 UTF-8、无 BOM 的 `config.json`。例如，使用自己的管理令牌：

```json
{
  "base_url": "https://ccx.autobits.cc",
  "auth": {
    "type": "bearer",
    "token": "<management-token>"
  }
}
```

运行示例：

```powershell
.\cchp_status.exe .\config.json
```

输出示意：

```text
Model: gpt-6.1-sol, last: gpt-6.1-sol@2026-10-20T15:59:59.000Z, ↑ 2880.3 tok/s ↓ 11.6 tok/s, balance: $0.14\$15.30
```

其中 `balance` 的前一个值为当日花费，后一个值为钱包余额。示例源码见 [examples/status.c](examples/status.c)。

### C 接入示例

```c
#include "cchp.h"
#include <stdio.h>

int show_status(const char* config_json) {
    cchp_client client = cchp_client_create(config_json);
    if (!client) {
        fprintf(stderr, "%s\n", cchp_last_error());
        return 1;
    }

    char* status = cchp_status_snapshot(client);
    if (!status) {
        /* 先使用错误信息，再调用其他 SDK 函数。 */
        fprintf(stderr, "%s\n", cchp_last_error());
        cchp_client_destroy(client);
        return 1;
    }

    puts(status);
    cchp_string_free(status);
    cchp_client_destroy(client);
    return 0;
}
```

## 鉴权

客户端创建时选择一种策略。下面的值均为占位符，请使用本人有权限访问的账户和组织。

### 管理令牌

适用于服务端和自动化程序。请求携带 `Authorization: Bearer ...`，无需组织头。

```json
{
  "auth": { "type": "bearer", "token": "<management-token>" },
  "timeout_ms": 30000
}
```

### 浏览器会话

使用登录后的 Cookie 和组织 ID。SDK 自动发送组织头；写请求额外获取 CSRF 并附加 Origin 和 Idempotency-Key。

```json
{
  "auth": {
    "type": "cookie",
    "cookie": "__Host-cch_session=<session>; cf_clearance=<clearance>",
    "org_id": "<organization-id>"
  }
}
```

Cookie 属于会话凭据，请保存在本地受保护的位置。Cloudflare clearance 与浏览器/IP 关联，复制到 SDK 后仍可能收到非 JSON 403；SDK 会返回明确提示。

### 邮箱与密码

首次请求自动执行 CSRF、密码登录、可选 MFA 和账户查询，随后使用浏览器会话。未指定 `org_id` 时从账户信息推断默认组织。

```json
{
  "auth": {
    "type": "password",
    "email": "<email>",
    "password": "<password>",
    "mfa": { "method": "totp", "code": "<one-time-code>" }
  }
}
```

`mfa` 可省略，method 支持 `totp`、`recovery_code`。服务端要求 MFA 但配置缺少验证码时，返回 `status=202, code=-7`，请补充配置后重新创建客户端。账户安全和恢复码接口需要浏览器会话。

所有策略均支持可选的 `base_url`（默认 `https://ccx.autobits.cc`）和 `timeout_ms`（默认 30000，范围 100～300000）。

## API

完整签名及参数约定见 [include/cchp.h](include/cchp.h)。

| 函数 | 用途 |
| --- | --- |
| `cchp_client_create` / `cchp_client_destroy` | 创建、销毁客户端 |
| `cchp_request` | 调用 `/api/` 下的通用 REST 接口 |
| `cchp_get_me` | 当前账户 |
| `cchp_usage_logs` | 用量日志；响应行位于 `page.items` |
| `cchp_usage_summary` | 用量汇总 |
| `cchp_usage_by_model` / `cchp_usage_timeseries` | 按模型、时间汇总 |
| `cchp_wallet_overview` | 钱包余额和套餐 |
| `cchp_quota_effective` | 查询有效额度；必填 `scopeType`、`scopeId` |
| `cchp_status_snapshot` | 组合状态快照 |
| `cchp_subscribe` / `cchp_unsubscribe` | 订阅、取消 SSE |
| `cchp_string_free` | 释放返回的 JSON 字符串 |
| `cchp_last_error` | 读取当前线程的错误 JSON |

`query_json` 是 JSON 对象，参数自动 URL 编码，NULL 表示无参数。时间参数使用 RFC3339 UTC；logs 的 `limit` 为 1～200。分页读取 `page.nextCursor`，直到 null。解析容忍未知字段，进行中日志的 tokens、status、completedAt 可以为 null。

通用接口示例：

```c
char* response = cchp_request(client, "GET",
    "/api/account/finance-events", "{\"limit\":50}", NULL);
if (response) {
    puts(response);
    cchp_string_free(response);
}
```

组织管理接口属于 spec-only，实站可能返回 404。接口覆盖和验证来源见 [冒烟清单](docs/smoke-matrix.md)。

### 状态快照

| 字段 | 计算口径 |
| --- | --- |
| `nowActiveModel` | 最近 120 秒内最新日志的模型，否则 null |
| `lastModel` / `lastActiveAt` | 最近日志，包含未完成行；无近期记录时向前查 30 天 |
| `inputTokPerSec` / `outputTokPerSec` | 最近 300 秒全部日志的 token 总和除以 300，null tokens 按 0 处理 |
| `todayCostUsd` | `summary.today.costUnits / 10^10` |
| `balanceUsd` | `wallet.balanceUnits / 10^10` |

日志以 `startedAt` 作为请求开始时间，支持 RFC3339 小数秒；`lastActiveAt` 保留原始时间字符串。模型优先使用 finalModel，缺失时使用 requestedModel。快照遍历窗口内全部游标页，按日志 ID 去重。金额使用 64 位整数解析后换算为美元，例如 `153017766000` units 对应 `$15.3017766`。查询失败会返回错误，403 不转换为空数据。

### SSE

stream 支持 `realtime`、`metrics`；types 是逗号分隔的事件类型，NULL 为默认。工作线程回调的数据格式如下：

```json
{"event":"usage_log_created","id":"event-id","data":{"logId":"log-id"}}
```

SDK 处理跨分块 UTF-8、CRLF、多行 data 和心跳，重连携带 Last-Event-ID。网络及可重试 HTTP 错误使用最高 30 秒退避。`sdk_error` 事件的 data 包含 status、code、message、terminal；terminal=true 表示订阅已停止，仍需取消或销毁以回收资源。

## 内存、线程与错误处理

- 返回的 `char*` 必须由 `cchp_string_free` 释放一次，不可使用宿主的 free/delete。
- `cchp_last_error` 返回线程内借用的指针，无需释放；同线程下一次 SDK 调用会更新它。
- SSE 事件字符串仅在回调期间有效，需保留时复制。UI 更新由宿主投递到 UI 线程。
- subscribe、unsubscribe、destroy 不可在 SDK 回调内调用，会返回 code=-8。请由宿主线程操作。
- unsubscribe/destroy 返回时，相关回调已经结束。卸载 DLL 前销毁全部客户端；销毁前停止其他线程继续使用句柄。
- 同一客户端的 REST 请求串行执行。需要独立并行请求时可创建多个客户端。

失败返回 NULL（subscribe 返回 0），错误格式为 `{"status":403,"code":10004,"message":"..."}`。服务端 HTTP status 和业务 code 保持原值。

| SDK code | 含义 |
| --- | --- |
| -1 / -2 | 参数或句柄无效 / 内部异常 |
| -3 / -4 / -5 | WinHTTP 失败 / 响应格式无效 / 非 JSON 403 |
| -6 / -7 | 会话上下文缺失 / 需要 MFA |
| -8 / -9 | 回调内生命周期操作 / 请求取消或客户端关闭 |

GET 最多自动重试 3 次（100/200/400ms），仅重试网络错误和 408/429/500/502/503/504。写操作执行一次，并携带 UUID Idempotency-Key。HTTPS 使用系统证书与代理，重定向禁用。

## 从源码构建

需要 Visual Studio 2026 的 C++ 桌面开发组件、v145、Windows SDK、CMake 和 Ninja。ASan 验证另需 C++ AddressSanitizer 组件。nlohmann/json 3.11.3 已随仓库提供，构建无需下载依赖。

### Visual Studio

打开 `cchp-api-dll.slnx`，选择 Release 和 x64/x86。输出分别位于：

```text
x64/Release/CCHPNetwork.dll
Win32/Release/CCHPNetwork.dll
```

### 构建脚本

在仓库根目录运行：

```powershell
# 双架构构建、测试、原生 VS 构建和打包
powershell -NoProfile -ExecutionPolicy RemoteSigned -File scripts/build.ps1 -Package

# 静态分析、AddressSanitizer（分开构建）
powershell -NoProfile -ExecutionPolicy RemoteSigned -File scripts/build.ps1 -Mode Analyze
powershell -NoProfile -ExecutionPolicy RemoteSigned -File scripts/build.ps1 -Mode Asan
```

发布包位于 `dist/CCHPNetwork-x64.zip`、`dist/CCHPNetwork-x86.zip`，DLL、导入库和导出清单均带对应的 `.x64` / `.x86` 后缀。打包使用独立的 `out/build/x64-package/`、`out/build/x86-package/` 构建目录；暂存文件位于 `out/package/`。普通 VS/CMake 构建输出仍为 `CCHPNetwork.dll`，可通过 CMake 选项 `-DCCHP_ARCH_SUFFIX=ON` 生成带架构后缀的文件。

在 VS Developer Command Prompt 中也可直接运行：

```bat
cmake -S . -B out/build/manual -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/manual --parallel
ctest --test-dir out/build/manual --output-on-failure
```

### 项目结构

```text
cchp-api-dll/   SDK 实现和模块定义
include/       公共 C 头文件
tests/         契约、ABI、SSE 与用量测试
examples/      C 示例和易语言声明
scripts/       构建、打包和 PE 导出检查
docs/          接口冒烟清单
third_party/   JSON 库及许可证
```

## 测试与兼容性

自动测试使用本机 mock，覆盖鉴权、MFA、CSRF、分页和 null 字段、金额换算、SSE 重连及回收、纯 C 全导出调用和示例。Release 与分析构建还校验 PE 架构、头文件/导出表一致性、系统 DLL 依赖、导入库指向及示例加载。ASan 构建仅用于验证，不作为发布包。

Release 的 DLL 依赖为 `WINHTTP.dll`、`RPCRT4.dll`、`KERNEL32.dll`。CI 分别运行 x64/x86 的 Release、Analyze、Asan 构建。

本地 mock 通过不代表真实站点的凭据、权限或 Cloudflare 状态已验证。既有实测事实与本次自动测试范围见 [冒烟清单](docs/smoke-matrix.md)。

## 许可证

项目许可证为 [GNU AGPL v3](LICENSE.txt)。nlohmann/json 使用 MIT 许可证，见 [第三方许可](third_party/nlohmann/LICENSE.MIT)。
