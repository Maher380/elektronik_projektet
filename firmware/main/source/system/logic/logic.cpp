#include <cstdio>

#include "system/logic/logic.h"


#include "driver/adc/interface.h"
#include "driver/ir_sensor/interface.h"
#include "driver/gpio/interface.h"
#include "driver/motor/interface.h"
#include "driver/serial/interface.h"
#include "driver/timer/interface.h"
#include "driver/wifi/interface.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr std::uint8_t AdcPin{1U};
    constexpr std::uint8_t LedPin{6U};
    constexpr std::uint32_t DefaultPeriodMs{500U};
    constexpr std::uint32_t SerialBaudRate{115200U};
    constexpr const char *WifiSsid{CONFIG_CNB_WIFI_SSID};
    constexpr const char *WifiPassword{CONFIG_CNB_WIFI_PASSWORD};

    constexpr std::uint8_t bufLen{64U};
} // namespace

namespace app::logic
{
Logic::~Logic() noexcept = default;

Logic::Logic(driver::factory::Interface& factory) noexcept
    : myMotorForwardsPwm({factory.pwm(mp6550MotorPwmForwardPin)})
    , myMotorBackwardsPwm({factory.pwm(mp6550MotorPwmBackwardPin)})
    , myIrSensorAdc({factory.adc(IrSensorAdcPin)})
    , myMotor({factory.motor(*myMotorForwardsPwm,*myMotorBackwardsPwm)})
    , myIrSensor({factory.ir_sensor(*myIrSensorAdc)})
{
    setStartState();
    if (!initializeDrivers())
    {
        // // Skriv ut eller indikera via en LED att initieringen misslyckades.
        // if (mySerial)
        // {
        //     mySerial->write("Initialization failed!\n");
        // }

        // Detta är en loop som bara får systemet att fastna. Blinka gärna en diod i denna loop.
        while (1) {}
    }
}

void Logic::setStartState() noexcept
{
    myBlinkEnabled = false;
    myPeriodMs = DefaultPeriodMs;

    // if (myLed) { myLed->write(false); }

    // if (myTimer)
    // {
    //     myTimer->setPeriod(myPeriodMs);
    //     myTimer->stop();
    // }
}

bool Logic::initializeDrivers() noexcept
{
    // if (mySerial)
    // {
    //     mySerial->connect();
    //     mySerial->write("CnB serial ready\n");
    // }
    // else
    // {
    //     return false;
    // }

    // if (myAdc && !myAdc->isInitialized())
    // {
    //     myAdc->init();
    // }

// #if CONFIG_CNB_ENABLE_WIFI
//     if (myWifi && !myWifi->isConnected())
//     {
//         myWifi->connect();
//     }
// #endif
//     // Om vi kommer hit har all hårdvara initierates korrekt => returnera true.
    return true;
}

void Logic::processWifi() noexcept
{
// #if CONFIG_CNB_ENABLE_WIFI
//     if (myWifi && myWifi->isInitialized() && !myWifi->isConnected())
//     {
//         myWifi->reconnect();
//     }
// #endif
}

void Logic::processTimer() noexcept
{
    // if (myBlinkEnabled && myLed && myTimer && myTimer->isTimeout())
    // {
    //     myLed->toggle();
    // }
}

void Logic::processDistance() noexcept
{
    // const auto distance = myIr->readDistance();
    // // Testa att skriva ut distansen, kolla att den ser rimlit ut (sanity check).
    // char buf[bufLen]{'\0'};
    // std::snprintf(buf, sizeof(buf), "Distance: %.2f\n", static_cast<double>(distance));
    // mySerial->write(buf);
}

void Logic::run(const std::atomic<bool>& stop) noexcept
{
    while (!stop.load())
    {
        processWifi();
        processTimer();
        vTaskDelay(pdMS_TO_TICKS(10U));
        processDistance();
    }
}

} // namespace app::logic
