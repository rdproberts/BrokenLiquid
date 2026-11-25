#pragma once

namespace bl::render {

// Native window that owns the current OpenGL context (an HWND on Windows), or null.
// Kept in its own file: OCCT headers define NOGDI, which hides the WGL declarations.
void* currentGlWindow();

} // namespace bl::render
