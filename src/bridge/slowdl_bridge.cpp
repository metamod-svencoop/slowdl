#include "slowdl_bridge.h"

#include <GoldSrcExt/hook_chain_host.h>

#include <cstdio>

#include "gamesymbols.h"
#include "legacy_enginedef.h"

namespace slowdl_bridge_detail
{

namespace
{

using CreateInterfaceRawFn = void* (*)(const char* name, int* returnCode);

void LogErrorFmt(const SlowDLHostOps& hostOps, const char* format, const char* arg1, const char* arg2)
{
    if (!hostOps.logError)
        return;

    char buffer[512] = {};
    std::snprintf(buffer, sizeof(buffer), format, arg1 ? arg1 : "", arg2 ? arg2 : "");
    hostOps.logError(hostOps.context, buffer);
}

void LogErrorText(const SlowDLHostOps& hostOps, const char* text)
{
    if (hostOps.logError && text)
        hostOps.logError(hostOps.context, text);
}

bool ResolveSymbolWithFallback(const SlowDLHostOps& hostOps,
                               void*                engineBase,
                               const char*          symbolName,
                               const char*          alternativeSymbolName,
                               SlowDLSymbolKind     kind,
                               void**               outAddress)
{
    const char*        statusText = nullptr;
    SlowDLSymbolStatus status     = hostOps.resolveGameSymbol(hostOps.context, engineBase, symbolName, kind, outAddress, &statusText);

    if (status == SlowDLSymbolStatus::kSymbolNotFound && alternativeSymbolName)
    {
        status = hostOps.resolveGameSymbol(hostOps.context, engineBase, alternativeSymbolName, kind, outAddress, &statusText);
    }

    if (status == SlowDLSymbolStatus::kOk && *outAddress != nullptr)
        return true;

    LogErrorFmt(hostOps, "Cannot resolve %s: %s", symbolName, statusText ? statusText : "unknown error");
    return false;
}

} // namespace

// Legacy 32-bit GoldSrc implementation of IGoldSrcDownloadExt backed by
// gamedata symbols, Metamod pfnInlineHook, and goldsrcext::CHookChainHost.
class CLegacyDownloadExt final : public goldsrcext::IGoldSrcDownloadExt
{
public:
    CLegacyDownloadExt() = default;

    ~CLegacyDownloadExt() noexcept override
    {
        Uninstall();
    }

    bool Install(const SlowDLHostOps&     hostOps,
                 FnSV_BeginFileDownload_f targetBeginDownload,
                 FnSV_DropClient          dropClientFn,
                 void**                   hostClientPtr,
                 LegacyCmdSource*         cmdSourcePtr,
                 edict_s**                serverPlayerPtr,
                 bool                     deferTransaction)
    {
        if (s_pActiveInstance != nullptr)
        {
            LogErrorText(hostOps, "Legacy download bridge is already active");
            return false;
        }

        m_hostOps        = hostOps;
        m_pfnDropClient  = dropClientFn;
        m_ppHostClient   = hostClientPtr;
        m_pCmdSource     = cmdSourcePtr;
        m_ppServerPlayer = serverPlayerPtr;

        s_pActiveInstance = this;

        FnSV_BeginFileDownload_f originalBeginDownload = nullptr;
        m_pHookHandle                                  = m_hostOps.inlineHook(
            m_hostOps.context,
            reinterpret_cast<void*>(targetBeginDownload),
            reinterpret_cast<void*>(&LegacyBeginFileDownloadThunk),
            reinterpret_cast<void**>(&originalBeginDownload),
            deferTransaction);

        if (!m_pHookHandle || !originalBeginDownload)
        {
            if (m_pHookHandle && m_hostOps.unHook)
                m_hostOps.unHook(m_hostOps.context, m_pHookHandle);

            m_pHookHandle     = nullptr;
            s_pActiveInstance = nullptr;
            LogErrorText(m_hostOps, "Unable to install the legacy UDP download inline hook");
            return false;
        }

        m_hookHost.SetOriginal(originalBeginDownload);
        return true;
    }

    bool Uninstall()
    {
        m_hookHost.Clear();

        if (!m_pHookHandle)
        {
            if (s_pActiveInstance == this)
                s_pActiveInstance = nullptr;
            return true;
        }

        if (!m_hostOps.unHook || !m_hostOps.unHook(m_hostOps.context, m_pHookHandle))
            return false;

        m_pHookHandle = nullptr;
        if (s_pActiveInstance == this)
            s_pActiveInstance = nullptr;
        return true;
    }

    goldsrcext::HookHandle RegisterBeginFileDownloadHook(
        goldsrcext::HookCallbackFn<void> callback,
        void*                            userData,
        std::int32_t                     altitude) noexcept override
    {
        return m_hookHost.Register(callback, userData, altitude);
    }

    bool UnregisterBeginFileDownloadHook(goldsrcext::HookHandle handle) noexcept override
    {
        return m_hookHost.Unregister(handle);
    }

    goldsrcext::CommandSource GetCommandSource() const noexcept override
    {
        if (!m_pCmdSource)
            return goldsrcext::CommandSource::kCommand;

        return (*m_pCmdSource == 0) ? goldsrcext::CommandSource::kClient : goldsrcext::CommandSource::kCommand;
    }

    goldsrcext::GoldSrcOpaqueClient* GetHostClient() const noexcept override
    {
        if (!m_ppHostClient || !*m_ppHostClient)
            return nullptr;

        return static_cast<goldsrcext::GoldSrcOpaqueClient*>(*m_ppHostClient);
    }

    edict_s* GetServerPlayerEdict() const noexcept override
    {
        return m_ppServerPlayer ? *m_ppServerPlayer : nullptr;
    }

    void DropClient(goldsrcext::GoldSrcOpaqueClient* client, bool crash, const char* reason) noexcept override
    {
        if (!m_pfnDropClient || !client)
            return;

        static char format[]   = "%s";
        const char* safeReason = reason ? reason : "";
        m_pfnDropClient(static_cast<void*>(client), crash ? 1 : 0, format, safeReason);
    }

private:
    static void LegacyBeginFileDownloadThunk()
    {
        if (s_pActiveInstance)
            s_pActiveInstance->m_hookHost.Invoke();
    }

    static CLegacyDownloadExt* s_pActiveInstance;

    SlowDLHostOps                    m_hostOps;
    goldsrcext::CHookChainHost<void> m_hookHost;
    void*                            m_pHookHandle    = nullptr;
    FnSV_DropClient                  m_pfnDropClient  = nullptr;
    void**                           m_ppHostClient   = nullptr;
    LegacyCmdSource*                 m_pCmdSource     = nullptr;
    edict_s**                        m_ppServerPlayer = nullptr;
};

CLegacyDownloadExt* CLegacyDownloadExt::s_pActiveInstance = nullptr;

} // namespace slowdl_bridge_detail

CSlowDLDownloadBridge::CSlowDLDownloadBridge() = default;

CSlowDLDownloadBridge::~CSlowDLDownloadBridge()
{
    Shutdown();
}

bool CSlowDLDownloadBridge::Initialize(const SlowDLHostOps& hostOps, bool deferInlineHookTransaction)
{
    Shutdown();

    // 1. Prefer host-native IGoldSrcDownloadExt via the engine's CreateInterface export.
    if (hostOps.getEngineHandle && hostOps.getProcAddress)
    {
        void* engineHandle = hostOps.getEngineHandle(hostOps.context);
        if (engineHandle)
        {
            void* createInterfaceAddr = hostOps.getProcAddress(hostOps.context, engineHandle, "CreateInterface");
            if (createInterfaceAddr)
            {
                auto  createInterface = reinterpret_cast<slowdl_bridge_detail::CreateInterfaceRawFn>(createInterfaceAddr);
                int   returnCode      = 0;
                void* rawInterface    = createInterface(goldsrcext::kGoldSrcDownloadExtInterfaceName, &returnCode);

                if (rawInterface && returnCode == 0)
                {
                    m_backendKind  = SlowDLBackendKind::kHostNative;
                    m_pDownloadExt = static_cast<goldsrcext::IGoldSrcDownloadExt*>(rawInterface);
                    return true;
                }
            }
        }
    }

    // 2. Fallback to legacy 32-bit GoldSrc gamedata + inline hook bridge.
    if (!hostOps.getEngineBase || !hostOps.resolveGameSymbol || !hostOps.inlineHook || !hostOps.unHook)
    {
        slowdl_bridge_detail::LogErrorText(hostOps, "Legacy download bridge host callbacks are incomplete");
        return false;
    }

    void* engineBase = hostOps.getEngineBase(hostOps.context);
    if (!engineBase)
    {
        slowdl_bridge_detail::LogErrorText(hostOps, "Cannot resolve engine base address");
        return false;
    }

    void* beginFileDownloadAddr = nullptr;
    void* dropClientAddr        = nullptr;
    void* hostClientAddr        = nullptr;
    void* cmdSourceAddr         = nullptr;
    void* serverPlayerAddr      = nullptr;

    if (!slowdl_bridge_detail::ResolveSymbolWithFallback(
            hostOps,
            engineBase,
            SV_BeginFileDownload_f_GameSymbol,
            SV_BeginFileDownload_f_GameSymbolAlternative,
            SlowDLSymbolKind::kFunction,
            &beginFileDownloadAddr))
    {
        return false;
    }

    if (!slowdl_bridge_detail::ResolveSymbolWithFallback(
            hostOps,
            engineBase,
            SV_DropClient_GameSymbol,
            SV_DropClient_GameSymbolAlternative,
            SlowDLSymbolKind::kFunction,
            &dropClientAddr))
    {
        return false;
    }

    if (!slowdl_bridge_detail::ResolveSymbolWithFallback(
            hostOps,
            engineBase,
            host_client_GameSymbol,
            nullptr,
            SlowDLSymbolKind::kGlobal,
            &hostClientAddr))
    {
        return false;
    }

    if (!slowdl_bridge_detail::ResolveSymbolWithFallback(
            hostOps,
            engineBase,
            cmd_source_GameSymbol,
            nullptr,
            SlowDLSymbolKind::kGlobal,
            &cmdSourceAddr))
    {
        return false;
    }

    if (!slowdl_bridge_detail::ResolveSymbolWithFallback(
            hostOps,
            engineBase,
            sv_player_GameSymbol,
            nullptr,
            SlowDLSymbolKind::kGlobal,
            &serverPlayerAddr))
    {
        return false;
    }

    auto legacyExt = std::make_unique<slowdl_bridge_detail::CLegacyDownloadExt>();
    if (!legacyExt->Install(
            hostOps,
            reinterpret_cast<slowdl_bridge_detail::FnSV_BeginFileDownload_f>(beginFileDownloadAddr),
            reinterpret_cast<slowdl_bridge_detail::FnSV_DropClient>(dropClientAddr),
            reinterpret_cast<void**>(hostClientAddr),
            reinterpret_cast<slowdl_bridge_detail::LegacyCmdSource*>(cmdSourceAddr),
            reinterpret_cast<edict_s**>(serverPlayerAddr),
            deferInlineHookTransaction))
    {
        return false;
    }

    m_legacyExt    = std::move(legacyExt);
    m_pDownloadExt = m_legacyExt.get();
    m_backendKind  = SlowDLBackendKind::kLegacyGamedata;
    return true;
}

bool CSlowDLDownloadBridge::Shutdown()
{
    if (m_backendKind == SlowDLBackendKind::kLegacyGamedata && m_legacyExt)
    {
        if (!m_legacyExt->Uninstall())
            return false;
        m_legacyExt.reset();
    }

    m_pDownloadExt = nullptr;
    m_backendKind  = SlowDLBackendKind::kUnavailable;
    return true;
}

goldsrcext::IGoldSrcDownloadExt* CSlowDLDownloadBridge::GetInterface() const noexcept
{
    return m_pDownloadExt;
}

SlowDLBackendKind CSlowDLDownloadBridge::GetBackendKind() const noexcept
{
    return m_backendKind;
}

const char* CSlowDLDownloadBridge::GetBackendName() const noexcept
{
    switch (m_backendKind)
    {
        case SlowDLBackendKind::kHostNative:
            return "host-native";
        case SlowDLBackendKind::kLegacyGamedata:
            return "legacy-gamedata";
        case SlowDLBackendKind::kUnavailable:
        default:
            return "unavailable";
    }
}
