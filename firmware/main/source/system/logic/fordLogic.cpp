/**
 * @file fordLogic.cpp
 * @brief Ford's logic: one loop, one drive style at a time. See ADR 0009.
 *
 * The loop is a fixed pipeline - read, decide, execute, publish - and `decideAction()`
 * dispatches to the selected drive style. A style fills in a PlannedDrive and nothing more;
 * the arm gate, the direction-change brake and the actuator-fault latch all sit in
 * `executeAction()`, below every style, so no style can reach past them.
 */
#include "system/logic/fordLogic.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

#include "driver/factory/interface.h"
#include "driver/nvs/interface.h"
#include "driver/odometer/gaps.h"
#include "driver/odometer/store.h"
#include "driver/wifi/store.h"
#include "system/ford.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace
{
// Ford's pins, geometry and settings come from system/ford.h, which the A89301
// configuration app shares so that both run the gap calibration on one wheel.
namespace ford = app::ford;

/** Ford's design gap fractions as a GapTable; see ford::DesignGapFractions. */
driver::odometer::GapTable fordDesignGaps() noexcept
{
    driver::odometer::GapTable table{};
    table.count = ford::OdometerMagnets;

    for (std::uint8_t index{0U}; index < ford::OdometerMagnets; ++index)
    {
        table.fraction[index] = ford::DesignGapFractions[index];
    }

    return table;
}

/** Name for the speed source, so the operator can see which reading they are looking at. */
const char* speedSourceName(driver::odometer::SpeedSource source) noexcept
{
    switch (source)
    {
        case driver::odometer::SpeedSource::None: return "none";
        case driver::odometer::SpeedSource::Revolution: return "revolution";
        case driver::odometer::SpeedSource::PerGap: return "per_gap";
    }
    return "none";
}

// Publication and command channels shared with the operator console.
// config/set is subscribed, but the Manager narrows it to the drive style for a
// remote-driven car: every other Ford setting stays compiled in, and a payload offering
// one is rejected rather than applied. See ADR 0009.
constexpr app::communication::Topics Topics{
    {"cnb/ford/telemetry", "cnb/ford/config/state", "cnb/ford/command/state", "cnb/ford/status"},
    {"cnb/ford/config/set", "cnb/ford/command"}};

// Car network: the operator laptop's hotspot, so the broker is always at 192.168.137.1.
// Compiled in, passwords included. The Wi-Fi network can be replaced over serial with the
// `wifi` command, which stores it in NVS; the broker stays compiled in.
constexpr app::communication::NetworkSettings Network{
    "cnb-net", "cnbrules",
    "mqtt://192.168.137.1:1883", "cnb-ford", "cnb-ford", "cnb"};

/** USB serial console speed. The USB-JTAG link ignores it, but the factory asks for one. */
constexpr std::uint32_t SerialBaudRate{115200U};

constexpr const char* SerialHelpText{
    "Serial commands:\n"
    "  wifi              show the network in use and any unsaved changes\n"
    "  wifi ssid <name>  set the network name (spaces allowed)\n"
    "  wifi pass <pass>  set the password; leave it out for an open network\n"
    "  wifi save         store the new network; it is used after a restart\n"
    "  wifi clear        forget the stored network, back to the built-in one\n"
    "  help              this text\n"};

/**
 * @brief Match a command word at the start of a line, ignoring case.
 *
 * @param[in] line Line to test.
 * @param[in] word Lower-case command word.
 * @return The text after the word with leading spaces skipped, or nullptr if the line does
 *         not start with the whole word.
 */
const char* afterWord(const char* line, const char* word) noexcept
{
    while (*word != '\0')
    {
        if (std::tolower(static_cast<unsigned char>(*line)) != *word) { return nullptr; }
        ++line;
        ++word;
    }
    if ((*line != '\0') && (*line != ' ')) { return nullptr; }
    while (*line == ' ') { ++line; }
    return line;
}

/**
 * @brief Copy a value typed over serial, refusing one that does not fit.
 *
 * @return True if copied, false if too long.
 */
template <std::size_t Size>
bool copyValue(char (&dst)[Size], const char* src) noexcept
{
    const std::size_t length{std::strlen(src)};
    if (length >= Size) { return false; }
    std::memcpy(dst, src, length + 1U);
    return true;
}

/**
 * @brief Turn a speed command into motor duty, skipping the duty band where the motor does not start.
 * @param speedCommand -100 full reverse, 0 no drive, +100 full forward.
 * @return 0 for no drive, otherwise ford::StartDuty to ford::TopSpeedDuty.
 * @todo Add host tests for the mapping and its clamping.
 */
float dutyFor(float speedCommand) noexcept
{
    const float magnitude = std::min(std::abs(speedCommand), 100.0F);
    if (magnitude <= 0.0F) { return 0.0F; }
    return ford::StartDuty + (ford::TopSpeedDuty - ford::StartDuty) * (std::max(magnitude, 1.0F) - 1.0F) / 99.0F;
}
} // namespace

namespace app::logic
{

const char* FordLogic::toString(const MotorState state) noexcept
{
    switch (state)
    {
        case MotorState::Braked: return "braked";
        case MotorState::NoDrive: return "no_drive";
        case MotorState::Braking: return "braking";
        case MotorState::DrivingForward: return "driving_forward";
        case MotorState::DrivingReverse: return "driving_reverse";
    }
    return "braked";
}

FordLogic::FordLogic(driver::factory::Interface& factory) noexcept
    : myFactory{factory}
{
    myPreviousState = myControl.controlState();
    myPreviousMotion = myControl.motionState();
    myPreviousReason = myControl.stateReason();
}

FordLogic::~FordLogic() noexcept = default;

bool FordLogic::initializeDrivers() noexcept
{
    // Brake before anything else, so the wheels cannot spin while Wi-Fi and MQTT start.
    // If start-up fails, the pin is released and the BRAKE pull-up keeps the brake on.
    myBrake = myFactory.gpioOutput(ford::pin::Brake);
    if (!myBrake || !myBrake->isInitialized()) { ESP_LOGE("FORD", "Brake GPIO failed"); return false; }
    myBrake->write(true);
    myDirection = myFactory.gpioOutput(ford::pin::Direction);
    mySpeedPwm = myFactory.pwm(ford::pin::Speed);
    driver::pwm::Config steeringConfig{};
    steeringConfig.pin = ford::pin::Steering;
    steeringConfig.frequencyHz = ford::SteeringPwmFrequencyHz;
    mySteeringPwm = myFactory.pwm(steeringConfig);
    if (!myDirection || !myDirection->isInitialized() || !mySpeedPwm || !mySteeringPwm)
    { ESP_LOGE("FORD", "Driver allocation failed"); return false; }
    // Wrappers use the drivers above; member declaration order keeps them alive.
    myMotor = myFactory.fordMotor(*mySpeedPwm, *myDirection, *myBrake, ford::InvertMotorDirection);
    mySteering = myFactory.fordServo(*mySteeringPwm);
    // A89301::init() releases BRAKE with SPD at 0, so brake again straight after it.
    if (!myMotor || !mySteering || !myMotor->init() || !myMotor->stop(driver::motor::StopMode::Brake)
        || !mySteering->init())
    { myBrake->write(true); ESP_LOGE("FORD", "Initialization failed; brake held"); return false; }
    // The battery meter is optional: the car drives without it, and telemetry leaves it out.
    myBatteryAdc = myFactory.adc(ford::pin::BatteryAdc);
    if (myBatteryAdc)
    { myBattery = myFactory.voltageMeter(*myBatteryAdc, ford::BatteryR1Ohm, ford::BatteryR2Ohm); }
    if (!myBattery || !myBatteryAdc->init() || !myBattery->isInitialized())
    { ESP_LOGW("FORD", "Battery meter failed; battery voltage not reported"); }
    // The odometer is optional too: without it the car still drives to a speed command,
    // it just cannot report measured speed. The speed loop of ADR 0006 will need it.
    myOdometerGpio = myFactory.gpioInputPullup(ford::pin::Odometer);
    if (myOdometerGpio && myOdometerGpio->isInitialized())
    {
        myOdometer = myFactory.odometer(*myOdometerGpio, driver::odometer::Config{
            .pulsesPerRevolution = ford::OdometerMagnets,
            .wheelDiameterM = static_cast<float>(ford::WheelDiameterM),
        });
    }
    if (!myOdometer || !myOdometer->init())
    {
        myOdometer = nullptr; // Not unique_ptr::reset(): Interface has a reset() of its own.
        ESP_LOGW("FORD", "Odometer failed; measured speed and distance not reported");
    }
    loadOdometerGaps();
    // The motor temperature sensor is optional too; it is reported, not acted on.
    myMotorTempAdc = myFactory.adc(ford::pin::MotorTempAdc);
    if (myMotorTempAdc) { myMotorTemp = myFactory.temperatureSensor(*myMotorTempAdc); }
    if (!myMotorTemp || !myMotorTempAdc->init() || !myMotorTemp->isInitialized())
    { ESP_LOGW("FORD", "Motor temperature sensor failed; motor temperature not reported"); }

    // The serial console is optional too; without it the car joins whatever network it has.
    mySerial = myFactory.serial(SerialBaudRate);
    if (!mySerial || !mySerial->connect())
    {
        mySerial = nullptr;
        ESP_LOGW("FORD", "Serial console failed; the Wi-Fi network cannot be changed over serial");
    }
    loadWifiNetwork();

    // Only now that the brake is on: the Manager's constructor allocates the Wi-Fi and
    // MQTT drivers, and the wheels must not be able to spin while those start.
    myCommunication = std::make_unique<app::communication::Manager>(myFactory, Topics, myNetwork);
    return myCommunication != nullptr;
}

void FordLogic::loadOdometerGaps() noexcept
{
    if (!myOdometer) { return; }

    // A measured table if one has been stored, the design values otherwise. Scoped so
    // the namespace is released again: the car only reads it, and the calibration
    // session needs to be able to open it.
    auto gaps = fordDesignGaps();
    const char* gapSource{"design values"};
    {
        const driver::odometer::Store store{myFactory.nvs()};
        driver::odometer::GapTable stored{};
        float spread{0.0F};
        if (store.load(ford::OdometerMagnets, stored, spread))
        {
            gaps = stored;
            gapSource = "calibration";
            ESP_LOGI("FORD", "Odometer gaps from calibration, spread %.4f", spread);
        }
    }
    if (!myOdometer->setGapTable(gaps))
    {
        ESP_LOGW("FORD", "Odometer gap table refused; speed stays on the revolution window");
    }
    else
    {
        ESP_LOGI("FORD", "Odometer: %u magnets, gaps from %s", ford::OdometerMagnets, gapSource);
    }
}

void FordLogic::loadWifiNetwork() noexcept
{
    myNetwork = Network;
    myWifiFromStore = false;
    {
        // Scoped so the namespace is released again for `wifi save` and `wifi clear`.
        const driver::wifi::Store store{myFactory.nvs()};
        if (store.load(myWifiSsid, myWifiPassword))
        {
            myNetwork.wifiSsid = myWifiSsid;
            myNetwork.wifiPassword = myWifiPassword;
            myWifiFromStore = true;
        }
    }
    ESP_LOGI("FORD", "Wi-Fi: joining \"%s\" (%s)", myNetwork.wifiSsid,
             myWifiFromStore ? "set over serial" : "built in");
}

void FordLogic::processSerialCommand() noexcept
{
    if (!mySerial || !mySerial->isDataAvailable()) { return; }

    char line[128]{};
    if (mySerial->read(line, sizeof(line)) == 0U) { return; }

    // Trailing spaces would otherwise end up in an SSID or password, invisibly.
    std::size_t length{std::strlen(line)};
    while ((length > 0U) && (line[length - 1U] == ' ')) { line[--length] = '\0'; }

    const char* start{line};
    while (*start == ' ') { ++start; }

    if (const char* args{afterWord(start, "wifi")}) { handleWifiCommand(args); }
    else if (afterWord(start, "help") != nullptr) { mySerial->write(SerialHelpText); }
    else if (*start != '\0') { mySerial->write("Unknown command. Type help.\n"); }
}

void FordLogic::handleWifiCommand(const char* args) noexcept
{
    char out[160]{};

    if (*args == '\0')
    {
        std::snprintf(out, sizeof(out), "Network in use: \"%s\" (%s), %s\n", myNetwork.wifiSsid,
                      myWifiFromStore ? "set over serial" : "built in",
                      ((myNetwork.wifiPassword != nullptr) && (*myNetwork.wifiPassword != '\0'))
                          ? "password set" : "open network");
        mySerial->write(out);
        if (myPendingWifi.hasSsid || myPendingWifi.hasPassword)
        {
            std::snprintf(out, sizeof(out), "Unsaved: name \"%s\", password %s. Type wifi save.\n",
                          myPendingWifi.hasSsid ? myPendingWifi.ssid : "(not set)",
                          !myPendingWifi.hasPassword ? "(not set)"
                              : ((*myPendingWifi.password == '\0') ? "none (open)" : "set"));
            mySerial->write(out);
        }
        return;
    }

    if (const char* value{afterWord(args, "ssid")})
    {
        if ((*value == '\0') || !copyValue(myPendingWifi.ssid, value))
        {
            mySerial->write("The network name must be 1 to 32 characters.\n");
            return;
        }
        myPendingWifi.hasSsid = true;
        std::snprintf(out, sizeof(out), "Name set to \"%s\". Now wifi pass <password>, then wifi save.\n",
                      myPendingWifi.ssid);
        mySerial->write(out);
        return;
    }

    if (const char* value{afterWord(args, "pass")})
    {
        const std::size_t length{std::strlen(value)};
        if (((length > 0U) && (length < 8U)) || !copyValue(myPendingWifi.password, value))
        {
            mySerial->write("The password must be 8 to 63 characters, or left out for an open network.\n");
            return;
        }
        myPendingWifi.hasPassword = true;
        mySerial->write((length == 0U) ? "Open network, no password. Now wifi save.\n"
                                       : "Password set. Now wifi save.\n");
        return;
    }

    if (afterWord(args, "save") != nullptr)
    {
        if (!myPendingWifi.hasSsid || !myPendingWifi.hasPassword)
        {
            mySerial->write("Set both first: wifi ssid <name> and wifi pass <password>.\n");
            return;
        }
        bool saved{false};
        {
            driver::wifi::Store store{myFactory.nvs()};
            saved = store.save(myPendingWifi.ssid, myPendingWifi.password);
        }
        if (!saved)
        {
            mySerial->write("Saving failed. Nothing was changed.\n");
            ESP_LOGE("FORD", "Wi-Fi store write failed");
            return;
        }
        std::snprintf(out, sizeof(out), "Saved \"%s\". Press reset to join it.\n", myPendingWifi.ssid);
        mySerial->write(out);
        ESP_LOGI("FORD", "Wi-Fi: \"%s\" stored; used after the next restart", myPendingWifi.ssid);
        myPendingWifi = PendingWifi{};
        return;
    }

    if (afterWord(args, "clear") != nullptr)
    {
        bool cleared{false};
        {
            driver::wifi::Store store{myFactory.nvs()};
            cleared = store.clear();
        }
        myPendingWifi = PendingWifi{};
        if (cleared)
        {
            std::snprintf(out, sizeof(out), "Stored network forgotten. After a restart: \"%s\".\n",
                          Network.wifiSsid);
            mySerial->write(out);
        }
        else { mySerial->write("Clearing failed.\n"); }
        return;
    }

    mySerial->write("Unknown wifi command. Type help.\n");
}

void FordLogic::readSensors(const std::uint32_t nowMs) noexcept
{
    if (myBattery && (!myBatteryRead || (nowMs - myLastBatteryReadMs) >= ford::BatteryReadIntervalMs))
    {
        mySnapshot.batteryVoltage = myBattery->readVoltage();
        myLastBatteryReadMs = nowMs;
        myBatteryRead = true;
    }
    if (myMotorTemp
        && (!myMotorTempRead || (nowMs - myLastMotorTempReadMs) >= ford::MotorTempReadIntervalMs))
    {
        myMotorTemperatureC = myMotorTemp->readTemperature();
        mySnapshot.motorTemperatureC = myMotorTemperatureC;
        myLastMotorTempReadMs = nowMs;
        myMotorTempRead = true;
    }
}

void FordLogic::decideAction(const std::uint32_t nowMs) noexcept
{
    switch (myControl.configuration().driveStyle)
    {
        case navigation::DriveStyle::ManualByRemote:
            decideManualByRemoteAction(nowMs);
            break;
        case navigation::DriveStyle::GapCalibration:
            decideGapCalibrationAction(nowMs);
            break;
        // A remote-driven car never has an autonomous style selected: Control's
        // supportsDriveStyle() refuses them, so these cannot be reached. No drive is
        // nevertheless the right answer rather than carrying on with a stale request.
        case navigation::DriveStyle::DecideAction:
        case navigation::DriveStyle::SlowLeft:
        case navigation::DriveStyle::SlowRight:
        case navigation::DriveStyle::GradualSweep:
            myPlannedDrive = {};
            break;
    }
}

void FordLogic::decideManualByRemoteAction(const std::uint32_t nowMs) noexcept
{
    // 0/0 while disarmed; speed 0 after a drive timeout. remoteDrive() also enforces the
    // operator's lease for this style.
    const auto drive = myControl.remoteDrive(nowMs, ford::DriveTimeoutMs);
    myManualByRemote.lastDrive = drive;

    // The operator asks in speed commands; every style asks the car in duty. dutyFor()
    // skips the band where the motor will not start, so this mapping is the style's job.
    const float magnitude = dutyFor(drive.speedCommand);
    myPlannedDrive.duty = drive.speedCommand < 0.0F ? -magnitude : magnitude;
    myPlannedDrive.steeringCommand = drive.steeringCommand;
}

void FordLogic::decideGapCalibrationAction(const std::uint32_t nowMs) noexcept
{
    // The measurement never steers: the wheel it measures is the right rear one, and the
    // car is on a stand. Centre the steering and ask for nothing until the run says so.
    myPlannedDrive = {};

    // The lease is style-independent. remoteDrive() enforces it for ManualByRemote, and
    // this style never calls remoteDrive, so without this a console that went quiet would
    // leave a powered wheel turning for the rest of the run.
    const bool armed = myControl.renewLease(nowMs);

    // Arming starts a run; the edge, not the level, because a finished run disarms.
    if (armed && !myCalibration.wasArmed)
    {
        myCalibration.run.start(nowMs, ford::OdometerMagnets);
        myCalibration.storeFailed = false;
        ESP_LOGI("FORD", "GapCalibration: starting, %u magnets", ford::OdometerMagnets);
    }
    myCalibration.wasArmed = armed;

    if (!myCalibration.run.isRunning()) { return; }

    myPlannedDrive.duty =
        myCalibration.run.update(nowMs, armed, myMotorTemperatureC, myOdometer.get());

    // A run that has just ended disarms the car, so Start triggers the next one. Finishing
    // is not a fault, so it does not go through the fail-safe disarm.
    if (!myCalibration.run.isRunning())
    {
        myPlannedDrive.duty = 0.0F;
        myControl.finishDriveStyle();
        myCalibration.wasArmed = false;
        if (myCalibration.run.hasTable())
        {
            ESP_LOGI("FORD", "GapCalibration: measured, spread %.4f, margin %.4f; awaiting confirm",
                     myCalibration.run.result().spread, myCalibration.run.result().margin);
        }
        else
        {
            ESP_LOGW("FORD", "GapCalibration: no table stored; failure %u",
                     static_cast<unsigned>(myCalibration.run.failure()));
        }
    }
}

const char* FordLogic::toString(const GapCalibration::Phase phase) noexcept
{
    switch (phase)
    {
        case GapCalibration::Phase::Idle: return "idle";
        case GapCalibration::Phase::Settling: return "settling";
        case GapCalibration::Phase::Sampling: return "sampling";
        case GapCalibration::Phase::Measured: return "measured";
        case GapCalibration::Phase::Failed: return "failed";
    }
    return "idle";
}

const char* FordLogic::toString(const GapCalibration::Failure failure) noexcept
{
    switch (failure)
    {
        case GapCalibration::Failure::None: return nullptr;
        case GapCalibration::Failure::NoOdometer: return "no_odometer";
        case GapCalibration::Failure::Stalled: return "stalled";
        case GapCalibration::Failure::TooHot: return "too_hot";
        case GapCalibration::Failure::Disagreed: return "disagreed";
        case GapCalibration::Failure::NotPlausible: return "not_plausible";
        case GapCalibration::Failure::ThinMargin: return "thin_phase_margin";
        case GapCalibration::Failure::Stopped: return "stopped";
    }
    return nullptr;
}

void FordLogic::publishCalibrationState() noexcept
{
    // Absent means "not this drive style", the same way every optional telemetry field
    // means "not available" by being missing rather than by being sent empty.
    if (myControl.configuration().driveStyle != navigation::DriveStyle::GapCalibration)
    {
        mySnapshot.calibrationPhase = nullptr;
        return;
    }

    const auto& run = myCalibration.run;
    mySnapshot.calibrationPhase = toString(run.phase());
    mySnapshot.calibrationFailure = toString(run.failure());
    mySnapshot.calibrationDutyIndex = run.dutyIndex();
    mySnapshot.calibrationDutyCount = ford::calibration::DutyCount;
    mySnapshot.calibrationSamples = run.samples();
    mySnapshot.calibrationRevolutions = ford::calibration::Revolutions;
    mySnapshot.calibrationStored = run.isStored();
    mySnapshot.calibrationStoreFailed = myCalibration.storeFailed;
    // The guard is off when the sensor is missing or failed, and the operator is told.
    mySnapshot.calibrationOverheatGuard = std::isfinite(myMotorTemperatureC);

    if (run.hasTable())
    {
        mySnapshot.calibrationGaps = run.result().table.fraction;
        mySnapshot.calibrationGapCount = run.result().table.count;
        mySnapshot.calibrationSpread = run.result().spread;
        mySnapshot.calibrationMargin = run.result().margin;
    }
    else
    {
        mySnapshot.calibrationGaps = nullptr;
        mySnapshot.calibrationGapCount = 0U;
        // A disagreement is worth reporting even though it produced no table: the spread
        // is the evidence, and it is what tells the operator to space the magnets better.
        const bool disagreed = run.failure() == GapCalibration::Failure::Disagreed;
        mySnapshot.calibrationSpread =
            disagreed ? run.result().spread : std::numeric_limits<float>::quiet_NaN();
        mySnapshot.calibrationMargin = std::numeric_limits<float>::quiet_NaN();
    }
}

void FordLogic::storeMeasuredGaps() noexcept
{
    if (!myCalibration.run.hasTable())
    {
        ESP_LOGW("FORD", "Store refused: no measured gap table is waiting");
        myCalibration.storeFailed = true;
        return;
    }

    // Scoped so the namespace is released again, as loadOdometerGaps() does: the store
    // owns the namespace for as long as it lives.
    bool saved{false};
    {
        driver::odometer::Store store{myFactory.nvs()};
        const auto& result = myCalibration.run.result();
        saved = store.isOpen() && store.save(result.table, result.spread);
    }
    myCalibration.storeFailed = !saved;
    if (!saved)
    {
        ESP_LOGE("FORD", "Store write failed; the measured table is still waiting");
        return;
    }

    myCalibration.run.markStored();
    ESP_LOGI("FORD", "GapCalibration: stored. It takes effect on the next restart.");
}

void FordLogic::executeAction(const std::uint32_t nowMs) noexcept
{
    const bool armed = myControl.controlState() == app::runtime::ControlState::Armed;

    MotorState wanted{MotorState::Braked};
    float duty{0.0F};
    const auto wantedDirection = myPlannedDrive.duty < 0.0F
        ? driver::motor::Direction::Backward
        : driver::motor::Direction::Forward;
    if (!armed || myActuatorFault) { myBraking = false; }
    else if (myPlannedDrive.duty == 0.0F) { wanted = MotorState::NoDrive; myBraking = false; }
    else
    {
        // Never reverse a spinning sensorless motor: brake first, then change DIR.
        const bool reversing = myHasDriven && wantedDirection != myDrivenDirection;
        if (!reversing) { myBraking = false; }
        else if (!myBraking) { myBraking = true; myBrakeStartMs = nowMs; }
        if (myBraking && (nowMs - myBrakeStartMs) < ford::DirectionChangeBrakeMs)
        { wanted = MotorState::Braking; }
        else
        {
            myBraking = false;
            wanted = wantedDirection == driver::motor::Direction::Forward
                ? MotorState::DrivingForward
                : MotorState::DrivingReverse;
            duty = std::abs(myPlannedDrive.duty);
            myDrivenDirection = wantedDirection;
            myHasDriven = true;
        }
    }

    bool outputOk{!myActuatorFault}; // Accumulate motor and servo write results.
    if (outputOk && (wanted != myAppliedState || duty != myAppliedDuty))
    {
        switch (wanted)
        {
            case MotorState::Braked:
            case MotorState::Braking: outputOk = myMotor->stop(driver::motor::StopMode::Brake); break;
            case MotorState::NoDrive: outputOk = myMotor->stop(driver::motor::StopMode::Coast); break;
            case MotorState::DrivingForward:
            case MotorState::DrivingReverse:
                outputOk = myMotor->setDirection(wantedDirection) && myMotor->setDuty(duty);
                break;
        }
        if (outputOk) { myAppliedState = wanted; myAppliedDuty = duty; }
    }
    // Disarmed cars centre the steering; a drive timeout keeps the last command.
    if (outputOk && myPlannedDrive.steeringCommand != myAppliedSteering)
    {
        outputOk = mySteering->setDirection(myPlannedDrive.steeringCommand);
        if (outputOk) { myAppliedSteering = myPlannedDrive.steeringCommand; }
    }
    if (!outputOk && !myActuatorFault)
    {
        myMotor->stop(driver::motor::StopMode::Brake);
        myBrake->write(true);
        myControl.forceDisarm(app::runtime::StateReason::ActuatorFault);
        myAppliedState = MotorState::Braked;
        myAppliedDuty = 0.0F;
        myActuatorFault = true;
        ESP_LOGE("FORD", "Actuator error; brake held until reboot");
    }
}

void FordLogic::publishState(const std::uint32_t nowMs) noexcept
{
    // Echo what the car actually applies, so the page can compare it with its sliders.
    const bool driving = myAppliedState == MotorState::DrivingForward
        || myAppliedState == MotorState::DrivingReverse;
    const bool operatorDriven =
        myControl.configuration().driveStyle == navigation::DriveStyle::ManualByRemote;
    mySnapshot.speedCommand =
        (driving && operatorDriven) ? myManualByRemote.lastDrive.speedCommand : 0.0F;
    mySnapshot.steeringDegrees = mySteering->getDirection();
    mySnapshot.forwardDuty = myAppliedState == MotorState::DrivingForward ? myAppliedDuty : 0.0F;
    mySnapshot.backwardDuty = myAppliedState == MotorState::DrivingReverse ? myAppliedDuty : 0.0F;
    mySnapshot.motorState = toString(myAppliedState);
    if (myOdometer)
    {
        // An odometer cannot see direction, so tell it what the motor was told. Only
        // while actually driving: coasting keeps the last known direction, which is
        // the best guess available, and a standstill discards the phase anyway.
        if (myAppliedState == MotorState::DrivingForward) { myOdometer->setForward(true); }
        else if (myAppliedState == MotorState::DrivingReverse) { myOdometer->setForward(false); }

        // Recovers which gap the wheel is in; returns at once if nothing is new.
        myOdometer->update();

        mySnapshot.measuredSpeedMs = myOdometer->speed();
        mySnapshot.odometerDistanceM = myOdometer->distance();
        mySnapshot.measuredSpeedSource = speedSourceName(myOdometer->speedSource());
        mySnapshot.odometerPhaseLosses = myOdometer->phaseLossCount();
    }
    publishCalibrationState();
    if (myPreviousState != myControl.controlState() || myPreviousMotion != myControl.motionState()
        || myPreviousReason != myControl.stateReason())
    { myCommunication->notifyControlStateChanged(); }
    myPreviousState = myControl.controlState();
    myPreviousMotion = myControl.motionState();
    myPreviousReason = myControl.stateReason();
    myCommunication->publishTelemetry(nowMs, mySnapshot, myControl);
}

void FordLogic::run(const std::atomic<bool>& stop) noexcept
{
    if (!initializeDrivers()) { return; }

    ESP_LOGI("FORD", "Ready: ManualByRemote, brake on; waiting for MQTT Start");
    if (mySerial) { mySerial->write(SerialHelpText); }
    while (!stop.load())
    {
        // Monotonic milliseconds; unsigned subtraction handles tick wraparound.
        const auto now = static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        myCommunication->process(now, myControl);
        processSerialCommand();

        // Confirming a measured table is an operator action, not part of driving, so it is
        // handled before the pipeline rather than inside a style's decide step.
        if (myControl.takeStoreGapsRequest()) { storeMeasuredGaps(); }

        readSensors(now);
        decideAction(now);
        executeAction(now);
        publishState(now);

        vTaskDelay(std::max<TickType_t>(1U, pdMS_TO_TICKS(10U)));
    }
    myMotor->stop(driver::motor::StopMode::Brake);
    myCommunication->disconnect();
}

} // namespace app::logic
