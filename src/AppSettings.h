#pragma once

#include <HeliosViewCore/Execution.h>
#include <algorithm>
#include <boost/describe.hpp>
#include <string>
#include <utility>
#include <vector>

namespace helios { class Async; }

// AppSettings — the whole app configuration as one plain struct. load() maps
// the JSON file (SettingDir()/app.json) onto the members with boost::json
// value_to, save() serializes them back with value_from; callers just read /
// write the members — no per-field getters/setters. A key missing in an old
// config file keeps the member's default.
//
// NOTE: autoStart is deliberately NOT here. Auto-start is an OS registration
// (a Task Scheduler logon task — see utils/AutoStart.h / AutoStart_win32.cpp),
// so the OS state is the single source of truth; it never gets persisted to
// app.json and is exposed over the bridge as a runtime field instead.
struct AppSettings
{
    using ProcessRule = std::pair<std::string, std::string>; // exe → config

    int popupPosition = 3;                 // 0..3 (tray popup corner)
    bool processAutoSwitch = false;        // process-monitor auto-activation
    std::vector<ProcessRule> processRules; // exe → config pairs to watch

    // Appearance (unified with the rest of app.json): the selected background
    // image file name ("" = default aurora gradient), an optional CSS solid
    // fill ("" = none), and the theme mode / brightness threshold that drive
    // the luminance→black/white switch. solidColor and backgroundName are
    // mutually exclusive (the frontend clears the other when one is set), so
    // each stays a clean field — no sentinels smuggled into the file name.
    std::string backgroundName;             // "" = none (aurora) or solidColor set
    std::string solidColor;                 // "" = none (aurora) or backgroundName set
    std::string themeMode = "auto";         // "auto" | "dark" | "light"
    int themeThreshold = 128;               // 0..255, used only in "auto"

    // User-tunable design tokens. Only the *base* colors are stored; derived
    // tones (accent-strong / accent-soft / glass, etc.) are computed from them
    // by the frontend, and the status colors stay fixed (they have meaning).
    struct DesignPrefs
    {
        int glassBlur = 6;                 // blur(px) for frosted surfaces (4..48)
        int cornerRadius = 6;              // base corner radius px (4..32)
        std::string accentColor = "#5ea6ff"; // primary/brand color
        std::string fontSize = "md";         // "sm" | "md" | "lg" — UI base font size
        std::string density = "md";          // "sm" | "md" | "lg" — UI spacing/control density
    };
    DesignPrefs design;

    int windowX = -1, windowY = -1;        // main-window position (-1 = not saved)
    int windowWidth = 0, windowHeight = 0; // main-window size (0 = not saved)

    // Clamped to the valid 0..3 range.
    void setPopupPosition(int pos) { popupPosition = std::clamp(pos, 0, 3); }

    // Async file I/O on the background pool. load() reads app.json; missing /
    // invalid files keep the defaults and complete with false. save() writes
    // it (creating the settings dir first).
    std::execution::task<bool> load(helios::Async& async);
    std::execution::task<bool> save(helios::Async& async) const;
};

// The persisted shape: every member is described, so load/save map the whole
// object in one value_to/value_from call instead of hand-written JSON code.
// (autoStart is not listed — see the struct comment: it is OS-side state now.)
BOOST_DESCRIBE_STRUCT(AppSettings, (), (popupPosition, processAutoSwitch,
                                        processRules, backgroundName, solidColor, themeMode, themeThreshold,
                                        design,
                                        windowX, windowY, windowWidth, windowHeight))
BOOST_DESCRIBE_STRUCT(AppSettings::DesignPrefs, (), (glassBlur, cornerRadius,
                                                     accentColor,
                                                     fontSize, density))
