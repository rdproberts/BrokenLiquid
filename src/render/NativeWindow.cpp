#include "render/NativeWindow.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace bl::render {

void* currentGlWindow() {
#ifdef _WIN32
    // QOpenGLWidget renders through a context bound to the top-level window's DC.
    return WindowFromDC(wglGetCurrentDC());
#else
    return nullptr;
#endif
}

} // namespace bl::render
