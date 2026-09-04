#pragma once
#include <boost/json.hpp>
#include <memory>
#include <mutex>
#include <IPlugin.h>
#include <PluginParamerter.h>
#include <string>
#include <string_view>
#include <vector>
#include <HeliosViewCore/Execution.h>

class Logger;
class IPlugin;

class PluginManager;

class PluginConfig
{

public:
    PluginConfig(Logger& logger,PluginManager& pluginManager);
    PluginConfig(const PluginConfig&) = delete;
    PluginConfig& operator=(const PluginConfig&) = delete;
    ~PluginConfig();
    std::span<PluginParameterInfo> getInfo(const std::string& name);
    PluginParameterValue* getCurrentConfigParameter(const std::string& name);
    std::execution::task<void> initConfig();
    std::execution::task<void> save();

    bool switchConfig(const std::string& name);
    bool deleteConfig(const std::string& name);
    std::execution::task<bool> createConfig(const std::string& name);

    // 当前激活配置中该插件是否启用 (配置里 _enabled=false 表示禁用, 缺省视为启用)
    bool isPluginEnabled(const std::string& pluginName);

    [[nodiscard]] const std::vector<std::string>& getConfigNameList() const;

    [[nodiscard]] const std::string& getActiveConfigName() const;
private:
    std::execution::task<void> adjustConfig();
private:

    struct Impl;
    std::unique_ptr<Impl> m_;
    PluginManager& pluginManager_;
};

struct PluginStatusReport
{
    std::string pluginName;
    NotifyLevel level = NotifyLevel::Info;
    std::string message;
};

// Host services exposed to plugins (IPluginContext): the currently active
// config name, OS toast notifications, status reporting, and a persistent per-plugin KV store.
// One instance is created per loaded plugin (configured with the plugin's
// storage file), so each plugin gets its own KV namespace.
class PluginHostContext : public IPluginContext
{
public:
    PluginHostContext(PluginConfig& config, std::string pluginName, std::string kvFilePath,
                      std::function<void(PluginStatusReport)> reportCallback)
        : config_(config), pluginName_(std::move(pluginName)),
          kvFilePath_(std::move(kvFilePath)), reportCallback_(std::move(reportCallback))
    {
    }

    const char* activeConfigName() noexcept override;
    void reportStatus(NotifyLevel level, const char* message) noexcept override;
    bool notifyUser(const char* title, const char* message, NotifyLevel level = NotifyLevel::Info) noexcept override;
    void kvSet(const char* key, const char* value) noexcept override;
    const char* kvGet(const char* key) noexcept override;
    void kvRemove(const char* key) noexcept override;

private:
    // Load the JSON file into kv_ (no-op after the first time). NOT locked —
    // callers hold mutex_.
    void ensureLoaded() noexcept;
    // Flush kv_ to the JSON file. Callers hold mutex_.
    void save() noexcept;

    PluginConfig& config_;
    std::string pluginName_;
    std::string kvFilePath_;
    std::function<void(PluginStatusReport)> reportCallback_;
    std::mutex mutex_;
    bool loaded_ = false;
    boost::json::object kv_;               // key -> string value
    std::string getBuffer_;                // kvGet return buffer (stable until next kv call)
};


class PluginManager
{
public:
    PluginManager(Logger& logger);
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    ~PluginManager();
   std::execution::task<void>loadPlugins() ;


    void destroyPlugins() const;

    [[nodiscard]] const std::vector<std::string>& getPluginNames() const;

    [[nodiscard]] PluginConfig& getPluginConfig();

    IPlugin* getPluginByName(const std::string& name);

    // Full path of the plugin's dll ("" if the plugin isn't loaded) — used to
    // reveal the file in Explorer.
    [[nodiscard]] std::string getPluginPath(const std::string& name) const;

    [[nodiscard]] const std::vector<std::string>& getConfigNameList() const;

    const std::vector<PluginStatusReport>& activateConfig(const std::string& configName);

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
    Logger& logger_;
    PluginConfig pluginConfig_;
};


