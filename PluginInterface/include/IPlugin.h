#pragma once
#include "PluginParamerter.h"

#define  PLUGIN_INTERFACE_VERSION 1

struct ILogger
{
    enum Level
    {
        Info,
        Warning,
        Error
    };

    virtual ~ILogger() = default;
    virtual void log(Level level, const char* tag,const char* message) = 0;
};











struct IPlugin
{
    virtual ~IPlugin() = default;
    virtual int version()noexcept = 0;
    virtual const char* name()noexcept = 0;
    virtual const char* description() noexcept { return ""; }
    virtual void initialize(ILogger* logger)noexcept = 0;
    virtual void uninitialize()noexcept =0;
    virtual void pluginParameters(PluginParameterInfo** parameters, int* count)noexcept = 0;
    virtual bool execute(PluginParameterValue* parameters)noexcept = 0;
};


typedef  IPlugin* (*CreatePluginFunc)();
typedef void (*DestroyPluginFunc)(IPlugin*);
typedef  int (*GetPluginVersionFunc)();

// Stringize with argument expansion: #x does NOT expand macros in x, so a
// two-level indirection is required (XSTR expands first, then STR stringizes).
#define  STR(x) #x
#define  XSTR(x) STR(x)

#define CREATE_FUN create_plugin
#define CREATE_FUN_STR XSTR(CREATE_FUN)

#define DESTROY_FUN destroy_plugin
#define DESTROY_FUN_STR XSTR(DESTROY_FUN)

#define GET_PLUGIN_INTERFACE_VERSION_FUN get_plugin_interface_version
#define GET_PLUGIN_INTERFACE_VERSION_FUN_STR XSTR(GET_PLUGIN_INTERFACE_VERSION_FUN)

// ============================================================================
// 1. Unified export attribute (no macro definitions required from users)
// ============================================================================
#ifdef _MSC_VER
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif


// ============================================================================
// 3. Registration macro – uses PLUGIN_EXPORT, no conditional directives inside.
//    Plugin developer: place REGISTER_PLUGIN(YourClass) at the end of your .cpp.
//    Host application: include this header as well; no definitions required.
// ============================================================================
#define REGISTER_PLUGIN(CLASS_NAME)                                                \
extern "C" {                                                                       \
    PLUGIN_EXPORT IPlugin* CREATE_FUN() {                                       \
        try { return new CLASS_NAME(); } catch (...) { return nullptr; }           \
    }                                                                              \
    PLUGIN_EXPORT void DESTROY_FUN(IPlugin* p) {                                \
        if (p) { try { delete p; } catch (...) { /* ignore */ } }                  \
    }                                                                              \
    PLUGIN_EXPORT int GET_PLUGIN_INTERFACE_VERSION_FUN() { return PLUGIN_INTERFACE_VERSION; }                           \
}




