
#include <chrono>
#include <IPlugin.h>
#include <format>
#include <print>
#include <vector>
namespace
{
    class TestPlugins : public IPlugin
    {
    public:
        ~TestPlugins() override= default;
        int version() noexcept override
        {
            return 1;
        }
        const char* name() noexcept override
        {
            return "TestPlugin";
        }
        const char* description() noexcept override
        {
            return "A test plugin for verifying the plugin system";
        }

        void initialize(ILogger* logger) noexcept override
        {
            this->logger = logger;
        
            // Reserve first: emplace_back() may reallocate the vector and
            // invalidate references to earlier elements while they are being
            // filled in. auto& (not auto) writes into the vector element itself.
            parameters.reserve(9);
        
            //int
            auto& param1 = parameters.emplace_back();
            param1.name = "param1";
            param1.label = "整数参数";
            param1.description = "This is a parameter of type int";
            param1.type = PluginParameterType::Int;
            param1.intValue.defaultValue = this->param1;
            param1.intValue.maxValue = 100.0f;
            param1.intValue.minValue = 0.0f;
        
            //double
            auto& param2 = parameters.emplace_back();
            param2.name = "param2";
            param2.label = "浮点参数";
            param2.description = "This is a parameter of type double";
            param2.type = PluginParameterType::Double;
            param2.doubleValue.defaultValue = this->param2;
            param2.doubleValue.maxValue = 100.0f;
            param2.doubleValue.minValue = 0.0f;
        
            // bool
            auto& param3 = parameters.emplace_back();
            param3.description = "This is a parameter of type bool";
            param3.name = "param3";
            param3.label = "开关参数";
            param3.type = PluginParameterType::Boolean;
            param3.boolValue.defaultValue = this->param3;
        
            // string
        
            auto& param4 = parameters.emplace_back();
            param4.description = "This is a parameter of type string";
            param4.name = "param4";
            param4.label = "文本参数";
            param4.type = PluginParameterType::String;
            param4.stringValue.defaultValue = this->param4.c_str();
        
            //Datetime
            auto& param5 = parameters.emplace_back();
            param5.description = "This is a parameter of type datetime";
            param5.name = "param5";
            param5.label = "时间参数";
            param5.type = PluginParameterType::Datetime;
            param5.datetimeValue.defaultValue = this->param5;
        
            //File
            auto& param6 = parameters.emplace_back();
            param6.description = "This is a parameter of type file";
            param6.name = "param6";
            param6.label = "文件参数";
            param6.type = PluginParameterType::File;
            param6.fileValue.defaultValue = this->param6.c_str();
        
            //Folder
            auto& param7 = parameters.emplace_back();
            param7.description = "This is a parameter of type folder";
            param7.name = "param7";
            param7.label = "文件夹参数";
            param7.type = PluginParameterType::Folder;
            param7.folderValue.defaultValue = this->param7.c_str();

            // Int WITHOUT a declared range — the UI must fall back to a plain
            // number input (a 0..0 slider would clamp every value).
            auto& param8 = parameters.emplace_back();
            param8.name = "param8";
            param8.label = "无范围整数";
            param8.description = "This is an int parameter WITHOUT min/max range";
            param8.type = PluginParameterType::Int;
            param8.intValue.defaultValue = this->param8;

            // Double WITHOUT a declared range — same fallback.
            auto& param9 = parameters.emplace_back();
            param9.name = "param9";
            param9.label = "无范围浮点";
            param9.description = "This is a double parameter WITHOUT min/max range";
            param9.type = PluginParameterType::Double;
            param9.doubleValue.defaultValue = this->param9;
        
        }

        template<class ...Args>
        void info(const std::format_string<Args...> format, Args... args)
        {
            auto mess = std::format(format, std::forward<Args>(args)...);
            logger->log( ILogger::Info, "TestPlugin", mess.c_str());
        }

        template<class ...Args>
        void error(const std::format_string<Args...> format, Args... args)
        {
            auto mess = std::format(format, std::forward<Args>(args)...);
            logger->log( ILogger::Error, "TestPlugin", mess.c_str());
        }

        template<class ...Args>
        void warn(const std::format_string<Args...> format, Args... args)
        {
            auto mess = std::format(format, std::forward<Args>(args)...);
            logger->log( ILogger::Warning, "TestPlugin", mess.c_str());
        }


        void uninitialize() noexcept override{}
        void pluginParameters(PluginParameterInfo** parameters, int* count) noexcept override
        {
            *parameters = this->parameters.data();

            *count = static_cast<int>(this->parameters.size());
        }
        bool execute(PluginParameterValue* parameters) noexcept override
        {
            param1 = parameters->getInt64Value("param1");
            param2 = parameters->getDoubleValue("param2");
            param3 = parameters->getBoolValue("param3");
            param4 = parameters->getStringValue("param4");
            param5 = parameters->getDateTimeValue("param5");
            auto chronoTime = std::chrono::system_clock::from_time_t(param5);
            param6 = parameters->getFileValue("param6");
            param7 = parameters->getFolderValue("param7");
            param8 = parameters->getInt64Value("param8");
            param9 = parameters->getDoubleValue("param9");

            info("exec int {}, double {}, bool {}, string {}, datetime {}, file {}, folder {}, int_norange {}, double_norange {}", param1, param2, param3, param4, param5, param6, param7, param8, param9);

            parameters->setInt64Value("param1", param1+1);
            parameters->setDoubleValue("param2", param2+.1);
            parameters->setBoolValue("param3", !param3);
            param4 += "_m";
            parameters->setStringValue("param4", param4.c_str());
            parameters->setDateTimeValue("param5", param5 + 86400);
            param6 += "_f";
            parameters->setFileValue("param6", param6.c_str());
            param7 += "_d";
            parameters->setFolderValue("param7", param7.c_str());
            parameters->setInt64Value("param8", param8 + 1);
            parameters->setDoubleValue("param9", param9 + 0.1);

            return true;
        }
    private:
        ILogger* logger = nullptr;
        std::vector<PluginParameterInfo> parameters;

        int64_t param1 = 0;
        double param2 = 0.0;
        bool param3 = false;
        std::string param4 = "default";
        int64_t param5 = 0;
        std::string param6 = "default.txt";
        std::string param7 = "default_folder";
        int64_t param8 = 42;
        double param9 = 3.14;
    };
}


REGISTER_PLUGIN(TestPlugins)