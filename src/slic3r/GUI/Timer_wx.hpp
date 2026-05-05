///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_Timer_wx_hpp_
#define slic3r_Timer_wx_hpp_

#include "ITimer.hpp"
#include <memory>

class wxTimer;

namespace Slic3r
{
namespace GUI
{

class Timer_wx : public ITimer
{
public:
    Timer_wx();
    ~Timer_wx() override;

    Timer_wx(const Timer_wx &) = delete;
    Timer_wx &operator=(const Timer_wx &) = delete;
    Timer_wx(Timer_wx &&) = delete;
    Timer_wx &operator=(Timer_wx &&) = delete;

    void start(int milliseconds, bool oneShot = false) override;
    void stop() override;
    bool isRunning() const override;
    int getInterval() const override;
    void setCallback(std::function<void()> callback) override;

private:
    std::unique_ptr<wxTimer> m_timer;
    std::function<void()> m_callback;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_Timer_wx_hpp_
