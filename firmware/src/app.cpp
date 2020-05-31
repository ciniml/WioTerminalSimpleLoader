#include "app.h"
#include "definitions.h"                // SYS function prototypes
#include <cstdint>
#include <array>
#include <vector>

#include <LovyanGFX.hpp>

#include "appmanager.hpp"

APP_DATA appData;

static constexpr std::uint32_t SwitchUp    = (1u << 0);
static constexpr std::uint32_t SwitchDown  = (1u << 1);
static constexpr std::uint32_t SwitchLeft  = (1u << 2);
static constexpr std::uint32_t SwitchRight = (1u << 3);
static constexpr std::uint32_t SwitchPush = (1u << 4);

static LGFX lcd;
void APP_Initialize ( void )
{
    appData.state = APP_STATE_INIT;
    appData.selectedApp = 0;
    appData.prevSwitchInputs = 0;
    appData.forceUpdateScreen = false;
    USER_LED_OutputEnable();
}


static int color = 0;
static std::uint8_t backlightOutput = 0;
static AppManager appManager;
static std::array<AppDescription, 10> apps;

void APP_Tasks ( void )
{
    std::uint32_t switchInputs = 0;
    switchInputs |= SWITCH_U_Get() ? 0 : SwitchUp;
    switchInputs |= SWITCH_X_Get() ? 0 : SwitchDown;
    switchInputs |= SWITCH_B_Get() ? 0 : SwitchLeft;
    switchInputs |= SWITCH_Y_Get() ? 0 : SwitchRight;
    switchInputs |= SWITCH_Z_Get() ? 0 : SwitchPush;
    
    std::uint32_t pushedInputs = (switchInputs ^ appData.prevSwitchInputs) & switchInputs;

    /* Check the application's current state. */
    switch ( appData.state )
    {
        /* Application's initial state. */
        case APP_STATE_INIT:
        {
            bool appInitialized = true;
            
            lcd.init();
            lcd.fillScreen(0);

            NVMCTRL_Initialize();
            if (appInitialized)
            {
                appData.state = APP_STATE_NO_SD;
            }
            TC0_Compare8bitMatch0Set(100);
            break;
        }
        case APP_STATE_NO_SD: {
            lcd.fillScreen(0);
            lcd.setFont(&fonts::FreeMonoOblique12pt7b);
            lcd.setTextDatum(lgfx::textdatum::middle_center);
            lcd.drawString("NO TF CARD", 160, 120);

            auto error = appManager.scan([](std::size_t, const AppDescription&){return false;});
            if( error == decltype(error)::Success) {
                appData.state = APP_STATE_LOAD_SD;
            }
            else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            break;
        }
        case APP_STATE_LOAD_SD:{
            lcd.fillScreen(0);
            lcd.setFont(&fonts::FreeMonoOblique12pt7b);
            lcd.setTextDatum(lgfx::textdatum::middle_center);
            lcd.drawString("LOADING...", 160, 120);

            appData.appsInPage = 0;
            auto error = appManager.scan([](std::size_t index, const AppDescription& description){
                apps[index] = description;
                appData.appsInPage = index + 1;
                return index < apps.size();
            });
            switch(error)
            {
                case AppManager::Error::FailedToMount: {
                    appData.state = APP_STATE_NO_SD;
                    break;
                }
                case AppManager::Error::Success: {
                    appData.state = APP_STATE_SELECT_APP;
                    appData.forceUpdateScreen = true;
                    appData.selectedApp = 0;
                    break;
                }
            }

            break;
        }

        case APP_STATE_SELECT_APP:
        {
            USER_LED_Toggle();

            bool moved = false;            
            if( pushedInputs & SwitchUp ) {
                if( appData.selectedApp > 0 ) {
                    appData.selectedApp--;
                }
                moved = true;
            }
            if( pushedInputs & SwitchDown ) {
                if( appData.selectedApp < appData.appsInPage - 1 ) {
                    appData.selectedApp++;
                }
                moved = true;
            }
            if( pushedInputs & SwitchPush ) {
                lcd.clear(0);
                appData.state = APP_STATE_LOAD_APP;
            }
            if( moved || appData.forceUpdateScreen ) {
                appData.forceUpdateScreen = false;
                lcd.setWindow(0, 0, 320, 240);
                lcd.clear(0);
                lcd.setColor(lgfx::color888(255, 255, 255));
                lcd.setTextDatum(lgfx::textdatum::top_left);
                lcd.setFont(&fonts::FreeMono9pt7b);
                lcd.setWindow(0, 0, 160, 240);
                for(std::size_t i = 0; i < apps.size(); i++ ) {
                    const auto& description = apps.at(i);
                    lcd.drawString(description.getName(), 0, 240*i/apps.size()+5);
                    if( i == appData.selectedApp ) {
                        lcd.drawRect(0, 240*appData.selectedApp/apps.size(), 160, 240/apps.size());
                    }
                }
                {
                    const auto& description = apps.at(appData.selectedApp);
                    lcd.setTextWrap(true, true);
                    lcd.setWindow(160, 120, 320, 240);
                    lcd.setTextDatum(textdatum_t::top_left);
                    lcd.drawString(description.getDescription(), 160, 120);
                    
                    char pathBuffer[65];
                    if( appManager.getAppIconPath(description, pathBuffer, sizeof(pathBuffer)) == AppManager::Error::Success ) {
                        SDCardMount mount;
                        if( mount ) {
                            switch(description.getAppIconFormat()) {
                                case AppIconFormat::Png: lcd.drawPngFile(pathBuffer,160, 0, 160, 120); break;
                                case AppIconFormat::Bmp: lcd.drawBmpFile(pathBuffer,160, 0); break;
                                case AppIconFormat::Jpg: lcd.drawJpgFile(pathBuffer,160, 0, 160, 120); break;
                            }
                        }
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }
        case APP_STATE_LOAD_APP:
        {
            const auto& description = apps.at(appData.selectedApp);
            lcd.setTextDatum(lgfx::textdatum::bottom_center);
            lcd.drawString(description.getName(), 160, 120);
            const auto result = appManager.load(description, 0x4000, [](std::size_t bytesWritten, std::size_t bytesTotal){
                lcd.fillRect(40, 120, 240*bytesWritten/bytesTotal, 32, lgfx::color888(0, 0, 255));
                lcd.drawRect(40, 120, 240, 32, lgfx::color888(255, 255, 255));
                return true;
            });
            appData.fallbackState = APP_STATE_SELECT_APP;
            switch(result) {
                case AppManager::Error::Success: {
                    appManager.run(0x4000);
                    // Never return
                    break;
                }
                case AppManager::Error::BinaryTooLarge: {
                    lcd.drawString("Error:\n app binary too large.", 160, 200);
                    appData.state = APP_STATE_ERROR;
                    break;
                }
                case AppManager::Error::FailedToOpen: {
                    lcd.drawString("Error:\n failed to open bin file.", 160, 200);
                    appData.state = APP_STATE_ERROR;
                    break;
                }
                case AppManager::Error::FailedToMount: {
                    lcd.drawString("Error:\n failed to mount TF card.", 160, 200);
                    appData.state = APP_STATE_ERROR;
                    appData.fallbackState = APP_STATE_NO_SD;
                    break;
                }
            }
            break;
        }
        case APP_STATE_ERROR: 
        {
            if( pushedInputs ) {
                appData.state = appData.fallbackState;
                appData.forceUpdateScreen = true;
            }
            break;
        }
        case APP_STATE_END:
            TC0_Compare8bitMatch0Set(backlightOutput);
            backlightOutput += 1;

            if( backlightOutput > 100 ) {
                backlightOutput = 0;
            }
            USER_LED_Toggle();
            lcd.fillScreen(lgfx::color565(0, 255, 0));
            break;
        
        /* The default state should never be executed. */
        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }

    appData.prevSwitchInputs = switchInputs;
}


/*******************************************************************************
 End of File
 */
