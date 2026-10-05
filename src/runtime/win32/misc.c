/* OLE32 and COMDLG32: only what the game needs to start. */
#include "com.h"

#define REGDB_E_CLASSNOTREG 0x80040154u

WINAPI_FN(ole32, CoInitialize) { RET(1, S_OK); }
WINAPI_FN(ole32, CoUninitialize) { RET(0, 0); }

/* No COM servers exist on the host. Video playback is on hold; anything that
 * asks for a COM class gets "class not registered" and can fall back. */
WINAPI_FN(ole32, CoCreateInstance)
{
    char clsid[40];
    guid_str(ARG(0), clsid);
    RT_WARN("CoCreateInstance(%s) refused: no COM servers in the native runtime", clsid);
    if (ARG(4))
        W32(ARG(4), 0);
    RET(5, REGDB_E_CLASSNOTREG);
}

WINAPI_FN(comdlg32, GetOpenFileNameA)
{
    RT_WARN("GetOpenFileNameA: file dialogs are not available; cancelled");
    RET(1, 0);
}
