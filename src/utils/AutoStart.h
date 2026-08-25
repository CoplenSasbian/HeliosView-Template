#pragma once

// Auto-start at OS login — platform-agnostic API. Each platform ships a
// _<platform>.cpp implementation (AutoStart_win32.cpp today); the interface
// stays the same so the rest of the app never sees platform code.
//
// Returns false on failure (e.g. the OS refused the registration); callers
// should surface that to the user rather than assuming the toggle took effect.

bool IsAutoStartEnabled();
bool SetAutoStartEnabled(bool enable);
