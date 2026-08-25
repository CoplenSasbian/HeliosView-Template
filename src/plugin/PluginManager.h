#pragma once
#include <memory>
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

    void activateConfig(const std::string& configName);

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
    Logger& logger_;
    PluginConfig pluginConfig_;
};


