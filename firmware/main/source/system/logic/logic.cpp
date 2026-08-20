#include "system/logic/logic.h"

#include "driver/gpio/interface.h"
#include "driver/serial/interface.h"
#include "driver/timer/interface.h"
#include "driver/wifi/interface.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
constexpr std::uint8_t LedPin{6U};
constexpr std::uint32_t DefaultPeriodMs{500U};
constexpr std::uint32_t SerialBaudRate{115200U};
constexpr const char* WifiSsid{CONFIG_CNB_WIFI_SSID};
constexpr const char* WifiPassword{CONFIG_CNB_WIFI_PASSWORD};
} // namespace

namespace app::logic
{
Logic::~Logic() noexcept = default;

Logic::Logic(driver::factory::Interface& factory) noexcept
    : mySerial{factory.serial(SerialBaudRate)}
    , myLed{factory.gpioOutput(LedPin)}
    , myTimer{factory.timer(DefaultPeriodMs)}
    , myWifi{factory.wifi(WifiSsid, WifiPassword)}
{
    setStartState();
    initializeDrivers();
}

void Logic::setStartState() noexcept
{
    myBlinkEnabled = false;
    myPeriodMs = DefaultPeriodMs;

    if (myLed) { myLed->write(false); }

    if (myTimer)
    {
        myTimer->setPeriod(myPeriodMs);
        myTimer->stop();
    }
}

void Logic::initializeDrivers() noexcept
{
    if (mySerial && !mySerial->isInitialized())
    {
        mySerial->connect();
        mySerial->write("CnB serial ready\n");
    }

#if CONFIG_CNB_ENABLE_WIFI
    if (myWifi && !myWifi->isConnected())
    {
        myWifi->connect();
    }
#endif
}

void Logic::processWifi() noexcept
{
#if CONFIG_CNB_ENABLE_WIFI
    if (myWifi && myWifi->isInitialized() && !myWifi->isConnected())
    {
        myWifi->reconnect();
    }
#endif
}

void Logic::processTimer() noexcept
{
    if (myBlinkEnabled && myLed && myTimer && myTimer->isTimeout())
    {
        myLed->toggle();
    }
}

void Logic::run(const std::atomic<bool>& stop) noexcept
{
    while (!stop.load())
    {
        processWifi();
        processTimer();
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

} // namespace app::logic
