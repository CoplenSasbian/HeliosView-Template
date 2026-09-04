#pragma once
#include <cstdint>
enum class PluginParameterType
{
    Int,
    Double,
    String,
    Boolean,
    Datetime,
    File,
    Folder,
    Select
};

struct PluginParameterValue
{
    virtual ~PluginParameterValue() = default;
    [[nodiscard]] virtual PluginParameterType getType(const char* name) const noexcept = 0;
    [[nodiscard]] virtual int64_t getInt64Value(const char* name) const noexcept = 0;
    [[nodiscard]] virtual double getDoubleValue(const char* name) const noexcept = 0;
    [[nodiscard]] virtual bool getBoolValue(const char* name) const noexcept = 0;
    [[nodiscard]] virtual const char* getStringValue(const char* name) const noexcept = 0;
    [[nodiscard]] virtual int64_t getDateTimeValue(const char* name) const noexcept = 0;
    [[nodiscard]] virtual const char* getFileValue(const char* name) const noexcept = 0;
    [[nodiscard]] virtual const char* getFolderValue(const char* name) const noexcept = 0;


    virtual void setInt64Value(const char* name, int64_t value) noexcept = 0;
    virtual void setDoubleValue(const char* name, double value) noexcept = 0;
    virtual void setBoolValue(const char* name, bool value) noexcept = 0;
    virtual void setStringValue(const char* name, const char* value) noexcept = 0;
    virtual void setDateTimeValue(const char* name, int64_t value) noexcept = 0;
    virtual void setFileValue(const char* name, const char* value) noexcept = 0;
    virtual void setFolderValue(const char* name, const char* value) noexcept = 0;
};


struct PluginParameterInfo
{
    PluginParameterType type;
    const char* name;        // 参数 key: 存取值时使用, 不适合直接展示
    const char* label;       // 展示名称 (UI 显示用); 可为 nullptr → 前端回退到 description/name
    const char* description; // 详细说明 (可能较长, UI 截断或 hover 展示)
    union
    {
        struct IntValue
        {
            int64_t defaultValue;
            int64_t minValue;
            int64_t maxValue;
        } intValue;

        struct DoubleValue
        {
            double defaultValue;
            double minValue;
            double maxValue;
            double step;
        } doubleValue;

        struct StringValue
        {
            const char* defaultValue;
        } stringValue;

        struct BoolValue
        {
            bool defaultValue;
        } boolValue;

        struct DatetimeValue
        {
            int64_t defaultValue;
        } datetimeValue;
        struct FileValue
        {
            const char* defaultValue;
            const char* filter;   // 文件对话框过滤串 (COMDLG 格式 "名称 (*.ext)|*.ext", 可为 nullptr = 全部文件)
        } fileValue;

        struct FolderValue
        {
            const char* defaultValue;
        } folderValue;

        struct SelectValue
        {
            const char* const* options;   // 选项数组: 由插件持有并保证在插件生命周期内有效
            int optionCount;              // 选项个数
            int defaultValue;             // 默认选中的下标 (值以 int 下标形式存取)
        } selectValue;
    };

};