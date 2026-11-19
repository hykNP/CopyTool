#define _WIN32_IE 0x0600
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#define COPYCORE_EXPORTS

#include <windows.h>
#include <aclapi.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <malloc.h>
#include <string.h>

#include "CopyCore.h"

#include "rpmalloc.h"
#include "rpnew.h"

#define MAX_WORKER_THREADS 32
#define LARGE_FILE_THRESHOLD (4 * 1024 * 1024)

enum CopyMode {
    MODE_AUTO, MODE_BALANCED, MODE_SSD_SSD, MODE_HDD_HDD, MODE_HDD_SSD,
    MODE_SSD_HDD, MODE_SSD_UFD, MODE_UFD_SSD, MODE_HDD_UFD, MODE_UFD_HDD,
    MODE_UFD_UFD, MODE_EXTREME
};

enum OptimizeStrategy {
    STRATEGY_AUTO, STRATEGY_SMALL, STRATEGY_LARGE
};

enum DiskType { DISK_UNKNOWN = 0, DISK_SSD, DISK_HDD, DISK_USB, DISK_NETWORK };
// 总线类型（M.2 只是插槽形态，SATA 与 NVMe 才是决定并行策略的关键）
enum BusKind { BUS_OTHER = 0, BUS_NVME, BUS_SATA, BUS_USB, BUS_RAID, BUS_VIRTUAL };

typedef struct {
    const wchar_t* displayName;
    const wchar_t* shortName;
    int threads;
} ModeConfig;

static const ModeConfig g_ModeConfigs[] = {
    {L"自动识别", L"自动", 0},
    {L"均衡模式", L"均衡", 16},
    {L"SSD→SSD", L"SSD→SSD", 16},
    {L"HDD→HDD", L"HDD→HDD", 4},
    {L"HDD→SSD", L"HDD→SSD", 8},
    {L"SSD→HDD", L"SSD→HDD", 8},
    {L"SSD→UFD", L"SSD→UFD", 2},
    {L"UFD→SSD", L"UFD→SSD", 4},
    {L"HDD→UFD", L"HDD→UFD", 2},
    {L"UFD→HDD", L"UFD→HDD", 2},
    {L"UFD→UFD", L"UFD→UFD", 1},
    {L"极速狂飙", L"极速", 32}
};

typedef struct {
    int threads;
    const wchar_t* name;
} StrategyConfig;

static const StrategyConfig g_strategyConfigs[] = {
    {0, L"智能检测"},
    {16, L"小文件优化"},
    {32, L"大文件优化"}
};

static int currentLang = 0;

typedef struct {
    const char* id;
    const wchar_t* zhCN;
    const wchar_t* zhTW;
    const wchar_t* enUS;
    const wchar_t* koKR;
    const wchar_t* jaJP;
} LangEntry;

static const LangEntry g_LangTable[] = {
    {"btn_setting", L"设置", L"設定", L"Settings", L"설정", L"設定"},
    {"label_src", L"源文件夹", L"來源資料夾", L"Source", L"원본", L"元"},
    {"label_dst", L"目标文件夹", L"目標資料夾", L"Target", L"대상", L"先"},
    {"btn_browse", L"浏览", L"瀏覽", L"Browse", L"찾기", L"参照"},
    {"btn_copy", L"开始复制", L"開始複製", L"Start", L"시작", L"開始"},
    {"btn_pause", L"暂停", L"暫停", L"Pause", L"일시중지", L"一時停止"},
    {"btn_resume", L"继续", L"繼續", L"Resume", L"재개", L"再開"},
    {"btn_stop", L"终止", L"終止", L"Stop", L"중지", L"停止"},
    {"status_ready", L"准备就绪", L"準備就緒", L"Ready", L"준비됨", L"準備完了"},
    {"status_copying", L"复制中", L"複製中", L"Copying", L"복사중", L"コピー中"},
    {"status_paused", L"已暂停", L"已暫停", L"Paused", L"일시중지됨", L"一時停止中"},
    {"status_stopped", L"已终止", L"已終止", L"Stopped", L"중지됨", L"停止済み"},
    {"status_done", L"完成", L"完成", L"Done", L"완료", L"完了"},
    {"status_scanning", L"正在扫描...", L"正在掃描...", L"Scanning...", L"스캔중...", L"スキャン中..."},
    {"status_terminating", L"正在终止...", L"正在終止...", L"Terminating...", L"종료중...", L"終了中..."},
    {"mode", L"复制模式", L"複製模式", L"Mode", L"모드", L"モード"},
    {"mode_auto", L"自动", L"自動", L"Auto", L"자동", L"自動"},
    {"mode_balanced", L"均衡", L"均衡", L"Balanced", L"균형", L"バランス"},
    {"mode_ssd_ssd", L"SSD→SSD", L"SSD→SSD", L"SSD→SSD", L"SSD→SSD", L"SSD→SSD"},
    {"mode_hdd_hdd", L"HDD→HDD", L"HDD→HDD", L"HDD→HDD", L"HDD→HDD", L"HDD→HDD"},
    {"mode_hdd_ssd", L"HDD→SSD", L"HDD→SSD", L"HDD→SSD", L"HDD→SSD", L"HDD→SSD"},
    {"mode_ssd_hdd", L"SSD→HDD", L"SSD→HDD", L"SSD→HDD", L"SSD→HDD", L"SSD→HDD"},
    {"mode_ssd_ufd", L"SSD→UFD", L"SSD→UFD", L"SSD→UFD", L"SSD→UFD", L"SSD→UFD"},
    {"mode_ufd_ssd", L"UFD→SSD", L"UFD→SSD", L"UFD→SSD", L"UFD→SSD", L"UFD→SSD"},
    {"mode_hdd_ufd", L"HDD→UFD", L"HDD→UFD", L"HDD→UFD", L"HDD→UFD", L"HDD→UFD"},
    {"mode_ufd_hdd", L"UFD→HDD", L"UFD→HDD", L"UFD→HDD", L"UFD→HDD", L"UFD→HDD"},
    {"mode_ufd_ufd", L"UFD→UFD", L"UFD→UFD", L"UFD→UFD", L"UFD→UFD", L"UFD→UFD"},
    {"mode_extreme", L"极速", L"極速", L"Extreme", L"익스트림", L"エクストリーム"},
    {"settings_title", L"设置", L"設定", L"Settings", L"설정", L"設定"},
    {"settings_speed_limit", L"速度限制", L"速度限制", L"Speed Limit", L"속도제한", L"速度制限"},
    {"settings_verify", L"校验完整性", L"校驗完整性", L"Verify", L"검증", L"検証"},
    {"settings_language", L"语言", L"語言", L"Language", L"언어", L"言語"},
    {"settings_save", L"保存", L"儲存", L"Save", L"저장", L"保存"},
    {"settings_cancel", L"取消", L"取消", L"Cancel", L"취소", L"キャンセル"},
    {"settings_saved", L"已保存！", L"已儲存！", L"Saved!", L"저장됨!", L"保存しました"},
    {"settings_hint_zero", L"0 = 不限速", L"0 = 不限速", L"0 = Unlimited", L"0 = 무제한", L"0 = 無制限"},
    {"dialog_done", L"全部复制完成", L"全部複製完成", L"All done!", L"모두 완료!", L"完了しました"},
    {"dialog_stopped", L"复制已终止", L"複製已終止", L"Stopped", L"중지됨", L"停止しました"},
    {"dialog_error_unable", L"无法完成任务", L"無法完成任務", L"Cannot complete task", L"작업을 완료할 수 없음", L"タスクを完了できません"},
    {"dialog_error_access_src", L"无法访问源文件夹", L"無法訪問來源資料夾", L"Cannot access source folder", L"원본 폴더에 접근할 수 없음", L"ソースフォルダにアクセスできません"},
    {"dialog_error_write_dst", L"无法写入目标文件夹", L"無法寫入目標資料夾", L"Cannot write to target folder", L"대상 폴더에 쓸 수 없음", L"先フォルダに書き込めません"},
    {"dialog_error_space", L"磁盘空间不足", L"磁碟空間不足", L"Not enough disk space", L"디스크 공간이 부족함", L"ディスク容量が不足しています"},
    {"dialog_error_path", L"路径过长", L"路徑過長", L"Path too long", L"경로가 너무 김", L"パスが長すぎます"},
    {"dialog_error_locked", L"目标文件被占用", L"目標檔案被佔用", L"Target file is locked", L"대상 파일이 잠겨 있음", L"先ファイルが使用中です"},
    {"dialog_error_read", L"源文件读取失败", L"來源檔案讀取失敗", L"Failed to read source file", L"원본 파일 읽기 실패", L"ソースファイルの読み取りに失敗しました"},
    {"dialog_error_write_file", L"目标文件写入失败", L"目標檔案寫入失敗", L"Failed to write target file", L"대상 파일 쓰기 실패", L"先ファイルの書き込みに失敗しました"},
    {"dialog_error_verify", L"文件缺失！将重新复制", L"檔案缺失！將重新複製", L"File missing! Recopying", L"파일 누락! 다시 복사", L"ファイルが不足しています！再コピーします"},
    {"dialog_error_thread", L"系统资源不足", L"系統資源不足", L"Not enough system resources", L"시스템 리소스가 부족", L"システムリソースが不足"},
    {"dialog_error_memory", L"内存不足", L"記憶體不足", L"Not enough memory", L"메모리 부족", L"メモリが不足しています"},
    {"dialog_ok", L"确定", L"確定", L"OK", L"확인", L"OK"},
    {"strategy_auto", L"智能检测", L"智慧偵測", L"Auto Detect", L"자동 감지", L"自動検出"},
    {"strategy_small", L"小文件优化", L"小檔案最佳化", L"Small Files", L"작은 파일", L"小ファイル最適化"},
    {"strategy_large", L"大文件优化", L"大檔案最佳化", L"Large Files", L"큰 파일", L"大ファイル最適化"},
    {"nav_home", L"主页", L"主頁", L"Home", L"홈", L"ホーム"},
    {"remaining_time", L"还需", L"還需", L"Remaining", L"남음", L"残り"},
    {"scanning_files", L"扫描中... %d 个文件", L"掃描中... %d 個檔案", L"Scanning... %d files", L"스캔중... %d개의 파일", L"スキャン中... %d個のファイル"},
    {"error_title", L"错误", L"錯誤", L"Error", L"오류", L"エラー"},
    {"settings_init_error", L"设置窗口初始化失败！", L"設定視窗初始化失敗！", L"Settings init failed!", L"설정 초기화 실패!", L"設定初期化失敗！"},
    {"d2d_init_error", L"D2D初始化失败", L"D2D初始化失败", L"D2D init failed", L"D2D 초기화 실패", L"D2D初期化失敗"},
    {"speed_unit", L"MB/s", L"MB/s", L"MB/s", L"MB/s", L"MB/s"},
    {"failed_count", L"%d 个失败", L"%d 個失敗", L", %d failed", L", %d개 실패", L"%d個失敗"},
    {"analyzing", L"分析文件分布...", L"分析檔案分布...", L"Analyzing files...", L"파일 분석 중...", L"ファイル分析中..."},
    {"status_verifying", L"校验中", L"校驗中", L"Verifying", L"검증중", L"検証中"},
    {"verify_done", L"校验完成", L"校驗完成", L"Verification done", L"검증 완료", L"検証完了"},
    {"verify_failed", L"%d 个校验失败", L"%d 個校驗失敗", L", %d verify failed", L", %d개 검증 실패", L"%d個検証失敗"}
};

static const wchar_t* GetText(const char* id) {
    int count = sizeof(g_LangTable) / sizeof(LangEntry);
    for (int i = 0; i < count; i++) {
        if (strcmp(g_LangTable[i].id, id) == 0) {
            const wchar_t* texts[] = {
                g_LangTable[i].zhCN, g_LangTable[i].zhTW, g_LangTable[i].enUS,
                g_LangTable[i].koKR, g_LangTable[i].jaJP
            };
            return texts[currentLang];
        }
    }
    return L"";
}

static int g_strategy = STRATEGY_AUTO;

static CRITICAL_SECTION cs;
static BOOL g_csInited = FALSE;

static int progressValue = 0, isCopying = 0;
static int lastProgress = 0;
static volatile int isPaused = 0, stopRequested = 0;
static HANDLE hCopyThread = NULL;
static HANDLE g_hIOCP = NULL;
static HANDLE hWakeEvent = NULL;
static HANDLE hStopEvent = NULL;
static HANDLE hScanDoneEvent = NULL;

static int speedLimitKB = 0;
static int enableVerify = 0;
static int currentMode = MODE_AUTO;

static wchar_t srcPath[MAX_PATH] = L"";
static wchar_t dstPath[MAX_PATH] = L"";
static wchar_t statusText[128] = L"准备就绪";

static int g_workerThreads = 16;
static BOOL g_destIsFresh = FALSE;
static DiskType g_srcDiskType = DISK_UNKNOWN;
static DiskType g_dstDiskType = DISK_UNKNOWN;
static BOOL g_srcNvme = FALSE;
static BOOL g_dstNvme = FALSE;
static BusKind g_srcBus = BUS_OTHER;
static BusKind g_dstBus = BUS_OTHER;
static wchar_t g_srcBusDesc[16] = L"OTHER";
static wchar_t g_dstBusDesc[16] = L"OTHER";

typedef struct FileNode {
    OVERLAPPED ov; // 占位，保证强转 LPOVERLAPPED 时的底层结构合法性
    wchar_t src[MAX_PATH];
    wchar_t dst[MAX_PATH];
    long long fsize;
    DWORD fattrs;
    FILETIME fwriteTime;
    FILETIME fcreateTime;
    struct FileNode* next;
} FileNode;

static FileNode* queueHead = NULL;
static FileNode* queueTail = NULL;
static FileNode* verifyHead = NULL;
static FileNode* verifyTail = NULL;
static BOOL isVerifying = FALSE;
static long long verifiedBytes = 0;
static int verifiedFiles = 0;
static int failedVerifyFiles = 0;
static long long totalBytes = 0;
static char _pad_total_copied[64];
static volatile long long copiedBytes = 0;
static char _pad_copied_verified[64];
static int totalFiles = 0;
static int copiedFiles = 0;
static int failedFiles = 0;
#define MAX_FAIL_LIST 256
static wchar_t g_failList[MAX_FAIL_LIST][MAX_PATH] = {0};
static DWORD g_failErrors[MAX_FAIL_LIST] = {0};
static int g_failCount = 0;

static BOOL g_useIOCP = FALSE;

static int filesScanned = 0;
static int scanDone = 0;
static HANDLE hScanThread = NULL;
static HANDLE g_workerHandles[64] = {0};
static int g_workerHandleCount = 0;
static double currentSpeedMB = 0.0;
static ULONGLONG lastSpeedTime = 0;
static long long lastSpeedBytes = 0;

typedef BOOL (WINAPI *SetThreadIoPriority_t)(HANDLE, int);
static SetThreadIoPriority_t g_pSetThreadIoPriority = NULL;
static BOOL g_ioPriorityInited = FALSE;
static void InitIoPriority() {
    if (!g_ioPriorityInited) {
        g_pSetThreadIoPriority = (SetThreadIoPriority_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadIoPriority");
        g_ioPriorityInited = TRUE;
    }
}

static ULONGLONG g_limLastTick = 0;
static long long g_limDebtBytes = 0;
static void RateLimitWait(long long bytes) {
    if (speedLimitKB <= 0 || bytes <= 0) return;
    const long long limitBytesPerSec = (long long)speedLimitKB * 1024;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        long long sleepMs = 0;
        EnterCriticalSection(&cs);
        if (stopRequested) { LeaveCriticalSection(&cs); return; }
        if (g_limLastTick == 0) g_limLastTick = now;
        long long elapsed = (long long)(now - g_limLastTick);
        long long drained = (elapsed * limitBytesPerSec) / 1000;
        g_limDebtBytes -= drained;
        if (g_limDebtBytes < 0) g_limDebtBytes = 0;
        g_limLastTick = now;
        if (g_limDebtBytes == 0 || g_limDebtBytes + bytes <= limitBytesPerSec) {
            g_limDebtBytes += bytes;
            LeaveCriticalSection(&cs);
            return;
        }
        long long excess = g_limDebtBytes + bytes - limitBytesPerSec;
        sleepMs = (excess * 1000) / limitBytesPerSec;
        if (sleepMs < 1) sleepMs = 1;
        if (sleepMs > 10) sleepMs = 10;
        LeaveCriticalSection(&cs);
        Sleep((DWORD)sleepMs);
        while (isPaused && !stopRequested) Sleep(10);
    }
}

#define DIR_CACHE_BUCKETS 4096
static wchar_t g_dirCache[DIR_CACHE_BUCKETS][MAX_PATH];
static volatile LONG g_dirCacheUsed[DIR_CACHE_BUCKETS];

static inline unsigned DirCache_Hash(const wchar_t* s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned)(*s++ | 0x20); h *= 16777619u; }
    return h & (DIR_CACHE_BUCKETS - 1);
}
static void DirCache_Init() { memset((void*)g_dirCacheUsed, 0, sizeof(g_dirCacheUsed)); }
static BOOL DirCache_Exists(const wchar_t* dir) {
    unsigned idx = DirCache_Hash(dir);
    for (int probe = 0; probe < 8; probe++) {
        unsigned i = (idx + probe) & (DIR_CACHE_BUCKETS - 1);
        if (!g_dirCacheUsed[i]) return FALSE;
        if (_wcsicmp(g_dirCache[i], dir) == 0) return TRUE;
    }
    return FALSE;
}
static void DirCache_Add(const wchar_t* dir) {
    unsigned idx = DirCache_Hash(dir);
    for (int probe = 0; probe < 8; probe++) {
        unsigned i = (idx + probe) & (DIR_CACHE_BUCKETS - 1);
        if (InterlockedCompareExchange(&g_dirCacheUsed[i], 1, 0) == 0) {
            wcsncpy_s(g_dirCache[i], MAX_PATH, dir, _TRUNCATE);
            return;
        }
        if (_wcsicmp(g_dirCache[i], dir) == 0) return;
    }
}

static void EnqueueFileNode(FileNode* node) {
    node->next = NULL;
    if (queueTail) { queueTail->next = node; queueTail = node; }
    else { queueHead = queueTail = node; }
}

static FileNode* DequeueNode() {
    FileNode* node = queueHead;
    if (node) {
        queueHead = node->next;
        if (!queueHead) queueTail = NULL;
    }
    return node;
}

static void ClearQueue() {
    FileNode* node;
    while ((node = DequeueNode()) != NULL) free(node);
}

static void ClearVerifyQueue() {
    while (verifyHead) {
        FileNode* tmp = verifyHead;
        verifyHead = verifyHead->next;
        free(tmp);
    }
    verifyHead = verifyTail = NULL;
}

DWORD WINAPI EventWorkerThreadProc(LPVOID lpParam);
DWORD WINAPI IOCPWorkerThreadProc(LPVOID lpParam);
DWORD WINAPI VerifyThreadProc(LPVOID lpParam);
BOOL CopySingleFile(const wchar_t* src, const wchar_t* dst, long long fsize, DWORD fattrs, const FILETIME* fwriteTime, const FILETIME* fcreateTime);
BOOL VerifyFileCopy(const wchar_t* src, const wchar_t* dst);
static void LoadTuningOnce();
static int  EnvIntL(const char* n, int def);

// 按源/目标设备类型自动生成的参数集：检测一次，复制前套用
struct AutoProfile {
    int threads;
    int parallel;
    int chunkKB;
    int qd;
    int maxPar;
    int minMB;
};
static AutoProfile ComputeProfile(DiskType srcType, DiskType dstType,
                                  BusKind sb, BusKind db, BOOL sameDisk, int logical);
static void ApplyProfileTuning(const AutoProfile& p);
static void ParAccount(DWORD bytes);
static void ParFlush();

static void ShowError(const wchar_t* /*id*/) {}

DiskType DetectDriveType(const wchar_t* path) {
    wchar_t root[8] = {0};
    root[0] = path[0]; root[1] = L':'; root[2] = L'\\'; root[3] = L'\0';
    UINT dt = GetDriveTypeW(root);
    if (dt == DRIVE_REMOTE) return DISK_NETWORK;
    if (dt == DRIVE_REMOVABLE) return DISK_USB;
    if (dt != DRIVE_FIXED) return DISK_UNKNOWN;
    wchar_t volPath[16];
    StringCchPrintfW(volPath, 16, L"\\\\.\\%c:", path[0]);
    HANDLE hVol = CreateFileW(volPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVol == INVALID_HANDLE_VALUE) return DISK_HDD;
    struct { STORAGE_PROPERTY_QUERY q; DEVICE_TRIM_DESCRIPTOR d; } buf;
    memset(&buf, 0, sizeof(buf));
    buf.q.PropertyId = (STORAGE_PROPERTY_ID)8;
    buf.q.QueryType = PropertyStandardQuery;
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(hVol, IOCTL_STORAGE_QUERY_PROPERTY,
        &buf.q, sizeof(buf.q), &buf.d, sizeof(buf.d), &returned, NULL);
    CloseHandle(hVol);
    if (ok && buf.d.TrimEnabled) return DISK_SSD;
    return DISK_HDD;
}

// 查询卷所在设备的总线类型：NVMe / SATA / USB / RAID / 虚拟
static BusKind BusFromCode(BYTE bt) {
    switch (bt) {
        case 0x11: return BUS_NVME;       // BusTypeNvme
        case 0x0B: return BUS_SATA;
        case 0x07: return BUS_USB;
        case 0x08: return BUS_RAID;
        case 0x0E: case 0x0F: case 0x10: return BUS_VIRTUAL;
        default:   return BUS_OTHER;
    }
}

static BusKind DetectBusKind(const wchar_t* path, wchar_t* desc, int cch) {
    if (desc && cch > 0) StringCchCopyW(desc, cch, L"OTHER");
    if (!path || !path[0]) return BUS_OTHER;
    wchar_t volPath[16];
    StringCchPrintfW(volPath, 16, L"\\\\.\\%c:", path[0]);
    HANDLE hVol = CreateFileW(volPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVol == INVALID_HANDLE_VALUE) return BUS_OTHER;
    BusKind k = BUS_OTHER;
    BOOL nameNvme = FALSE;
    {
        STORAGE_PROPERTY_QUERY q;
        memset(&q, 0, sizeof(q));
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        char buf[2048];
        memset(buf, 0, sizeof(buf));
        DWORD returned = 0;
        if (DeviceIoControl(hVol, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &returned, NULL)
            && returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            STORAGE_DEVICE_DESCRIPTOR* d = (STORAGE_DEVICE_DESCRIPTOR*)buf;
            k = BusFromCode(d->BusType);
            if (d->ProductIdOffset && d->ProductIdOffset < returned) {
                const char* pid = buf + d->ProductIdOffset;
                if (pid < buf + returned && strstr(pid, "NVMe") != NULL) nameNvme = TRUE;
            }
        }
    }
    // Intel RST 等 RAID 模式下 BusType 会报 0x08，此时再查适配器描述或设备名
    if (k == BUS_RAID && !nameNvme) {
        STORAGE_PROPERTY_QUERY aq;
        memset(&aq, 0, sizeof(aq));
        aq.PropertyId = StorageAdapterProperty;
        aq.QueryType = PropertyStandardQuery;
        char abuf[1024];
        memset(abuf, 0, sizeof(abuf));
        DWORD aret = 0;
        if (DeviceIoControl(hVol, IOCTL_STORAGE_QUERY_PROPERTY, &aq, sizeof(aq), abuf, sizeof(abuf), &aret, NULL)
            && aret >= sizeof(STORAGE_ADAPTER_DESCRIPTOR)) {
            STORAGE_ADAPTER_DESCRIPTOR* ad = (STORAGE_ADAPTER_DESCRIPTOR*)abuf;
            if (BusFromCode(ad->BusType) == BUS_NVME) k = BUS_NVME;
        }
    }
    if (k == BUS_RAID && nameNvme) k = BUS_NVME;
    CloseHandle(hVol);
    if (desc && cch > 0) {
        const wchar_t* t = L"OTHER";
        switch (k) {
            case BUS_NVME:    t = L"NVME"; break;
            case BUS_SATA:    t = L"SATA"; break;
            case BUS_USB:     t = L"USB";  break;
            case BUS_RAID:    t = L"RAID"; break;
            case BUS_VIRTUAL: t = L"VIRT"; break;
            default: break;
        }
        StringCchCopyW(desc, cch, t);
    }
    return k;
}

int SelectBestMode(const wchar_t* src, const wchar_t* dst) {
    if (currentMode != MODE_AUTO) return currentMode;
    DiskType srcType = DetectDriveType(src);
    DiskType dstType = DetectDriveType(dst);
    if (srcType == DISK_SSD && dstType == DISK_SSD) return MODE_SSD_SSD;
    if (srcType == DISK_HDD && dstType == DISK_HDD) return MODE_HDD_HDD;
    if (srcType == DISK_HDD && dstType == DISK_SSD) return MODE_HDD_SSD;
    if (srcType == DISK_SSD && dstType == DISK_HDD) return MODE_SSD_HDD;
    if (srcType == DISK_SSD && dstType == DISK_USB) return MODE_SSD_UFD;
    if (srcType == DISK_USB && dstType == DISK_SSD) return MODE_UFD_SSD;
    if (srcType == DISK_HDD && dstType == DISK_USB) return MODE_HDD_UFD;
    if (srcType == DISK_USB && dstType == DISK_HDD) return MODE_UFD_HDD;
    if (srcType == DISK_USB && dstType == DISK_USB) return MODE_UFD_UFD;
    return MODE_BALANCED;
}

void ApplyModeConfig(int mode) {
    const ModeConfig* cfg = &g_ModeConfigs[mode];
    if (mode == MODE_AUTO) {
        int autoMode = SelectBestMode(srcPath, dstPath);
        cfg = &g_ModeConfigs[autoMode];
    }
    g_workerThreads = cfg->threads;
    if (g_strategy != STRATEGY_AUTO) {
        const StrategyConfig* sc = &g_strategyConfigs[g_strategy];
        if (sc->threads > 0) g_workerThreads = sc->threads;
    }
}

void ScanDirectory(const wchar_t* path, const wchar_t* baseSrc, size_t baseSrcLen, const wchar_t* baseDst) {
    wchar_t sp[MAX_PATH];
    WIN32_FIND_DATAW fd;
    StringCchPrintfW(sp, ARRAYSIZE(sp), L"%s\\*", path);
    HANDLE h = FindFirstFileExW(sp, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (stopRequested) break;
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == L'\0' || (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0'))) continue;
        wchar_t src[MAX_PATH], dst[MAX_PATH];
        StringCchPrintfW(src, ARRAYSIZE(src), L"%s\\%s", path, fd.cFileName);
        const wchar_t* relStart = src + baseSrcLen;
        while (*relStart == L'\\') relStart++;
        StringCchPrintfW(dst, ARRAYSIZE(dst), L"%s%s", baseDst, relStart);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            CreateDirectoryW(dst, NULL);
            ScanDirectory(src, baseSrc, baseSrcLen, baseDst);
            if (stopRequested) break;
        } else {
            long long fsize = (long long)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
            FileNode* node = (FileNode*)malloc(sizeof(FileNode));
            FileNode* vnode = NULL;
            if (node) {
                wcscpy(node->src, src); wcscpy(node->dst, dst);
                node->fsize = fsize; node->fattrs = fd.dwFileAttributes;
                node->fwriteTime = fd.ftLastWriteTime;
                node->fcreateTime = fd.ftCreationTime;
                node->next = NULL;
            }
            if (enableVerify) {
                vnode = (FileNode*)malloc(sizeof(FileNode));
                if (vnode) {
                    wcscpy(vnode->src, src); wcscpy(vnode->dst, dst);
                    vnode->fsize = fsize; vnode->fattrs = fd.dwFileAttributes;
                    vnode->fwriteTime = fd.ftLastWriteTime;
                    vnode->fcreateTime = fd.ftCreationTime;
                    vnode->next = NULL;
                }
            }
            if (!stopRequested && node) {
                FileNode* toPost = NULL;
                BOOL needWake = FALSE;
                EnterCriticalSection(&cs);
                filesScanned++; totalBytes += fsize; totalFiles++;
                if (vnode) {
                    if (verifyTail) { verifyTail->next = vnode; verifyTail = vnode; }
                    else { verifyHead = verifyTail = vnode; }
                    vnode = NULL;
                }
                if (g_useIOCP) {
                    toPost = node;
                    node = NULL;
                } else {
                    EnqueueFileNode(node);
                    node = NULL;
                    needWake = TRUE;
                }
                LeaveCriticalSection(&cs);
                if (toPost) {
                    if (!PostQueuedCompletionStatus(g_hIOCP, 0, 0, (LPOVERLAPPED)toPost)) free(toPost);
                }
                if (needWake && hWakeEvent) SetEvent(hWakeEvent);
            }
            if (node) free(node);
            if (vnode) free(vnode);
        }
    } while (FindNextFileW(h, &fd) && !stopRequested);
    FindClose(h);
}

DWORD WINAPI ScanThreadProc(LPVOID lpParam) {
    InitIoPriority();
    if (g_pSetThreadIoPriority) g_pSetThreadIoPriority(GetCurrentThread(), 3);
    wchar_t* pPaths = (wchar_t*)lpParam;
    wchar_t* srcRoot = pPaths;
    wchar_t* dstRoot = pPaths + MAX_PATH;
    size_t srcLen = wcslen(srcRoot);
    if (srcLen > 0 && srcRoot[srcLen-1] == L'\\') srcRoot[srcLen-1] = L'\0';
    wchar_t folder[MAX_PATH];
    wchar_t* p = wcsrchr(srcRoot, L'\\');
    if (p && *(p+1) != L'\0') wcscpy(folder, p + 1);
    else wcscpy(folder, srcRoot);
    wchar_t dstRootFixed[MAX_PATH];
    wcscpy(dstRootFixed, dstRoot);
    size_t len = wcslen(dstRootFixed);
    if (len > 0 && dstRootFixed[len-1] != L'\\') {
        StringCchPrintfW(dstRootFixed, ARRAYSIZE(dstRootFixed), L"%s\\", dstRoot);
    }
    wchar_t finalDst[MAX_PATH];
    StringCchPrintfW(finalDst, ARRAYSIZE(finalDst), L"%s%s\\", dstRootFixed, folder);
    g_destIsFresh = CreateDirectoryW(finalDst, NULL);
    if (!g_destIsFresh && GetLastError() != ERROR_ALREADY_EXISTS) g_destIsFresh = FALSE;
    ScanDirectory(srcRoot, srcRoot, wcslen(srcRoot), finalDst);
    EnterCriticalSection(&cs);
    scanDone = 1;
    LeaveCriticalSection(&cs);
    if (hScanDoneEvent) SetEvent(hScanDoneEvent);
    if (hWakeEvent) { for (int i = 0; i < MAX_WORKER_THREADS; i++) SetEvent(hWakeEvent); }
    rpmalloc_thread_finalize();
    return 0;
}

DWORD WINAPI CopyThreadProc(LPVOID lpParam) {
    wchar_t* pPaths = (wchar_t*)lpParam;
    totalBytes = 0; totalFiles = 0; copiedBytes = 0; copiedFiles = 0; failedFiles = 0;
    g_failCount = 0; memset(g_failList, 0, sizeof(g_failList)); memset(g_failErrors, 0, sizeof(g_failErrors));
    isVerifying = FALSE; verifiedBytes = 0; verifiedFiles = 0; failedVerifyFiles = 0;
    DirCache_Init();
    ClearVerifyQueue();
    filesScanned = 0; scanDone = 0;
    ClearQueue();
    g_useIOCP = FALSE;

    ApplyModeConfig(currentMode);

    wchar_t srcVol[MAX_PATH] = L"", dstVol[MAX_PATH] = L"";
    wcsncpy_s(srcVol, MAX_PATH, pPaths, 3); srcVol[2] = L'\\'; srcVol[3] = L'\0';
    wcsncpy_s(dstVol, MAX_PATH, pPaths + MAX_PATH, 3); dstVol[2] = L'\\'; dstVol[3] = L'\0';
    DWORD srcSerial = 0, dstSerial = 0;
    GetVolumeInformationW(srcVol, NULL, 0, &srcSerial, NULL, NULL, NULL, 0);
    GetVolumeInformationW(dstVol, NULL, 0, &dstSerial, NULL, NULL, NULL, 0);
    BOOL sameDisk = (srcSerial != 0 && srcSerial == dstSerial);
    DiskType srcType = DetectDriveType(pPaths);
    DiskType dstType = DetectDriveType(pPaths + MAX_PATH);
    g_srcDiskType = srcType;
    g_dstDiskType = dstType;
    g_srcNvme = FALSE;
    g_dstNvme = FALSE;
    g_srcBus = DetectBusKind(pPaths, g_srcBusDesc, 16);
    g_dstBus = DetectBusKind(pPaths + MAX_PATH, g_dstBusDesc, 16);
    g_srcNvme = (g_srcBus == BUS_NVME);
    g_dstNvme = (g_dstBus == BUS_NVME);
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    // 复制前先看机器：SATA / NVMe / USB / HDD 各自套用不同的线程与并行策略
    AutoProfile prof = ComputeProfile(srcType, dstType, g_srcBus, g_dstBus,
                                      sameDisk, (int)si.dwNumberOfProcessors);
    ApplyProfileTuning(prof);

    int workerCount = g_workerThreads > 0 ? g_workerThreads : si.dwNumberOfProcessors * 2;

    if (sameDisk) {
        if (srcType == DISK_HDD || dstType == DISK_HDD) {
            if (workerCount > 4) workerCount = 4;
        } else {
            if (workerCount > 16) workerCount = 16;
        }
    } else if (srcType == DISK_NETWORK || dstType == DISK_NETWORK) {
        if (workerCount > 8) workerCount = 8;
    } else if (srcType == DISK_USB || dstType == DISK_USB) {
        if (workerCount > 8) workerCount = 8;
    } else if (srcType == DISK_HDD || dstType == DISK_HDD) {
        if (workerCount > 16) workerCount = 16;
    }
    if (workerCount < 2) workerCount = 2;
    if (workerCount > MAX_WORKER_THREADS) workerCount = MAX_WORKER_THREADS;
    // 按检测结果收敛：4C/8T 机器上 16 线程会让小文件负载慢一倍（实测 2.03s -> 1.11s）
    if (currentMode != MODE_EXTREME && workerCount > prof.threads) {
        workerCount = prof.threads;
    }
    // 基准/运维可覆盖线程数（放在所有模式裁剪之后，允许测 32 线程）
    {
        LoadTuningOnce();
        int envT = EnvIntL("COPYCORE_THREADS", 0);
        if (envT > 0) {
            workerCount = envT;
            if (workerCount > MAX_WORKER_THREADS) workerCount = MAX_WORKER_THREADS;
            if (workerCount < 1) workerCount = 1;
        }
    }

    if (hStopEvent) CloseHandle(hStopEvent);
    hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (hScanDoneEvent) CloseHandle(hScanDoneEvent);
    hScanDoneEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (hWakeEvent) CloseHandle(hWakeEvent);
    hWakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

    g_limLastTick = 0;
    g_limDebtBytes = 0;

    if (g_hIOCP) CloseHandle(g_hIOCP);
    g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

    HANDLE* workers = (HANDLE*)malloc(workerCount * sizeof(HANDLE));
    if (!workers) {
        isCopying = 0;
        free(pPaths);
        if (g_hIOCP) { CloseHandle(g_hIOCP); g_hIOCP = NULL; }
        if (hStopEvent) { CloseHandle(hStopEvent); hStopEvent = NULL; }
        if (hScanDoneEvent) { CloseHandle(hScanDoneEvent); hScanDoneEvent = NULL; }
        if (hWakeEvent) { CloseHandle(hWakeEvent); hWakeEvent = NULL; }
        rpmalloc_thread_finalize();
        return 0;
    }
    memset(workers, 0, workerCount * sizeof(HANDLE));
    g_workerHandleCount = 0;

    g_useIOCP = (currentMode == MODE_EXTREME) ? TRUE : FALSE;

    int started = 0;
    for (int i = 0; i < workerCount; i++) {
        if (g_useIOCP) {
            workers[i] = CreateThread(NULL, 0, IOCPWorkerThreadProc, NULL, 0, NULL);
        } else {
            workers[i] = CreateThread(NULL, 0, EventWorkerThreadProc, NULL, 0, NULL);
        }
        if (workers[i]) {
            g_workerHandles[i] = workers[i];
            started++;
        }
    }
    g_workerHandleCount = started;

    hScanThread = CreateThread(NULL, 0, ScanThreadProc, pPaths, 0, NULL);
    if (!hScanThread) {
        stopRequested = 1;
        if (g_useIOCP) {
            for (int i = 0; i < g_workerHandleCount; i++) {
                if (g_workerHandles[i]) PostQueuedCompletionStatus(g_hIOCP, 0, 999, NULL);
            }
        } else {
            if (hWakeEvent) { for (int i = 0; i < g_workerHandleCount; i++) SetEvent(hWakeEvent); }
            if (hScanDoneEvent) SetEvent(hScanDoneEvent);
        }
        WaitForMultipleObjects(g_workerHandleCount, workers, TRUE, INFINITE);
        for (int i = 0; i < workerCount; i++) if (workers[i]) CloseHandle(workers[i]);
        free(workers);
        isCopying = 0;
        free(pPaths);
        if (g_hIOCP) { CloseHandle(g_hIOCP); g_hIOCP = NULL; }
        if (hStopEvent) { CloseHandle(hStopEvent); hStopEvent = NULL; }
        if (hScanDoneEvent) { CloseHandle(hScanDoneEvent); hScanDoneEvent = NULL; }
        if (hWakeEvent) { CloseHandle(hWakeEvent); hWakeEvent = NULL; }
        g_workerHandleCount = 0;
        memset(g_workerHandles, 0, sizeof(g_workerHandles));
        rpmalloc_thread_finalize();
        return 0;
    }

    WaitForSingleObject(hScanThread, INFINITE);
    CloseHandle(hScanThread);
    hScanThread = NULL;

    if (g_workerHandleCount > 0) {
        if (g_useIOCP) {
            for (int i = 0; i < g_workerHandleCount; i++) {
                if (g_workerHandles[i]) PostQueuedCompletionStatus(g_hIOCP, 0, 999, NULL);
            }
        } else {
            if (hWakeEvent) { for (int i = 0; i < g_workerHandleCount; i++) SetEvent(hWakeEvent); }
            if (hScanDoneEvent) SetEvent(hScanDoneEvent);
        }
        WaitForMultipleObjects(g_workerHandleCount, workers, TRUE, INFINITE);
    }

    for (int i = 0; i < workerCount; i++) if (workers[i]) CloseHandle(workers[i]);
    free(workers);
    g_workerHandleCount = 0;
    memset(g_workerHandles, 0, sizeof(g_workerHandles));

    if (g_hIOCP) {
        DWORD bytes; ULONG_PTR key; LPOVERLAPPED pov;
        while (GetQueuedCompletionStatus(g_hIOCP, &bytes, &key, &pov, 0)) {
            if (key != 999 && pov != NULL) free((FileNode*)pov);
        }
        CloseHandle(g_hIOCP);
        g_hIOCP = NULL;
    }
    if (hStopEvent) { CloseHandle(hStopEvent); hStopEvent = NULL; }
    if (hScanDoneEvent) { CloseHandle(hScanDoneEvent); hScanDoneEvent = NULL; }
    if (hWakeEvent) { CloseHandle(hWakeEvent); hWakeEvent = NULL; }

    EnterCriticalSection(&cs);
    int wasStopped = stopRequested;
    LeaveCriticalSection(&cs);

    if (enableVerify && !wasStopped && verifyHead) {
        isVerifying = TRUE;
        verifiedBytes = 0; verifiedFiles = 0; failedVerifyFiles = 0;
        copiedBytes = 0; lastProgress = 0; progressValue = 0;
        currentSpeedMB = 0.0; lastSpeedTime = 0; lastSpeedBytes = 0;
        StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("status_verifying"));
        int verifyThreads = g_workerThreads / 2;
        if (verifyThreads < 1) verifyThreads = 1;
        if (verifyThreads > 8) verifyThreads = 8;
        HANDLE verifyHandles[8];
        for (int i = 0; i < verifyThreads; i++) {
            verifyHandles[i] = CreateThread(NULL, 0, VerifyThreadProc, NULL, 0, NULL);
        }
        WaitForMultipleObjects(verifyThreads, verifyHandles, TRUE, INFINITE);
        for (int i = 0; i < verifyThreads; i++) CloseHandle(verifyHandles[i]);
        isVerifying = FALSE;
        verifyHead = verifyTail = NULL;
    } else {
        ClearVerifyQueue();
    }

    EnterCriticalSection(&cs);
    isPaused = 0;
    LeaveCriticalSection(&cs);
    if (wasStopped) {
        progressValue = 0; isCopying = 0; stopRequested = 0;
        StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("status_stopped"));
    } else {
        progressValue = 100; isCopying = 0;
        if (failedVerifyFiles > 0) failedFiles += failedVerifyFiles;
        const wchar_t* done = GetText("dialog_done");
        const wchar_t* stat = GetText("status_done");
        wchar_t failStr[64] = L"";
        if (failedFiles > 0) StringCchPrintfW(failStr, ARRAYSIZE(failStr), GetText("failed_count"), failedFiles);
        wchar_t verifyStr[128] = L"";
        if (enableVerify && failedVerifyFiles == 0 && verifiedFiles > 0) {
            StringCchPrintfW(verifyStr, ARRAYSIZE(verifyStr), L"  %s %d/%d", GetText("verify_done"), verifiedFiles, totalFiles);
        }
        StringCchPrintfW(statusText, ARRAYSIZE(statusText), L"%s %d %s%s%s",
                         stat, totalFiles, done, failStr, verifyStr);
    }
    free(pPaths);
    rpmalloc_thread_finalize();
    return 0;
}

#if defined(_MSC_VER)
#define COPYCORE_THREAD_LOCAL __declspec(thread)
#else
#define COPYCORE_THREAD_LOCAL __thread
#endif

static COPYCORE_THREAD_LOCAL long long tls_file_transferred = 0;
static COPYCORE_THREAD_LOCAL long long tls_file_acc = 0;

DWORD CALLBACK CopyProgressRoutine(
    LARGE_INTEGER TotalFileSize,
    LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER StreamSize,
    LARGE_INTEGER StreamBytesTransferred,
    DWORD dwStreamNumber,
    DWORD dwCallbackReason,
    HANDLE hSourceFile,
    HANDLE hDestinationFile,
    LPVOID lpData
) {
    if (stopRequested) {
        return PROGRESS_CANCEL;
    }
    while (isPaused && !stopRequested) {
        Sleep(50);
    }

    if (lpData) {
        LARGE_INTEGER* plast = (LARGE_INTEGER*)lpData;
        if (dwCallbackReason == CALLBACK_STREAM_SWITCH) {
            plast->QuadPart = 0;
            tls_file_transferred = 0;
        } else {
            long long current = TotalBytesTransferred.QuadPart;
            long long delta = current - tls_file_transferred;
            if (delta > 0) {
                tls_file_transferred = current;
                ParAccount((DWORD)delta);
            }
        }
    }

    return PROGRESS_CONTINUE;
}

// ===================== 大文件并行分块复制 =====================
// 单个 CopyFileExW 是同步单流：一个大文件只占 1 个线程、队列深度 1。
// 这里用 OVERLAPPED 流水线把大文件切块，维持 QD 个并发读写，用内存换队列深度。
// 阈值 / 块大小 / QD / 并发文件数均可用环境变量覆盖，便于基准 A/B：
//   COPYCORE_PARALLEL_MIN_MB  默认 64
//   COPYCORE_CHUNK_KB         默认 4096
//   COPYCORE_QD               默认 8
//   COPYCORE_MAX_PARALLEL     默认 4
//   COPYCORE_NOBUF            默认 0
#define PAR_MAX_QD 16

typedef struct {
    OVERLAPPED rov;
    OVERLAPPED wov;
    HANDLE     rev;
    HANDLE     wev;
    char*      buf;
    DWORD      iolen;
    long long  off;
    int        state;   // 0=空闲 1=读中 2=写中
} IoSlot;

static int  g_tuneParMinMB = 256;
static int  g_tuneChunkKB  = 16384;
static int  g_tuneQD       = 4;
static int  g_tuneMaxPar   = 2;
static int  g_tuneNoBuf    = 0;
static BOOL g_tuneLoaded   = FALSE;
static BOOL g_envMinMB     = FALSE;
static BOOL g_envChunk     = FALSE;
static BOOL g_envQD        = FALSE;
static BOOL g_envMaxPar    = FALSE;
static BOOL g_profileParallel = FALSE;
static volatile LONG g_parActive = 0;

static COPYCORE_THREAD_LOCAL long long tls_parPending = 0;
static COPYCORE_THREAD_LOCAL ULONGLONG tls_parFlushTick = 0;

static int EnvIntL(const char* n, int def) {
    const char* v = getenv(n);
    if (!v || !*v) return def;
    int x = atoi(v);
    return x > 0 ? x : def;
}

static void LoadTuningOnce() {
    if (g_tuneLoaded) return;
    g_tuneLoaded = TRUE;
    const char* e;
    e = getenv("COPYCORE_PARALLEL_MIN_MB"); if (e && *e) g_envMinMB  = TRUE;
    e = getenv("COPYCORE_CHUNK_KB");        if (e && *e) g_envChunk  = TRUE;
    e = getenv("COPYCORE_QD");              if (e && *e) g_envQD     = TRUE;
    e = getenv("COPYCORE_MAX_PARALLEL");    if (e && *e) g_envMaxPar = TRUE;
    g_tuneParMinMB = EnvIntL("COPYCORE_PARALLEL_MIN_MB", 256);
    g_tuneChunkKB  = EnvIntL("COPYCORE_CHUNK_KB", 16384);
    g_tuneQD       = EnvIntL("COPYCORE_QD", 4);
    g_tuneMaxPar   = EnvIntL("COPYCORE_MAX_PARALLEL", 2);
    g_tuneNoBuf    = EnvIntL("COPYCORE_NOBUF", 0);
    if (g_tuneQD > PAR_MAX_QD) g_tuneQD = PAR_MAX_QD;
    if (g_tuneQD < 2) g_tuneQD = 2;
    if (g_tuneChunkKB < 256) g_tuneChunkKB = 256;
    if (g_tuneChunkKB > 65536) g_tuneChunkKB = 65536;
    g_tuneChunkKB = (g_tuneChunkKB / 4) * 4;   // 扇区对齐
    if (g_tuneMaxPar < 1) g_tuneMaxPar = 1;
}

// 套用自动参数；显式环境变量优先，便于基准测试做受控实验
static void ApplyProfileTuning(const AutoProfile& p) {
    LoadTuningOnce();
    if (!g_envMinMB)  g_tuneParMinMB = p.minMB;
    if (!g_envChunk)  g_tuneChunkKB  = p.chunkKB;
    if (!g_envQD)     g_tuneQD       = p.qd;
    if (!g_envMaxPar) g_tuneMaxPar   = p.maxPar;
    g_profileParallel = p.parallel ? TRUE : FALSE;
}

static AutoProfile ComputeProfile(DiskType srcType, DiskType dstType,
                                  BusKind sb, BusKind db, BOOL sameDisk, int logical) {
    AutoProfile p;
    p.chunkKB = 16384;
    p.qd      = 4;
    p.maxPar  = sameDisk ? 1 : 2;
    p.minMB   = 256;

    BOOL hdd  = (srcType == DISK_HDD || dstType == DISK_HDD);
    BOOL net  = (srcType == DISK_NETWORK || dstType == DISK_NETWORK);
    BOOL usb  = (sb == BUS_USB || db == BUS_USB ||
                 srcType == DISK_USB || dstType == DISK_USB);
    BOOL nvmeBoth = (sb == BUS_NVME && db == BUS_NVME);

    // 线程数按瓶颈设备定：HDD 寻道是串行瓶颈，堆线程只会加剧抖动
    if (hdd)      p.threads = logical < 4 ? logical : 4;
    else if (net) p.threads = logical < 8 ? logical : 8;
    else if (usb) p.threads = logical < 4 ? logical : 4;
    else          p.threads = logical;

    // 分块并行只在两端都是 NVMe 时才有意义（SATA 单流已打满 6Gb/s）
    p.parallel = (nvmeBoth && !usb && !net) ? 1 : 0;
    if (p.parallel) {
        while ((long long)p.chunkKB * p.qd * p.maxPar > 262144 && p.qd > 2) p.qd--;
    }
    return p;
}

static long long MinLL(long long a, long long b) { return a < b ? a : b; }

static void ParFlush() {
    if (tls_parPending > 0) {
        InterlockedAdd64((LONG64 volatile*)&copiedBytes, tls_parPending);
        tls_file_acc += tls_parPending;
        tls_parPending = 0;
    }
    tls_parFlushTick = GetTickCount64();
}

// 进度记账：每 4MB 或 50ms 才碰一次全局原子变量，降低 16~32 线程的 cache line 争用
static void ParAccount(DWORD bytes) {
    tls_parPending += (long long)bytes;
    ULONGLONG now = GetTickCount64();
    if (tls_parPending >= (4LL << 20) || (now - tls_parFlushTick) >= 50) ParFlush();
    RateLimitWait((long long)bytes);
}

static BOOL ParIssueRead(HANDLE h, IoSlot* s, long long off, DWORD len) {
    ResetEvent(s->rev);
    ZeroMemory(&s->rov, sizeof(s->rov));
    s->rov.hEvent = s->rev;
    LARGE_INTEGER li; li.QuadPart = off;
    s->rov.Offset = li.LowPart;
    s->rov.OffsetHigh = li.HighPart;
    s->off = off;
    s->iolen = len;
    BOOL ok = ReadFile(h, s->buf, len, NULL, &s->rov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) return FALSE;
    s->state = 1;
    return TRUE;
}

static BOOL ParIssueWrite(HANDLE h, IoSlot* s) {
    ResetEvent(s->wev);
    ZeroMemory(&s->wov, sizeof(s->wov));
    s->wov.hEvent = s->wev;
    LARGE_INTEGER li; li.QuadPart = s->off;
    s->wov.Offset = li.LowPart;
    s->wov.OffsetHigh = li.HighPart;
    BOOL ok = WriteFile(h, s->buf, s->iolen, NULL, &s->wov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) return FALSE;
    s->state = 2;
    return TRUE;
}

static BOOL CopyFileParallel(const wchar_t* src, const wchar_t* dst, long long fsize) {
    LoadTuningOnce();
    BOOL nobuf = g_tuneNoBuf ? TRUE : FALSE;
    long long chunk = (long long)g_tuneChunkKB * 1024;
    int qd = g_tuneQD;
    long long total = nobuf ? ((fsize / 4096) * 4096) : fsize;

    DWORD sf = FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED;
    DWORD df = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED;
    if (nobuf) { sf |= FILE_FLAG_NO_BUFFERING; df |= FILE_FLAG_NO_BUFFERING; }

    HANDLE hS = CreateFileW(src, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, sf, NULL);
    if (hS == INVALID_HANDLE_VALUE) return FALSE;
    HANDLE hD = CreateFileW(dst, GENERIC_WRITE | GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, CREATE_ALWAYS, df, NULL);
    if (hD == INVALID_HANDLE_VALUE) { CloseHandle(hS); return FALSE; }

    // 预分配：避免逐块扩展文件元数据
    LARGE_INTEGER li; li.QuadPart = fsize;
    SetFilePointerEx(hD, li, NULL, FILE_BEGIN);
    if (!SetEndOfFile(hD)) {
        CloseHandle(hS); CloseHandle(hD);
        DeleteFileW(dst);
        return FALSE;
    }

    IoSlot* slots = (IoSlot*)calloc((size_t)qd, sizeof(IoSlot));
    if (!slots) { CloseHandle(hS); CloseHandle(hD); DeleteFileW(dst); return FALSE; }

    BOOL ok = TRUE;
    for (int i = 0; i < qd; i++) {
        slots[i].buf = (char*)_aligned_malloc((size_t)chunk, 4096);
        slots[i].rev = CreateEventW(NULL, TRUE, FALSE, NULL);
        slots[i].wev = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!slots[i].buf || !slots[i].rev || !slots[i].wev) { ok = FALSE; break; }
    }

    long long nextOff = 0;
    int inflightReads = 0, inflightWrites = 0;

    if (ok) {
        for (int i = 0; i < qd && nextOff < total; i++) {
            DWORD len = (DWORD)MinLL(chunk, total - nextOff);
            if (!ParIssueRead(hS, &slots[i], nextOff, len)) { ok = FALSE; break; }
            nextOff += len;
            inflightReads++;
        }
    }

    while (ok && (inflightReads > 0 || inflightWrites > 0)) {
        if (stopRequested) { ok = FALSE; break; }
        while (isPaused && !stopRequested) Sleep(10);
        if (stopRequested) { ok = FALSE; break; }

        HANDLE evs[2 * PAR_MAX_QD];
        int idx[2 * PAR_MAX_QD];
        int n = 0;
        for (int i = 0; i < qd; i++) {
            if (slots[i].state == 1) { evs[n] = slots[i].rev; idx[n] = i; n++; }
            else if (slots[i].state == 2) { evs[n] = slots[i].wev; idx[n] = i; n++; }
        }
        if (n == 0) break;

        DWORD w = WaitForMultipleObjects((DWORD)n, evs, FALSE, 100);
        if (w == WAIT_TIMEOUT) continue;
        if (w == WAIT_FAILED) { ok = FALSE; break; }

        IoSlot* s = &slots[idx[w - WAIT_OBJECT_0]];
        DWORD br = 0;

        if (s->state == 1) {
            if (!GetOverlappedResult(hS, &s->rov, &br, FALSE) || br != s->iolen) { ok = FALSE; break; }
            inflightReads--;
            s->state = 0;
            if (!ParIssueWrite(hD, s)) { ok = FALSE; break; }
            inflightWrites++;
        } else {
            if (!GetOverlappedResult(hD, &s->wov, &br, FALSE) || br != s->iolen) { ok = FALSE; break; }
            inflightWrites--;
            s->state = 0;
            ParAccount(s->iolen);
            if (nextOff < total) {
                DWORD len = (DWORD)MinLL(chunk, total - nextOff);
                if (!ParIssueRead(hS, s, nextOff, len)) { ok = FALSE; break; }
                nextOff += len;
                inflightReads++;
            }
        }
    }

    if (!ok) { CancelIoEx(hS, NULL); CancelIoEx(hD, NULL); }

    for (int i = 0; i < qd; i++) {
        if (slots[i].buf) _aligned_free(slots[i].buf);
        if (slots[i].rev) CloseHandle(slots[i].rev);
        if (slots[i].wev) CloseHandle(slots[i].wev);
    }
    free(slots);
    CloseHandle(hS);
    CloseHandle(hD);

    // NO_BUFFERING 下尾部不足一个扇区的部分用常规句柄补齐
    if (ok && nobuf) {
        long long tail = fsize - total;
        if (tail > 0) {
            HANDLE tS = CreateFileW(src, GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            HANDLE tD = CreateFileW(dst, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (tS == INVALID_HANDLE_VALUE || tD == INVALID_HANDLE_VALUE) {
                ok = FALSE;
            } else {
                LARGE_INTEGER z; z.QuadPart = total;
                SetFilePointerEx(tS, z, NULL, FILE_BEGIN);
                SetFilePointerEx(tD, z, NULL, FILE_BEGIN);
                char tb[4096];
                DWORD br2 = 0, bw2 = 0;
                if (ReadFile(tS, tb, (DWORD)tail, &br2, NULL) && br2 == (DWORD)tail &&
                    WriteFile(tD, tb, br2, &bw2, NULL) && bw2 == br2) {
                    // ok
                } else {
                    ok = FALSE;
                }
            }
            if (tS != INVALID_HANDLE_VALUE) CloseHandle(tS);
            if (tD != INVALID_HANDLE_VALUE) CloseHandle(tD);
        }
    }

    if (!ok) { DeleteFileW(dst); return FALSE; }
    ParFlush();
    return TRUE;
}

static BOOL ShouldUseParallel(long long fsize, DWORD fattrs) {
    LoadTuningOnce();
    if (g_srcDiskType == DISK_HDD || g_dstDiskType == DISK_HDD) return FALSE;
    // 分块并行只在两端都是 NVMe 时启用（检测结果见 ComputeProfile）；
    // SATA 单流已打满 6Gb/s，拆块只会增加开销（本项目实测 601 vs 593 MB/s，属噪声）。
    if (!g_profileParallel && !EnvIntL("COPYCORE_FORCE_PARALLEL", 0)) return FALSE;
    if (fsize < (long long)g_tuneParMinMB * 1024 * 1024) return FALSE;
    if (fattrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SPARSE_FILE |
                  FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED)) return FALSE;
    return TRUE;
}

// 一次句柄同时设时间与属性：原来 CreateFileW+SetFileTime+Close+SetFileAttributesW
// 等于两次打开，小文件场景纯属浪费。
static void ApplyFileMeta(const wchar_t* dst, DWORD fattrs, const FILETIME* fcreateTime, const FILETIME* fwriteTime) {
    HANDLE h = CreateFileW(dst, FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    FILE_BASIC_INFO bi;
    ZeroMemory(&bi, sizeof(bi));
    if (fcreateTime) {
        bi.CreationTime.LowPart = fcreateTime->dwLowDateTime;
        bi.CreationTime.HighPart = (LONG)fcreateTime->dwHighDateTime;
    }
    if (fwriteTime) {
        bi.LastWriteTime.LowPart = fwriteTime->dwLowDateTime;
        bi.LastWriteTime.HighPart = (LONG)fwriteTime->dwHighDateTime;
    }
    bi.FileAttributes = (fattrs != INVALID_FILE_ATTRIBUTES) ? fattrs : 0;
    BOOL ok = SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof(bi));
    CloseHandle(h);
    if (!ok) {
        HANDLE h2 = CreateFileW(dst, FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h2 != INVALID_HANDLE_VALUE) {
            SetFileTime(h2, fcreateTime, NULL, fwriteTime);
            CloseHandle(h2);
        }
        if (fattrs != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(dst, fattrs);
    }
}

BOOL CopySingleFile(const wchar_t* src, const wchar_t* dst, long long fsize, DWORD fattrs, const FILETIME* fwriteTime, const FILETIME* fcreateTime) {
    tls_file_transferred = 0;
    tls_file_acc = 0;
    tls_parPending = 0;

    WIN32_FILE_ATTRIBUTE_DATA dstAttr;
    memset(&dstAttr, 0, sizeof(dstAttr));
    LARGE_INTEGER srcSize;
    srcSize.QuadPart = fsize;

    if (!g_destIsFresh) {
        BOOL dstExists = GetFileAttributesExW(dst, GetFileExInfoStandard, &dstAttr);
        if (dstExists && fwriteTime) {
            LARGE_INTEGER dstSize;
            dstSize.HighPart = dstAttr.nFileSizeHigh;
            dstSize.LowPart = dstAttr.nFileSizeLow;
            BOOL sameSize = (dstSize.QuadPart == srcSize.QuadPart);
            BOOL sameTime = (dstAttr.ftLastWriteTime.dwHighDateTime == fwriteTime->dwHighDateTime &&
                             dstAttr.ftLastWriteTime.dwLowDateTime == fwriteTime->dwLowDateTime);
            if (sameSize && sameTime) {
                InterlockedAdd64((LONG64 volatile*)&copiedBytes, srcSize.QuadPart);
                return TRUE;
            }
        }
    }

    if (fsize == 0 && !(fattrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (CopyFileW(src, dst, FALSE)) {
            ApplyFileMeta(dst, fattrs, fcreateTime, fwriteTime);
            return TRUE;
        }
    }

    if (fsize > 0 && fsize <= (1LL << 20)) {
        RateLimitWait(fsize);
    }
    LARGE_INTEGER lastCb;
    lastCb.QuadPart = 0;
    LPVOID cbCtx = (fsize > (1LL << 20)) ? (LPVOID)&lastCb : NULL;

    BOOL result = FALSE;
    BOOL parAttempted = FALSE;
    if (ShouldUseParallel(fsize, fattrs)) {
        parAttempted = TRUE;
        // 限制同时走并行路径的文件数，避免 N 个 worker × QD 个缓冲把内存打爆
        if (InterlockedIncrement(&g_parActive) <= g_tuneMaxPar) {
            result = CopyFileParallel(src, dst, fsize);
        }
        InterlockedDecrement(&g_parActive);
        if (!result && !stopRequested) {
            // 并行路径失败：回滚已记账字节，退回标准路径重试
            if (tls_file_acc > 0) {
                InterlockedAdd64((LONG64 volatile*)&copiedBytes, -tls_file_acc);
                tls_file_acc = 0;
            }
            tls_parPending = 0;
        }
    }
    if (!parAttempted || (!result && !stopRequested)) {
        lastCb.QuadPart = 0;
        result = CopyFileExW(src, dst, CopyProgressRoutine, cbCtx, NULL, 0);
        if (!result && GetLastError() == ERROR_ACCESS_DENIED) {
            SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);
            lastCb.QuadPart = 0;
            result = CopyFileExW(src, dst, CopyProgressRoutine, cbCtx, NULL, 0);
        }
    }

    if (!result) {
        if (tls_file_acc > 0) {
            InterlockedAdd64((LONG64 volatile*)&copiedBytes, -tls_file_acc);
            tls_file_acc = 0;
        }
        tls_file_transferred = 0;
        DeleteFileW(dst);
        return FALSE;
    }

    // 元数据在末尾用单一句柄统一设置（见 ApplyFileMeta）

    if (enableVerify) {
        HANDLE hFlush = CreateFileW(dst, GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFlush == INVALID_HANDLE_VALUE) {
            SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);
            hFlush = CreateFileW(dst, GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (hFlush != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hFlush);
            CloseHandle(hFlush);
        }
    }

    ApplyFileMeta(dst, fattrs, fcreateTime, fwriteTime);
    ParFlush();

    if (tls_file_acc < fsize) {
        InterlockedAdd64((LONG64 volatile*)&copiedBytes, fsize - tls_file_acc);
    }
    tls_file_acc = 0;
    tls_file_transferred = 0;
    return TRUE;
}

static void ProcessNode(FileNode* node) {
    wchar_t src[MAX_PATH], dst[MAX_PATH];
    long long nodeFsize = node->fsize;
    DWORD nodeFattrs = node->fattrs;
    FILETIME nodeFtime = node->fwriteTime;
    FILETIME nodeFctime = node->fcreateTime;
    wcscpy(src, node->src);
    wcscpy(dst, node->dst);
    free(node);

    while (isPaused && !stopRequested) Sleep(10);
    if (stopRequested) return;

    BOOL copyOk = FALSE;
    DWORD lastErr = 0;
    for (int retry = 0; retry < 3; retry++) {
        if (stopRequested) break;
        copyOk = CopySingleFile(src, dst, nodeFsize, nodeFattrs, &nodeFtime, &nodeFctime);
        if (copyOk) break;
        lastErr = GetLastError();
        if (retry < 2 && !stopRequested) {
            for (int s = 0; s < 20 && !stopRequested; s++) Sleep(10);
        }
    }
    if (!copyOk) {
        if (stopRequested) return;
        InterlockedIncrement((LONG volatile*)&failedFiles);
        int idx = InterlockedIncrement((LONG volatile*)&g_failCount) - 1;
        if (idx < MAX_FAIL_LIST) {
            wcscpy_s(g_failList[idx], MAX_PATH, src);
            g_failErrors[idx] = lastErr;
        }
        return;
    }
    InterlockedIncrement((LONG volatile*)&copiedFiles);
}

DWORD WINAPI EventWorkerThreadProc(LPVOID lpParam) {
    (void)lpParam;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    InitIoPriority();
    if (g_pSetThreadIoPriority) g_pSetThreadIoPriority(GetCurrentThread(), 4);
    while (1) {
        EnterCriticalSection(&cs);
        if (stopRequested) { LeaveCriticalSection(&cs); break; }
        FileNode* node = DequeueNode();
        BOOL done = (!node && scanDone);
        LeaveCriticalSection(&cs);
        if (done) break;
        if (!node) {
            HANDLE waits[3] = { hWakeEvent, hScanDoneEvent, hStopEvent };
            int n = 0;
            if (hWakeEvent) waits[n++] = hWakeEvent;
            if (hScanDoneEvent) waits[n++] = hScanDoneEvent;
            if (hStopEvent) waits[n++] = hStopEvent;
            if (n > 0) WaitForMultipleObjects(n, waits, FALSE, INFINITE);
            else Sleep(1);
            continue;
        }
        ProcessNode(node);
    }
    rpmalloc_thread_finalize();
    return 0;
}

DWORD WINAPI IOCPWorkerThreadProc(LPVOID lpParam) {
    (void)lpParam;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    InitIoPriority();
    if (g_pSetThreadIoPriority) g_pSetThreadIoPriority(GetCurrentThread(), 4);
    while (1) {
        if (stopRequested) break;
        while (isPaused && !stopRequested) Sleep(10);
        if (stopRequested) break;

        DWORD bytes;
        ULONG_PTR key;
        LPOVERLAPPED pov;
        BOOL ok = GetQueuedCompletionStatus(g_hIOCP, &bytes, &key, &pov, INFINITE);
        if (key == 999) break;
        if (!ok && pov == NULL) continue;
        FileNode* node = (FileNode*)pov;
        if (!node) continue;
        if (stopRequested) { free(node); break; }
        ProcessNode(node);
    }
    rpmalloc_thread_finalize();
    return 0;
}

DWORD WINAPI VerifyThreadProc(LPVOID lpParam) {
    (void)lpParam;
    while (1) {
        if (stopRequested) break;
        EnterCriticalSection(&cs);
        FileNode* cur = verifyHead;
        if (cur) verifyHead = verifyHead->next;
        LeaveCriticalSection(&cs);
        if (!cur) break;
        long long fsize = cur->fsize;
        if (VerifyFileCopy(cur->src, cur->dst)) {
            InterlockedIncrement((LONG volatile*)&verifiedFiles);
        } else {
            InterlockedIncrement((LONG volatile*)&failedVerifyFiles);
            int idx = InterlockedIncrement((LONG volatile*)&g_failCount) - 1;
            if (idx < MAX_FAIL_LIST) {
                wcscpy_s(g_failList[idx], MAX_PATH, cur->src);
                g_failErrors[idx] = 999;
            }
        }
        InterlockedAdd64((LONG64 volatile*)&verifiedBytes, fsize);
        free(cur);
    }
    rpmalloc_thread_finalize();
    return 0;
}

BOOL VerifyFileCopy(const wchar_t* src, const wchar_t* dst) {
    WIN32_FILE_ATTRIBUTE_DATA srcAttr, dstAttr;
    if (!GetFileAttributesExW(src, GetFileExInfoStandard, &srcAttr)) return FALSE;
    if (!GetFileAttributesExW(dst, GetFileExInfoStandard, &dstAttr)) return FALSE;
    if (srcAttr.nFileSizeHigh != dstAttr.nFileSizeHigh || srcAttr.nFileSizeLow != dstAttr.nFileSizeLow) {
        return FALSE;
    }
    LARGE_INTEGER fs;
    fs.HighPart = srcAttr.nFileSizeHigh;
    fs.LowPart = srcAttr.nFileSizeLow;
    if (fs.QuadPart == 0) return TRUE;

    HANDLE hS = INVALID_HANDLE_VALUE;
    HANDLE hD = INVALID_HANDLE_VALUE;
    int retry = 0;
    while (retry < 100) {
        hS = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hS != INVALID_HANDLE_VALUE) break;
        Sleep(20);
        retry++;
    }
    if (hS == INVALID_HANDLE_VALUE) return FALSE;

    retry = 0;
    while (retry < 100) {
        hD = CreateFileW(dst, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hD != INVALID_HANDLE_VALUE) break;
        Sleep(20);
        retry++;
    }
    if (hD == INVALID_HANDLE_VALUE) {
        CloseHandle(hS);
        return FALSE;
    }

    const DWORD bufSize = 1024 * 1024;
    char* bufS = (char*)malloc(bufSize);
    char* bufD = (char*)malloc(bufSize);
    if (!bufS || !bufD) {
        free(bufS); free(bufD);
        CloseHandle(hS); CloseHandle(hD);
        return FALSE;
    }
    BOOL ok = TRUE;
    while (1) {
        DWORD brS = 0, brD = 0;
        if (!ReadFile(hS, bufS, bufSize, &brS, NULL)) { ok = FALSE; break; }
        if (!ReadFile(hD, bufD, bufSize, &brD, NULL)) { ok = FALSE; break; }
        if (brS != brD) { ok = FALSE; break; }
        if (brS == 0) break;
        if (memcmp(bufS, bufD, brS) != 0) { ok = FALSE; break; }
        if (stopRequested) { ok = FALSE; break; }
    }
    free(bufS); free(bufD);
    CloseHandle(hS); CloseHandle(hD);
    return ok;
}

void CreateDirectoriesRecursive(const wchar_t* path) {
    wchar_t temp[MAX_PATH];
    wcscpy(temp, path);
    size_t len = wcslen(temp);
    if (len == 0) return;
    if (temp[len-1] == L'\\') temp[len-1] = L'\0';
    if (DirCache_Exists(temp)) return;
    if (GetFileAttributesW(temp) != INVALID_FILE_ATTRIBUTES) {
        DirCache_Add(temp);
        return;
    }
    wchar_t* p = wcsrchr(temp, L'\\');
    if (p) { *p = L'\0'; CreateDirectoriesRecursive(temp); *p = L'\\'; }
    CreateDirectoryW(temp, NULL);
    DirCache_Add(temp);
}

int CheckImpossibleTask(const wchar_t* src, const wchar_t* dst) {
    wchar_t srcFull[MAX_PATH], dstFull[MAX_PATH];
    GetFullPathNameW(src, MAX_PATH, srcFull, NULL);
    GetFullPathNameW(dst, MAX_PATH, dstFull, NULL);
    size_t srcLen = wcslen(srcFull);
    size_t dstLen = wcslen(dstFull);
    if (_wcsnicmp(srcFull, dstFull, srcLen) == 0) {
        if (dstLen > srcLen && dstFull[srcLen] == L'\\') return 1;
    }
    if (_wcsnicmp(dstFull, srcFull, dstLen) == 0) {
        if (srcLen > dstLen && srcFull[dstLen] == L'\\') return 1;
    }
    if (_wcsicmp(srcFull, dstFull) == 0) return 1;
    return 0;
}

BOOL CopyCore_Init(void) {
    rpmalloc_initialize(0);
    if (!g_csInited) {
        InitializeCriticalSectionAndSpinCount(&cs, 4000);
        g_csInited = TRUE;
    }
    isCopying = 0; isPaused = 0; stopRequested = 0;
    StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("status_ready"));
    return TRUE;
}

void CopyCore_Shutdown(void) {
    if (hCopyThread || isCopying) {
        EnterCriticalSection(&cs);
        stopRequested = 1; isPaused = 0;
        LeaveCriticalSection(&cs);
        if (hStopEvent) SetEvent(hStopEvent);
        if (hScanDoneEvent) SetEvent(hScanDoneEvent);
        if (hWakeEvent) { for (int i = 0; i < MAX_WORKER_THREADS; i++) SetEvent(hWakeEvent); }
        if (g_hIOCP) {
            for (int i = 0; i < MAX_WORKER_THREADS; i++) {
                PostQueuedCompletionStatus(g_hIOCP, 0, 999, NULL);
            }
        }
        for (int i = 0; i < g_workerHandleCount; i++) {
            if (g_workerHandles[i]) CancelSynchronousIo(g_workerHandles[i]);
        }
        if (hScanThread) CancelSynchronousIo(hScanThread);
        if (hCopyThread) {
            WaitForSingleObject(hCopyThread, INFINITE);
            CloseHandle(hCopyThread);
            hCopyThread = NULL;
        }
    }
    ClearQueue();
    ClearVerifyQueue();
    if (g_csInited) {
        DeleteCriticalSection(&cs);
        g_csInited = FALSE;
    }
    rpmalloc_finalize();
}

BOOL CopyCore_Start(const wchar_t* src, const wchar_t* dst) {
    if (isCopying) return FALSE;
    if (!src || !dst || !*src || !*dst || CheckImpossibleTask(src, dst)) {
        StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("dialog_error_unable"));
        return FALSE;
    }
    DWORD sa = GetFileAttributesW(src);
    DWORD da = GetFileAttributesW(dst);
    if (sa == INVALID_FILE_ATTRIBUTES || !(sa & FILE_ATTRIBUTE_DIRECTORY)) {
        StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("dialog_error_unable"));
        return FALSE;
    }
    if (da == INVALID_FILE_ATTRIBUTES) {
        StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("dialog_error_unable"));
        return FALSE;
    }
    if (!g_csInited) CopyCore_Init();
    if (hCopyThread) { CloseHandle(hCopyThread); hCopyThread = NULL; }

    StringCchCopyW(srcPath, ARRAYSIZE(srcPath), src);
    StringCchCopyW(dstPath, ARRAYSIZE(dstPath), dst);

    progressValue = 0; lastProgress = 0; isCopying = 1; isPaused = 0; stopRequested = 0;
    scanDone = 0;
    currentSpeedMB = 0.0; lastSpeedTime = 0; lastSpeedBytes = 0;
    StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("status_scanning"));

    wchar_t* ps = (wchar_t*)malloc(MAX_PATH * 2 * sizeof(wchar_t));
    if (!ps) {
        isCopying = 0;
        StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("dialog_error_memory"));
        return FALSE;
    }
    wchar_t* pd = ps + MAX_PATH;
    wcscpy(ps, src); wcscpy(pd, dst);
    hCopyThread = CreateThread(NULL, 0, CopyThreadProc, (LPVOID)ps, 0, NULL);
    if (!hCopyThread) {
        free(ps);
        isCopying = 0;
        StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("dialog_error_thread"));
        return FALSE;
    }
    return TRUE;
}

void CopyCore_Pause(void) {
    if (!isCopying) return;
    EnterCriticalSection(&cs);
    isPaused = 1;
    LeaveCriticalSection(&cs);
    StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("status_paused"));
}

void CopyCore_Resume(void) {
    if (!isCopying) return;
    EnterCriticalSection(&cs);
    isPaused = 0;
    LeaveCriticalSection(&cs);
}

void CopyCore_Stop(void) {
    if (!isCopying) return;
    EnterCriticalSection(&cs);
    stopRequested = 1; isPaused = 0;
    LeaveCriticalSection(&cs);
    if (hStopEvent) SetEvent(hStopEvent);
    if (hScanDoneEvent) SetEvent(hScanDoneEvent);
    if (hWakeEvent) { for (int i = 0; i < MAX_WORKER_THREADS; i++) SetEvent(hWakeEvent); }
    if (g_hIOCP) {
        for (int i = 0; i < MAX_WORKER_THREADS; i++) {
            PostQueuedCompletionStatus(g_hIOCP, 0, 999, NULL);
        }
    }
    for (int i = 0; i < g_workerHandleCount; i++) {
        if (g_workerHandles[i]) CancelSynchronousIo(g_workerHandles[i]);
    }
    if (hScanThread) CancelSynchronousIo(hScanThread);
    StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("status_terminating"));
}

void CopyCore_TogglePause(void) {
    if (!isCopying) return;
    EnterCriticalSection(&cs);
    isPaused = !isPaused;
    int nowPaused = isPaused;
    LeaveCriticalSection(&cs);
    if (nowPaused) StringCchCopyW(statusText, ARRAYSIZE(statusText), GetText("status_paused"));
}

void CopyCore_GetStatus(
    int* out_isCopying, int* out_isPaused, int* out_progressPct, double* out_speedMB,
    long long* out_copiedBytes, long long* out_totalBytes,
    int* out_copiedFiles, int* out_totalFiles, int* out_failedFiles,
    int* out_filesScanned, int* out_scanDone,
    wchar_t* out_statusText, int out_statusBufLen)
{
    if (isCopying) {
        ULONGLONG now = GetTickCount64();
        long long curBytes = isVerifying ? verifiedBytes : copiedBytes;
        if (lastSpeedTime == 0) { lastSpeedTime = now; lastSpeedBytes = curBytes; }
        if (now - lastSpeedTime >= 1000) {
            double deltaMB = (double)(curBytes - lastSpeedBytes) / (1024.0 * 1024.0);
            double deltaSec = (double)(now - lastSpeedTime) / 1000.0;
            double inst = deltaSec > 0 ? deltaMB / deltaSec : 0.0;
            currentSpeedMB = (currentSpeedMB <= 0.0) ? inst : (currentSpeedMB * 0.6 + inst * 0.4);
            lastSpeedTime = now;
            lastSpeedBytes = curBytes;
        }
        if (!scanDone) {
            progressValue = 0;
            StringCchPrintfW(statusText, ARRAYSIZE(statusText), GetText("scanning_files"), filesScanned);
        } else {
            long long progressBytes = isVerifying ? verifiedBytes : copiedBytes;
            if (totalBytes > 0) {
                int pct = (int)((progressBytes * 100) / totalBytes);
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
                if (isVerifying) progressValue = pct;
                else {
                    if (pct < lastProgress) pct = lastProgress;
                    else lastProgress = pct;
                    progressValue = pct;
                }
            }
            const wchar_t* status = isVerifying ? GetText("status_verifying") : GetText("status_copying");
            wchar_t remainStr[32] = L"";
            if (currentSpeedMB > 0.1 && totalBytes > progressBytes) {
                double remainBytes = (double)(totalBytes - progressBytes);
                double remainSec = remainBytes / (currentSpeedMB * 1024.0 * 1024.0);
                if (remainSec < 3600) {
                    int mins = (int)(remainSec / 60);
                    int secs = (int)(remainSec - mins * 60);
                    StringCchPrintfW(remainStr, ARRAYSIZE(remainStr), L"  %s %d:%02d", GetText("remaining_time"), mins, secs);
                } else {
                    int hours = (int)(remainSec / 3600);
                    int mins = (int)((remainSec - hours * 3600) / 60);
                    StringCchPrintfW(remainStr, ARRAYSIZE(remainStr), L"  %s %d:%02d:%02d", GetText("remaining_time"), hours, mins, (int)(remainSec - hours*3600 - mins*60));
                }
            }
            StringCchPrintfW(statusText, ARRAYSIZE(statusText), L"%s %d%%  %.1f %s%s",
                status, progressValue, currentSpeedMB, GetText("speed_unit"), remainStr);
        }
    }

    if (out_isCopying)    *out_isCopying = isCopying;
    if (out_isPaused)     *out_isPaused = isPaused ? 1 : 0;
    if (out_progressPct)  *out_progressPct = progressValue;
    if (out_speedMB)      *out_speedMB = currentSpeedMB;
    if (out_copiedBytes)  *out_copiedBytes = isVerifying ? verifiedBytes : copiedBytes;
    if (out_totalBytes)   *out_totalBytes = totalBytes;
    if (out_copiedFiles)  *out_copiedFiles = isVerifying ? verifiedFiles : copiedFiles;
    if (out_totalFiles)   *out_totalFiles = totalFiles;
    if (out_failedFiles)  *out_failedFiles = failedFiles;
    if (out_filesScanned) *out_filesScanned = filesScanned;
    if (out_scanDone)     *out_scanDone = scanDone ? 1 : 0;
    if (out_statusText && out_statusBufLen > 0) {
        StringCchCopyW(out_statusText, (size_t)out_statusBufLen, statusText);
    }
}

int CopyCore_GetFailList(wchar_t* out_buf, int bufLen) {
    if (!out_buf || bufLen <= 0) return 0;
    out_buf[0] = L'\0';
    int used = 0;

    for (int i = 0; i < g_failCount && i < MAX_FAIL_LIST; i++) {
        wchar_t line[MAX_PATH + 32];
        int n = StringCchPrintfW(line, ARRAYSIZE(line), L"%lu|%s\n",
                                 (unsigned long)g_failErrors[i], g_failList[i]);
        if (n < 0) continue;
        if (used + n + 1 >= bufLen) break;
        wcscpy_s(out_buf + used, (size_t)(bufLen - used), line);
        used += n;
    }
    return used;
}

void CopyCore_SetSpeedLimit(int kbPerSec) {
    if (kbPerSec < 0) kbPerSec = 0;
    if (kbPerSec > 10 * 1024 * 1024) kbPerSec = 10 * 1024 * 1024;
    speedLimitKB = kbPerSec;
}
void CopyCore_SetVerify(int enable) { enableVerify = enable ? 1 : 0; }
void CopyCore_SetMode(int mode) { if (mode >= 0 && mode <= MODE_EXTREME) currentMode = mode; }
void CopyCore_SetStrategy(int strategy) { if (strategy >= 0 && strategy <= STRATEGY_LARGE) g_strategy = strategy; }
void CopyCore_SetLanguage(int lang) { if (lang >= 0 && lang < 5) currentLang = lang; }
void CopyCore_SetSrcPath(const wchar_t* src) {
    if (!src) return;
    if (g_csInited) EnterCriticalSection(&cs);
    StringCchCopyW(srcPath, ARRAYSIZE(srcPath), src);
    if (g_csInited) LeaveCriticalSection(&cs);
}
void CopyCore_SetDstPath(const wchar_t* dst) {
    if (!dst) return;
    if (g_csInited) EnterCriticalSection(&cs);
    StringCchCopyW(dstPath, ARRAYSIZE(dstPath), dst);
    if (g_csInited) LeaveCriticalSection(&cs);
}

int  CopyCore_GetSpeedLimit(void) { return speedLimitKB; }
int  CopyCore_GetVerify(void) { return enableVerify; }
int  CopyCore_GetMode(void) { return currentMode; }
int  CopyCore_GetStrategy(void) { return g_strategy; }
int  CopyCore_GetLanguage(void) { return currentLang; }
void CopyCore_GetSrcPath(wchar_t* buf, int len) {
    if (buf && len > 0) StringCchCopyW(buf, (size_t)len, srcPath);
}
void CopyCore_GetDstPath(wchar_t* buf, int len) {
    if (buf && len > 0) StringCchCopyW(buf, (size_t)len, dstPath);
}

// 诊断用：报告本机源/目标设备的类型，以及复制时将要套用的参数（不发起复制）
extern "C" __declspec(dllexport) void CopyCore_DescribeTargets(const wchar_t* src, const wchar_t* dst,
                                         wchar_t* out, int outLen) {
    if (!out || outLen <= 0) return;
    out[0] = L'\0';

    wchar_t sbDesc[16] = L"OTHER", dbDesc[16] = L"OTHER";
    BusKind sb = BUS_OTHER, db = BUS_OTHER;
    DiskType st = DISK_UNKNOWN, dt = DISK_UNKNOWN;
    BOOL sameDisk = FALSE;

    if (src && src[0]) { st = DetectDriveType(src); sb = DetectBusKind(src, sbDesc, 16); }
    if (dst && dst[0]) { dt = DetectDriveType(dst); db = DetectBusKind(dst, dbDesc, 16); }

    if (src && dst && src[0] && dst[0]) {
        wchar_t v1[8] = {0}, v2[8] = {0};
        v1[0] = src[0]; v1[1] = L':'; v1[2] = L'\\'; v2[0] = dst[0]; v2[1] = L':'; v2[2] = L'\\';
        DWORD s1 = 0, s2 = 0;
        GetVolumeInformationW(v1, NULL, 0, &s1, NULL, NULL, NULL, 0);
        GetVolumeInformationW(v2, NULL, 0, &s2, NULL, NULL, NULL, 0);
        sameDisk = (s1 != 0 && s1 == s2) ? TRUE : FALSE;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    AutoProfile p = ComputeProfile(st, dt, sb, db, sameDisk, (int)si.dwNumberOfProcessors);

    // 与 CopyThreadProc 的收敛逻辑保持一致，报告的是真正会用的线程数
    int effective = g_workerThreads > 0 ? g_workerThreads : (int)si.dwNumberOfProcessors * 2;
    if (effective > MAX_WORKER_THREADS) effective = MAX_WORKER_THREADS;
    if (currentMode != MODE_EXTREME && effective > p.threads) effective = p.threads;
    p.threads = effective;

    LoadTuningOnce();
    if (g_envMinMB)  p.minMB   = g_tuneParMinMB;
    if (g_envChunk)  p.chunkKB = g_tuneChunkKB;
    if (g_envQD)     p.qd      = g_tuneQD;
    if (g_envMaxPar) p.maxPar  = g_tuneMaxPar;

    const wchar_t* smed = L"UNKNOWN";
    if (st == DISK_HDD) smed = L"HDD";
    else if (st == DISK_SSD) smed = L"SSD";
    else if (st == DISK_USB) smed = L"USB";
    else if (st == DISK_NETWORK) smed = L"NET";
    const wchar_t* dmed = L"UNKNOWN";
    if (dt == DISK_HDD) dmed = L"HDD";
    else if (dt == DISK_SSD) dmed = L"SSD";
    else if (dt == DISK_USB) dmed = L"USB";
    else if (dt == DISK_NETWORK) dmed = L"NET";

    StringCchPrintfW(out, (size_t)outLen,
        L"src=%c: bus=%ls media=%ls | dst=%c: bus=%ls media=%ls | sameDisk=%d | "
        L"threads=%d parallel=%ls chunkKB=%d qd=%d maxPar=%d minMB=%d",
        (src && src[0]) ? src[0] : L'?', sbDesc, smed,
        (dst && dst[0]) ? dst[0] : L'?', dbDesc, dmed,
        sameDisk ? 1 : 0,
        p.threads, p.parallel ? L"on" : L"off",
        p.chunkKB, p.qd, p.maxPar, p.minMB);
}