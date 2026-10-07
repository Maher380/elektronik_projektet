/**
 * @file fordLogic.h
 * @brief Declaration of Ford's application logic and its drive styles.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>

#include "driver/adc/interface.h"
#include "driver/gpio/interface.h"
#include "driver/motor/interface.h"
#include "driver/odometer/interface.h"
#include "driver/pwm/interface.h"
#include "driver/serial/interface.h"
#include "driver/servo/interface.h"
#include "driver/temperature_sensor/interface.h"
#include "driver/voltage_meter/interface.h"
#include "driver/wifi/store.h"
#include "system/communication/manager.h"
#include "system/logic/gapCalibration.h"
#include "system/logic/speedCalibration.h"
#include "system/navigation/types.h"
#include "system/runtime/control.h"

namespace driver::factory { class Interface; }

namespace app::logic
{

/**
 * @brief What a Ford drive style asks the car to do next.
 *
 * A drive style fills this in and nothing else. Everything safety-bearing - the arm gate,
 * the direction-change brake, the actuator-fault latch - is applied below every style, so
 * no style can reach past it. See ADR 0009.
 *
 * The currency is a signed motor duty, not a Speed command. ADR 0009 originally chose a
 * speed command and the implementation disproved it: `dutyFor()` deliberately maps the
 * whole operator range onto ford::StartDuty to ford::TopSpeedDuty, 0.08 to 1.0, because
 * that is the band the car is driveable in. The gap calibration's recipe measures at
 * 0.10, 0.15 and 0.20, and the operator range only reached 0.15 when this was written.
 * A speed command is therefore what ManualByRemote is *given*, and the duty it maps to is
 * what every style *asks for*. ADR 0006's Speed target will sit above this, not replace it.
 */
struct PlannedDrive
{
    /** Signed motor duty: -1 full reverse, 0 no drive, +1 full forward. */
    float duty{0.0F};
    /** -90 full left, 0 straight ahead, +90 full right. */
    float steeringCommand{0.0F};
    /**
     * @brief With duty 0: hold the brake rather than coast.
     *
     * SpeedCalibration brakes between legs, so that a fixed run on the floor ends in a
     * short, repeatable distance. The arm gate still decides first: disarmed always brakes.
     */
    bool brake{false};
};

/**
 * @brief Ford's system logic: one loop, one drive style at a time.
 *
 * Drivers are owned as members and the loop is a fixed pipeline, so adding a drive style
 * means adding a decide step and nothing else. Member declaration order is load-bearing:
 * the motor and servo wrappers hold references to the GPIO and PWM drivers above them,
 * and reverse-order destruction is what keeps those alive for as long as the wrappers are.
 */
class FordLogic final
{
public:
    explicit FordLogic(driver::factory::Interface& factory) noexcept;
    ~FordLogic() noexcept;

    void run(const std::atomic<bool>& stop) noexcept;

    FordLogic(const FordLogic&) = delete;
    FordLogic& operator=(const FordLogic&) = delete;
    FordLogic(FordLogic&&) = delete;
    FordLogic& operator=(FordLogic&&) = delete;

private:
    /** What the motor is doing, reported in telemetry as motor.state. */
    enum class MotorState : std::uint8_t
    {
        Braked,
        NoDrive,
        Braking,
        DrivingForward,
        DrivingReverse,
    };

    /** Name for the motor state, reported in telemetry as motor.state. */
    static const char* toString(MotorState state) noexcept;

    /** Name for a calibration phase, reported in telemetry as calibration.phase. */
    static const char* toString(GapCalibration::Phase phase) noexcept;

    /** Name for a calibration failure; nullptr when there was none, which omits the field. */
    static const char* toString(GapCalibration::Failure failure) noexcept;

    /** Fill in the nested calibration object, or leave it out for any other style. */
    void publishCalibrationState() noexcept;

    /** Name for a speed calibration phase, reported as speed_calibration.phase. */
    static const char* toString(SpeedCalibration::Phase phase) noexcept;

    /** Name for a speed calibration failure; nullptr when there was none. */
    static const char* toString(SpeedCalibration::Failure failure) noexcept;

    /** Name for a leg's outcome, reported as speed_calibration.last.result. */
    static const char* toString(SpeedCalibration::Outcome outcome) noexcept;

    /** Fill in the nested speed_calibration object, or leave it out for any other style. */
    void publishSpeedCalibrationState() noexcept;

    /**
     * @brief Construct and initialize every driver, brake first.
     *
     * @return False if a required driver failed; the brake is held in that case.
     */
    bool initializeDrivers() noexcept;

    /** Hand the odometer a measured gap table if one is stored, the design values if not. */
    void loadOdometerGaps() noexcept;

    /** Join the network stored over serial if there is one, the compiled-in one if not. */
    void loadWifiNetwork() noexcept;

    /**
     * @brief Handle one serial line if one has arrived.
     *
     * Network settings, and a raw steering pulse for measuring the servo. Never drive.
     */
    void processSerialCommand() noexcept;

    /**
     * @brief Handle the arguments of a `wifi` command.
     *
     * @param[in] args Everything after "wifi", leading spaces removed. Values keep their case.
     */
    void handleWifiCommand(const char* args) noexcept;

    /**
     * @brief Handle the arguments of a `servo` command: send a raw steering pulse.
     *
     * For measuring where the wheels point straight and where they hit the end stops.
     * Disarmed only. The pulse is held until `servo` alone or Start, which centre it.
     *
     * @param[in] args A pulse width in microseconds, or nothing to centre again.
     */
    void handleServoCommand(const char* args) noexcept;

    /**
     * @brief Read what the car can sense, before any style decides on it.
     *
     * Ford's equivalent of Vagrant's getEnvironmentPicture(): the battery and the motor can
     * are sampled here so that a drive style reads one value rather than deciding when to
     * take one. The Odometer is deliberately not here - it is told the direction the motor
     * was actually given, so it is updated after executeAction().
     */
    void readSensors(std::uint32_t nowMs) noexcept;

    /**
     * @brief Safe mode: brake and select Disabled if the motor or servo is too hot.
     *
     * Runs after readSensors() so it sees this tick's temperatures. See ford::SafeModeEnabled.
     */
    void checkSafeMode() noexcept;

    /** Dispatch to the decide step of whichever drive style is selected. */
    void decideAction(std::uint32_t nowMs) noexcept;

    /** ManualByRemote: follow the operator's latest drive commands. */
    void decideManualByRemoteAction(std::uint32_t nowMs) noexcept;

    /** GapCalibration: drive the measurement's own script and report what it found. */
    void decideGapCalibrationAction(std::uint32_t nowMs) noexcept;

    /** Log each new revolution, each finished duty and the result, for analysis offline. */
    void logCalibration() noexcept;

    /** SpeedCalibration: drive the recipe's legs on the floor and report each one. */
    void decideSpeedCalibrationAction(std::uint32_t nowMs) noexcept;

    /** Log each finished leg over serial, tagged SPD. */
    void logSpeedCalibration() noexcept;

    /** Store a measured gap table when the operator confirms it. */
    void storeMeasuredGaps() noexcept;

    /** Apply myPlannedDrive through the arm gate, the brake interlock and the outputs. */
    void executeAction(std::uint32_t nowMs) noexcept;

    /** Fill in the telemetry snapshot from what was actually applied, and publish it. */
    void publishState(std::uint32_t nowMs) noexcept;

    driver::factory::Interface& myFactory;

    // Declaration order is construction order; see the class comment.
    std::unique_ptr<driver::gpio::Interface> myBrake;
    std::unique_ptr<driver::gpio::Interface> myDirection;
    std::unique_ptr<driver::pwm::Interface> mySpeedPwm;
    std::unique_ptr<driver::pwm::Interface> mySteeringPwm;
    std::unique_ptr<driver::motor::Interface> myMotor;
    std::unique_ptr<driver::servo::Interface> mySteering;
    std::unique_ptr<driver::adc::Interface> myBatteryAdc;
    std::unique_ptr<driver::voltage_meter::Interface> myBattery;
    std::unique_ptr<driver::gpio::Interface> myOdometerGpio;
    std::unique_ptr<driver::odometer::Interface> myOdometer;
    std::unique_ptr<driver::adc::Interface> myMotorTempAdc;
    std::unique_ptr<driver::temperature_sensor::Interface> myMotorTemp;
    std::unique_ptr<driver::adc::Interface> myServoTempAdc;
    std::unique_ptr<driver::temperature_sensor::Interface> myServoTemp;

    /** USB serial console, for setting the Wi-Fi network. Optional: the car drives without it. */
    std::unique_ptr<driver::serial::Interface> mySerial;

    /**
     * @brief The network the Manager joins, and the stored credentials it points into.
     *
     * Declared before myCommunication on purpose: the Manager keeps these pointers, so they
     * must be destroyed after it.
     */
    char myWifiSsid[driver::wifi::SsidMaxLength + 1U]{};
    char myWifiPassword[driver::wifi::PasswordMaxLength + 1U]{};
    app::communication::NetworkSettings myNetwork{};
    /** True if myNetwork came from NVS rather than the firmware. */
    bool myWifiFromStore{false};

    /** Network typed over serial, waiting for `wifi save`. */
    struct PendingWifi
    {
        char ssid[driver::wifi::SsidMaxLength + 1U]{};
        char password[driver::wifi::PasswordMaxLength + 1U]{};
        bool hasSsid{false};
        bool hasPassword{false};
    };
    PendingWifi myPendingWifi{};

    /**
     * @brief Wi-Fi and MQTT lifecycle, constructed only once the brake is on.
     *
     * A unique_ptr rather than a plain member on purpose: the Manager's constructor
     * allocates the Wi-Fi and MQTT drivers, and the wheels must not be able to spin while
     * those start. Constructing it in initializeDrivers() keeps that ordering visible
     * instead of leaving it to depend on where this line sits.
     */
    std::unique_ptr<app::communication::Manager> myCommunication;

    /** Ford is a remote-driven car: no obstacle distances, so no autonomous drive styles. */
    app::runtime::Control myControl{false, true};

    /** No distance sensors, so those snapshot fields stay null. */
    app::communication::TelemetrySnapshot mySnapshot{};

    /** What the selected drive style last asked for. */
    PlannedDrive myPlannedDrive{};

    /**
     * @brief ManualByRemote's own state.
     *
     * Only the operator's last commands, kept so telemetry can echo the speed command the
     * operator asked for rather than the duty it mapped to.
     */
    struct ManualByRemoteState
    {
        app::runtime::RemoteDrive lastDrive{};
    };
    ManualByRemoteState myManualByRemote{};

    /**
     * @brief GapCalibration's own state.
     *
     * Grouped rather than spread across the class: the measurement carries about ten
     * fields of its own, and nothing in ManualByRemote has any business reading them.
     */
    struct GapCalibrationState
    {
        /** The measurement itself. */
        GapCalibration run{};
        /**
         * @brief Whether the car was armed on the previous tick.
         *
         * Arming is what starts a run, and the edge is what matters rather than the level:
         * a finished run disarms the car, so testing the level would start it again forever.
         */
        bool wasArmed{false};
        /** Whether the last store attempt failed, so the page can say so. */
        bool storeFailed{false};
        /** Serial of the last revolution logged, so each is logged once. */
        std::uint32_t loggedSerial{0U};
        /** Duties whose averaged table has been logged. */
        std::uint8_t loggedDuties{0U};
    };
    GapCalibrationState myCalibration{};

    /** SpeedCalibration's own state, kept apart for the same reason. */
    struct SpeedCalibrationState
    {
        /** The measurement itself. */
        SpeedCalibration run{};
        /** Whether the car was armed on the previous tick; arming starts a run. */
        bool wasArmed{false};
        /** Whether a leg has been logged in this run, and which. */
        bool loggedAny{false};
        std::uint8_t loggedLeg{0U};
    };
    SpeedCalibrationState mySpeedCalibration{};

    /** Last motor can temperature, or NaN when there is no sensor. Read by readSensors(). */
    float myMotorTemperatureC{std::numeric_limits<float>::quiet_NaN()};

    // Last published state, so a transition is reported without waiting for telemetry.
    app::runtime::ControlState myPreviousState{};
    app::runtime::MotionState myPreviousMotion{};
    app::runtime::StateReason myPreviousReason{};

    // Applied outputs; the motor is braked after start-up.
    MotorState myAppliedState{MotorState::Braked};
    float myAppliedDuty{0.0F};
    float myAppliedSteering{std::numeric_limits<float>::quiet_NaN()};
    /** True while a `servo <us>` pulse is on the steering instead of the applied command. */
    bool myRawSteeringPulse{false};

    /** Direction-change brake: the last driven direction and when the brake started. */
    driver::motor::Direction myDrivenDirection{driver::motor::Direction::Forward};
    bool myHasDriven{false};
    bool myBraking{false};
    std::uint32_t myBrakeStartMs{0U};

    /** Latched output failure; a restart is required to drive again. */
    bool myActuatorFault{false};

    bool myBatteryRead{false};
    std::uint32_t myLastBatteryReadMs{0U};
    bool myMotorTempRead{false};
    std::uint32_t myLastMotorTempReadMs{0U};
    bool myServoTempRead{false};
    std::uint32_t myLastServoTempReadMs{0U};
};

} // namespace app::logic
