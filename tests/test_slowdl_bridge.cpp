#include "bridge/slowdl_bridge.h"

#include <GoldSrcExt/hook_chain_host.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#define CHECK(condition)                                   \
    do                                                     \
    {                                                      \
        if (!(condition))                                  \
        {                                                  \
            std::cerr << __LINE__ << ": " #condition "\n"; \
            return 1;                                      \
        }                                                  \
    } while (0)

namespace
{

using goldsrcext::CHookChainHost;
using goldsrcext::CommandSource;
using goldsrcext::GoldSrcOpaqueClient;
using goldsrcext::HookHandle;
using goldsrcext::IGoldSrcDownloadExt;
using goldsrcext::IHookChain;
using goldsrcext::kAltitudeHigh;
using goldsrcext::kAltitudeNormal;
using goldsrcext::kGoldSrcDownloadExtInterfaceName;
using goldsrcext::kInvalidHookHandle;

// Fake native engine IGoldSrcDownloadExt
class CFakeNativeDownloadExt final : public IGoldSrcDownloadExt
{
public:
    HookHandle RegisterBeginFileDownloadHook(
        goldsrcext::HookCallbackFn<void> callback,
        void*                            userData,
        std::int32_t                     altitude) noexcept override
    {
        return m_host.Register(callback, userData, altitude);
    }

    bool UnregisterBeginFileDownloadHook(HookHandle handle) noexcept override
    {
        return m_host.Unregister(handle);
    }

    CommandSource GetCommandSource() const noexcept override
    {
        return CommandSource::kClient;
    }

    GoldSrcOpaqueClient* GetHostClient() const noexcept override
    {
        return nullptr;
    }

    edict_s* GetServerPlayerEdict() const noexcept override
    {
        return nullptr;
    }

    void DropClient(GoldSrcOpaqueClient* /*client*/, bool /*crash*/, const char* /*reason*/) noexcept override
    {
    }

private:
    CHookChainHost<void> m_host;
};

// Fake legacy engine globals and functions
int         g_fakeOriginalBeginCalls = 0;
void*       g_fakeHostClient         = nullptr;
int         g_fakeCmdSource          = 0;
edict_s*    g_fakeSvPlayer           = nullptr;
void*       g_lastDroppedClient      = nullptr;
int         g_lastDropCrash          = -1;
std::string g_lastDropFormat;
std::string g_lastDropFormatted;
int         g_dropCallCount = 0;

void FakeOriginalBeginFileDownload()
{
    ++g_fakeOriginalBeginCalls;
}

void FakeSV_DropClient(void* cl, int crash, char* fmt, ...)
{
    ++g_dropCallCount;
    g_lastDroppedClient = cl;
    g_lastDropCrash     = crash;
    g_lastDropFormat    = fmt ? fmt : "";

    char    buffer[512] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    g_lastDropFormatted = buffer;
}

// Active global CreateInterface response for mock engine
IGoldSrcDownloadExt* g_mockNativeInterface = nullptr;
int                  g_mockCreateIfaceCode = 1;

void* FakeCreateInterface(const char* name, int* returnCode)
{
    if (name && std::strcmp(name, kGoldSrcDownloadExtInterfaceName) == 0 && g_mockNativeInterface)
    {
        if (returnCode)
            *returnCode = g_mockCreateIfaceCode;
        return g_mockNativeInterface;
    }
    if (returnCode)
        *returnCode = 1;
    return nullptr;
}

struct FakeHostState
{
    void* engineHandle                                   = nullptr;
    void* engineBase                                     = nullptr;
    bool  exposeCreateInterface                          = false;
    bool  failInlineHook                                 = false;
    bool  lastDeferTransaction                           = false;
    int   resolveCalls                                   = 0;
    int   inlineHookCalls                                = 0;
    int   unHookCalls                                    = 0;
    void (*installedDetour)()                            = nullptr;
    int                                    fakeHookToken = 1;
    std::unordered_map<std::string, void*> symbols;
    std::vector<std::string>               errors;
};

SlowDLHostOps MakeFakeHostOps(FakeHostState* state)
{
    SlowDLHostOps ops;
    ops.context         = state;
    ops.getEngineHandle = [](void* ctx) -> void* {
        return static_cast<FakeHostState*>(ctx)->engineHandle;
    };
    ops.getEngineBase = [](void* ctx) -> void* {
        return static_cast<FakeHostState*>(ctx)->engineBase;
    };
    ops.getProcAddress = [](void* ctx, void* moduleHandle, const char* procName) -> void* {
        auto* s = static_cast<FakeHostState*>(ctx);
        if (s->exposeCreateInterface && moduleHandle == s->engineHandle && procName && std::strcmp(procName, "CreateInterface") == 0)
            return reinterpret_cast<void*>(&FakeCreateInterface);
        return nullptr;
    };
    ops.resolveGameSymbol = [](void*       ctx,
                               void*       moduleBase,
                               const char* symbolName,
                               SlowDLSymbolKind /*kind*/,
                               void**       outAddress,
                               const char** outStatusText) -> SlowDLSymbolStatus {
        auto* s = static_cast<FakeHostState*>(ctx);
        ++s->resolveCalls;
        if (moduleBase != s->engineBase || !symbolName)
        {
            if (outStatusText)
                *outStatusText = "invalid module base";
            return SlowDLSymbolStatus::kOtherError;
        }
        auto it = s->symbols.find(symbolName);
        if (it == s->symbols.end())
        {
            if (outStatusText)
                *outStatusText = "symbol not found";
            return SlowDLSymbolStatus::kSymbolNotFound;
        }
        *outAddress = it->second;
        if (outStatusText)
            *outStatusText = "ok";
        return SlowDLSymbolStatus::kOk;
    };
    ops.inlineHook = [](void* ctx, void* target, void* detour, void** outOriginal, bool deferTransaction) -> void* {
        auto* s = static_cast<FakeHostState*>(ctx);
        ++s->inlineHookCalls;
        s->lastDeferTransaction = deferTransaction;
        if (s->failInlineHook)
            return nullptr;

        *outOriginal       = target;
        s->installedDetour = reinterpret_cast<void (*)()>(detour);
        return &s->fakeHookToken;
    };
    ops.unHook = [](void* ctx, void* hookHandle) -> bool {
        auto* s = static_cast<FakeHostState*>(ctx);
        ++s->unHookCalls;
        if (hookHandle != &s->fakeHookToken)
            return false;
        s->installedDetour = nullptr;
        return true;
    };
    ops.logError = [](void* ctx, const char* message) {
        auto* s = static_cast<FakeHostState*>(ctx);
        if (message)
            s->errors.emplace_back(message);
    };
    return ops;
}

int TestHostNativePriority()
{
    CFakeNativeDownloadExt nativeExt;
    g_mockNativeInterface = &nativeExt;
    g_mockCreateIfaceCode = 0;

    int           dummyHandle = 1;
    int           dummyBase   = 2;
    FakeHostState host;
    host.engineHandle          = &dummyHandle;
    host.engineBase            = &dummyBase;
    host.exposeCreateInterface = true;

    CSlowDLDownloadBridge bridge;
    CHECK(bridge.Initialize(MakeFakeHostOps(&host), true));
    CHECK(bridge.GetBackendKind() == SlowDLBackendKind::kHostNative);
    CHECK(std::strcmp(bridge.GetBackendName(), "host-native") == 0);
    CHECK(bridge.GetInterface() == &nativeExt);
    CHECK(host.resolveCalls == 0);
    CHECK(host.inlineHookCalls == 0);

    CHECK(bridge.Shutdown());
    CHECK(host.unHookCalls == 0);
    CHECK(bridge.GetBackendKind() == SlowDLBackendKind::kUnavailable);
    CHECK(bridge.GetInterface() == nullptr);

    g_mockNativeInterface = nullptr;
    return 0;
}

int TestLegacyGamedataFallbackAndDispatch()
{
    // Native CreateInterface exists on engine, but does not expose GoldSrcServerExt_Download_001.
    g_mockNativeInterface = nullptr;

    int           dummyHandle = 1;
    int           dummyBase   = 2;
    FakeHostState host;
    host.engineHandle          = &dummyHandle;
    host.engineBase            = &dummyBase;
    host.exposeCreateInterface = true;
    // Register mangled Linux alternative names for functions to verify fallback lookup too.
    host.symbols["_Z22SV_BeginFileDownload_fv"]     = reinterpret_cast<void*>(&FakeOriginalBeginFileDownload);
    host.symbols["_Z13SV_DropClientP8client_siPcz"] = reinterpret_cast<void*>(&FakeSV_DropClient);
    host.symbols["host_client"]                     = &g_fakeHostClient;
    host.symbols["cmd_source"]                      = &g_fakeCmdSource;
    host.symbols["sv_player"]                       = &g_fakeSvPlayer;

    CSlowDLDownloadBridge bridge;
    CHECK(bridge.Initialize(MakeFakeHostOps(&host), true));
    CHECK(bridge.GetBackendKind() == SlowDLBackendKind::kLegacyGamedata);
    CHECK(std::strcmp(bridge.GetBackendName(), "legacy-gamedata") == 0);
    CHECK(bridge.GetInterface() != nullptr);
    CHECK(host.inlineHookCalls == 1);
    CHECK(host.lastDeferTransaction);
    CHECK(host.installedDetour != nullptr);

    IGoldSrcDownloadExt* ext = bridge.GetInterface();

    // Context getters
    int fakeClientStorage = 77;
    int fakeEdictStorage  = 88;
    g_fakeCmdSource       = 0;
    g_fakeHostClient      = &fakeClientStorage;
    g_fakeSvPlayer        = reinterpret_cast<edict_s*>(&fakeEdictStorage);

    CHECK(ext->GetCommandSource() == CommandSource::kClient);
    CHECK(ext->GetHostClient() == reinterpret_cast<GoldSrcOpaqueClient*>(&fakeClientStorage));
    CHECK(ext->GetServerPlayerEdict() == reinterpret_cast<edict_s*>(&fakeEdictStorage));

    g_fakeCmdSource  = 1;
    g_fakeHostClient = nullptr;
    CHECK(ext->GetCommandSource() == CommandSource::kCommand);
    CHECK(ext->GetHostClient() == nullptr);

    // Format-string safety on DropClient
    g_dropCallCount = 0;
    ext->DropClient(reinterpret_cast<GoldSrcOpaqueClient*>(&fakeClientStorage), false, "reason with %s %n literal");
    CHECK(g_dropCallCount == 1);
    CHECK(g_lastDroppedClient == &fakeClientStorage);
    CHECK(g_lastDropCrash == 0);
    CHECK(g_lastDropFormat == "%s");
    CHECK(g_lastDropFormatted == "reason with %s %n literal");

    // HookChain ordering & suppression through the legacy bridge thunk
    struct DispatchTrace
    {
        std::vector<std::string> events;
        bool                     suppress = false;
    } trace;

    const HookHandle hHigh = ext->RegisterBeginFileDownloadHook(
        [](IHookChain<void>* chain, void* userData) noexcept {
            auto* t = static_cast<DispatchTrace*>(userData);
            t->events.emplace_back("high");
            chain->CallNext();
        },
        &trace,
        kAltitudeHigh);

    const HookHandle hNormal = ext->RegisterBeginFileDownloadHook(
        [](IHookChain<void>* chain, void* userData) noexcept {
            auto* t = static_cast<DispatchTrace*>(userData);
            t->events.emplace_back("normal");
            if (!t->suppress)
                chain->CallNext();
        },
        &trace,
        kAltitudeNormal);

    CHECK(hHigh != kInvalidHookHandle);
    CHECK(hNormal != kInvalidHookHandle);

    g_fakeOriginalBeginCalls = 0;
    host.installedDetour();
    CHECK(g_fakeOriginalBeginCalls == 1);
    CHECK((trace.events == std::vector<std::string>{"high", "normal"}));

    trace.events.clear();
    trace.suppress = true;
    host.installedDetour();
    CHECK(g_fakeOriginalBeginCalls == 1);
    CHECK((trace.events == std::vector<std::string>{"high", "normal"}));

    CHECK(ext->UnregisterBeginFileDownloadHook(hHigh));
    CHECK(ext->UnregisterBeginFileDownloadHook(hNormal));
    CHECK(bridge.Shutdown());
    CHECK(host.unHookCalls == 1);
    CHECK(host.installedDetour == nullptr);
    CHECK(bridge.GetBackendKind() == SlowDLBackendKind::kUnavailable);

    return 0;
}

int TestUnavailableRejections()
{
    g_mockNativeInterface = nullptr;

    // 1. Missing engine base
    {
        FakeHostState         host;
        CSlowDLDownloadBridge bridge;
        CHECK(!bridge.Initialize(MakeFakeHostOps(&host), false));
        CHECK(bridge.GetBackendKind() == SlowDLBackendKind::kUnavailable);
        CHECK(bridge.GetInterface() == nullptr);
        CHECK(!host.errors.empty());
    }

    // 2. Missing required gamedata symbol (sv_player)
    {
        int           dummyBase = 2;
        FakeHostState host;
        host.engineBase                        = &dummyBase;
        host.symbols["SV_BeginFileDownload_f"] = reinterpret_cast<void*>(&FakeOriginalBeginFileDownload);
        host.symbols["SV_DropClient"]          = reinterpret_cast<void*>(&FakeSV_DropClient);
        host.symbols["host_client"]            = &g_fakeHostClient;
        host.symbols["cmd_source"]             = &g_fakeCmdSource;

        CSlowDLDownloadBridge bridge;
        CHECK(!bridge.Initialize(MakeFakeHostOps(&host), false));
        CHECK(bridge.GetBackendKind() == SlowDLBackendKind::kUnavailable);
        CHECK(host.inlineHookCalls == 0);
        CHECK(!host.errors.empty());
        CHECK(host.errors.back().find("sv_player") != std::string::npos);
    }

    // 3. Inline hook installation failure cleans up singleton guard cleanly
    {
        int           dummyBase = 2;
        FakeHostState host;
        host.engineBase                        = &dummyBase;
        host.failInlineHook                    = true;
        host.symbols["SV_BeginFileDownload_f"] = reinterpret_cast<void*>(&FakeOriginalBeginFileDownload);
        host.symbols["SV_DropClient"]          = reinterpret_cast<void*>(&FakeSV_DropClient);
        host.symbols["host_client"]            = &g_fakeHostClient;
        host.symbols["cmd_source"]             = &g_fakeCmdSource;
        host.symbols["sv_player"]              = &g_fakeSvPlayer;

        CSlowDLDownloadBridge bridge;
        CHECK(!bridge.Initialize(MakeFakeHostOps(&host), false));
        CHECK(bridge.GetBackendKind() == SlowDLBackendKind::kUnavailable);
        CHECK(host.inlineHookCalls == 1);

        // Re-attempting with working inlineHook must succeed (singleton was released on failure).
        host.failInlineHook = false;
        CHECK(bridge.Initialize(MakeFakeHostOps(&host), false));
        CHECK(bridge.GetBackendKind() == SlowDLBackendKind::kLegacyGamedata);
        CHECK(bridge.Shutdown());
    }

    return 0;
}

} // namespace

int main()
{
    if (TestHostNativePriority() != 0)
        return 1;
    if (TestLegacyGamedataFallbackAndDispatch() != 0)
        return 1;
    if (TestUnavailableRejections() != 0)
        return 1;

    std::cout << "All SlowDL bridge tests passed.\n";
    return 0;
}
