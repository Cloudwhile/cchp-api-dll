# CCHP 原生 DLL SDK

Windows 原生 C++17 / WinHTTP SDK，通过纯 C ABI 提供 REST、用量状态快照和 SSE。所有字符串均为 UTF-8，x86 使用 stdcall、导出名称无修饰。Release 静态链接 CRT，构建不联网。

## 构建与目录

安装 Visual Studio 2026 的“使用 C++ 的桌面开发”、v145、Windows SDK、CMake/Ninja；ASan 检查还需安装 C++ AddressSanitizer 组件。打开 `cchp-api-dll.slnx`，选择 Release 与 x64/x86 即可编译 DLL。SDK 源码在 `cchp-api-dll/`，公共头在 `include/`，测试、示例分别在 `tests/`、`examples/`。

```powershell
# 同时构建并测试两种架构，构建原生 VS 解决方案并打包
powershell -NoProfile -ExecutionPolicy RemoteSigned -File scripts/build.ps1 -Package
# 分开的分析与 ASan 构建，均运行测试和 C 示例
powershell -NoProfile -ExecutionPolicy RemoteSigned -File scripts/build.ps1 -Mode Analyze
powershell -NoProfile -ExecutionPolicy RemoteSigned -File scripts/build.ps1 -Mode Asan
```

VS 输出：`x64/Release/cchp.dll`、`Win32/Release/cchp.dll`；中间文件位于同一目录的 `obj/`。CMake 验证构建集中在 `out/build/<架构>-<模式>/`。交付文件集中在 `dist/x64/`、`dist/x86/`，并提供 `dist/cchp-x64.zip`、`dist/cchp-x86.zip` 和 SHA256 清单。目录中的 `.lib` 是导入库，必须与宿主架构匹配。

也可在 VS Developer Command Prompt 中直接运行：

```bat
cmake -S . -B out/build/manual -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/manual --parallel
ctest --test-dir out/build/manual --output-on-failure
```

`CCHP_ANALYZE=ON` 和 `CCHP_ASAN=ON` 必须分开配置。第三方 JSON 为 vendored nlohmann/json 3.11.3，许可证位于 `third_party/nlohmann/`。

## 鉴权配置

以下均为占位符。配置文件使用 UTF-8、无 BOM，不要提交真实凭据。

浏览器会话：每次请求自动带组织头；写请求先从 account/me 获取 CSRF，并带 Origin 与 UUID Idempotency-Key。

```json
{"base_url":"https://ccx.autobits.cc","auth":{"type":"cookie","cookie":"__Host-cch_session=<session>; cf_clearance=<clearance>","org_id":"<organization-id>"}}
```

管理令牌：自动带 Bearer，不发送组织头。账号安全和恢复码操作须使用浏览器会话。

```json
{"auth":{"type":"bearer","token":"<management-token>"},"timeout_ms":30000}
```

密码登录：第一次请求时依次执行 csrf、password、可选 MFA、account/me；合并 Set-Cookie，按 activeOrganizationId、memberships 或 organizations 推断组织。可用 org_id 显式指定。MFA 尚未提供时返回 status=202、code=-7，重新用包含 MFA 的配置创建客户端。

```json
{"auth":{"type":"password","email":"<email>","password":"<password>","org_id":"<organization-id>","mfa":{"method":"totp","code":"<one-time-code>"}}}
```

`mfa` 和 `org_id` 可省略；MFA method 支持 `totp`、`recovery_code`。`timeout_ms` 范围 100..300000。HTTPS 使用系统证书校验和代理；HTTP 仅允许 loopback 测试。Cloudflare clearance 与浏览器/IP 绑定，非 JSON 403 会返回明确的 Cloudflare 提示。

## C 调用

```c
#include "cchp.h"
/* config_json 由宿主从安全配置读取 */
cchp_client client = cchp_client_create(config_json);
if (client) {
    char* value = cchp_status_snapshot(client);
    if (value) {
        puts(value);
        cchp_string_free(value);
    } else {
        fprintf(stderr, "%s\n", cchp_last_error());
    }
    cchp_client_destroy(client);
}
```

链接匹配架构的 `cchp.lib`，将 `cchp.dll` 放到程序目录。纯 C 示例 `examples/status.c` 构建为 `cchp_status.exe`，运行 `cchp_status config.json`，输出模型、最近时间、窗口 tok/s、当日花费和余额的一行状态。该示例使用独立配置文件，最大 8191 字节。易语言 x86 声明见 `examples/e-language-x86.txt`，指针使用整数型并在宿主侧转换 UTF-8。

便捷接口：`cchp_get_me`、`cchp_usage_logs`、`cchp_usage_summary`、`cchp_usage_by_model`、`cchp_usage_timeseries`、`cchp_wallet_overview`、`cchp_quota_effective`、`cchp_status_snapshot`。参数和返回值见 `include/cchp.h`。query_json 是对象，自动 URL 编码；无参数时传 NULL。quota 必填 scopeType（user/org/key）和 scopeId。logs limit 为 1..200，时间使用 RFC3339 UTC，游标从 `page.nextCursor` 取，直至 null。未知字段保留，`$schema` 不参与业务处理，进行中行的 tokens/status/completedAt 可为 null。

其他接口用通用入口，例如：

```c
char* identity = cchp_request(client, "GET", "/api/admin/whoami", NULL, NULL);
char* permissions = cchp_request(client, "GET", "/api/account/permissions/summary", NULL, NULL);
char* finance = cchp_request(client, "GET", "/api/account/finance-events", "{\"limit\":50}", NULL);
/* 分别检查 NULL，成功结果均调用 cchp_string_free。 */
```

组织列表及组织管理接口为 spec-only；`/api/account/orgs` 已知实站返回纯文本 404。详细接口状态见 [冒烟清单](docs/smoke-matrix.md)。

## 快照口径

`nowActiveModel`：最近 120 秒内最新日志的 finalModel（缺失时用 requestedModel），否则 null。`lastModel` / `lastActiveAt` 包含未完成行，最近 300 秒无日志时向前查 30 天。`lastActiveAt` 保留服务端 RFC3339 时间。input/outputTokPerSec 为最近 300 秒日志 token 总和除以 300，遍历全部游标页并对行 ID 去重；null tokens 按 0 处理。summary.today.costUnits 和 wallet.balanceUnits 均以 int64_t 解析，再除以 `10000000000.0`。`153017766000` units 得到 `$15.3017766`。无日志时模型和时间为 null，速率为 0；403 等错误直接使快照失败。

## SSE、内存与线程

`cchp_subscribe(client,"realtime",types,callback,user_data)` 或 `"metrics"` 返回 uint32_t ID，0 为失败。types 是逗号分隔的类型名，NULL 为默认。回调接收 `{"event":"usage_log_created","id":"...","data":{...}}`；心跳也正常转发。网络错误和可重试 HTTP 状态触发 0.5 秒至 30 秒指数退避，遵循服务端 retry，重连发送 Last-Event-ID。空闲流持续等待数据，不因 REST 超时而重连。

回调在 DLL 工作线程上执行。事件字符串仅在回调期间有效，宿主需要复制；user_data 由宿主管理，取消/销毁返回后才能释放。UI 更新应投递到 UI 线程。`sdk_error` 事件的 data 含 status/code/message/terminal；不可重试错误停止该订阅，宿主仍应取消或销毁以回收资源。宿主回调不能抛异常，DLL 会隔离异常并停止订阅。

禁止在任何 SDK 回调中调用 subscribe、unsubscribe、destroy：返回 code=-8，句柄和订阅保留供宿主线程随后释放。外部线程 unsubscribe/destroy 会取消网络读取并 join；返回时相关回调均已结束。销毁前宿主应停止其他线程继续访问句柄；卸载 DLL 前必须销毁全部客户端。DllMain 不做网络、创建线程或线程回收。

普通请求在同一客户端内串行以保护登录/Cookie/CSRF 状态；可创建多个客户端独立并行。全部 FFI 边界用 /EHa 和 catch 隔离 C++ 异常与 SEH。输入指针必须有效，返回字符串只能调用 `cchp_string_free` 一次；不得用宿主 CRT 的 free/delete。`cchp_last_error()` 返回同线程借用的 JSON 指针，无需释放，有效到该线程下一次 SDK 调用；应在释放其他字符串前复制错误。

错误 JSON 包含 status、code、message。服务端业务 code 与 HTTP status 原样保留；任意服务端 detail 不直接回显。SDK code：-1 参数/句柄，-2 内部异常，-3 WinHTTP，-4 响应格式，-5 Cloudflare，-6 会话上下文，-7 MFA，-8 回调生命周期，-9 取消/关闭。成功调用清除线程错误。日志脱敏覆盖 cookie/set-cookie/authorization/password/token/*_token；SDK 不输出原始网络响应或凭据日志。

只有 GET 自动重试，最多 3 次，退避为 100/200/400ms；408/429/500/502/503/504 或网络错误可重试。写操作只执行一次并发送 Idempotency-Key。重定向被禁用，不把凭据发送到另一个地址。

## 验证记录

本次验证均为本地脱敏 mock，无真实生产认证调用。任务书的既有实测事实与本次 mock 结果分开列在冒烟清单中。

| 构建 | x64 | x86 |
| --- | --- | --- |
| MSVC Release | 7/7 | 7/7 |
| MSVC /analyze | 7/7 | 7/7 |
| MSVC ASan | 6/6 | 6/6 |
| 原生 VS2026 Release | 已构建 | 已构建 |

测试包括三种鉴权、MFA、CSRF/Cookie、403、重试、UTF-8 查询、分页与空值、units 换算、30 天兜底、SSE parser/reconnect/空闲取消、回调生命周期、TLS 错误，以及从纯 C 编译并调用全部导出符号的完整流程。C 示例也在 ASan 下运行。ASan 构建额外依赖本机 sanitizer runtime，仅供验证，不用于交付。

Release 的 `dumpbin /dependents`（x86 / x64 一致）：

```text
WINHTTP.dll
RPCRT4.dll
KERNEL32.dll
```

`scripts/check-exports.ps1` 自动比较头文件、.def 与实际 PE 导出，核对架构并限制为系统 DLL 依赖。CI 对两架构分别运行 Release、分析、ASan 并上传 Release 包。未运行远程 CI 时不代表已通过 GitHub 构建。
