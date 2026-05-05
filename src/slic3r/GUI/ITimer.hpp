///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_ITimer_hpp_
#define slic3r_ITimer_hpp_

#include <functional>

namespace Slic3r
{
namespace GUI
{

class ITimer
{
public:
    virtual ~ITimer() = default;

    virtual void start(int milliseconds, bool oneShot = false) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual int getInterval() const = 0;
    virtual void setCallback(std::function<void()> callback) = 0;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ITimer_hpp_
