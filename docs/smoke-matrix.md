# 接口冒烟清单

“任务书实测”来自 2026-10-20 已提供的站点验证事实。本次开发只运行本地回放，未携带真实凭据访问生产站点。
“接口范围”仅说明实现支持，不代表生产权限或部署已验收。spec-only 不自动回退成成功或空数据。

| 接口 | 任务书事实 | 本次本地验证 | SDK 入口 |
| --- | --- | --- | --- |
| GET /api/account/me | 已实测，含 CSRF/组织 | Cookie、密码、MFA、Bearer | get_me |
| GET /api/admin/whoami | 清单内，未单独给出实测结果 | 通用路径与鉴权测试 | request |
| GET /api/account/permissions/summary | 已实测，无组织时权限为空 | 通用路径与鉴权测试 | request |
| GET /api/account/usage/logs | 已实测，page.items、未完成行可空 | startedAt、小数秒、分页、中文游标、null、未知字段 | usage_logs |
| GET /api/account/usage/summary | 已实测，组织缺失为 403/10004 | today、units、403 透传 | usage_summary |
| GET /api/account/usage/by-model | 本期接口，未单独给出实测结果 | 路径与 query | usage_by_model |
| GET /api/account/usage/timeseries | 本期接口，未单独给出实测结果 | 路径与 query | usage_timeseries |
| GET /api/account/quota/effective | 本期接口，必填 scopeType/scopeId | 参数校验与 query | quota_effective |
| GET /api/account/wallet | 已实测，1 USD=10^10 units | 153017766000 → 15.3017766 | wallet_overview |
| GET /api/account/finance-events | 本期接口，未单独给出实测结果 | 通用分页参数编码 | request |
| GET /api/account/usage/realtime | 已实测，有心跳，重连带 ID | 分块、断线续传、取消、鉴权 | subscribe realtime |
| GET /api/account/metrics/stream | 本期 SSE | scope 帧、空闲销毁 | subscribe metrics |
| GET /api/account/orgs | 实站 404 纯文本 | 保留 404，不伪造 JSON 成功 | request，spec-only |
| GET /api/account/orgs/{orgId} | spec-only | 通用 request 可调用，未验证实站 | request |
| GET /api/account/orgs/{orgId}/members/{userId} | spec-only | 通用 request 可调用，未验证实站 | request |
| GET /api/account/users/{userId}/memberships | spec-only | 通用 request 可调用，未验证实站 | request |
| GET /api/account/orgs/{orgId}/llm-tokens | spec-only | 通用 request 可调用，未验证实站 | request |

生产冒烟时使用调用者自己的账号、组织和权限；记录 HTTP status、业务 code、响应结构，避免记录凭据及原始鉴权响应。Cloudflare 非 JSON 403 保留 status=403，SDK code=-5。组织查询 404 不表示登录失败。
