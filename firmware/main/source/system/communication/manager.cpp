#include "system/communication/manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <initializer_list>
#include <limits>
#include <utility>

#include "driver/factory/interface.h"
#include "driver/mqtt/interface.h"
#include "driver/wifi/interface.h"
#include "system/runtime/names.h"
#include "sdkconfig.h"

extern "C" {
#include "cJSON.h"
}

namespace app::communication
{
namespace
{
constexpr int SchemaVersion{1};

#if CONFIG_CNB_ENABLE_WIFI
constexpr std::array<std::uint32_t, 5U> NetworkRetryDelaysMs{
    1000U, 2000U, 5000U, 10000U, 30000U};
#endif

#if CONFIG_CNB_ENABLE_MQTT
constexpr const char* MqttOnlinePayload{"{\"schema_version\":1,\"online\":true}"};
constexpr const char* MqttOfflinePayload{"{\"schema_version\":1,\"online\":false}"};
constexpr std::size_t MaxMqttMessagesPerTick{8U};
#endif

enum class ParseError : std::uint8_t
{
    None,
    InvalidJson,
    MissingField,
    UnknownField,
    DuplicateField,
    UnsupportedSchema,
    InvalidType,
    InvalidRevision,
    InvalidSession,
    UnsupportedCommand,
    InvalidDriveStyle,
};

struct WireTelemetrySnapshot
{
    navigation::DriveStyle driveStyle{navigation::DriveStyle::DecideAction};
    std::uint32_t sequence{0U};
    std::uint32_t uptimeMs{0U};
    std::array<float, runtime::IrSensorCount> distancesCm{};
    std::array<float, runtime::IrSensorCount> decisionDistancesCm{};
    bool systemTest{false};
    bool servoTest{false};
    std::array<std::int32_t, runtime::IrSensorCount> adcRaw{-1, -1, -1};
    float speedCommand{0.0F};
    float steeringDegrees{0.0F};
    float forwardDuty{0.0F};
    float backwardDuty{0.0F};
    const char* motorState{nullptr};
    float batteryVoltage{std::numeric_limits<float>::quiet_NaN()};
    float measuredSpeedMs{std::numeric_limits<float>::quiet_NaN()};
    float odometerDistanceM{std::numeric_limits<float>::quiet_NaN()};
    const char* measuredSpeedSource{nullptr};
    std::uint32_t odometerPhaseLosses{0U};
    float motorTemperatureC{std::numeric_limits<float>::quiet_NaN()};
    float servoTemperatureC{std::numeric_limits<float>::quiet_NaN()};
    const char* piLink{nullptr};
    std::uint32_t piLinkDropped{0U};
    std::int8_t motorFault{-1};
    std::uint32_t motorFaultCount{0U};
    // GapCalibration; see TelemetrySnapshot for what each one means.
    const char* calibrationPhase{nullptr};
    const char* calibrationFailure{nullptr};
    std::uint8_t calibrationDutyIndex{0U};
    std::uint8_t calibrationDutyCount{0U};
    std::uint8_t calibrationSamples{0U};
    std::uint8_t calibrationRevolutions{0U};
    const float* calibrationGaps{nullptr};
    std::uint8_t calibrationGapCount{0U};
    float calibrationSpread{std::numeric_limits<float>::quiet_NaN()};
    float calibrationMargin{std::numeric_limits<float>::quiet_NaN()};
    bool calibrationStored{false};
    bool calibrationStoreFailed{false};
    bool calibrationOverheatGuard{false};
    const char* disabledCause{nullptr};
    float disabledTemperatureC{std::numeric_limits<float>::quiet_NaN()};
    SpeedCalibrationTelemetry speedCalibration{};
    runtime::ControlState controlState{runtime::ControlState::Disarmed};
    runtime::MotionState motionState{runtime::MotionState::Stopped};
    runtime::StateReason reason{runtime::StateReason::Boot};
};

const char* toString(ParseError error) noexcept;
const char* toString(runtime::ConfigurationResult result) noexcept;
const char* toString(runtime::CommandError error) noexcept;
const char* toString(runtime::MotionState state) noexcept;

bool readDriveStyle(const cJSON* item, navigation::DriveStyle& style, ParseError& error) noexcept
{
    if (!cJSON_IsString(item)) { error = ParseError::InvalidType; return false; }
    // Control decides which of these styles the car can actually run. Disabled is left out:
    // only the car's safe mode selects it.
    constexpr navigation::DriveStyle styles[]{navigation::DriveStyle::DecideAction,
        navigation::DriveStyle::SlowLeft, navigation::DriveStyle::SlowRight,
        navigation::DriveStyle::GradualSweep, navigation::DriveStyle::ManualByRemote,
        navigation::DriveStyle::GapCalibration, navigation::DriveStyle::SpeedCalibration};
    for (const auto candidate : styles)
    {
        if (std::strcmp(item->valuestring, toString(candidate)) == 0) { style = candidate; return true; }
    }
    error = ParseError::InvalidDriveStyle;
    return false;
}

bool boundedJson(const char* payload) noexcept
{
    // Bound cJSON's recursive parser stack before parsing untrusted input.
    // Escaped NULs cannot be represented by cJSON's C-string fields.
    unsigned depth{0U};
    bool quoted{false};
    for (const char* cursor = payload; *cursor != '\0'; ++cursor)
    {
        if (quoted && *cursor == '\\')
        {
            ++cursor;
            if (*cursor == '\0' || std::strncmp(cursor, "u0000", 5U) == 0) { return false; }
            continue;
        }
        if (*cursor == '"') { quoted = !quoted; }
        else if (!quoted && (*cursor == '{' || *cursor == '['))
        {
            if (++depth > 4U) { return false; }
        }
        else if (!quoted && (*cursor == '}' || *cursor == ']'))
        {
            if (depth == 0U) { return false; }
            --depth;
        }
    }
    return !quoted && depth == 0U;
}

bool validateFields(const cJSON* object,
                    const char* const* fields,
                    std::size_t fieldCount,
                    ParseError& error, std::initializer_list<const char*> optionalFields = {}) noexcept
{
    std::array<bool, 10U> seen{};
    if ((fieldCount > seen.size()) || !cJSON_IsObject(object))
    {
        error = ParseError::InvalidType;
        return false;
    }

    for (const cJSON* child = object->child; child != nullptr; child = child->next)
    {
        if (child->string == nullptr)
        {
            error = ParseError::UnknownField;
            return false;
        }
        std::size_t index{0U};
        while ((index < fieldCount) && (std::strcmp(child->string, fields[index]) != 0))
        {
            ++index;
        }
        if (index == fieldCount)
        {
            error = ParseError::UnknownField;
            return false;
        }
        if (seen[index])
        {
            error = ParseError::DuplicateField;
            return false;
        }
        seen[index] = true;
    }

    for (std::size_t index{0U}; index < fieldCount; ++index)
    {
        const bool optional = std::any_of(optionalFields.begin(), optionalFields.end(),
            [&](const char* field) { return std::strcmp(fields[index], field) == 0; });
        if (!seen[index] && !optional)
        {
            error = ParseError::MissingField;
            return false;
        }
    }
    return true;
}

bool readUnsigned(const cJSON* object,
                  const char* name,
                  std::uint32_t& value) noexcept
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)
        || (item->valuedouble < 0.0)
        || (item->valuedouble > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        || (std::floor(item->valuedouble) != item->valuedouble))
    {
        return false;
    }
    value = static_cast<std::uint32_t>(item->valuedouble);
    return true;
}

bool readFloat(const cJSON* object, const char* name, float& value) noexcept
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) { return false; }
    value = static_cast<float>(item->valuedouble);
    return std::isfinite(value);
}

bool readSchema(const cJSON* object, ParseError& error) noexcept
{
    std::uint32_t schema{0U};
    if (!readUnsigned(object, "schema_version", schema))
    {
        error = ParseError::InvalidType;
        return false;
    }
    if (schema != static_cast<std::uint32_t>(SchemaVersion))
    {
        error = ParseError::UnsupportedSchema;
        return false;
    }
    return true;
}

bool copySession(const cJSON* object,
                 std::array<char, runtime::SessionIdSize>& destination) noexcept
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, "session_id");
    if (!cJSON_IsString(item) || (item->valuestring == nullptr)) { return false; }
    const std::size_t length = std::strlen(item->valuestring);
    if ((length == 0U) || (length >= destination.size())) { return false; }
    std::memcpy(destination.data(), item->valuestring, length + 1U);
    return true;
}

bool printAndDelete(cJSON* root,
                    char* destination,
                    std::size_t destinationSize) noexcept
{
    const bool validDestination = (destination != nullptr) && (destinationSize > 0U)
        && (destinationSize <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const bool printed = validDestination
        && cJSON_PrintPreallocated(root,
                                  destination,
                                  static_cast<int>(destinationSize),
                                  false);
    cJSON_Delete(root);
    return printed;
}

bool addConfiguration(cJSON* root, const runtime::Configuration& config) noexcept
{
    return (cJSON_AddStringToObject(root, "drive_style", toString(config.driveStyle)) != nullptr)
        && (cJSON_AddNumberToObject(root, "stop_distance_cm", config.stopDistanceCm) != nullptr)
        && (cJSON_AddNumberToObject(root, "reaction_distance_cm", config.reactionDistanceCm) != nullptr)
        && (cJSON_AddNumberToObject(root, "loop_interval_ms", config.loopIntervalMs) != nullptr)
        && (cJSON_AddNumberToObject(root, "drive_duty", config.driveDuty) != nullptr)
        && (cJSON_AddNumberToObject(root,
                                    "telemetry_interval_ms",
                                    config.telemetryIntervalMs)
            != nullptr);
}
/**
 * @brief Parse a config/set payload.
 *
 * @param[in] driveStyleOnly Accept nothing but the drive style, as Ford does. Every other
 *            Ford setting is compiled in, so a payload offering one is a mistake worth
 *            reporting rather than a value worth applying. The caller seeds the request
 *            with the car's current configuration, so the untouched fields keep their
 *            values. See ADR 0009.
 */
bool parseConfiguration(const char* payload,
                        runtime::ConfigurationRequest& request,
                        ParseError& error,
                        const bool driveStyleOnly = false) noexcept
{
    error = ParseError::None;
    if (payload == nullptr || !boundedJson(payload))
    {
        error = ParseError::InvalidJson;
        return false;
    }

    cJSON* root = cJSON_ParseWithLengthOpts(payload, std::strlen(payload) + 1U, nullptr, true);
    if (root == nullptr)
    {
        error = ParseError::InvalidJson;
        return false;
    }

    constexpr const char* Fields[]{
        "schema_version",
        "revision",
        "stop_distance_cm",
        "drive_duty",
        "telemetry_interval_ms",
        "drive_style",
        "reaction_distance_cm",
        "loop_interval_ms",
    };

    constexpr const char* StyleOnlyFields[]{"schema_version", "revision", "drive_style"};

    bool valid{false};
    if (driveStyleOnly)
    {
        // Nothing is optional here: a narrowed payload carries the style and no more.
        valid = validateFields(root, StyleOnlyFields, std::size(StyleOnlyFields), error)
            && readSchema(root, error)
            && readUnsigned(root, "revision", request.revision);
        // The caller seeded these from the live configuration; keep them.
        request.hasReactionDistance = false;
        request.hasLoopInterval = false;
    }
    else
    {
        valid = validateFields(root, Fields, std::size(Fields), error, {"drive_style", "reaction_distance_cm", "loop_interval_ms"})
            && readSchema(root, error)
            && readUnsigned(root, "revision", request.revision)
            && readFloat(root, "stop_distance_cm", request.values.stopDistanceCm)
            && readFloat(root, "drive_duty", request.values.driveDuty)
            && readUnsigned(root,
                            "telemetry_interval_ms",
                            request.values.telemetryIntervalMs);

        request.hasReactionDistance = cJSON_HasObjectItem(root, "reaction_distance_cm");
        request.hasLoopInterval = cJSON_HasObjectItem(root, "loop_interval_ms");
        if (valid && request.hasReactionDistance)
        {
            valid = readFloat(root, "reaction_distance_cm", request.values.reactionDistanceCm);
        }
        if (valid && request.hasLoopInterval)
        {
            valid = readUnsigned(root, "loop_interval_ms", request.values.loopIntervalMs);
        }
    }
    const cJSON* style = cJSON_GetObjectItemCaseSensitive(root, "drive_style");
    request.hasDriveStyle = style != nullptr;
    if (valid && request.hasDriveStyle)
    {
        valid = readDriveStyle(style, request.values.driveStyle, error);
    }

    if (valid && (request.revision == 0U))
    {
        error = ParseError::InvalidRevision;
        valid = false;
    }
    else if (!valid && (error == ParseError::None))
    {
        error = ParseError::InvalidType;
    }

    cJSON_Delete(root);
    return valid;
}

bool parseCommand(const char* payload,
                  runtime::Command& command,
                  ParseError& error) noexcept
{
    error = ParseError::None;
    if (payload == nullptr || !boundedJson(payload))
    {
        error = ParseError::InvalidJson;
        return false;
    }

    cJSON* root = cJSON_ParseWithLengthOpts(payload, std::strlen(payload) + 1U, nullptr, true);
    if (root == nullptr)
    {
        error = ParseError::InvalidJson;
        return false;
    }

    const cJSON* commandItem = cJSON_GetObjectItemCaseSensitive(root, "command");
    if (!cJSON_IsString(commandItem) || (commandItem->valuestring == nullptr))
    {
        error = ParseError::InvalidType;
        cJSON_Delete(root);
        return false;
    }

    const bool servo = std::strcmp(commandItem->valuestring, "servo") == 0;
    const bool heartbeat = std::strcmp(commandItem->valuestring, "heartbeat") == 0;
    const bool drive = std::strcmp(commandItem->valuestring, "drive") == 0;
    constexpr const char* HeartbeatFields[]{"schema_version", "session_id", "command"};
    constexpr const char* ControlFields[]{
        "schema_version", "request_id", "session_id", "command"};

    constexpr const char* ServoFields[]{
        "schema_version", "request_id", "session_id", "command", "angle_deg"};
    // Streamed several times a second without acknowledgement, like a heartbeat.
    constexpr const char* DriveFields[]{
        "schema_version", "session_id", "command", "steering_command", "speed_command"};
    const char* const* fields = heartbeat ? HeartbeatFields : drive ? DriveFields
        : servo ? ServoFields : ControlFields;
    const std::size_t fieldCount = heartbeat ? std::size(HeartbeatFields)
        : drive ? std::size(DriveFields) : servo ? std::size(ServoFields) : std::size(ControlFields);
    bool valid = validateFields(root, fields, fieldCount, error) && readSchema(root, error)
        && copySession(root, command.sessionId);

    if (!valid)
    {
        if (error == ParseError::None) { error = ParseError::InvalidSession; }
        cJSON_Delete(root);
        return false;
    }

    if (heartbeat)
    {
        command.type = runtime::CommandType::Heartbeat;
        command.hasRequestId = false;
    }
    else if (drive)
    {
        command.type = runtime::CommandType::Drive;
        command.hasRequestId = false;
        if (!readFloat(root, "steering_command", command.steeringCommand)
            || !readFloat(root, "speed_command", command.speedCommand))
        {
            error = ParseError::InvalidType;
            cJSON_Delete(root);
            return false;
        }
    }
    else
    {
        if (!readUnsigned(root, "request_id", command.requestId) || (command.requestId == 0U))
        {
            error = ParseError::InvalidType;
            cJSON_Delete(root);
            return false;
        }
        command.hasRequestId = true;

        if (std::strcmp(commandItem->valuestring, "start") == 0)
        {
            command.type = runtime::CommandType::Start;
        }
        else if (servo)
        {
            command.type = runtime::CommandType::Servo;
            if (!readFloat(root, "angle_deg", command.servoAngleDegrees))
            {
                error = ParseError::InvalidType;
                cJSON_Delete(root);
                return false;
            }
        }
        else if (std::strcmp(commandItem->valuestring, "stop") == 0)
        {
            command.type = runtime::CommandType::Stop;
        }
        else if (std::strcmp(commandItem->valuestring, "store_gaps") == 0)
        {
            command.type = runtime::CommandType::StoreGaps;
        }
        else
        {
            error = ParseError::UnsupportedCommand;
            cJSON_Delete(root);
            return false;
        }
    }

    cJSON_Delete(root);
    return true;
}

bool writeConfigurationState(char* destination,
                             std::size_t destinationSize,
                             std::uint32_t requestedRevision,
                             const char* error,
                             const runtime::Control& control) noexcept
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) { return false; }

    const bool applied = (error == nullptr) || (error[0] == '\0');
    bool valid = (cJSON_AddNumberToObject(root, "schema_version", SchemaVersion) != nullptr)
        && (cJSON_AddNumberToObject(root, "revision", requestedRevision) != nullptr)
        && (cJSON_AddStringToObject(root, "result", applied ? (control.hasConfigurationRevision() ? "applied" : "defaults") : "rejected")
            != nullptr);

    if (!applied)
    {
        valid = valid && (cJSON_AddStringToObject(root, "error", error) != nullptr);
    }
    valid = valid && addConfiguration(root, control.configuration())
        && cJSON_AddBoolToObject(root, "system_test", control.isSystemTest()) != nullptr;
    if (!valid)
    {
        cJSON_Delete(root);
        return false;
    }
    return printAndDelete(root, destination, destinationSize);
}

bool writeCommandState(char* destination,
                       std::size_t destinationSize,
                       std::uint32_t requestId,
                       const char* result,
                       const char* error,
                       const runtime::Control& control) noexcept
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) { return false; }

    bool valid = (cJSON_AddNumberToObject(root, "schema_version", SchemaVersion) != nullptr)
        && (cJSON_AddNumberToObject(root, "last_request_id", requestId) != nullptr)
        && (cJSON_AddBoolToObject(root, "servo_test", control.isServoTest()) != nullptr)
        && (cJSON_AddNumberToObject(root, "servo_angle_deg", control.servoAngleDegrees()) != nullptr)
        && (cJSON_AddStringToObject(root, "session_id", control.activeSessionId()) != nullptr)
        && (cJSON_AddStringToObject(root, "drive_style", toString(control.configuration().driveStyle)) != nullptr)
        && (cJSON_AddStringToObject(root, "result", result) != nullptr)
        && (cJSON_AddStringToObject(root,
                                    "control_state",
                                    toString(control.controlState()))
            != nullptr)
        && (cJSON_AddStringToObject(root,
                                    "motion_state",
                                    toString(control.motionState()))
            != nullptr)
        && (cJSON_AddStringToObject(root, "reason", toString(control.stateReason()))
            != nullptr);

    if ((error != nullptr) && (error[0] != '\0'))
    {
        valid = valid && (cJSON_AddStringToObject(root, "error", error) != nullptr);
    }
    if (!valid)
    {
        cJSON_Delete(root);
        return false;
    }
    return printAndDelete(root, destination, destinationSize);
}

/**
 * @brief A value rounded to three decimals.
 *
 * cJSON prints a float as the double it widens to, 0.15 as 0.15000000596046448. With the
 * speed calibration object Ford's telemetry would not fit the 1024-byte payload that way,
 * so Ford's measured values and that object are rounded first.
 */
double rounded(const float value) noexcept
{
    return std::round(static_cast<double>(value) * 1000.0) / 1000.0;
}

/** Add a rounded number when it is finite; a NaN leaves the field out. */
bool addOptional(cJSON* object, const char* name, const float value) noexcept
{
    return !std::isfinite(value) || (cJSON_AddNumberToObject(object, name, rounded(value)) != nullptr);
}

/** Add SpeedCalibration's nested object; nothing when that style is not selected. */
bool addSpeedCalibration(cJSON* root, const SpeedCalibrationTelemetry& speed) noexcept
{
    if (speed.phase == nullptr) { return true; }

    cJSON* object = cJSON_AddObjectToObject(root, "speed_calibration");
    bool valid = (object != nullptr)
        && (cJSON_AddStringToObject(object, "phase", speed.phase) != nullptr)
        && (cJSON_AddNumberToObject(object, "leg", speed.leg) != nullptr)
        && (cJSON_AddNumberToObject(object, "leg_count", speed.legCount) != nullptr)
        && (cJSON_AddNumberToObject(object, "leg_m", rounded(speed.legM)) != nullptr)
        && (cJSON_AddNumberToObject(object, "from", rounded(speed.fromMs)) != nullptr)
        && (cJSON_AddNumberToObject(object, "target", rounded(speed.targetMs)) != nullptr)
        && (cJSON_AddBoolToObject(object, "forward", speed.forward) != nullptr)
        && (cJSON_AddNumberToObject(object, "stop_k", rounded(speed.stopK)) != nullptr);
    if (valid && (speed.failure != nullptr))
    {
        valid = cJSON_AddStringToObject(object, "failure", speed.failure) != nullptr;
    }
    if (!valid || (speed.lastResult == nullptr)) { return valid; }

    cJSON* last = cJSON_AddObjectToObject(object, "last");
    return (last != nullptr)
        && (cJSON_AddNumberToObject(last, "leg", speed.lastLeg) != nullptr)
        && (cJSON_AddNumberToObject(last, "from", rounded(speed.lastFromMs)) != nullptr)
        && (cJSON_AddNumberToObject(last, "target", rounded(speed.lastTargetMs)) != nullptr)
        && (cJSON_AddBoolToObject(last, "forward", speed.lastForward) != nullptr)
        && (cJSON_AddStringToObject(last, "result", speed.lastResult) != nullptr)
        && addOptional(last, "speed_ms", speed.lastSpeedMs)
        && addOptional(last, "duty", speed.lastDuty)
        && addOptional(last, "rise_s", speed.lastRiseS)
        && addOptional(last, "overshoot", speed.lastOvershootMs)
        && (cJSON_AddNumberToObject(last, "stop_m", rounded(speed.lastStopM)) != nullptr)
        && (cJSON_AddNumberToObject(last, "distance_m", rounded(speed.lastDistanceM)) != nullptr);
}

bool writeTelemetry(char* destination,
                    std::size_t destinationSize,
                    const WireTelemetrySnapshot& snapshot) noexcept
{
    cJSON* root = cJSON_CreateObject();
    cJSON* distances = root != nullptr ? cJSON_AddObjectToObject(root, "distance_cm") : nullptr;
    cJSON* motor = root != nullptr ? cJSON_AddObjectToObject(root, "motor") : nullptr;
    cJSON* adc = root != nullptr ? cJSON_AddObjectToObject(root, "adc_raw") : nullptr;
    if ((root == nullptr) || (distances == nullptr) || (motor == nullptr) || (adc == nullptr))
    {
        cJSON_Delete(root);
        return false;
    }

    bool valid = (cJSON_AddNumberToObject(root, "schema_version", SchemaVersion) != nullptr)
        && (cJSON_AddNumberToObject(root, "sequence", snapshot.sequence) != nullptr)
        && (cJSON_AddNumberToObject(root, "uptime_ms", snapshot.uptimeMs) != nullptr);

    constexpr const char* SensorNames[]{"left", "center", "right"};
    if (snapshot.systemTest)
    {
        cJSON* decision = cJSON_AddObjectToObject(root, "decision_distance_cm");
        valid = valid && decision != nullptr;
        for (std::size_t index{0U}; valid && index < snapshot.decisionDistancesCm.size(); ++index)
        {
            valid = cJSON_AddNumberToObject(decision, SensorNames[index], snapshot.decisionDistancesCm[index]) != nullptr;
        }
    }
    valid = valid && cJSON_AddBoolToObject(root, "system_test", snapshot.systemTest) != nullptr
        && cJSON_AddBoolToObject(root, "servo_test", snapshot.servoTest) != nullptr;
    bool hasClosest{false};
    std::size_t closestIndex{0U};
    float closestDistance{std::numeric_limits<float>::infinity()};

    for (std::size_t index{0U}; index < snapshot.distancesCm.size(); ++index)
    {
        const float distance = snapshot.distancesCm[index];
        if (std::isfinite(distance) && (distance > 0.0F))
        {
            valid = valid
                && (cJSON_AddNumberToObject(distances, SensorNames[index], distance) != nullptr);
            if (distance < closestDistance)
            {
                closestDistance = distance;
                closestIndex = index;
                hasClosest = true;
            }
        }
        else
        {
            valid = valid && (cJSON_AddNullToObject(distances, SensorNames[index]) != nullptr);
        }
    }

    for (std::size_t index{0U}; index < snapshot.adcRaw.size(); ++index)
    {
        const auto raw = snapshot.adcRaw[index];
        valid = valid && ((raw >= 0 && raw <= 4095)
            ? cJSON_AddNumberToObject(adc, SensorNames[index], raw) != nullptr
            : cJSON_AddNullToObject(adc, SensorNames[index]) != nullptr);
    }

    if (hasClosest)
    {
        cJSON* closest = cJSON_AddObjectToObject(root, "closest");
        valid = valid && (closest != nullptr)
            && (cJSON_AddStringToObject(closest, "sensor", SensorNames[closestIndex]) != nullptr)
            && (cJSON_AddNumberToObject(closest, "distance_cm", closestDistance) != nullptr);
    }
    else
    {
        valid = valid && (cJSON_AddNullToObject(root, "closest") != nullptr);
    }

    valid = valid
        && (cJSON_AddStringToObject(root, "drive_style", toString(snapshot.driveStyle)) != nullptr)
        && (cJSON_AddNumberToObject(root, "steering_deg", snapshot.steeringDegrees) != nullptr)
        && (cJSON_AddNumberToObject(motor, "speed_command", snapshot.speedCommand) != nullptr)
        && (cJSON_AddNumberToObject(motor, "forward_duty", snapshot.forwardDuty) != nullptr)
        && (cJSON_AddNumberToObject(motor, "backward_duty", snapshot.backwardDuty) != nullptr)
        && ((snapshot.motorState == nullptr)
            || (cJSON_AddStringToObject(motor, "state", snapshot.motorState) != nullptr))
        && (!std::isfinite(snapshot.batteryVoltage)
            || (cJSON_AddNumberToObject(root, "battery_v", rounded(snapshot.batteryVoltage)) != nullptr))
        && (!std::isfinite(snapshot.measuredSpeedMs)
            || (cJSON_AddNumberToObject(root, "measured_speed_ms", rounded(snapshot.measuredSpeedMs))
                != nullptr))
        && (!std::isfinite(snapshot.odometerDistanceM)
            || (cJSON_AddNumberToObject(root, "odometer_distance_m", rounded(snapshot.odometerDistanceM))
                != nullptr))
        && ((snapshot.measuredSpeedSource == nullptr)
            || (cJSON_AddStringToObject(root, "measured_speed_source", snapshot.measuredSpeedSource)
                != nullptr))
        && ((snapshot.measuredSpeedSource == nullptr)
            || (cJSON_AddNumberToObject(root,
                                        "odometer_phase_losses",
                                        static_cast<double>(snapshot.odometerPhaseLosses))
                != nullptr))
        && (!std::isfinite(snapshot.motorTemperatureC)
            || (cJSON_AddNumberToObject(root, "motor_temp_c", rounded(snapshot.motorTemperatureC)) != nullptr))
        && (!std::isfinite(snapshot.servoTemperatureC)
            || (cJSON_AddNumberToObject(root, "servo_temp_c", rounded(snapshot.servoTemperatureC)) != nullptr))
        && ((snapshot.piLink == nullptr)
            || (cJSON_AddStringToObject(root, "pi_link", snapshot.piLink) != nullptr))
        && ((snapshot.piLink == nullptr)
            || (cJSON_AddNumberToObject(root, "pi_link_dropped",
                                        static_cast<double>(snapshot.piLinkDropped)) != nullptr))
        && ((snapshot.motorFault < 0)
            || (cJSON_AddBoolToObject(root, "motor_fault", snapshot.motorFault > 0) != nullptr))
        && ((snapshot.motorFault < 0)
            || (cJSON_AddNumberToObject(root, "motor_faults",
                                        static_cast<double>(snapshot.motorFaultCount)) != nullptr))
        && (cJSON_AddStringToObject(root,
                                    "control_state",
                                    toString(snapshot.controlState))
            != nullptr)
        && (cJSON_AddStringToObject(root,
                                    "motion_state",
                                    toString(snapshot.motionState))
            != nullptr)
        && (cJSON_AddStringToObject(root, "reason", toString(snapshot.reason)) != nullptr);

    // GapCalibration's progress and result, nested and present only while that style is
    // selected. Absent means "not this style", the same way every optional field here
    // means "not available" by being missing rather than by being sent empty.
    if (valid && (snapshot.calibrationPhase != nullptr))
    {
        cJSON* calibration = cJSON_AddObjectToObject(root, "calibration");
        valid = (calibration != nullptr)
            && (cJSON_AddStringToObject(calibration, "phase", snapshot.calibrationPhase) != nullptr)
            && (cJSON_AddNumberToObject(calibration, "duty_index",
                                        snapshot.calibrationDutyIndex) != nullptr)
            && (cJSON_AddNumberToObject(calibration, "duty_count",
                                        snapshot.calibrationDutyCount) != nullptr)
            && (cJSON_AddNumberToObject(calibration, "revolutions",
                                        snapshot.calibrationSamples) != nullptr)
            && (cJSON_AddNumberToObject(calibration, "revolutions_wanted",
                                        snapshot.calibrationRevolutions) != nullptr)
            && (cJSON_AddBoolToObject(calibration, "stored",
                                      snapshot.calibrationStored) != nullptr)
            && (cJSON_AddBoolToObject(calibration, "store_failed",
                                      snapshot.calibrationStoreFailed) != nullptr)
            && (cJSON_AddBoolToObject(calibration, "overheat_guard",
                                      snapshot.calibrationOverheatGuard) != nullptr);
        if (valid && (snapshot.calibrationFailure != nullptr))
        {
            valid = cJSON_AddStringToObject(calibration, "failure",
                                            snapshot.calibrationFailure) != nullptr;
        }
        if (valid && std::isfinite(snapshot.calibrationSpread))
        {
            valid = cJSON_AddNumberToObject(calibration, "spread",
                                            snapshot.calibrationSpread) != nullptr;
        }
        if (valid && std::isfinite(snapshot.calibrationMargin))
        {
            valid = cJSON_AddNumberToObject(calibration, "phase_margin",
                                            snapshot.calibrationMargin) != nullptr;
        }
        if (valid && (snapshot.calibrationGaps != nullptr) && (snapshot.calibrationGapCount > 0U))
        {
            cJSON* gaps = cJSON_AddArrayToObject(calibration, "gaps");
            valid = gaps != nullptr;
            for (std::uint8_t index{0U}; valid && (index < snapshot.calibrationGapCount); ++index)
            {
                cJSON* value = cJSON_CreateNumber(snapshot.calibrationGaps[index]);
                valid = (value != nullptr);
                if (valid) { cJSON_AddItemToArray(gaps, value); }
            }
        }
    }

    valid = valid && addSpeedCalibration(root, snapshot.speedCalibration);

    if (valid && snapshot.driveStyle == navigation::DriveStyle::Disabled && snapshot.disabledCause != nullptr)
    {
        cJSON* disabled = cJSON_AddObjectToObject(root, "disabled");
        valid = (disabled != nullptr)
            && (cJSON_AddStringToObject(disabled, "cause", snapshot.disabledCause) != nullptr)
            && (!std::isfinite(snapshot.disabledTemperatureC)
                || (cJSON_AddNumberToObject(disabled, "temp_c", rounded(snapshot.disabledTemperatureC)) != nullptr));
    }

    if (!valid)
    {
        cJSON_Delete(root);
        return false;
    }
    return printAndDelete(root, destination, destinationSize);
}

const char* toString(ParseError error) noexcept
{
    switch (error)
    {
        case ParseError::None: return "none";
        case ParseError::InvalidJson: return "invalid_json";
        case ParseError::MissingField: return "missing_field";
        case ParseError::UnknownField: return "unknown_field";
        case ParseError::DuplicateField: return "duplicate_field";
        case ParseError::UnsupportedSchema: return "unsupported_schema";
        case ParseError::InvalidType: return "invalid_type";
        case ParseError::InvalidRevision: return "invalid_revision";
        case ParseError::InvalidSession: return "invalid_session";
        case ParseError::UnsupportedCommand: return "unsupported_command";
        case ParseError::InvalidDriveStyle: return "invalid_drive_style";
    }
    return "unknown_error";
}

const char* toString(runtime::ConfigurationResult result) noexcept
{
    switch (result)
    {
        case runtime::ConfigurationResult::Applied: return "none";
        case runtime::ConfigurationResult::Duplicate: return "none";
        case runtime::ConfigurationResult::InvalidRevision: return "invalid_revision";
        case runtime::ConfigurationResult::StopDistanceOutOfRange:
            return "stop_distance_out_of_range";
        case runtime::ConfigurationResult::ReactionDistanceOutOfRange: return "reaction_distance_out_of_range";
        case runtime::ConfigurationResult::LoopIntervalOutOfRange: return "loop_interval_out_of_range";
        case runtime::ConfigurationResult::DriveDutyOutOfRange:
            return "drive_duty_out_of_range";
        case runtime::ConfigurationResult::TelemetryIntervalOutOfRange:
            return "telemetry_interval_out_of_range";
        case runtime::ConfigurationResult::StaleRevision: return "stale_revision";
        case runtime::ConfigurationResult::InvalidDriveStyle: return "invalid_drive_style";
        case runtime::ConfigurationResult::DriveStyleRequiresDisarmed: return "drive_style_requires_disarmed";
    }
    return "unknown_error";
}

const char* toString(runtime::CommandError error) noexcept
{
    switch (error)
    {
        case runtime::CommandError::InvalidRequest: return "invalid_request";
        case runtime::CommandError::StaleRequest: return "stale_request";
        case runtime::CommandError::None: return "none";
        case runtime::CommandError::MqttDisconnected: return "mqtt_disconnected";
        case runtime::CommandError::InvalidSession: return "invalid_session";
        case runtime::CommandError::NotArmed: return "not_armed";
        case runtime::CommandError::SessionMismatch: return "session_mismatch";
    }
    return "unknown_error";
}

const char* toString(runtime::MotionState state) noexcept
{
    switch (state)
    {
        case runtime::MotionState::Stopped: return "stopped";
        case runtime::MotionState::Moving: return "moving";
        case runtime::MotionState::Inhibited: return "inhibited";
    }
    return "stopped";
}

} // namespace

NetworkSettings kconfigNetworkSettings() noexcept
{
    NetworkSettings settings{};
#if CONFIG_CNB_ENABLE_WIFI
    settings.wifiSsid = CONFIG_CNB_WIFI_SSID;
    settings.wifiPassword = CONFIG_CNB_WIFI_PASSWORD;
#endif
#if CONFIG_CNB_ENABLE_MQTT
    settings.mqttBrokerUri = CONFIG_CNB_MQTT_BROKER_URI;
    settings.mqttClientId = CONFIG_CNB_MQTT_CLIENT_ID;
    settings.mqttUsername = CONFIG_CNB_MQTT_USERNAME;
    settings.mqttPassword = CONFIG_CNB_MQTT_PASSWORD;
#endif
    return settings;
}

Manager::Manager(driver::factory::Interface& factory, const Topics& topics,
                 const NetworkSettings& network) noexcept
    : myTopics{topics}
{
    (void)factory;
    (void)network;

#if CONFIG_CNB_ENABLE_WIFI
    myWifi = factory.wifi(network.wifiSsid, network.wifiPassword);
#endif

#if CONFIG_CNB_ENABLE_MQTT
    const driver::mqtt::Config config{
        network.mqttBrokerUri, network.mqttClientId,
        network.mqttUsername, network.mqttPassword,
        static_cast<std::uint16_t>(CONFIG_CNB_MQTT_KEEPALIVE_SEC),
        myTopics.publish.status,
        myTopics.publish.status != nullptr ? MqttOfflinePayload : nullptr,
        driver::mqtt::Qos::AtLeastOnce, true,
    };

    myMqtt = factory.mqtt(config);
#endif
    configureSubscriptions();
}

Manager::Manager(std::unique_ptr<driver::wifi::Interface> wifi,
                 std::unique_ptr<driver::mqtt::Interface> mqtt, const Topics& topics) noexcept
    : myWifi{std::move(wifi)}, myMqtt{std::move(mqtt)}, myTopics{topics}
{
    configureSubscriptions();
}

void Manager::configureSubscriptions() noexcept
{
#if CONFIG_CNB_ENABLE_MQTT
    const bool configSubscriptionValid = myMqtt && ((myTopics.subscribe.configSet == nullptr)
        || myMqtt->subscribe(myTopics.subscribe.configSet,
                             driver::mqtt::Qos::AtLeastOnce));
    const bool commandSubscriptionValid = myMqtt && ((myTopics.subscribe.command == nullptr)
        || myMqtt->subscribe(myTopics.subscribe.command,
                             driver::mqtt::Qos::AtLeastOnce));
    if (myMqtt && (!configSubscriptionValid || !commandSubscriptionValid))
    {
        myMqtt.reset();
    }
#endif
}

Manager::~Manager() noexcept = default;

void Manager::process(std::uint32_t nowMs, runtime::Control& control) noexcept
{
#if CONFIG_CNB_ENABLE_WIFI
    if (myWifi)
    {
        if (myWifi->isConnected())
        {
            myWifiAttempted = false;
            myWifiRetryIndex = 0U;
        }
        else
        {
            const std::uint32_t delay = NetworkRetryDelaysMs[myWifiRetryIndex];
            const bool firstAttempt = !myWifiAttempted;
            if (firstAttempt || ((nowMs - myLastWifiAttemptMs) >= delay))
            {
                myWifiAttempted = true;
                myLastWifiAttemptMs = nowMs;
                if (myWifi->isInitialized()) { myWifi->reconnect(); }
                else { myWifi->connect(); }
                if (!firstAttempt && (myWifiRetryIndex + 1U < NetworkRetryDelaysMs.size()))
                {
                    ++myWifiRetryIndex;
                }
            }
        }
    }
#endif

#if CONFIG_CNB_ENABLE_MQTT
    const bool wifiConnected = myWifi && myWifi->isConnected();
    if (wifiConnected && myMqtt)
    {
        if (myMqtt->isConnected())
        {
            myMqttAttempted = false;
            myMqttRetryIndex = 0U;
        }
        else
        {
            const std::uint32_t delay = NetworkRetryDelaysMs[myMqttRetryIndex];
            const bool firstAttempt = !myMqttAttempted;
            if (firstAttempt || ((nowMs - myLastMqttAttemptMs) >= delay))
            {
                myMqttAttempted = true;
                myLastMqttAttemptMs = nowMs;
                if (myMqtt->isInitialized()) { myMqtt->reconnect(); }
                else { myMqtt->connect(); }
                if (!firstAttempt && (myMqttRetryIndex + 1U < NetworkRetryDelaysMs.size()))
                {
                    ++myMqttRetryIndex;
                }
            }
        }
    }
    else
    {
        myMqttAttempted = false;
        myMqttRetryIndex = 0U;
    }

    const bool mqttConnected = wifiConnected && myMqtt && myMqtt->isConnected();
    if (myMqtt && myMqtt->consumeDisconnect())
    {
        control.setMqttConnected(false);
        myPreviousMqttConnected = false;
    }
    control.setMqttConnected(mqttConnected);

    if (mqttConnected && !myPreviousMqttConnected)
    {
        myOnlineStateDirty = true;
        publishConfigurationState(control.configurationRevision(), nullptr, control);
        setCommandReport(myLastCommandRequestId, "state", nullptr);
    }
    else if (!mqttConnected && myPreviousMqttConnected)
    {
        setCommandReport(myLastCommandRequestId, "state", nullptr);
    }
    myPreviousMqttConnected = mqttConnected;

    if (mqttConnected)
    {
        processMqttMessages(nowMs, control);
        flushState(control);
        publishCommandState(control);
    }
#else
    (void)nowMs;
    control.setMqttConnected(false);
#endif
}

void Manager::notifyControlStateChanged() noexcept
{
    // Preserve a pending command acknowledgement until it has been queued.
    if (!myCommandStateDirty) { setCommandReport(myLastCommandRequestId, "state", nullptr); }
}

void Manager::publishTelemetry(std::uint32_t nowMs,
                               const TelemetrySnapshot& snapshot,
                               const runtime::Control& control) noexcept
{
#if CONFIG_CNB_ENABLE_MQTT
    if (!myMqtt || !myMqtt->isConnected() || (myTopics.publish.telemetry == nullptr))
    {
        return;
    }
    if ((nowMs - myLastTelemetryMs) < control.configuration().telemetryIntervalMs)
    {
        return;
    }
    myLastTelemetryMs = nowMs;

    WireTelemetrySnapshot wireSnapshot{};
    wireSnapshot.driveStyle = control.configuration().driveStyle;
    wireSnapshot.sequence = myTelemetrySequence + 1U;
    wireSnapshot.uptimeMs = nowMs;
    wireSnapshot.distancesCm = snapshot.distancesCm;
    wireSnapshot.decisionDistancesCm = snapshot.decisionDistancesCm;
    wireSnapshot.systemTest = control.isSystemTest();
    wireSnapshot.servoTest = control.isServoTest();
    wireSnapshot.adcRaw = snapshot.adcRaw;
    wireSnapshot.speedCommand = snapshot.speedCommand;
    wireSnapshot.steeringDegrees = snapshot.steeringDegrees;
    wireSnapshot.forwardDuty = snapshot.forwardDuty;
    wireSnapshot.backwardDuty = snapshot.backwardDuty;
    wireSnapshot.motorState = snapshot.motorState;
    wireSnapshot.batteryVoltage = snapshot.batteryVoltage;
    wireSnapshot.measuredSpeedMs = snapshot.measuredSpeedMs;
    wireSnapshot.odometerDistanceM = snapshot.odometerDistanceM;
    wireSnapshot.measuredSpeedSource = snapshot.measuredSpeedSource;
    wireSnapshot.odometerPhaseLosses = snapshot.odometerPhaseLosses;
    wireSnapshot.motorTemperatureC = snapshot.motorTemperatureC;
    wireSnapshot.servoTemperatureC = snapshot.servoTemperatureC;
    wireSnapshot.piLink = snapshot.piLink;
    wireSnapshot.piLinkDropped = snapshot.piLinkDropped;
    wireSnapshot.motorFault = snapshot.motorFault;
    wireSnapshot.motorFaultCount = snapshot.motorFaultCount;
    wireSnapshot.calibrationPhase = snapshot.calibrationPhase;
    wireSnapshot.calibrationFailure = snapshot.calibrationFailure;
    wireSnapshot.calibrationDutyIndex = snapshot.calibrationDutyIndex;
    wireSnapshot.calibrationDutyCount = snapshot.calibrationDutyCount;
    wireSnapshot.calibrationSamples = snapshot.calibrationSamples;
    wireSnapshot.calibrationRevolutions = snapshot.calibrationRevolutions;
    wireSnapshot.calibrationGaps = snapshot.calibrationGaps;
    wireSnapshot.calibrationGapCount = snapshot.calibrationGapCount;
    wireSnapshot.calibrationSpread = snapshot.calibrationSpread;
    wireSnapshot.calibrationMargin = snapshot.calibrationMargin;
    wireSnapshot.calibrationStored = snapshot.calibrationStored;
    wireSnapshot.calibrationStoreFailed = snapshot.calibrationStoreFailed;
    wireSnapshot.calibrationOverheatGuard = snapshot.calibrationOverheatGuard;
    wireSnapshot.disabledCause = snapshot.disabledCause;
    wireSnapshot.disabledTemperatureC = snapshot.disabledTemperatureC;
    wireSnapshot.speedCalibration = snapshot.speedCalibration;
    wireSnapshot.controlState = control.controlState();
    wireSnapshot.motionState = control.motionState();
    wireSnapshot.reason = control.stateReason();

    char payload[driver::mqtt::PayloadSize]{};
    if (writeTelemetry(payload, sizeof(payload), wireSnapshot)
        && myMqtt->publish(myTopics.publish.telemetry,
                           payload,
                           driver::mqtt::Qos::AtMostOnce,
                           false))
    {
        myTelemetrySequence = wireSnapshot.sequence;
    }
#else
    (void)nowMs;
    (void)snapshot;
    (void)control;
#endif
}

void Manager::disconnect() noexcept
{
    if (myMqtt) { myMqtt->disconnect(); }
    if (myWifi) { myWifi->disconnect(); }
}

void Manager::processMqttMessages(std::uint32_t nowMs,
                                  runtime::Control& control) noexcept
{
#if CONFIG_CNB_ENABLE_MQTT
    if (!myMqtt) { return; }

    // Both paths consume into the same buffer. Separate locals reserve two
    // full messages in this stack frame with the ESP32 debug compiler.
    driver::mqtt::Message message{};
    if (myMqtt->consumeReceiveOverflow())
    {
        control.forceDisarm(runtime::StateReason::MessageOverflow);
        setCommandReport(myLastCommandRequestId, "state", "message_overflow");

        // Unknown dropped commands make the complete batch unsafe to process.
        for (std::size_t i{0U}; i < MaxMqttMessagesPerTick && myMqtt->readMessage(message); ++i) {}
        return;
    }

    std::size_t processed{0U};
    while ((processed < MaxMqttMessagesPerTick) && myMqtt->readMessage(message))
    {
        processMqttMessage(message, nowMs, control);
        ++processed;
    }
#else
    (void)nowMs;
    (void)control;
#endif
}

void Manager::processMqttMessage(const driver::mqtt::Message& message,
                                 std::uint32_t nowMs,
                                 runtime::Control& control) noexcept
{
#if CONFIG_CNB_ENABLE_MQTT
    if ((myTopics.subscribe.configSet != nullptr)
        && (std::strcmp(message.topic.data(), myTopics.subscribe.configSet) == 0))
    {
        runtime::ConfigurationRequest request{};
        // A remote-driven car accepts the drive style and nothing else, so the request
        // starts from what is already live and only the style can move.
        const bool driveStyleOnly{control.isRemoteDrivenCar()};
        if (driveStyleOnly) { request.values = control.configuration(); }
        ParseError parseError{};
        if (!parseConfiguration(message.payload.data(), request, parseError, driveStyleOnly))
        {
            publishConfigurationState(request.revision, toString(parseError), control);
            return;
        }

        const auto previousStyle = control.configuration().driveStyle;
        const auto result = control.applyConfiguration(request);
        if (previousStyle != control.configuration().driveStyle) { notifyControlStateChanged(); }
        const bool applied = (result == runtime::ConfigurationResult::Applied)
            || (result == runtime::ConfigurationResult::Duplicate);
        publishConfigurationState(request.revision,
                                  applied ? nullptr : toString(result),
                                  control);
        return;
    }

    if ((myTopics.subscribe.command == nullptr)
        || (std::strcmp(message.topic.data(), myTopics.subscribe.command) != 0))
    {
        return;
    }
    if (message.retained)
    {
        setCommandReport(0U, "rejected", "retained_command");
        return;
    }

    runtime::Command command{};
    ParseError parseError{};
    if (!parseCommand(message.payload.data(), command, parseError))
    {
        const std::uint32_t requestId = command.hasRequestId
            ? command.requestId
            : 0U;
        setCommandReport(requestId, "rejected", toString(parseError));
        return;
    }

    const auto result = control.handleCommand(command, nowMs);
    // Accepted heartbeats and drive commands are streamed, so only their rejections are reported.
    const bool streamed = (command.type == runtime::CommandType::Heartbeat)
        || (command.type == runtime::CommandType::Drive);
    if (!streamed || !result.accepted)
    {
        setCommandReport(command.hasRequestId ? command.requestId : 0U,
                         result.accepted ? "accepted" : "rejected",
                         result.accepted ? nullptr : toString(result.error));
    }
#else
    (void)message;
    (void)nowMs;
    (void)control;
#endif
}

void Manager::publishConfigurationState(std::uint32_t requestedRevision,
                                        const char* error,
                                        const runtime::Control& control) noexcept
{
    myPendingConfigRevision = requestedRevision;
    myPendingConfigError = error;
    myConfigStateDirty = true;
    flushState(control);
}

void Manager::flushState(const runtime::Control& control) noexcept
{
#if CONFIG_CNB_ENABLE_MQTT
    if (!myMqtt || !myMqtt->isConnected()) { return; }
    if (myOnlineStateDirty && myTopics.publish.status != nullptr
        && myMqtt->publish(myTopics.publish.status, MqttOnlinePayload,
                           driver::mqtt::Qos::AtLeastOnce, true))
    {
        myOnlineStateDirty = false;
    }
    if (myConfigStateDirty && myTopics.publish.configState != nullptr)
    {
        char payload[driver::mqtt::PayloadSize]{};
        if (writeConfigurationState(payload, sizeof(payload), myPendingConfigRevision,
                                     myPendingConfigError, control)
            && myMqtt->publish(myTopics.publish.configState, payload,
                               driver::mqtt::Qos::AtLeastOnce, true))
        {
            myConfigStateDirty = false;
        }
    }
#else
    (void)control;
#endif
}

void Manager::publishCommandState(const runtime::Control& control) noexcept
{
#if CONFIG_CNB_ENABLE_MQTT
    if (!myCommandStateDirty || !myMqtt || !myMqtt->isConnected()
        || (myTopics.publish.commandState == nullptr))
    {
        return;
    }
    char payload[driver::mqtt::PayloadSize]{};
    if (writeCommandState(payload,
                          sizeof(payload),
                          myLastCommandRequestId,
                          myLastCommandResult,
                          myLastCommandError,
                          control)
        && myMqtt->publish(myTopics.publish.commandState,
                           payload,
                           driver::mqtt::Qos::AtLeastOnce,
                           true))
    {
        myCommandStateDirty = false;
    }
#else
    (void)control;
#endif
}

void Manager::setCommandReport(std::uint32_t requestId,
                               const char* result,
                               const char* error) noexcept
{
    myLastCommandRequestId = requestId;
    myLastCommandResult = result;
    myLastCommandError = error;
    myCommandStateDirty = true;
}

} // namespace app::communication
