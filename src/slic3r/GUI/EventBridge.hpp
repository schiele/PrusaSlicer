///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_EventBridge_hpp_
#define slic3r_EventBridge_hpp_

#include <array>
#include <exception>
#include <functional>
#include <string>
#include <utility>

#include "libslic3r/Point.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/PrintBase.hpp"
#include "EventTypes.hpp"

namespace Slic3r
{
namespace GUI
{

// Canvas event poster - abstracts how GLCanvas3D sends events to the Plater.
// The wx implementation posts wxEvents. The Qt implementation will use signals/slots.
class ICanvasEventPoster
{
public:
    virtual ~ICanvasEventPoster() = default;

    // Post a simple event with no data
    virtual void postEvent(CanvasEventType type) = 0;

    // Post an event with typed data
    virtual void postEvent(CanvasEventType type, int data) = 0;
    virtual void postEvent(CanvasEventType type, bool data) = 0;
    virtual void postEvent(CanvasEventType type, float data) = 0;
    virtual void postEvent(CanvasEventType type, const std::pair<Vec2d, bool> &data) = 0;
    virtual void postEvent(CanvasEventType type, const std::array<Vec3d, 2> &data) = 0;
    virtual void postEvent(CanvasEventType type, const HeightProfileSmoothingParams &data) = 0;
    virtual void postEvent(CanvasEventType type, const std::string &data) = 0;

    // Post a key event (for sliders manipulation)
    virtual void postKeyEvent(CanvasEventType type, int key_code, int modifiers) = 0;
};

// Slicing event poster - abstracts how BackgroundSlicingProcess communicates with the UI.
// All methods are thread-safe (called from the background slicing thread).
class ISlicingEventPoster
{
public:
    virtual ~ISlicingEventPoster() = default;

    // Progress update from the slicing engine
    virtual void postSlicingStatus(const PrintBase::SlicingStatus &status) = 0;

    // Slicing phase completed (before G-code export)
    virtual void postSlicingCompleted(int timestamp) = 0;

    // G-code export to file began
    virtual void postExportBegan() = 0;

    // Background processing finished (final event)
    virtual void postProcessCompleted(SlicingCompletedStatus status, std::exception_ptr exception = nullptr) = 0;

    // Execute a function on the UI thread (async, fire-and-forget)
    virtual void callOnUIThreadAsync(std::function<void()> task) = 0;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_EventBridge_hpp_
