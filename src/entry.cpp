// entry.cpp — the process entry point; forwards to the app's AppMain().
//
// All app logic lives in AppMain() (src/main.cpp); this file only adapts the
// platform's native entry point to it:
//   - Windows:  WinMain — the Win32 GUI entry. The exe is built with the WIN32
//     CMake flag (GUI subsystem), so no console window appears; __argc/__argv
//     are the CRT's parsed command line (ANSI).
//   - macOS / Linux: main.
//
// Keep this file free of app logic — it exists so main.cpp never has to know
// about platform entry points.

#include <cstdlib>

int AppMain(int argc, char* argv[]);

#ifdef _WIN32
#  include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return AppMain(__argc, __argv);
}
#else
int main(int argc, char* argv[])
{
    return AppMain(argc, argv);
}
#endif
