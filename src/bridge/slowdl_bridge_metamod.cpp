#include "slowdl_bridge_metamod.h"

namespace
{

void* MetamodGetEngineHandle(void* ctx)
{
    auto* host = static_cast<MetamodSlowDLHostContext*>(ctx);
    return (host && host->metaUtilFuncs) ? reinterpret_cast<void*>(host->metaUtilFuncs->pfnGetEngineHandle()) : nullptr;
}

void* MetamodGetEngineBase(void* ctx)
{
    auto* host = static_cast<MetamodSlowDLHostContext*>(ctx);
    return (host && host->metaUtilFuncs) ? host->metaUtilFuncs->pfnGetEngineBase() : nullptr;
}

void* MetamodGetProcAddress(void* ctx, void* moduleHandle, const char* procName)
{
    auto* host = static_cast<MetamodSlowDLHostContext*>(ctx);
    if (!host || !host->metaUtilFuncs || !moduleHandle || !procName)
        return nullptr;

    return host->metaUtilFuncs->pfnGetProcAddress(reinterpret_cast<DLHANDLE>(moduleHandle), procName);
}

SlowDLSymbolStatus MetamodResolveGameSymbol(void*            ctx,
                                            void*            moduleBase,
                                            const char*      symbolName,
                                            SlowDLSymbolKind kind,
                                            void**           outAddress,
                                            const char**     outStatusText)
{
    auto* host = static_cast<MetamodSlowDLHostContext*>(ctx);
    if (!host || !host->metaUtilFuncs)
    {
        if (outStatusText)
            *outStatusText = "metamod utility table is unavailable";
        return SlowDLSymbolStatus::kOtherError;
    }

    const meta_gamesymbol_kind_t metaKind =
        (kind == SlowDLSymbolKind::kFunction) ? META_GAMESYMBOL_KIND_FUNCTION : META_GAMESYMBOL_KIND_GLOBAL;

    const meta_gamesymbol_status_t status =
        host->metaUtilFuncs->pfnResolveGameSymbol(moduleBase, symbolName, metaKind, outAddress);

    if (outStatusText)
        *outStatusText = host->metaUtilFuncs->pfnGetGameSymbolStatusString(status);

    if (status == META_GAMESYMBOL_OK)
        return SlowDLSymbolStatus::kOk;
    if (status == META_GAMESYMBOL_SYMBOL_NOT_FOUND)
        return SlowDLSymbolStatus::kSymbolNotFound;
    return SlowDLSymbolStatus::kOtherError;
}

void* MetamodInlineHook(void* ctx, void* target, void* detour, void** outOriginal, bool deferTransaction)
{
    auto* host = static_cast<MetamodSlowDLHostContext*>(ctx);
    if (!host || !host->metaUtilFuncs)
        return nullptr;

    return host->metaUtilFuncs->pfnInlineHook(target, detour, outOriginal, deferTransaction);
}

bool MetamodUnHook(void* ctx, void* hookHandle)
{
    auto* host = static_cast<MetamodSlowDLHostContext*>(ctx);
    if (!host || !host->metaUtilFuncs || !hookHandle)
        return false;

    return host->metaUtilFuncs->pfnUnHook(static_cast<hook_t*>(hookHandle)) != FALSE;
}

void MetamodLogError(void* ctx, const char* message)
{
    auto* host = static_cast<MetamodSlowDLHostContext*>(ctx);
    if (host && host->metaUtilFuncs && host->plid && message)
        host->metaUtilFuncs->pfnLogError(host->plid, "%s", message);
}

void MetamodLogMessage(void* ctx, const char* message)
{
    auto* host = static_cast<MetamodSlowDLHostContext*>(ctx);
    if (host && host->metaUtilFuncs && host->plid && message)
        host->metaUtilFuncs->pfnLogMessage(host->plid, "%s", message);
}

} // namespace

SlowDLHostOps MakeMetamodSlowDLHostOps(MetamodSlowDLHostContext* hostContext)
{
    SlowDLHostOps ops;
    ops.context           = hostContext;
    ops.getEngineHandle   = &MetamodGetEngineHandle;
    ops.getEngineBase     = &MetamodGetEngineBase;
    ops.getProcAddress    = &MetamodGetProcAddress;
    ops.resolveGameSymbol = &MetamodResolveGameSymbol;
    ops.inlineHook        = &MetamodInlineHook;
    ops.unHook            = &MetamodUnHook;
    ops.logError          = &MetamodLogError;
    ops.logMessage        = &MetamodLogMessage;
    return ops;
}
