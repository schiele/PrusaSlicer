///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_GLSurface_hpp_
#define slic3r_GLSurface_hpp_

#include <utility>

namespace Slic3r
{
namespace GUI
{

enum class CursorType : unsigned char
{
    Standard,
    Cross
};

class IGLSurface
{
public:
    virtual ~IGLSurface() = default;

    virtual bool makeCurrent() = 0;
    virtual void releaseCurrent() = 0;
    virtual void swapBuffers() = 0;
    virtual std::pair<int, int> getSize() const = 0;
    virtual float getScaleFactor() const = 0;
    virtual void setCursor(CursorType type) = 0;
    virtual bool isShownOnScreen() const = 0;
    virtual bool isShown() const = 0;
    virtual void setFocus() = 0;
    virtual bool hasFocus() const = 0;
    virtual bool hasCapture() const = 0;
    virtual void releaseMouse() = 0;
    virtual std::pair<int, int> screenToClient(int screen_x, int screen_y) const = 0;
    virtual void refresh() = 0;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLSurface_hpp_
