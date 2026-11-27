#pragma once

#include <cstdint>

// Legacy 32-bit GoldSrc private engine signatures kept strictly internal to
// slowdl_hook_bridge. Business logic must only talk to IGoldSrcDownloadExt.

namespace slowdl_bridge_detail
{

// Mirrors GoldSrc's cmd_source_t values (src_client = 0, src_command = 1).
using LegacyCmdSource = int;

// SV_BeginFileDownload_f: handler of the "dlfile" client command.
using FnSV_BeginFileDownload_f = void (*)();

// SV_DropClient(client_t* cl, qboolean crash, char* fmt, ...).
// qboolean is `int` in GoldSrc HLSDK; client_t* is kept opaque as void*.
using FnSV_DropClient = void (*)(void* cl, int crash, char* fmt, ...);

} // namespace slowdl_bridge_detail
