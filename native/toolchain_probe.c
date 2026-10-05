/* Build and API-link probe for future handwritten 32-bit Windows modules.
 * This is project tooling, not a recompiled version of WET.EXE. */
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>

int main(void) {
    /* Reference the same API family as the original game. The API is not invoked. */
    HRESULT (WINAPI *volatile direct_draw_create)(GUID *, LPDIRECTDRAW *, IUnknown *) = DirectDrawCreate;
    printf("Lula development probe: Windows x86, %u-bit pointers, DirectDraw linked=%s\n",
           (unsigned)(sizeof(void *) * 8), direct_draw_create ? "yes" : "no");
    return sizeof(void *) == 4 ? 0 : 1;
}
