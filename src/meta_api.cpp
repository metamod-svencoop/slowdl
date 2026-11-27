#include <extdll.h> // always

#include <meta_api.h> // of course

#include "bridge/slowdl_bridge.h"
#include "bridge/slowdl_bridge_metamod.h"
#include "engine_hook.h"
#include "slowdl.h"

#define REQUIRED_INTERFACE_MAJOR_VERSION 5
#define REQUIRED_INTERFACE_MINOR_VERSION 18

// Must provide at least one of these..
static META_FUNCTIONS gMetaFunctionTable = {
    NULL,          // pfnGetEntityAPI				HL SDK; called before game DLL
    NULL,          // pfnGetEntityAPI_Post			META; called after game DLL
    GetEntityAPI2, // pfnGetEntityAPI2				HL SDK2; called before game DLL
    NULL,          // pfnGetEntityAPI2_Post		META; called after game DLL
    NULL,          // pfnGetNewDLLFunctions		HL SDK2; called before game DLL
    NULL,          // pfnGetNewDLLFunctions_Post	META; called after game DLL
    NULL,          // pfnGetEngineFunctions	META; called before HL engine
    NULL,          // pfnGetEngineFunctions_Post	META; called after HL engine
    NULL,          // pfnGetStudioBlendingInterface 2022/07/02 Added by hzqst
    NULL,          // pfnGetStudioBlendingInterface_Post 2022/07/02 Added by hzqst
};

// Description of plugin
plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,                       // ifvers
    "SlowDL",                                     // name
    "1.0",                                        // version
    "2026",                                       // date
    "hzqst",                                      // author
    "https://github.com/metamod-svencoop/slowdl", // url
    "SLOWDL",                                     // logtag, all caps please
    PT_ANYTIME,                                   // (when) loadable
    PT_NEVER,                                     // (when) unloadable
};

// Global vars from metamod:
meta_globals_t*  gpMetaGlobals;   // metamod globals
gamedll_funcs_t* gpGamedllFuncs;  // gameDLL function tables
mutil_funcs_t*   gpMetaUtilFuncs; // metamod utility functions

static MetamodSlowDLHostContext g_SlowDLHostContext;
static CSlowDLDownloadBridge    g_SlowDLBridge;

// Accept "<major>:<minor>" with the required major version and at least the
// required minor version. Trailing characters are rejected.
static bool IsCompatibleInterfaceVersion(const char* interfaceVersion)
{
    if (!interfaceVersion)
        return false;

    int  iMajorVersion = 0;
    int  iMinorVersion = 0;
    char trailing      = 0;

    if (sscanf(interfaceVersion, "%d:%d%c", &iMajorVersion, &iMinorVersion, &trailing) != 2)
        return false;

    return iMajorVersion == REQUIRED_INTERFACE_MAJOR_VERSION && iMinorVersion >= REQUIRED_INTERFACE_MINOR_VERSION;
}

// Metamod requesting info about this plugin:
//  ifvers			(given) interface_version metamod is using
//  pPlugInfo		(requested) struct with info about plugin
//  pMetaUtilFuncs	(given) table of utility functions provided by metamod
C_DLLEXPORT int Meta_Query(const char* interfaceVersion, plugin_info_t** pPlugInfo, mutil_funcs_t* pMetaUtilFuncs)
{
    if (!IsCompatibleInterfaceVersion(interfaceVersion))
    {
        pMetaUtilFuncs->pfnLogError(PLID, "Metamod interface 5:18 or newer compatible 5.x is required");
        return FALSE;
    }

    // Give metamod our plugin_info struct
    *pPlugInfo = &Plugin_info;
    // Get metamod utility function table.
    gpMetaUtilFuncs = pMetaUtilFuncs;
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME    now,
                            META_FUNCTIONS*  pFunctionTable,
                            meta_globals_t*  pMGlobals,
                            gamedll_funcs_t* pGamedllFuncs)
{
    if (!pFunctionTable || !pMGlobals || !gpGlobals)
        return FALSE;

    gpMetaGlobals  = pMGlobals;
    gpGamedllFuncs = pGamedllFuncs;

    g_SlowDLHostContext.metaUtilFuncs = gpMetaUtilFuncs;
    g_SlowDLHostContext.plid          = PLID;

    // Only startup has a later Metamod transaction commit for legacy inline hooks.
    if (!g_SlowDLBridge.Initialize(MakeMetamodSlowDLHostOps(&g_SlowDLHostContext), now == PT_STARTUP))
    {
        LOG_ERROR(PLID, "Download interception backend is unavailable; refusing to load SlowDL");
        return FALSE;
    }

    char gameDirectory[1024] = {};
    g_engfuncs.pfnGetGameDir(gameDirectory);

    SlowDL_Init(gameDirectory);

    if (!InstallEngineHooks(g_SlowDLBridge.GetInterface()))
    {
        g_SlowDLBridge.Shutdown();
        SlowDL_Shutdown();
        LOG_ERROR(PLID, "Unable to install the UDP download hook");
        return FALSE;
    }

    LOG_MESSAGE(PLID, "Download interception backend active: %s", g_SlowDLBridge.GetBackendName());

    SlowDL_RegisterCvars();

    memcpy(pFunctionTable, &gMetaFunctionTable, sizeof(META_FUNCTIONS));
    return TRUE;
}

// Metamod detaching plugin from the server.
// now		(given) current phase, ie during map, etc
// reason	(given) why detaching (refresh, console unload, forced unload, etc)
C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME /* now */,
                            PL_UNLOAD_REASON /* reason */)
{
    const bool hooksRemoved   = UninstallEngineHooks();
    const bool bridgeShutdown = g_SlowDLBridge.Shutdown();

    SlowDL_Shutdown();
    return (hooksRemoved && bridgeShutdown) ? TRUE : FALSE;
}
