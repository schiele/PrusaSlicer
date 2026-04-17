///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_TD1SDialog_hpp_
#define slic3r_TD1SDialog_hpp_

#include <wx/choice.h>
#include <wx/dialog.h>

#include "libslic3r/Color.hpp"

namespace Slic3r {
namespace GUI {

// Modal dialog shown when the TD1S sensor detects a new filament.
// Displays the measured TD value and lets the user assign it to a filament preset.
class TD1SDialog : public wxDialog
{
public:
    TD1SDialog(wxWindow *parent, const ColorRGB &color, float td, const std::string &hex_color);

private:
    void on_apply(wxCommandEvent &event);
    void on_dismiss(wxCommandEvent &event);

    float      m_td;
    wxChoice  *m_preset_choice{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_TD1SDialog_hpp_
