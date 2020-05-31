#include "appmanager.hpp"

#include "definitions.h"                // SYS function prototypes

#include <memory>

struct Deferred
{
    std::function<void (void)> function;
    template<typename TFunction>
    Deferred(const TFunction& function) : function(function) {}
    ~Deferred() 
    {
        this->function();
    }
};


template<typename T>
struct Option
{
    T value;
    bool hasValue;

    Option() : hasValue(false) {}
    Option(const T& value) : value(value), hasValue(true) {}
    Option(T&& value) : value(value), hasValue(true) {}
    template<typename S>
    Option(const Option<S>& rhs) : value(rhs.value), hasValue(rhs.hasValue) {}
    template<typename S>
    Option(Option<S>&& rhs) : value(std::forward<T>(rhs.value)), hasValue(rhs.hasValue) {}

    operator bool() const { return this->hasValue; }
};

template<typename T>
static Option<T> makeOption(T value) { return Option<T>(value); }


static Option<std::size_t> readMetaData(const char* appsRoot, const char* appName, const char* metaName, char* buffer, std::size_t maxLength )
{
    FixedString<64> path(appsRoot);
    path.append("/");
    path.append(appName);
    path.append("/");
    path.append(metaName);
    auto hfile = SYS_FS_FileOpen(path.c_str(), SYS_FS_FILE_OPEN_ATTRIBUTES::SYS_FS_FILE_OPEN_READ);
    if( hfile == SYS_FS_HANDLE_INVALID ) {
        return Option<std::size_t>();
    }

    Deferred closeFile = [hfile](){SYS_FS_FileClose(hfile);};

    auto fileSize = SYS_FS_FileSize(hfile);
    fileSize = std::min(maxLength, static_cast<std::size_t>(fileSize));
    for(std::size_t totalBytesRead = 0; totalBytesRead < fileSize; ) {
        auto bytesToRead = fileSize - totalBytesRead;
        auto bytesRead = SYS_FS_FileRead(hfile, buffer + totalBytesRead, bytesToRead);
        if( bytesRead == 0 ) {
            return Option<std::size_t>();
        }
        totalBytesRead += bytesRead;
    }

    return makeOption(fileSize);
}


static const char* getExtension(AppIconFormat format) 
{
    switch(format) {
        case AppIconFormat::Jpg: return ".jpg";
        case AppIconFormat::Bmp: return ".bmp";
        case AppIconFormat::Png: return ".png";
        default: return "";
    }
}

static AppIconFormat getIconFormat(const char* appsRoot, const char* appName, const char* iconName)
{
    std::array<AppIconFormat, 3> formats = {
        AppIconFormat::Jpg,
        AppIconFormat::Bmp,
        AppIconFormat::Png,
    };
    for(const auto format : formats) {
        FixedString<64> path(appsRoot);
        path.append("/");
        path.append(appName);
        path.append("/");
        path.append(iconName);
        path.append(getExtension(format));
        SYS_FS_FSTAT fstat;
        fstat.lfname = nullptr;
        fstat.lfsize = 0;
        if( SYS_FS_FileStat(path.c_str(), &fstat) == SYS_FS_ERROR_OK ) {
            return format;
        }
    }
    return AppIconFormat::None;
}


bool AppManager::readDescription(const char* appsRoot, const char* appName, AppDescription& description)
{
    auto nameSize = readMetaData(appsRoot, appName, "name", description.name.data(), description.name.max_size());
    if( nameSize ) {
        description.name.set_length(nameSize.value);
    }
    else {
        // Name metadata is required.
        return false;
    }
    
    {
        auto size = readMetaData(appsRoot, appName, "desc", description.description.data(), description.description.max_size());
        description.description.set_length(size ? size.value : 0);
    }

    {
        auto size = readMetaData(appsRoot, appName, "author", description.authorName.data(), description.authorName.max_size());
        description.authorName.set_length(size ? size.value : 0);
    }
    
    description.setLocation(appName);

    description.authorIcon = getIconFormat(appsRoot, appName, "author");
    description.appIcon = getIconFormat(appsRoot, appName, "app");
    return true;
}

static constexpr const char* SdBlockDevice = "/dev/mmcblka1";
static constexpr const char* SdRoot = "/mnt/sd";
static constexpr const char* AppsRoot = "/mnt/sd/apps";
static constexpr std::size_t AppAddressLowerLimit = 0x4000;
static constexpr std::size_t AppAddressUpperLimit = 0x80000;

SDCardMount::SDCardMount() : SDCardMount(SdBlockDevice, SdRoot) {}
SDCardMount::SDCardMount(const char* blockDevicePath, const char* mountPoint) 
{
    this->mountPoint = SYS_FS_Mount(blockDevicePath, mountPoint, SYS_FS_FILE_SYSTEM_TYPE::FAT, 0, nullptr) == SYS_FS_RES_SUCCESS ? mountPoint : nullptr;
}
SDCardMount::~SDCardMount()
{
    if( this->mountPoint != nullptr ) {
        SYS_FS_Unmount(this->mountPoint);
    }
}

AppManager::Error AppManager::scan(std::function<bool (std::size_t index, const AppDescription& description)>&& callback)
{
    SDCardMount mount(SdBlockDevice, SdRoot);
    if( !mount ) {
        return AppManager::Error::FailedToMount;
    }

    auto hdir = SYS_FS_DirOpen(AppsRoot);
    if( hdir == SYS_FS_HANDLE_INVALID ) {
        return Error::NoAppsDir;
    }

    Deferred closeDir = [hdir](){SYS_FS_DirClose(hdir);};

    FixedString<256> lfnBuffer;
    SYS_FS_FSTAT fstat;
    fstat.lfname = lfnBuffer.data();
    fstat.lfsize = lfnBuffer.max_size();
    std::size_t index = 0;
    while( SYS_FS_DirRead(hdir, &fstat) == SYS_FS_RES_SUCCESS && fstat.fname[0] != 0) {
        if( fstat.fname[0] == '.' && (fstat.fname[1] == 0 || (fstat.fname[1] == '.' && fstat.fname[2] == 0)) ) {
            continue;
        }
        if(  fstat.fattrib & SYS_FS_ATTR_DIR ) {
            AppDescription description;
            const char* name = fstat.lfname[0] != 0 ? fstat.lfname : fstat.fname;
            if( readDescription(AppsRoot, name, description) ) {
                if( !callback(index, description) ) {
                    break;
                }
            }
            index++;
        }
    }

    return Error::Success;
}


AppManager::Error AppManager::load(const AppDescription& description, std::uintptr_t offset, std::function<bool (std::size_t bytesWritten, std::size_t bytesTotal)>&& callback)
{
    if( offset < AppAddressLowerLimit ) {
        return Error::InvalidOffset;
    }

    SDCardMount mount;
    if( !mount ) {
        return AppManager::Error::FailedToMount;
    }

    FixedString<128> path(AppsRoot);
    path.append("/");
    path.append(description.getLocation());
    path.append("/");
    path.append("app.bin");

    auto handle = SYS_FS_FileOpen(path.c_str(), SYS_FS_FILE_OPEN_ATTRIBUTES::SYS_FS_FILE_OPEN_READ);
    if( handle == SYS_FS_HANDLE_INVALID ) {
        return Error::FailedToOpen;
    }

    Deferred close = [handle](){ SYS_FS_FileClose(handle); };

    auto fileSize = SYS_FS_FileSize(handle);
    auto upperAddress = fileSize + offset;
    if( upperAddress > AppAddressUpperLimit ) {
        return Error::BinaryTooLarge;
    }

    if( fileSize > 0 ) {
        std::uint32_t pageBuffer[NVMCTRL_FLASH_PAGESIZE/4];
        for(std::uintptr_t bytesWritten = 0; bytesWritten < fileSize; bytesWritten += NVMCTRL_FLASH_PAGESIZE) {
            if( (bytesWritten & (NVMCTRL_FLASH_BLOCKSIZE-1)) == 0 ) {
                // Erase block
                NVMCTRL_BlockErase(bytesWritten + offset);
                while(NVMCTRL_IsBusy());
            }
            auto bytesToRead = fileSize - bytesWritten;
            bytesToRead = bytesToRead > NVMCTRL_FLASH_PAGESIZE ? NVMCTRL_FLASH_PAGESIZE : bytesToRead;
            SYS_FS_FileRead(handle, pageBuffer, bytesToRead);
            memset(reinterpret_cast<std::uint8_t*>(pageBuffer) + bytesToRead, 0xff, NVMCTRL_FLASH_PAGESIZE - bytesToRead);
            NVMCTRL_PageWrite(pageBuffer, offset+bytesWritten);
            while(NVMCTRL_IsBusy());

            if( !callback(bytesWritten, fileSize) ) {
                return Error::UserCancelled;
            }
        }
    }

    return Error::Success;
}

void __attribute__((noreturn)) AppManager::run(std::uintptr_t offset)
{
    __disable_irq();
    auto vector_top = reinterpret_cast<volatile std::uint32_t*>(offset);
    auto reset_vector = reinterpret_cast<void (*)()>(*(vector_top + 1));
    SCB->VTOR = offset;
    __DSB();
    auto stack_top = *vector_top;
    __asm__("mov r13, %[stack_top]":: [stack_top] "r" (stack_top));
    reset_vector();
}

AppManager::Error AppManager::getIconPath(AppIconFormat format, const char* location, const char* baseName, char* buffer, std::size_t bufferSize)
{
    if( format == AppIconFormat::None ) {
        return Error::NoIcon;
    }

    FixedString<64> path(AppsRoot);
    path.append("/");
    path.append(location);
    path.append("/");
    path.append(baseName);
    path.append(getExtension(format));

    if( bufferSize < path.length + 1 ) {
        return Error::TooFewBuffer;
    }
    strcpy(buffer, path.c_str());
    return Error::Success;
}
AppManager::Error AppManager::getAuthorIconPath(const AppDescription& description, char* buffer, std::size_t bufferSize)
{
    return getIconPath(description.getAuthorIconFormat(), description.getLocation(), "author", buffer, bufferSize);
}
AppManager::Error AppManager::getAppIconPath(const AppDescription& description, char* buffer, std::size_t bufferSize)
{
    return getIconPath(description.getAppIconFormat(), description.getLocation(), "app", buffer, bufferSize);
}