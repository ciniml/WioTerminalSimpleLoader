#ifndef APPMANAGER_HPP___
#define APPMANAGER_HPP___

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <functional>

static std::size_t strnlen_s(const char* s, std::size_t n) {
    std::size_t i = 0;
    for(; i < n && *s; i++, s++);
    return i;
}

template<std::size_t N>
struct FixedString
{
    std::size_t length;
    std::array<char, N+1> body;

    FixedString() {
        this->set_length(0);
    }

    template<std::size_t RN>
    FixedString(const FixedString<RN>& rhs)  { this->set(rhs); }
    FixedString(const std::string& rhs)  { this->set(rhs); }
    FixedString(const char* str, std::size_t length) { this->set(str, length); }
    FixedString(const char* c_str) { this->set(c_str); }
    
    template<std::size_t RN>
    FixedString& operator=(const FixedString<RN>& rhs) { this->set(rhs); return *this; }
    FixedString& operator=(const std::string& rhs) { this->set(rhs); return *this; }
    FixedString& operator=(const char* c_str) { this->set(c_str); return *this; }

    template<std::size_t RN>
    void set(const FixedString<RN>& rhs)
    {
        this->length = rhs.length;
        std::copy(rhs.begin(), rhs.begin()+rhs.length, this->body.begin());
        this->body[this->length] = 0;
    }

    void set(const std::string& rhs)
    {
        this->length = std::min(N, rhs.size());
        std::copy(rhs.begin(), rhs.begin()+this->length, this->body.begin());
        this->body[this->length] = 0;
    }

    void set(const char* str, std::size_t length)
    {
        this->length = std::min(N, length);
        std::copy(str, str+length, this->body.begin());
        this->body[this->length] = 0;
    }

    void set(const char* c_str)
    {
        this->length = strnlen_s(c_str, N);
        std::copy(c_str, c_str+length, this->body.begin());
        this->body[this->length] = 0;
    }

    char& operator[](std::size_t index) { return this->body[index]; }

    std::size_t size() const { return this->length; }
    std::size_t max_size() const { return N; }

    char* data() { return this->body.data(); }
    const char* c_str() const { return this->body.data(); }

    void set_length(std::size_t length) {
        this->length = std::min(length, N);
        this->body[this->length] = 0;
    }

    void append(const char* s) {
        auto remaining = N - this->length;
        auto charsToCopy = strnlen_s(s, remaining);
        std::copy(s, s+charsToCopy, this->body.begin()+this->length);
        this->length += charsToCopy;
        this->body[this->length] = 0;
    }
};

enum class AppIconFormat
{
    None,
    Bmp,
    Png,
    Jpg,
};

class AppManager;
class AppDescription
{
public:
    static constexpr std::size_t MaxLocationLength = 64;
    static constexpr std::size_t MaxNameLength = 64;
    static constexpr std::size_t MaxDescriptionLength = 64;
    static constexpr std::size_t MaxAuthorNameLength = 32;
private:
    FixedString<MaxLocationLength> location;
    FixedString<MaxNameLength> name;
    FixedString<MaxDescriptionLength> description;
    FixedString<MaxAuthorNameLength> authorName;
    AppIconFormat authorIcon = AppIconFormat::None;
    AppIconFormat appIcon = AppIconFormat::None;
public:
    AppDescription() {}

    const char* getLocation() const { return this->location.c_str(); }
    const char* getName() const { return this->name.c_str(); }
    const char* getDescription() const { return this->description.c_str(); }
    const char* getAuthorName() const { return this->authorName.c_str(); }

    void setLocation(const char* value) { this->location.set(value); }
    void setName(const char* value) { this->name.set(value); }
    void setDescription(const char* value) { this->description.set(value); }
    void setAuthorName(const char* value) { this->authorName.set(value); }

    AppIconFormat getAuthorIconFormat() const { return this->authorIcon; }
    AppIconFormat getAppIconFormat() const { return this->appIcon; }

    friend class AppManager;
};


class SDCardMount
{
private:
    const char* mountPoint;
public:
    SDCardMount();
    SDCardMount(const char* blockDevicePath, const char* mountPoint);
    SDCardMount(SDCardMount&& rhs)
    {
        this->mountPoint = rhs.mountPoint;
        rhs.mountPoint = nullptr;
    }
    ~SDCardMount();

    operator bool() const { return this->mountPoint != nullptr; }
};

class AppManager
{
public:
    enum class Error
    {
        Success,
        FailedToMount,
        NoAppsDir,
        FailedToOpen,
        InvalidOffset,
        BinaryTooLarge,
        UserCancelled,
        TooFewBuffer,
        NoIcon,
    };
private:
    static bool readDescription(const char* appsRoot, const char* appName, AppDescription& description);
    static Error getIconPath(AppIconFormat format, const char* location, const char* baseName, char* buffer, std::size_t bufferSize);
public:
    Error getAuthorIconPath(const AppDescription& description, char* buffer, std::size_t bufferSize);
    Error getAppIconPath(const AppDescription& description, char* buffer, std::size_t bufferSize);
    Error scan(std::function<bool (std::size_t index, const AppDescription& description)>&& callback);
    Error load(const AppDescription& description, std::uintptr_t offset, std::function<bool (std::size_t bytesWritten, std::size_t bytesTotal)>&& callback);
    void run(std::uintptr_t offset);
};



#endif //APPMANAGER_HPP___