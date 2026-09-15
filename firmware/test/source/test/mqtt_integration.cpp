#include "test/mqtt_integration.h"
#include "system/communication/manager.h"
#include "system/navigation/planner.h"
#include "system/runtime/output.h"
#include "driver/mqtt/interface.h"
#include "driver/wifi/interface.h"
#include "driver/motor/mp6550.h"
#include "driver/servo/vagrant.h"
#include "driver/pwm/interface.h"
#include "driver/gpio/stub.h"
#include "driver/adc/stub.h"
#include "driver/ir_sensor/esp32s3.h"
#include "cJSON.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::printf("MQTT integration failure at line %d: %s\n", __LINE__, #value); return false; } } while (false)

namespace test
{
namespace
{
using namespace app;
constexpr communication::Topics Topics{{"telemetry", "config/state", "command/state", "status"}, {"config/set", "command"}};
class Wifi final : public driver::wifi::Interface
{
public:
    bool initialized{false}, connected{true};
    int attempts{0};
    bool connect() noexcept override { ++attempts; initialized = true; return true; }
    bool reconnect() noexcept override { ++attempts; return true; }
    void disconnect() noexcept override { connected = false; initialized = false; }
    bool isInitialized() const noexcept override { return initialized; }
    bool isConnected() const noexcept override { return connected; }
};
class Mqtt final : public driver::mqtt::Interface
{
public:
    bool initialized{false}, connected{true}, overflow{false}, lost{false}, rejectPublish{false};
    int attempts{0}, publications{0};
    std::deque<driver::mqtt::Message> incoming;
    std::map<std::string, driver::mqtt::Message> sent;
    std::vector<std::string> subscriptions;
    bool connect() noexcept override { ++attempts; initialized = true; return true; }
    bool reconnect() noexcept override { ++attempts; return true; }
    void disconnect() noexcept override { connected = false; initialized = false; lost = true; }
    bool isInitialized() const noexcept override { return initialized; }
    bool isConnected() const noexcept override { return connected; }
    bool consumeDisconnect() noexcept override { bool result = lost; lost = false; return result; }
    bool consumeReceiveOverflow() noexcept override { bool result = overflow; overflow = false; return result; }
    bool subscribe(const char* topic, driver::mqtt::Qos) noexcept override { subscriptions.emplace_back(topic); return true; }
    bool readMessage(driver::mqtt::Message& result) noexcept override
    {
        if (incoming.empty()) { return false; }
        result = incoming.front(); incoming.pop_front(); return true;
    }
    static driver::mqtt::Message message(const char* topic, const char* payload,
                                         driver::mqtt::Qos qos, bool retained)
    {
        driver::mqtt::Message result{};
        std::snprintf(result.topic.data(), result.topic.size(), "%s", topic);
        std::snprintf(result.payload.data(), result.payload.size(), "%s", payload);
        result.qos = qos; result.retained = retained; return result;
    }
    bool publish(const char* topic, const char* payload, driver::mqtt::Qos qos, bool retained) noexcept override
    {
        if (rejectPublish || !connected) { return false; }
        sent[topic] = message(topic, payload, qos, retained); ++publications; return true;
    }
    void receive(const char* topic, const char* payload, bool retained = false)
    { incoming.push_back(message(topic, payload, driver::mqtt::Qos::AtLeastOnce, retained)); }
    std::string field(const char* topic, const char* key)
    {
        cJSON* root = cJSON_Parse(sent[topic].payload.data());
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, key);
        std::string result = cJSON_IsString(value) ? value->valuestring : "";
        cJSON_Delete(root); return result;
    }
};

class Pwm final : public driver::pwm::Interface
{
public:
    bool initialized{false}, fail{false}; float value{0}; int writes{0}; std::uint32_t hz{300};
    bool init() noexcept override { initialized = true; return true; }
    bool deinit() noexcept override { initialized = false; return true; }
    bool isInitialized() const noexcept override { return initialized; }
    bool setDuty(float duty) noexcept override { ++writes; if (fail) { return false; } value = duty; return true; }
    float duty() const noexcept override { return value; }
    std::uint32_t frequencyHz() const noexcept override { return hz; }
    bool setFrequencyHz(std::uint32_t frequency) noexcept override { if (fail) { return false; } hz = frequency; return true; }
};

runtime::Command command(runtime::CommandType type, std::uint32_t id, const char* session = "a")
{
    runtime::Command result{}; result.type = type; result.requestId = id;
    result.hasRequestId = type != runtime::CommandType::Heartbeat;
    std::strncpy(result.sessionId.data(), session, result.sessionId.size() - 1U);
    return result;
}

bool navigationAndLease()
{
    using namespace navigation;
    using namespace runtime;
    Planner planner;
    Control control;
    control.setMqttConnected(true);
    CHECK(control.handleCommand(command(CommandType::Start, 1), 100).accepted);
    struct Case { Distances distances; float angle; std::size_t sensor; };
    const Case cases[]{ {{70,20,25},-90,Left}, {{10,20,70},90,Right},
                        {{50,60,40},0,Forward}, {{60,60,60},90,Right}, {{60,60,40},-90,Left} };
    for (const auto& item : cases)
    {
        const auto choice = planner.decide(item.distances);
        CHECK(choice.steeringDegrees == item.angle && choice.pathSensor == item.sensor);
        CHECK(control.evaluate(item.distances, 100, choice.pathSensor, choice.driveDuty) == 0.5F);
    }
    CHECK(control.evaluate({50,30,50},100,Forward) == 0.5F);
    CHECK(control.evaluate({50,29,50},100,Forward) == 0.0F);
    CHECK(control.evaluate({std::nanf(""),60,70},100,Right) == 0.0F);
    CHECK(control.evaluate({60,60,70},100,SensorCount) == 0.0F);
    CHECK(control.evaluate({60,60,70},100,Right,0.6F) == 0.0F);
    planner.setDriverStyle(DriverStyle::SlowLeft);
    auto choice = planner.decide({10,60,70});
    CHECK(choice.steeringDegrees == -90 && choice.pathSensor == Left && choice.driveDuty == 0.2F);
    CHECK(control.evaluate({10,60,70},100,choice.pathSensor,choice.driveDuty) == 0.0F);
    CHECK(control.evaluate({60,60,70},100,choice.pathSensor,choice.driveDuty) == 0.2F);
    planner.setDriverStyle(DriverStyle::GradualSweep);
    float previous = -95;
    bool reachedRight = false;
    for (int i = 0; i < 100; ++i)
    {
        choice = planner.decide({60,60,70});
        CHECK(choice.steeringDegrees >= -90 && choice.steeringDegrees <= 90);
        CHECK(std::fabs(choice.steeringDegrees - previous) == 5);
        reachedRight = reachedRight || choice.steeringDegrees == 90;
        previous = choice.steeringDegrees;
    }
    CHECK(reachedRight);
    CHECK(control.handleCommand(command(CommandType::Start,1), 3000).accepted);
    CHECK(control.evaluate({60,60,70},3100) == 0); // duplicate did not extend lease
    CHECK(!control.handleCommand(command(CommandType::Start,1),3101).accepted);
    CHECK(control.handleCommand(command(CommandType::Start,2),4000).accepted);
    CHECK(!control.handleCommand(command(CommandType::Heartbeat,0),7000).accepted); // expired before next tick
    CHECK(control.handleCommand(command(CommandType::Start,3),8000).accepted);
    CHECK(control.handleCommand(command(CommandType::Stop,4,"other"),8001).accepted);
    CHECK(!control.handleCommand(command(CommandType::Start,3),8002).accepted);
    CHECK(control.handleCommand(command(CommandType::Start,5),9000).accepted);
    control.setMqttConnected(false); control.setMqttConnected(true);
    CHECK(!control.handleCommand(command(CommandType::Start,5),9001).accepted);
    CHECK(control.handleCommand(command(CommandType::Start,6),0xfffffff0U).accepted);
    CHECK(control.evaluate({60,60,70},100) == 0.5F); // uptime wrap
    control.forceDisarm(StateReason::ActuatorFault);
    CHECK(!control.handleCommand(command(CommandType::Start,7),101).accepted);
    control.forceDisarm(StateReason::OperatorStop);
    CHECK(control.stateReason() == StateReason::ActuatorFault);
    return true;
}

bool outputFailures()
{
    Pwm forward, backward, steering;
    driver::gpio::Stub sleep;
    driver::motor::MP6550 motor{forward,backward};
    driver::servo::Vagrant servo{steering};
    CHECK(motor.init() && servo.init());
    CHECK(runtime::applyOutput(motor,servo,forward,backward,sleep,0.4F,-90,true));
    CHECK(sleep.read() && forward.value == 0.4F && backward.value == 0);
    CHECK(runtime::applyOutput(motor,servo,forward,backward,sleep,0,90,true));
    CHECK(sleep.read() && forward.value == 1 && backward.value == 1);
    forward.fail = true;
    const int attempts = backward.writes;
    CHECK(!runtime::applyOutput(motor,servo,forward,backward,sleep,0.4F,0,true));
    CHECK(!sleep.read() && backward.value == 0 && backward.writes > attempts);
    forward.fail = false;
    CHECK(runtime::applyOutput(motor,servo,forward,backward,sleep,0.4F,0,true));
    steering.fail = true;
    CHECK(!runtime::applyOutput(motor,servo,forward,backward,sleep,0.4F,90,true));
    CHECK(!sleep.read() && forward.value == 0 && backward.value == 0);
    steering.fail = false;
    CHECK(!servo.setDirection(std::nanf("")) && !servo.setDirection(91));
    CHECK(runtime::applyOutput(motor,servo,forward,backward,sleep,0,0,false));
    CHECK(!sleep.read());
    return true;
}

bool protocol()
{
    auto wifi = std::make_unique<Wifi>();
    auto mqtt = std::make_unique<Mqtt>(); auto* link = mqtt.get();
    communication::Manager manager{std::move(wifi),std::move(mqtt),Topics};
    runtime::Control control;
    CHECK(link->subscriptions.size() == 2);
    manager.process(0,control);
    CHECK(link->field("config/state","result") == "defaults");
    const char* config = R"({"schema_version":1,"revision":1,"stop_distance_cm":35,"drive_duty":0.4,"telemetry_interval_ms":200})";
    link->receive("config/set",config,true); manager.process(50,control);
    CHECK(control.configurationRevision() == 1 && control.configuration().driveDuty == 0.4F);
    CHECK(control.controlState() == runtime::ControlState::Disarmed);
    CHECK(link->sent["config/state"].retained && link->field("config/state","result") == "applied");
    const char* invalid[]{
        R"({"schema_version":1,"revision":2,"stop_distance_cm":35,"drive_duty":0.4,"telemetry_interval_ms":200,"extra":[[[[[]]]]]})",
        R"({"schema_version":1,"revision":2,"stop_distance_cm":35,"drive_duty\u0000":0.4,"telemetry_interval_ms":200})",
        R"({"schema_version":1,"revision":2,"stop_distance_cm":35,"drive_duty":0.4,"telemetry_interval_ms":200}junk)",
        R"({"schema_version":1,"revision":2,"stop_distance_cm":35,"drive_duty":0.4,"telemetry_interval_ms":200,"extra":1})",
        R"({"schema_version":1,"revision":2,"stop_distance_cm":35,"drive_duty":0.4,"drive_duty":0.5,"telemetry_interval_ms":200})",
        R"({"schema_version":1,"revision":2,"stop_distance_cm":35,"drive_duty":1e999,"telemetry_interval_ms":200})",
        R"({"schema_version":1,"revision":4294967296,"stop_distance_cm":35,"drive_duty":0.4,"telemetry_interval_ms":200})",
        R"({"schema_version":1,"revision":2,"stop_distance_cm":29,"drive_duty":0.4,"telemetry_interval_ms":200})"
    };
    for (const char* payload : invalid)
    {
        link->receive("config/set",payload); manager.process(100,control);
        CHECK(control.configurationRevision() == 1 && link->field("config/state","result") == "rejected");
    }
    const char* start = R"({"schema_version":1,"request_id":10,"session_id":"a","command":"start"})";
    link->receive("command",start,true); manager.process(100,control);
    CHECK(control.controlState() == runtime::ControlState::Disarmed);
    CHECK(link->field("command/state","error") == "retained_command");
    link->rejectPublish = true;
    link->receive("command",start); manager.process(150,control);
    CHECK(control.controlState() == runtime::ControlState::Armed);
    control.evaluate({60,60,70},150);
    manager.notifyControlStateChanged(); // must not erase pending acknowledgement
    link->rejectPublish = false; manager.process(200,control);
    CHECK(link->field("command/state","result") == "accepted");
    CHECK(link->field("command/state","session_id") == "a");
    link->receive("command",R"({"schema_version":1,"session_id":"other","command":"heartbeat"})");
    manager.process(250,control);
    CHECK(link->field("command/state","error") == "session_mismatch");
    CHECK(std::strstr(link->sent["command/state"].payload.data(),"\"last_request_id\":0") != nullptr);
    communication::TelemetrySnapshot snapshot{{std::nanf(""),60,70},0.4F,-90,0.4F,0};
    snapshot.adcRaw = {0,4095,-1};
    manager.publishTelemetry(200,snapshot,control);
    CHECK(!link->sent["telemetry"].retained && link->sent["telemetry"].qos == driver::mqtt::Qos::AtMostOnce);
    cJSON* telemetry = cJSON_Parse(link->sent["telemetry"].payload.data());
    CHECK(telemetry != nullptr);
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(telemetry,"distance_cm"),"left")));
    CHECK(cJSON_GetObjectItemCaseSensitive(telemetry,"steering_deg")->valuedouble == -90);
    const auto* adc = cJSON_GetObjectItemCaseSensitive(telemetry,"adc_raw");
    CHECK(cJSON_GetObjectItemCaseSensitive(adc,"left")->valuedouble == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(adc,"center")->valuedouble == 4095);
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(adc,"right")));
    cJSON_Delete(telemetry);
    const int publications = link->publications;
    manager.publishTelemetry(399,snapshot,control); CHECK(link->publications == publications);
    manager.publishTelemetry(400,snapshot,control); CHECK(link->publications == publications + 1);
    link->lost = true; // disconnect/reconnect both happened between ticks
    manager.process(450,control);
    CHECK(control.controlState() == runtime::ControlState::Disarmed);
    link->receive("command",start); manager.process(500,control);
    CHECK(control.controlState() == runtime::ControlState::Disarmed);
    link->receive("command",R"({"schema_version":1,"request_id":11,"session_id":"a","command":"start"})");
    manager.process(550,control); CHECK(control.controlState() == runtime::ControlState::Armed);
    link->overflow = true; link->receive("command",start); manager.process(600,control);
    CHECK(control.controlState() == runtime::ControlState::Disarmed && link->incoming.empty());
    for (int i=0; i<9; ++i) { link->receive("unrelated","{}"); }
    manager.process(650,control); CHECK(link->incoming.size() == 1);
    return true;
}

bool driverStyleConfiguration()
{
    using namespace runtime;
    using navigation::DriverStyle;
    Control control;
    CHECK(control.configuration().driverStyle == DriverStyle::DecideAction);
    ConfigurationRequest request{1, {35, 0.4F, 200, DriverStyle::SlowLeft}};
    CHECK(control.applyConfiguration(request) == ConfigurationResult::Applied);
    CHECK(control.applyConfiguration(request) == ConfigurationResult::Duplicate);
    control.setMqttConnected(true);
    CHECK(control.handleCommand(command(CommandType::Start,1),0).accepted);
    request = {2, {40, 0, 500, DriverStyle::SlowRight}};
    CHECK(control.applyConfiguration(request) == ConfigurationResult::DriverStyleRequiresDisarmed);
    CHECK(control.configurationRevision() == 1 && control.configuration().driveDuty == 0.4F);
    CHECK(control.configuration().stopDistanceCm == 35 && control.configuration().telemetryIntervalMs == 200);
    CHECK(!control.setDriverStyle(DriverStyle::SlowRight));
    CHECK(control.setDriverStyle(DriverStyle::SlowLeft));
    request.values.driverStyle = DriverStyle::SlowLeft;
    CHECK(control.applyConfiguration(request) == ConfigurationResult::Applied);
    CHECK(control.evaluate({60,60,70},0) == 0);
    CHECK(control.controlState() == ControlState::Armed); // duty zero does not permit a mode change
    request = {3, {40, 0, 500, DriverStyle::GradualSweep}};
    CHECK(control.applyConfiguration(request) == ConfigurationResult::DriverStyleRequiresDisarmed);
    CHECK(control.handleCommand(command(CommandType::Stop,2),50).accepted);
    CHECK(control.configuration().driverStyle == DriverStyle::SlowLeft); // rejection is not deferred
    CHECK(control.applyConfiguration(request) == ConfigurationResult::Applied);
    CHECK(control.controlState() == ControlState::Disarmed);
    request = {4, {35, 0.3F, 200}, false}; // legacy JSON must preserve the active style
    CHECK(control.applyConfiguration(request) == ConfigurationResult::Applied);
    CHECK(control.configuration().driverStyle == DriverStyle::GradualSweep);
    request.hasDriverStyle = true;
    request.values.driverStyle = DriverStyle::SlowRight;
    CHECK(control.applyConfiguration(request) == ConfigurationResult::StaleRevision);
    request.revision = 5;
    CHECK(control.applyConfiguration(request) == ConfigurationResult::Applied);
    request.revision = 6;
    request.values.driverStyle = static_cast<DriverStyle>(255);
    CHECK(control.applyConfiguration(request) == ConfigurationResult::InvalidDriverStyle);
    CHECK(!control.setDriverStyle(request.values.driverStyle));
    CHECK(control.configurationRevision() == 5 && control.configuration().driverStyle == DriverStyle::SlowRight);
    return true;
}

bool driverStyleProtocol()
{
    auto wifi = std::make_unique<Wifi>();
    auto mqtt = std::make_unique<Mqtt>(); auto* link = mqtt.get();
    communication::Manager manager{std::move(wifi),std::move(mqtt),Topics};
    runtime::Control control;
    manager.process(0,control);
    CHECK(link->field("config/state","driver_style") == "decide_action");
    const char* styles[]{"slow_left", "slow_right", "gradual_sweep", "decide_action"};
    unsigned revision = 0;
    for (const auto* style : styles)
    {
        char payload[256];
        std::snprintf(payload,sizeof(payload),R"({"schema_version":1,"revision":%u,"stop_distance_cm":35,"drive_duty":0.4,"telemetry_interval_ms":200,"driver_style":"%s"})",++revision,style);
        link->receive("config/set",payload,true); manager.process(50,control);
        CHECK(link->field("config/state","result") == "applied");
        CHECK(link->field("config/state","driver_style") == style);
        CHECK(link->field("command/state","driver_style") == style);
        CHECK(control.controlState() == runtime::ControlState::Disarmed);
    }
    const char* invalid[]{"\"SlowLeft\"", "\"unknown\"", "null", "7", "\"slow_left\",\"driver_style\":\"slow_right\""};
    for (const auto* value : invalid)
    {
        const std::string payload = std::string{R"({"schema_version":1,"revision":5,"stop_distance_cm":35,"drive_duty":0.4,"telemetry_interval_ms":200,"driver_style":)"} + value + "}";
        link->receive("config/set",payload.c_str()); manager.process(100,control);
        CHECK(link->field("config/state","result") == "rejected");
        CHECK(link->field("config/state","driver_style") == "decide_action");
        CHECK(control.configurationRevision() == 4);
    }
    CHECK(control.handleCommand(command(runtime::CommandType::Start,1),100).accepted);
    const char* change = R"({"schema_version":1,"revision":5,"stop_distance_cm":40,"drive_duty":0,"telemetry_interval_ms":200,"driver_style":"gradual_sweep"})";
    link->receive("config/set",change,true); manager.process(150,control);
    CHECK(link->field("config/state","error") == "driver_style_requires_disarmed");
    CHECK(control.configurationRevision() == 4 && control.configuration().driveDuty == 0.4F);
    link->lost = true; manager.process(200,control);
    link->receive("config/set",change,true); manager.process(250,control);
    CHECK(link->field("config/state","result") == "applied");
    CHECK(control.controlState() == runtime::ControlState::Disarmed);
    link->receive("config/set",R"({"schema_version":1,"revision":6,"stop_distance_cm":40,"drive_duty":0.4,"telemetry_interval_ms":200})");
    manager.process(300,control);
    CHECK(link->field("config/state","driver_style") == "gradual_sweep");
    // Long finite float representations and uptime must still serialize completely.
    const float large = std::numeric_limits<float>::max();
    communication::TelemetrySnapshot snapshot{{large,large,large},0.123456789F,-89.123456F,0.123456789F,0.987654321F};
    snapshot.adcRaw = {4095,4095,4096}; // out-of-range diagnostic input must become null
    control.forceDisarm(runtime::StateReason::HeartbeatTimeout);
    const int publications = link->publications;
    manager.publishTelemetry(0xffffffffU,snapshot,control);
    CHECK(link->publications == publications + 1);
    CHECK(link->field("telemetry","driver_style") == "gradual_sweep");
    const auto& payload = link->sent["telemetry"].payload;
    const char* end = nullptr;
    cJSON* parsed = cJSON_ParseWithOpts(payload.data(),&end,true);
    CHECK(parsed != nullptr && end != nullptr && *end == '\0');
    CHECK(cJSON_GetObjectItemCaseSensitive(parsed,"uptime_ms")->valuedouble == 4294967295.0);
    const auto* adc = cJSON_GetObjectItemCaseSensitive(parsed,"adc_raw");
    CHECK(cJSON_GetObjectItemCaseSensitive(adc,"left")->valuedouble == 4095);
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(adc,"right")));
    cJSON_Delete(parsed);
    return true;
}

bool adcSampleCoherence()
{
    driver::adc::Stub adc;
    driver::ir_sensor::Esp32s3 sensor{adc};
    CHECK(adc.lastRaw() == -1 && std::isnan(adc.readVoltage()));
    CHECK(adc.init() && adc.lastRaw() == -1);
    adc.simulateInput(1200);
    CHECK(std::isfinite(sensor.readDistance()) && adc.lastRaw() == 1200);
    adc.simulateInput(2400);
    CHECK(adc.lastRaw() == 1200); // diagnostics do not acquire another sample
    CHECK(std::isfinite(sensor.readDistance()) && adc.lastRaw() == 2400);
    adc.simulateInput(0);
    CHECK(std::isnan(sensor.readDistance()) && adc.lastRaw() == 0); // valid zero != read failure
    CHECK(adc.deinit() && adc.lastRaw() == -1);
    CHECK(adc.init() && adc.lastRaw() == -1);
    CHECK(adc.readRaw() == 0 && adc.lastRaw() == 0);
    return true;
}

bool backoff()
{
    auto wifi = std::make_unique<Wifi>(); auto* radio = wifi.get(); radio->connected = false;
    auto mqtt = std::make_unique<Mqtt>(); auto* link = mqtt.get(); link->connected = false;
    communication::Manager manager{std::move(wifi),std::move(mqtt),Topics};
    runtime::Control control;
    manager.process(0,control); CHECK(radio->attempts == 1 && link->attempts == 0);
    manager.process(999,control); CHECK(radio->attempts == 1);
    manager.process(1000,control); CHECK(radio->attempts == 2);
    manager.process(2999,control); CHECK(radio->attempts == 2);
    manager.process(3000,control); CHECK(radio->attempts == 3);
    radio->connected = true;
    manager.process(3050,control); CHECK(link->attempts == 1);
    manager.process(4049,control); CHECK(link->attempts == 1);
    manager.process(4050,control); CHECK(link->attempts == 2);
    return true;
}
} // namespace

bool runMqttIntegrationTests()
{
    return navigationAndLease() && outputFailures() && protocol()
        && driverStyleConfiguration() && driverStyleProtocol() && adcSampleCoherence() && backoff();
}
} // namespace test
