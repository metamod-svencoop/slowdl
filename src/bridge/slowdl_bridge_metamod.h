#pragma once

#include <extdll.h>
#include <meta_api.h>

#include "slowdl_bridge.h"

struct MetamodSlowDLHostContext
{
    mutil_funcs_t* metaUtilFuncs = nullptr;
    plid_t         plid          = nullptr;
};

// Adapts Metamod's mutil_funcs_t table to SlowDLHostOps.
// The caller must keep `hostContext` alive until CSlowDLDownloadBridge::Shutdown().
SlowDLHostOps MakeMetamodSlowDLHostOps(MetamodSlowDLHostContext* hostContext);
