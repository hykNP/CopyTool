# 更新日志

本项目按语义化版本号发布。**版本对比基准是 git tag `v1.0.0`**。

以后要回答“这一版和上一版差在哪”，按顺序做三件事：

1. `git diff v1.0.0..HEAD --stat` —— 看改了哪些文件、多少行。
2. `git log v1.0.0..HEAD --oneline` —— 看改动意图。
3. 对照 `docs/baseline-v1.0.0.json` —— 看产物大小、SHA-256、内核参数与性能基线有没有变化。

---

## [1.0.0] - 2026-11-19

首个公开版本。核心是把复制速度推到硬件上限，并把运行期数据从安装目录里赶出去。

### 新增

- 大文件分块并行异步 I/O：16MB 块、队列深度 4、最多 2 路并发；源与目标同盘时降为 1 路；单文件 ≥256MB 才启用。
- 磁盘总线自动探测（NVMe / SATA / USB / HDD / RAID / 网络），按类型生成线程数、并行开关与缓冲上限。
- `CopyCore_DescribeTargets` 导出接口，供界面与诊断查询源/目标的实际策略。
- 版本信息资源（文件说明、产品名、版权、原始文件名、语言中立 + Unicode）。
- 发布工具链：`pack_release.bat`（白名单打包 + strip + 压缩）、`clean_release.bat`（历史包净化）。
- `THIRD-PARTY-NOTICES.txt`（rpmalloc 声明）、`RELEASE-README.txt`（随包说明）。

### 性能

| 负载 | 优化前 | 优化后 |
|---|---|---|
| 5000 × 8KB 小文件 | 2.20 ~ 3.73 s | 1.00 ~ 1.10 s |
| 20 × 64MB 中文件 | 持平 | 持平 |
| 1 × 2GB 大文件 | 4.33 ~ 7.50 s | 持平（SATA 带宽上限） |
| 1687MB 混合 | 持平 | 持平 |

具体改动：

- 线程数按逻辑核收敛（4C/8T 上 16 → 8 线程，小文件从 2.03~2.43s 降到 1.11~1.36s）。
- 进度回调聚合降频（每 4MB 或 50ms 一次），不再逐块通知 UI。
- 元数据用单句柄 `SetFileInformationByHandle` 合并写入（同 16 线程下 2.28s → 2.09s，约 8%）。
- `NO_BUFFERING` 分支实现但默认关闭：实测 4.33s → 12.27s，明显更差。

### 变更

- 运行期数据从 exe 目录迁到 `%LOCALAPPDATA%\CopyTool`：`EBWebView` 缓存、`config.ini`、调试日志。程序目录保持 5 个文件不被写脏。
- 发布二进制统一 `strip --strip-all`：exe 1,257,507 → 394,752 字节，DLL 259,952 → 144,896 字节。
- `dist/` 加入 `.gitignore`。

### 修复

- 分块并行首版因 `OVERLAPPED.hEvent` 未赋值导致 `WaitForMultipleObjects` 永久等待、bench 超时；已修正。
- 并行阈值曾设为 64MB，导致混合负载劣化到 11.6s；回调到 256MB。
