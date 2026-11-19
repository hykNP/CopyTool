# CopyTool v1.0.0 发布说明

**发布日期：2026-11-19**
**平台：Windows 10 x64 及以上**
**首个公开版本**

CopyTool 是一个 Windows 文件复制工具：WebView2 做界面，C++ 写复制内核。这一版把重点放在两件事上——把复制速度推到硬件上限，以及让安装目录保持干净。

---

## 亮点

- **小文件复制约快一倍**：5000 个 8KB 文件，从 2.2~3.7 秒降到 1.0~1.1 秒。
- **按磁盘类型自动调参**：启动时探测源盘和目标盘的总线类型（NVMe / SATA / USB / HDD / 网络），各自套用不同策略，不需要用户手动设置。
- **大文件分块并行 I/O**：仅在 NVMe 对 NVMe、且单文件 ≥256MB 时启用，最多 2 路并发、队列深度 4。
- **安装目录无污染**：缓存与配置写在 `%LOCALAPPDATA%\CopyTool`，程序文件夹始终只有 5 个文件。
- **发布包 0.67MB**，压缩包 312KB。

## 性能

测试机：i3-12100F（4 核 8 线程）/ 16GB / 系统盘与数据盘均为 SATA SSD。

| 负载 | 规模 | 优化前 | 优化后 | 结论 |
|---|---|---|---|---|
| 小文件 | 5000 × 8KB | 2.20 ~ 3.73 s | **1.00 ~ 1.10 s** | 约 2 倍，主要来自线程数收敛 |
| 中文件 | 20 × 64MB | — | 持平 | 已到写入带宽上限 |
| 大文件 | 1 × 2GB | 4.33 ~ 7.50 s | 持平 | SATA 顺序写约 450~590MB/s |
| 混合 | 1687MB | — | 持平 | 受目标盘带宽约束 |

结论：小文件是这一版真正的收益点；中等及以上负载在本机上已接近 SATA SSD 的物理上限，继续优化内核没有意义。

## 内核改动

1. **分块并行异步 I/O**：大文件切成 16MB 块，`ReadFile`/`WriteFile` 带 OVERLAPPED，队列深度 4，最多 2 路并发；源与目标在同一物理盘时自动降为 1 路。启用阈值 256MB。
2. **进度回调聚合降频**：每 4MB 或每 50ms 才向 UI 汇总一次，不再逐块回调。
3. **元数据合并写入**：用一次 `SetFileInformationByHandle(FileBasicInfo)` 取代原来两次打开文件设置时间戳，同线程数下小文件再快约 8%。
4. **线程数按逻辑核收敛**：4 核 8 线程机器上，从 16 线程降到 8 线程，小文件耗时从 2.03~2.43 秒降到 1.11~1.36 秒。
5. **磁盘总线探测**：`IOCTL_STORAGE_QUERY_PROPERTY` 读 `BusType`，RAID 情况下用适配器属性与设备名兜底，输出 `threads` / `parallel` 供诊断。
6. **NO_BUFFERING 分支保留但默认关闭**：实测 4.33 秒劣化到 12.27 秒，仅作为可选实验开关。

## 界面与宿主

- 运行期数据迁移到 `%LOCALAPPDATA%\CopyTool`（`EBWebView` 缓存与 `config.ini`），不再写入 exe 所在目录。
- 补全版本信息资源：文件说明、产品名、版权、原始文件名，语言设为中立 + Unicode，中文不再乱码。
- 界面资源编译进 exe，运行时不依赖磁盘上的 WebUI 目录。

## 安装

1. 解压 `CopyTool-v1.0.0-win64.zip` 到任意目录。
2. 双击 `CopyToolWeb.exe`。
3. 五个文件必须放在一起，不要单独移动 `CopyCore.dll` 或 `WebView2Loader.dll`。

**环境要求**：Windows 10 x64 及以上；若提示缺少 WebView2，请安装微软官方 WebView2 Runtime。

## 文件清单与校验

| 文件 | 大小 | SHA-256 |
|---|---|---|
| CopyToolWeb.exe | 394,752 | `E4DB8E3F1A13A2CFAC587EF0673BD009BA03D36EFE3E700B8DA0D487F3ABC1B2` |
| CopyCore.dll | 144,896 | `16CEBDE10E75BBA536A7437899ED5E147CCAA762410E0F3B2C4D7ECD468BD936` |
| WebView2Loader.dll | 163,680 | `C66E4A92FDC7A216118E43B7A5024EA2200E8C43F9310BF20D96A0084F82C5BC` |
| README.txt | 777 | `1D47434DFEBA6EA287BB9E32CFE0F8193A472441DDA59D850C0B6C67B353768D` |
| THIRD-PARTY-NOTICES.txt | 160 | `70D2206B09A2E75B4DF4F2512A9F4010151A8CBACAB77C98D12EAE160BE3EFDC` |

压缩包 `CopyTool-v1.0.0-win64.zip`：319,764 字节
SHA-256：`078CEAFC9AD3C1FCB9BF369E656A9D8958C69FB75210C28A3480B3544EDB59E6`

## 已知限制

- SATA SSD 上分块并行没有收益，默认关闭；这是总线限制，不是实现问题。
- 中/大/混合负载受目标盘写入带宽约束，本机已无优化空间。
- 程序未做代码签名，首次运行可能出现 SmartScreen“未知发布者”提示。
- `NO_BUFFERING` 在本机实测明显更慢，保持默认关闭。

## 第三方组件

- rpmalloc — Copyright (c) 2016-2020 Mattias Jansson，Unlicense OR MIT（本项目按 MIT 使用）。
  详见随包 `THIRD-PARTY-NOTICES.txt`。
