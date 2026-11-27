#pragma once

#include <GoldSrcExt/download_ext.h>

// Registers and unregisters SlowDL's download interception callback against the
// active IGoldSrcDownloadExt implementation (native engine host or legacy bridge).
bool InstallEngineHooks(goldsrcext::IGoldSrcDownloadExt* downloadExt);
bool UninstallEngineHooks();
