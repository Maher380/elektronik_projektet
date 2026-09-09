#include "test/runtime_control.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "system/runtime/control.h"

namespace test
{
namespace
{
bool expect(bool condition, const char* message)
{
    if (condition) { return true; }
    std::printf("Runtime control test failed: %s\n", message);
    return false;
}

app::runtime::Command command(app::runtime::CommandType type,
                              const char* session,
                              std::uint32_t requestId = 0U)
{
    app::runtime::Command result{};
    result.type = type;
    result.requestId = requestId;
    result.hasRequestId = type != app::runtime::CommandType::Heartbeat;
    std::strncpy(result.sessionId.data(), session, result.sessionId.size() - 1U);
    return result;
}
} // namespace

bool runRuntimeControlTests()
{
    using namespace app::runtime;

    Control control;
    const std::array<float, IrSensorCount> clearDistances{50.0F, 60.0F, 70.0F};

    if (!expect(control.controlState() == ControlState::Disarmed, "boot is disarmed"))
    {
        return false;
    }
    if (!expect(control.evaluate(clearDistances, 0U) == 0.0F, "boot motor output is zero"))
    {
        return false;
    }

    ConfigurationRequest validConfig{1U, {35.0F, 0.4F, 500U}};
    if (!expect(control.applyConfiguration(validConfig) == ConfigurationResult::Applied,
                "valid configuration applies"))
    {
        return false;
    }
    if (!expect(control.applyConfiguration(validConfig) == ConfigurationResult::Duplicate,
                "duplicate configuration is idempotent"))
    {
        return false;
    }

    ConfigurationRequest invalidDuty{2U, {40.0F, 0.8F, 1000U}};
    if (!expect(control.applyConfiguration(invalidDuty)
                    == ConfigurationResult::DriveDutyOutOfRange,
                "unsafe duty is rejected"))
    {
        return false;
    }
    if (!expect(control.configuration().driveDuty == 0.4F,
                "rejected configuration is atomic"))
    {
        return false;
    }

    ConfigurationRequest conflictingRevision{1U, {40.0F, 0.3F, 1000U}};
    if (!expect(control.applyConfiguration(conflictingRevision)
                    == ConfigurationResult::StaleRevision,
                "conflicting revision is stale"))
    {
        return false;
    }

    const auto start = command(CommandType::Start, "session-a", 10U);
    if (!expect(!control.handleCommand(start, 100U).accepted,
                "start is rejected while MQTT is disconnected"))
    {
        return false;
    }

    control.setMqttConnected(true);
    if (!expect(control.handleCommand(start, 100U).accepted, "connected start is accepted"))
    {
        return false;
    }
    if (!expect(control.evaluate(clearDistances, 100U) == 0.4F,
                "armed car uses configured duty"))
    {
        return false;
    }

    const std::array<float, IrSensorCount> obstacleDistances{50.0F, 20.0F, 70.0F};
    if (!expect(control.evaluate(obstacleDistances, 200U) == 0.0F,
                "obstacle inhibits motion"))
    {
        return false;
    }
    if (!expect((control.controlState() == ControlState::Armed)
                    && (control.stateReason() == StateReason::Obstacle),
                "obstacle does not disarm autonomous control"))
    {
        return false;
    }

    auto invalidDistances = clearDistances;
    invalidDistances[0] = std::nanf("");
    if (!expect(control.evaluate(invalidDistances, 300U) == 0.0F,
                "invalid sensor inhibits motion"))
    {
        return false;
    }

    const auto wrongHeartbeat = command(CommandType::Heartbeat, "session-b");
    if (!expect(control.handleCommand(wrongHeartbeat, 1000U).error
                    == CommandError::SessionMismatch,
                "wrong heartbeat session is rejected"))
    {
        return false;
    }

    const auto heartbeat = command(CommandType::Heartbeat, "session-a");
    if (!expect(control.handleCommand(heartbeat, 2500U).accepted,
                "active session heartbeat is accepted"))
    {
        return false;
    }
    if (!expect(control.evaluate(clearDistances, 5499U) == 0.4F,
                "heartbeat lease remains active before timeout"))
    {
        return false;
    }
    if (!expect(control.evaluate(clearDistances, 5500U) == 0.0F,
                "heartbeat timeout stops the motor"))
    {
        return false;
    }
    if (!expect((control.controlState() == ControlState::Disarmed)
                    && (control.stateReason() == StateReason::HeartbeatTimeout),
                "heartbeat timeout disarms the car"))
    {
        return false;
    }

    control.handleCommand(command(CommandType::Start, "session-a", 12U), 6000U);
    control.setMqttConnected(false);
    if (!expect((control.controlState() == ControlState::Disarmed)
                    && (control.stateReason() == StateReason::MqttDisconnected),
                "MQTT loss disarms the car"))
    {
        return false;
    }

    control.setMqttConnected(true);
    control.handleCommand(command(CommandType::Start, "session-a", 13U), 7000U);
    const auto stop = command(CommandType::Stop, "different-session", 11U);
    if (!expect(control.handleCommand(stop, 7100U).accepted,
                "stop from another session is accepted"))
    {
        return false;
    }
    if (!expect(control.controlState() == ControlState::Disarmed,
                "operator stop disarms the car"))
    {
        return false;
    }

    return true;
}

} // namespace test
