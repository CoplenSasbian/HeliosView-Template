#include "PluginManager.h"

#include <flat_map>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>

#include "../utils/AppFilePath.h"

#include "DynamicLibraryApi.h"
#include <IPlugin.h>

#include "../AppContext.h"
#include "../Logger.h"
#include <HeliosViewCore/Dialogs.h>

#ifdef _WIN32
constexpr char kPluginExtension[] = ".dll";
#endif
namespace fs = std::filesystem;

struct JsonPluginParameterValue : public PluginParameterValue
{
    JsonPluginParameterValue(boost::json::value& value, std::span<const PluginParameterInfo> info)
        : configObj(value.as_object()),
          type(info)
    {
    }

    [[nodiscard]] PluginParameterType getType(const char* name) const override
    {
        auto find_if = std::ranges::find_if(type, [&](const PluginParameterInfo& info) { return std::string_view{info.name} == name; });
        return find_if != type.end() ? find_if->type : PluginParameterType::Int;
    }
    [[nodiscard]] int64_t getInt64Value(const char* name) const override
    {
        return configObj.get().at(name).to_number<int64_t>();
    }
    [[nodiscard]] double getDoubleValue(const char* name) const override
    {
        return configObj.get().at(name).to_number<double>();
    }
    [[nodiscard]] bool getBoolValue(const char* name) const override
    {
        return configObj.get().at(name).as_bool();
    }
    [[nodiscard]] const char* getStringValue(const char* name) const override
    {
        return configObj.get().at(name).as_string().c_str();
    }
    [[nodiscard]] int64_t getDateTimeValue(const char* name) const override
    {
        return configObj.get().at(name).to_number<int64_t>();
    }
    [[nodiscard]] const char* getFileValue(const char* name) const override
    {
        return configObj.get().at(name).as_string().c_str();
    }
    [[nodiscard]] const char* getFolderValue(const char* name) const override
    {
        return configObj.get().at(name).as_string().c_str();
    }

    void setInt64Value(const char* name, int64_t value) override
    {
        configObj.get()[name] = value;
    }
    void setDoubleValue(const char* name, double value) override
    {
        configObj.get()[name] = value;
    }
    void setBoolValue(const char* name, bool value) override
    {
        configObj.get()[name] = value;
    }
    void setStringValue(const char* name, const char* value) override
    {
        configObj.get()[name] = value;
    }
    void setDateTimeValue(const char* name, int64_t value) override
    {
        configObj.get()[name] = value;
    }
    void setFileValue(const char* name, const char* value) override
    {
        configObj.get()[name] = value;
    }
    void setFolderValue(const char* name, const char* value) override
    {
        configObj.get()[name] = value;
    }
    std::reference_wrapper<boost::json::object> configObj;
    std::span<const PluginParameterInfo> type;
};


struct PluginConfig::Impl
{
    explicit Impl(Logger& tagLogger)
        : tagLogger_(tagLogger, "PluginConfig")
    {
    }

    TagLogger tagLogger_;

    // Serializes switchConfig and the config readers: activations can arrive
    // concurrently from the process-monitor auto-switch (pool thread), the UI
    // (plugins_activate) and initConfig's default activation — without a lock
    // they race on the maps below.
    std::mutex mutex;

    std::flat_map<std::string, boost::json::value> pluginConfigs;

    std::vector<std::string> configNameList;

    boost::json::value* currentConfig{};
    std::string currentPluginName;

    std::flat_map<std::string, JsonPluginParameterValue> currentParamValue;
};

// One-time migration: plugin configs used to live directly in the settings
// dir; they now live in a configs/ subdir so SettingDir() stays reserved for
// app-level configuration. Files whose json_type is not "pluginConfig" are
// app config and are left untouched. Idempotent: after the move there is
// nothing left in the old location to migrate.
static void MigrateOldConfigs(TagLogger& log)
{
    std::error_code ec;
    auto oldDir = SettingDir();
    auto newDir = ConfigDir();
    fs::create_directories(newDir, ec);
    if (!fs::exists(oldDir, ec)) return;

    for (fs::directory_iterator it(oldDir, ec); it != fs::directory_iterator() && !ec; it.increment(ec))
    {
        const auto& entry = *it;
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") continue;

        std::ifstream in(entry.path());
        if (!in.is_open()) continue;
        std::stringstream ss;
        ss << in.rdbuf();
        boost::json::value value;
        try { value = boost::json::parse(ss.str()); }
        catch (...) { continue; }
        if (!value.is_object()) continue;
        auto typeIt = value.as_object().if_contains("json_type");
        if (!typeIt || !typeIt->is_string() || typeIt->as_string() != "pluginConfig") continue;

        auto target = newDir / entry.path().filename();
        if (fs::exists(target, ec)) continue; // already migrated once — leave the copy alone
        fs::rename(entry.path(), target, ec);
        if (!ec)
            log.Info("Migrated plugin config {} -> {}", entry.path().filename().string(), target.string());
        else
            log.Warning("Failed to migrate plugin config {} : {}", entry.path().string(), ec.message());
    }
}


PluginConfig::PluginConfig(Logger& logger, PluginManager& pluginManager)
    : m_(std::make_unique<Impl>(logger)), pluginManager_(pluginManager)
{
}

PluginConfig::~PluginConfig()
= default;

std::span<PluginParameterInfo> PluginConfig::getInfo(const std::string& name)
{
    IPlugin* plugin = pluginManager_.getPluginByName(name);
    if (plugin)
    {
        PluginParameterInfo* info;
        int count;
        plugin->pluginParameters(&info, &count);
        return std::span<PluginParameterInfo>{info, (size_t)count};
    }
    return std::span<PluginParameterInfo>{};
}


PluginParameterValue* PluginConfig::getCurrentConfigParameter(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_->mutex);
    auto found = m_->currentParamValue.find(name);

    if (found != m_->currentParamValue.end())
    {
        return &found->second;
    }
    return nullptr;
}


std::execution::task<void> PluginConfig::initConfig()
{
    // Move any configs left in the old settings-dir location into the
    // configs/ subdir before loading (no-op on fresh installs).
    MigrateOldConfigs(m_->tagLogger_);

    auto dir = ConfigDir();
    fs::create_directories(dir);
    auto& async = AppContext::instance()->async();
    auto executor = async.get_executor();
    // Always ensure the built-in "close" config exists (in memory; persisted on
    // the first save), so the app always has a default active parameter set even
    // when the user already has other configs.
    if (!m_->pluginConfigs.contains("close"))
    {
        m_->pluginConfigs["close"] = boost::json::object{std::make_pair("json_type", "pluginConfig")};
    }
    for (fs::directory_iterator iter(dir); auto& entry : iter)
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            // One unreadable/corrupt config file must not abort the whole
            // config load: the stream_file ctor below throws on open failure
            // (and boost::json::parse can throw too).
            try
            {
                boost::asio::stream_file file{executor, entry.path().string(), boost::asio::file_base::read_only};
                if (file.is_open())
                {
                    uint64_t size = file.size();
                    std::string data;
                    data.resize(size);


                    auto bytesRead = co_await async.readAsync(file, boost::asio::buffer(data));

                    if (bytesRead == size)
                    {
                        boost::json::value config = boost::json::parse(data);
                        if (config.is_object())
                        {
                            auto jsonType = config.as_object().if_contains("json_type");
                            if (jsonType && jsonType->is_string() && jsonType->as_string() == "pluginConfig")
                            {
                                std::string configName = entry.path().stem().string();
                                m_->pluginConfigs[configName] = config;
                                m_->tagLogger_.Info("Loaded plugin config: {}", configName);
                            }
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                m_->tagLogger_.Error("Failed to read config '{}': {}", entry.path().string(), e.what());
            }
        }
    }

    m_->configNameList.clear();
    for (const auto& [name, _] : m_->pluginConfigs)
    {
        m_->configNameList.push_back(name);
    }

    co_await adjustConfig();

    // Default-activate the built-in "close" config (created above when the
    // configs dir was empty) so the UI always starts with an active parameter
    // set instead of an empty one. A config picked later overrides this.
    if (m_->currentPluginName.empty())
    {
        auto found = m_->pluginConfigs.find("close");
        if (found != m_->pluginConfigs.end())
        {
            switchConfig("close");
            m_->tagLogger_.Info("Activated default config: close");
        }
    }
    co_return;
}

std::execution::task<void> PluginConfig::save()
{
    auto dir = ConfigDir();
    auto& async = AppContext::instance()->async();
    auto executor = async.get_executor();
    for (auto [name, config] : m_->pluginConfigs)
    {
        auto configPath = dir / (name + ".json");

        bool success = false;

        while (!success)
        {
            boost::asio::stream_file file{
                executor, configPath.string(),
                boost::asio::stream_file::read_write
                | boost::asio::stream_file::create
                | boost::asio::stream_file::truncate
            };

            if (file.is_open())
            {
                success = (co_await async.writeAsync(file, boost::asio::buffer(serialize(config)))) > 0;
            }
            else
            {
                auto message = std::format("Failed to open file for writing: {}", configPath.string());
                auto res = helios::messageBox(nullptr, helios::MessageBoxType::Error,
                                              helios::MessageBoxButtons::AbortRetryIgnore,
                                              "Error", message.c_str());

                if (res == helios::MessageBoxResult::Retry)
                {
                    continue;
                }
                if (res == helios::MessageBoxResult::Ignore)
                {
                    break;
                }
                // Abort
                throw std::runtime_error(message + "User aborted");
            }
        }
    }
    co_return;
}

bool PluginConfig::switchConfig(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_->mutex);
    auto found = m_->pluginConfigs.find(name);
    if (found == m_->pluginConfigs.end()) return false;
    m_->currentConfig = &found->second;
    m_->currentPluginName = name;

    auto& config = m_->currentConfig->as_object();

    m_->currentParamValue.clear();


    for (auto& [k,v] : config)
    {
        // Skip meta keys like "json_type": only plugin parameter objects are
        // wrapped. JsonPluginParameterValue calls v.as_object(), which would
        // throw on a non-object value and abort the whole switch.
        if (!v.is_object()) continue;
        auto pluginParameterInfos = getInfo(k);
        m_->tagLogger_.Info("switchConfig: plugin='{}' infoCount={}", k, pluginParameterInfos.size());
        if (pluginParameterInfos.empty())
        {
            m_->tagLogger_.Warning("switchConfig: plugin '{}' not found or has no parameters, skipping", k);
            continue;
        }
        m_->currentParamValue.emplace(
            k, JsonPluginParameterValue{v,pluginParameterInfos}
        );
    }


    return true;
}

bool PluginConfig::deleteConfig(const std::string& name)
{
    auto found = m_->pluginConfigs.find(name);
    if (found == m_->pluginConfigs.end()) return false;

    // Remove the persisted file first (ignore missing-file errors).
    std::error_code ec;
    fs::remove(ConfigDir() / (name + ".json"), ec);

    m_->pluginConfigs.erase(found);
    m_->configNameList.clear();
    for (const auto& [k, _] : m_->pluginConfigs)
        m_->configNameList.push_back(k);

    // If the deleted config was active, fall back to the first remaining one
    // (or clear the active state when none is left).
    if (m_->currentPluginName == name)
    {
        if (m_->pluginConfigs.empty())
        {
            m_->currentConfig = nullptr;
            m_->currentPluginName.clear();
            m_->currentParamValue.clear();
        }
        else
        {
            switchConfig(m_->pluginConfigs.begin()->first);
        }
    }
    m_->tagLogger_.Info("Deleted config: {}", name);
    return true;
}

const std::vector<std::string>& PluginConfig::getConfigNameList() const
{
    std::lock_guard<std::mutex> lock(m_->mutex);
    return m_->configNameList;
}

const std::string& PluginConfig::getActiveConfigName() const
{
    std::lock_guard<std::mutex> lock(m_->mutex);
    return m_->currentPluginName;
}

bool PluginConfig::isPluginEnabled(const std::string& pluginName)
{
    std::lock_guard<std::mutex> lock(m_->mutex);
    if (!m_->currentConfig || !m_->currentConfig->is_object()) return true;

    const auto& obj = m_->currentConfig->as_object();
    auto it = obj.find(pluginName);
    if (it == obj.end() || !it->value().is_object()) return true;

    // 插件对象里的 _enabled=false 表示该配置禁用此插件; 缺省视为启用
    const auto* enabled = it->value().as_object().if_contains("_enabled");
    if (!enabled || !enabled->is_bool()) return true;
    return enabled->as_bool();
}

std::execution::task<bool> PluginConfig::createConfig(const std::string& name)
{
    if (m_->pluginConfigs.contains(name))
        co_return false;

    m_->pluginConfigs[name] = boost::json::object{std::make_pair("json_type", "pluginConfig")};
    m_->configNameList.clear();
    for (const auto& [k, _] : m_->pluginConfigs)
        m_->configNameList.push_back(k);

    co_await adjustConfig();
    co_return true;
}

std::execution::task<void> PluginConfig::adjustConfig()
{
    bool needSave = false;
    const auto& pluginNames = pluginManager_.getPluginNames();
    for (auto [name, config] : m_->pluginConfigs)
    {

        auto& object = config.as_object();

        for (auto& pluginName : pluginNames)
        {
            boost::json::object* curConfig;
            if (!object.contains(pluginName))
            {
                auto [it, inserted] = object.emplace(pluginName, boost::json::object{});
                curConfig = &it->value().as_object();
            }
            else
            {
                curConfig = &object[pluginName].as_object();
            }

            auto pluginParameterInfos = getInfo(pluginName);
            for (const auto& param : pluginParameterInfos)
            {
                auto paramName = param.name;
                if (!curConfig->contains(paramName))
                {
                    needSave = true;
                    auto [it,_] = curConfig->emplace(paramName, boost::json::value{});
                    switch (param.type)
                    {
                    case PluginParameterType::Int:
                        it->value().emplace_int64() = param.intValue.defaultValue;
                        break;
                    case PluginParameterType::Double:
                        it->value().emplace_double() = param.doubleValue.defaultValue;
                        break;
                    case PluginParameterType::String:
                        it->value().emplace_string() = param.stringValue.defaultValue;
                        break;
                    case PluginParameterType::Boolean:
                        it->value().emplace_bool() = param.boolValue.defaultValue;
                        break;
                    case PluginParameterType::Datetime:
                        it->value().emplace_int64() = param.datetimeValue.defaultValue;
                        break;
                    case PluginParameterType::File:
                        it->value().emplace_string() = param.fileValue.defaultValue;
                        break;
                    case PluginParameterType::Folder:
                        it->value().emplace_string() = param.folderValue.defaultValue;
                        break;
                    case PluginParameterType::Select:
                        it->value().emplace_int64() = param.selectValue.defaultValue;
                        break;
                    default:
                        it->value().emplace_null();
                    }
                }
            }
        }
    }
    if (needSave) co_await save();
    co_return;
}


struct PluginPkg
{
    library_t library = nullptr;
    IPlugin* plugin = nullptr;
    CreatePluginFunc createPlugin = nullptr;
    DestroyPluginFunc destroyPlugin = nullptr;
    GetPluginVersionFunc getPluginVersion = nullptr;
    std::string filePath;   // dll 的完整路径 (用于“在资源管理器中显示”)

    PluginPkg() = default;

    PluginPkg(library_t lib, IPlugin* p, CreatePluginFunc c,
              GetPluginVersionFunc g, DestroyPluginFunc d)
        : library(lib), plugin(p), createPlugin(c),
          destroyPlugin(d), getPluginVersion(g)
    {
    }

    PluginPkg(const PluginPkg&) = delete;
    PluginPkg& operator=(const PluginPkg&) = delete;

    PluginPkg(PluginPkg&& other) noexcept
        : library(std::exchange(other.library, nullptr)),
          plugin(std::exchange(other.plugin, nullptr)),
          createPlugin(std::exchange(other.createPlugin, nullptr)),
          destroyPlugin(std::exchange(other.destroyPlugin, nullptr)),
          getPluginVersion(std::exchange(other.getPluginVersion, nullptr)),
          filePath(std::move(other.filePath))
    {
    }

    PluginPkg& operator=(PluginPkg&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            library = std::exchange(other.library, nullptr);
            plugin = std::exchange(other.plugin, nullptr);
            createPlugin = std::exchange(other.createPlugin, nullptr);
            destroyPlugin = std::exchange(other.destroyPlugin, nullptr);
            getPluginVersion = std::exchange(other.getPluginVersion, nullptr);
            filePath = std::move(other.filePath);
        }
        return *this;
    }

    ~PluginPkg()
    {
        reset();
    }

private:
    void reset()
    {
        if (plugin && destroyPlugin)
        {
            destroyPlugin(plugin);
        }
        if (library)
        {
            ReleaseLibrary(library);
        }
        library = nullptr;
        plugin = nullptr;
        createPlugin = nullptr;
        destroyPlugin = nullptr;
        getPluginVersion = nullptr;
    }
};


struct PluginManager::Impl
{
    Impl(Logger& tagLogger)
        : tagLogger_(tagLogger, "PluginManager")
    {
    }

    // Serializes activateConfig (switchConfig + plugin execute): activations
    // can arrive from the process-monitor auto-switch (UI thread) and the
    // frontend plugins_activate / pluginsCreateConfig (pool thread) at the
    // same time, and must not overlap.
    std::mutex activateMutex;

    std::flat_map<std::string, PluginPkg> plugins;

    TagLogger tagLogger_;
};


PluginManager::PluginManager(Logger& logger)
    : m_(std::make_unique<Impl>(logger)), logger_(logger), pluginConfig_(logger, *this)
{
}

PluginManager::~PluginManager()
{

}

std::execution::task<void> PluginManager::loadPlugins()
{
    const auto pluginPath = PluginDir();
    fs::create_directories(pluginPath);

    for (fs::directory_iterator iter(pluginPath); auto& entry : iter)
    {
        if (entry.is_regular_file() && entry.path().extension() == kPluginExtension)
        {
            PluginPkg pkg;
            pkg.filePath = entry.path().string();

            library_t library = OpenLibrary(entry.path());
            if (!library)
            {
                m_->tagLogger_.Error("Failed to open plugin library: {}", entry.path().string());
                continue;
            }
            pkg.library = library;
            auto getIVerFun = GetProcAddress<GetPluginVersionFunc>(library, GET_PLUGIN_INTERFACE_VERSION_FUN_STR);
            if (!getIVerFun)
            {
                m_->tagLogger_.Error("Failed to load plugin '{}': missing interface version function",
                                     entry.path().string());
                continue;
            }
            pkg.getPluginVersion = getIVerFun;
            auto pIVersion = getIVerFun();
            if (pIVersion != PLUGIN_INTERFACE_VERSION)
            {
                m_->tagLogger_.Error("Failed to load plugin '{}': interface version mismatch (expected {}, got {})",
                                     entry.path().string(), PLUGIN_INTERFACE_VERSION, pIVersion);
                continue;
            }

            auto createFun = GetProcAddress<CreatePluginFunc>(library, CREATE_FUN_STR);
            if (!createFun)
            {
                m_->tagLogger_.Error("Failed to load plugin '{}': missing create function", entry.path().string());
                continue;
            }
            pkg.createPlugin = createFun;
            auto destroyFun = GetProcAddress<DestroyPluginFunc>(library, DESTROY_FUN_STR);
            if (!destroyFun)
            {
                m_->tagLogger_.Error("Failed to load plugin '{}': missing destroy function", entry.path().string());
                continue;
            }
            pkg.destroyPlugin = destroyFun;
            auto plugin = createFun();
            if (!plugin)
            {
                m_->tagLogger_.Error("Failed to load plugin '{}': create function returned null",
                                     entry.path().string());
                continue;
            }
            pkg.plugin = plugin;
            pkg.plugin->initialize(&logger_);
            auto [it, inserted] = m_->plugins.emplace(plugin->name(), std::move(pkg));
            if (!inserted)
            {
                m_->tagLogger_.Error("Duplicate plugin name: {}", plugin->name());
            }
        }
    }
    co_await pluginConfig_.initConfig();
    co_return;
}



void PluginManager::destroyPlugins() const
{
    for (auto& pkg : m_->plugins.values())
    {
        pkg.plugin->uninitialize();
    }
    m_->plugins.clear();
}

const std::vector<std::string>& PluginManager::getPluginNames() const
{
    return m_->plugins.keys();
}

PluginConfig& PluginManager::getPluginConfig()
{
    return pluginConfig_;
}

IPlugin* PluginManager::getPluginByName(const std::string& name)
{
    auto found = m_->plugins.find(name);
    if (found != m_->plugins.end())
        return found->second.plugin;
    return nullptr;
}

std::string PluginManager::getPluginPath(const std::string& name) const
{
    auto found = m_->plugins.find(name);
    if (found != m_->plugins.end())
        return found->second.filePath;
    return {};
}

const std::vector<std::string>& PluginManager::getConfigNameList() const
{
    return pluginConfig_.getConfigNameList();
}

void PluginManager::activateConfig(const std::string& configName)
{
    // Serialize the whole activation (switch + plugin execute): concurrent
    // calls from the process-monitor auto-switch and the frontend would
    // otherwise race on PluginConfig state and on the plugins themselves.
    std::lock_guard<std::mutex> lock(m_->activateMutex);
    bool config = pluginConfig_.switchConfig(configName);
    if (!config) return throw std::runtime_error("Config not found");

    for (auto [name, pkg] : m_->plugins)
    {
        // 该配置中禁用的插件不执行
        if (!pluginConfig_.isPluginEnabled(name))
        {
            m_->tagLogger_.Info("配置 '{}': 插件 '{}' 已禁用, 跳过执行", configName, name);
            continue;
        }

        PluginParameterValue* value = pluginConfig_.getCurrentConfigParameter(name);
        if (value)
        {
            pkg.plugin->execute(value);
        }
    }


}
