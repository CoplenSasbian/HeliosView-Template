#pragma once
#include "PluginParamerter.h"

#define  PLUGIN_INTERFACE_VERSION 3

struct ILogger
{
    enum Level
    {
        Info,
        Warning,
        Error
    };

    virtual ~ILogger() = default;
    virtual void log(Level level, const char* tag,const char* message) noexcept = 0;
};

// Notification & report severity levels for user-facing feedback
enum class NotifyLevel : int
{
    Info    = 0, // 普通状态提示
    Success = 1, // 成功 (如: HDR 成功开启, 色彩已增强)
    Warning = 2, // 警告 (如: 健康提醒休息, 硬件环境异常)
    Error   = 3  // 失败 (如: 电源方案切换失败, 脚本报错)
};

// Host-provided services injected into a plugin at initialize() time.
// A plugin otherwise only sees its own parameters; this context lets it ask
// about the app state and reach the user (OS toast notifications).
struct IPluginContext
{
    virtual ~IPluginContext() = default;

    // Name of the currently active config. "close" = 游戏关闭/未启用配置,
    // any other non-empty name = an active game config.
    virtual const char* activeConfigName() noexcept = 0;

    // Report a brief execution outcome during execute().
    // The host collects reports from all executed plugins and summarizes them
    // into a single system notification when switching configs.
    // If the plugin has nothing noteworthy to report, do not call this (keeps quiet).
    virtual void reportStatus(NotifyLevel level, const char* message) noexcept = 0;

    // Show an immediate standalone user notification (OS toast).
    // Intended for background events, health anti-addiction reminders, or async triggers.
    // Thread-safe; returns true when accepted by the OS.
    virtual bool notifyUser(const char* title, const char* message, NotifyLevel level = NotifyLevel::Info) noexcept = 0;

    // Host-provided persistent key-value store — one namespace per plugin,
    // persisted by the host on disk (the plugin never touches file paths).
    // Keys/values are UTF-8 strings. kvGet returns a pointer valid until the
    // next kv call on this context — copy the value if you need it longer.
    // All methods are thread-safe.
    virtual void kvSet(const char* key, const char* value) noexcept = 0;
    virtual const char* kvGet(const char* key) noexcept = 0;
    virtual void kvRemove(const char* key) noexcept = 0;
};











struct IPlugin
{
    virtual ~IPlugin() = default;
    virtual int version()noexcept = 0;
    virtual const char* name()noexcept = 0;
    virtual const char* description() noexcept { return ""; }
    virtual void initialize(ILogger* logger, IPluginContext* context)noexcept = 0;
    virtual void uninitialize()noexcept =0;
    virtual void pluginParameters(PluginParameterInfo** parameters, int* count)noexcept = 0;
    virtual bool execute(PluginParameterValue* parameters)noexcept = 0;

    // Optional plugin-provided custom info: an HTML fragment shown by the host
    // UI in the plugin's detail dialog ("自定义信息" section). Return "" when
    // the plugin has nothing custom to show. Called on demand (dialog opened),
    // on any thread — keep it cheap.
    virtual const char* customInfo() noexcept { return ""; }
};


typedef  IPlugin* (*CreatePluginFunc)() noexcept;
typedef void (*DestroyPluginFunc)(IPlugin*) noexcept;
typedef  int (*GetPluginVersionFunc)() noexcept;

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
    PLUGIN_EXPORT IPlugin* CREATE_FUN() noexcept {                                 \
        try { return new CLASS_NAME(); } catch (...) { return nullptr; }           \
    }                                                                              \
    PLUGIN_EXPORT void DESTROY_FUN(IPlugin* p) noexcept {                          \
        if (p) { try { delete p; } catch (...) { /* ignore */ } }                  \
    }                                                                              \
    PLUGIN_EXPORT int GET_PLUGIN_INTERFACE_VERSION_FUN() noexcept { return PLUGIN_INTERFACE_VERSION; } \
}




