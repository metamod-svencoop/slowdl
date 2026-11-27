#include <extdll.h>

#include <meta_api.h>

#include <ctime>

#include "engine_hook.h"
#include "slowdl.h"

namespace
{

goldsrcext::IGoldSrcDownloadExt* g_pDownloadExt        = nullptr;
goldsrcext::HookHandle           g_BeginDownloadHandle = goldsrcext::kInvalidHookHandle;

// Applies the decision of CSlowDLMonitor to the active download extension.
class CEngineSlowDLSink final : public ISlowDLSink
{
public:
    CEngineSlowDLSink(goldsrcext::IGoldSrcDownloadExt* downloadExt,
                      goldsrcext::IHookChain<void>*    chain,
                      goldsrcext::GoldSrcOpaqueClient* client) :
        m_pDownloadExt(downloadExt),
        m_pChain(chain),
        m_pClient(client)
    {
    }

    void ForwardRequest() override
    {
        m_pChain->CallNext();
    }

    void DropClient() override
    {
        m_pDownloadExt->DropClient(
            m_pClient,
            false,
            "UDP downloads are disabled on this server. Please enable FastDL and reconnect.");
    }

    void PrintConsole(const std::string& line) override
    {
        LOG_CONSOLE(PLID, "%s", line.c_str());
    }

    bool AppendFileLog(const std::string& line) override
    {
        return g_SlowDLFileLog->Append(line, std::time(nullptr));
    }

    void ReportFileLogError() override
    {
        LOG_ERROR(PLID, "Unable to append logs/slowdl_YYYYMMDD.log; check the game directory permissions");
    }

private:
    goldsrcext::IGoldSrcDownloadExt* m_pDownloadExt;
    goldsrcext::IHookChain<void>*    m_pChain;
    goldsrcext::GoldSrcOpaqueClient* m_pClient;
};

void CopyPlayerIdentity(edict_t* pPlayer, slowdl_request_t& request)
{
    request.player = STRING(pPlayer->v.netname);

    // netname is not set before the player spawns.
    if (request.player.empty())
    {
        static char nameKey[] = "name";

        char*       infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(pPlayer);
        const char* name       = infoBuffer ? g_engfuncs.pfnInfoKeyValue(infoBuffer, nameKey) : NULL;

        request.player = (name && *name) ? name : "unknown";
    }

    request.userid = g_engfuncs.pfnGetPlayerUserId(pPlayer);

    const char* authid = g_engfuncs.pfnGetPlayerAuthId(pPlayer);

    if (authid)
        request.authid = authid;
}

void OnBeginFileDownload(goldsrcext::IHookChain<void>* chain, void* userData) noexcept
{
    auto* downloadExt = static_cast<goldsrcext::IGoldSrcDownloadExt*>(userData);

    slowdl_request_t request;

    request.client_source = (downloadExt->GetCommandSource() == goldsrcext::CommandSource::kClient);

    goldsrcext::GoldSrcOpaqueClient* client = request.client_source ? downloadExt->GetHostClient() : nullptr;

    request.has_client = (client != nullptr);

    // dlfile <filename>
    if (request.client_source && request.has_client && g_engfuncs.pfnCmd_Argc() == 2)
    {
        const char* filename = g_engfuncs.pfnCmd_Argv(1);

        if (filename)
            request.filename = filename;

        request.mapname  = STRING(gpGlobals->mapname);
        request.svencoop = g_bIsSvenCoop;

        edict_t* pPlayer = downloadExt->GetServerPlayerEdict();

        if (pPlayer && !pPlayer->free)
        {
            CopyPlayerIdentity(pPlayer, request);
        }
    }

    CEngineSlowDLSink sink(downloadExt, chain, client);

    g_SlowDLMonitor.HandleRequest(request, SlowDL_GetSettings(), sink);
}

} // namespace

bool InstallEngineHooks(goldsrcext::IGoldSrcDownloadExt* downloadExt)
{
    if (!downloadExt)
        return false;

    const goldsrcext::HookHandle handle = downloadExt->RegisterBeginFileDownloadHook(
        &OnBeginFileDownload,
        downloadExt,
        goldsrcext::kAltitudeNormal);

    if (handle == goldsrcext::kInvalidHookHandle)
        return false;

    g_pDownloadExt        = downloadExt;
    g_BeginDownloadHandle = handle;
    return true;
}

bool UninstallEngineHooks()
{
    bool ok = true;

    if (g_pDownloadExt && g_BeginDownloadHandle != goldsrcext::kInvalidHookHandle)
        ok = g_pDownloadExt->UnregisterBeginFileDownloadHook(g_BeginDownloadHandle);

    g_pDownloadExt        = nullptr;
    g_BeginDownloadHandle = goldsrcext::kInvalidHookHandle;
    return ok;
}
