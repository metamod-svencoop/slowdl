#pragma once

#include <GoldSrcExt/download_ext.h>

#include <memory>

enum class SlowDLBackendKind
{
    kUnavailable = 0,
    kHostNative,
    kLegacyGamedata,
};

enum class SlowDLSymbolKind
{
    kFunction = 0,
    kGlobal,
};

enum class SlowDLSymbolStatus
{
    kOk = 0,
    kSymbolNotFound,
    kOtherError,
};

// Host operations injected into CSlowDLDownloadBridge so backend resolution
// and legacy gamedata hooking can be tested deterministically without a live
// Metamod runtime.
struct SlowDLHostOps
{
    void* context = nullptr;

    void* (*getEngineHandle)(void* ctx)                                          = nullptr;
    void* (*getEngineBase)(void* ctx)                                            = nullptr;
    void* (*getProcAddress)(void* ctx, void* moduleHandle, const char* procName) = nullptr;
    SlowDLSymbolStatus (*resolveGameSymbol)(void*            ctx,
                                            void*            moduleBase,
                                            const char*      symbolName,
                                            SlowDLSymbolKind kind,
                                            void**           outAddress,
                                            const char**     outStatusText)      = nullptr;
    void* (*inlineHook)(void*  ctx,
                        void*  target,
                        void*  detour,
                        void** outOriginal,
                        bool   deferTransaction)                                 = nullptr;
    bool (*unHook)(void* ctx, void* hookHandle)                                  = nullptr;
    void (*logError)(void* ctx, const char* message)                             = nullptr;
    void (*logMessage)(void* ctx, const char* message)                           = nullptr;
};

namespace slowdl_bridge_detail
{
class CLegacyDownloadExt;
}

// Selects and manages SlowDL's IGoldSrcDownloadExt backend:
// 1. Host-native interface via engine CreateInterface("GoldSrcServerExt_Download_001")
// 2. Legacy 32-bit GoldSrc fallback via gamedata + pfnInlineHook + CHookChainHost
// 3. Unavailable (caller refuses plugin load)
class CSlowDLDownloadBridge
{
public:
    CSlowDLDownloadBridge();
    ~CSlowDLDownloadBridge();

    CSlowDLDownloadBridge(const CSlowDLDownloadBridge&)            = delete;
    CSlowDLDownloadBridge& operator=(const CSlowDLDownloadBridge&) = delete;

    bool Initialize(const SlowDLHostOps& hostOps, bool deferInlineHookTransaction);
    bool Shutdown();

    goldsrcext::IGoldSrcDownloadExt* GetInterface() const noexcept;
    SlowDLBackendKind                GetBackendKind() const noexcept;
    const char*                      GetBackendName() const noexcept;

private:
    SlowDLBackendKind                                         m_backendKind  = SlowDLBackendKind::kUnavailable;
    goldsrcext::IGoldSrcDownloadExt*                          m_pDownloadExt = nullptr;
    std::unique_ptr<slowdl_bridge_detail::CLegacyDownloadExt> m_legacyExt;
};
