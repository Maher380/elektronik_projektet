#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <vector>
#include "driver/factory/interface.h"
#include "driver/distance_sensor/interface.h"
#include "driver/distance_sensor/stub.h"
#include "driver/adc/stub.h"
#include "driver/gpio/stub.h"
#include "driver/mqtt/stub.h"
#include "driver/pwm/stub.h"
#include "driver/motor/mp6550.h"
#include "driver/odometer/stub.h"
#include "driver/servo/vagrant.h"
#include "driver/serial/stub.h"
#include "driver/timer/stub.h"
#include "driver/wifi/stub.h"
#include "freertos/task.h"
#include "cJSON.h"

namespace host {
TickType_t ticks = 0;
std::function<void()> afterTick;
}

void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
}

struct Sensor final : driver::distance_sensor::Interface {
    driver::adc::Interface& adc;
    std::vector<int>& reads;
    int pin;
    float distance{60.0F};
    Sensor(driver::adc::Interface& a, std::vector<int>& r, int p) : adc(a), reads(r), pin(p) {}
    bool isInitialized() const noexcept override { return adc.isInitialized(); }
    float readDistance() noexcept override {
        reads.push_back(pin);
        (void)adc.readVoltage();
        return distance;
    }
};

struct Factory final : driver::factory::Interface {
    std::array<driver::adc::Stub*, 10> adcs{};
    std::array<driver::pwm::Stub*, 10> pwms{};
    std::array<Sensor*, 10> sensors{};
    driver::gpio::Stub* sleep{};
    driver::mqtt::Stub* broker{};
    driver::wifi::Stub* radio{};
    std::vector<int> reads;

    std::unique_ptr<driver::adc::Interface> adc(std::uint8_t pin) noexcept override {
        auto value = std::make_unique<driver::adc::Stub>();
        value->simulateInput(static_cast<std::uint16_t>(pin * 100));
        adcs[pin] = value.get();
        return value;
    }
    std::unique_ptr<driver::gpio::Interface> gpioInput(std::uint8_t) noexcept override {
        return std::make_unique<driver::gpio::Stub>();
    }
    std::unique_ptr<driver::gpio::Interface> gpioInputPullup(std::uint8_t pin) noexcept override {
        return gpioInput(pin);
    }
    std::unique_ptr<driver::odometer::Interface> odometer(driver::gpio::Interface&,
        const driver::odometer::Config& config) noexcept override {
        return std::make_unique<driver::odometer::Stub>(config);
    }
    std::unique_ptr<driver::gpio::Interface> gpioOutput(std::uint8_t) noexcept override {
        auto value = std::make_unique<driver::gpio::Stub>(); sleep = value.get(); return value;
    }
    std::unique_ptr<driver::pwm::Interface> pwm(std::uint8_t pin) noexcept override {
        return pwm(driver::pwm::Config{pin});
    }
    std::unique_ptr<driver::pwm::Interface> pwm(const driver::pwm::Config& config) noexcept override {
        auto value = std::make_unique<driver::pwm::Stub>(config);
        pwms[config.pin] = value.get(); return value;
    }
    std::unique_ptr<driver::servo::Interface> servo(driver::pwm::Interface& p) noexcept override {
        return std::make_unique<driver::servo::Vagrant>(p);
    }
    std::unique_ptr<driver::distance_sensor::Interface> ir_sensor(driver::adc::Interface& a) noexcept override {
        for (unsigned pin = 0; pin < adcs.size(); ++pin) {
            if (adcs[pin] == &a) {
                auto value = std::make_unique<Sensor>(a, reads, static_cast<int>(pin));
                sensors[pin] = value.get(); return value;
            }
        }
        std::abort();
    }
    std::unique_ptr<driver::distance_sensor::Interface> ultrasonic_sensor(driver::gpio::Interface&,
                                                                          driver::gpio::Interface&) noexcept override {
        return std::make_unique<driver::distance_sensor::Stub>();
    }
    std::unique_ptr<driver::motor::Interface> motor(driver::pwm::Interface& f, driver::pwm::Interface& b) noexcept override {
        return std::make_unique<driver::motor::MP6550>(f, b);
    }
    std::unique_ptr<driver::mqtt::Interface> mqtt(const driver::mqtt::Config& config) noexcept override {
        auto value = std::make_unique<driver::mqtt::Stub>(config); broker = value.get(); return value;
    }
    std::unique_ptr<driver::serial::Interface> serial(std::uint32_t) noexcept override {
        return std::make_unique<driver::serial::Stub>();
    }
    std::unique_ptr<driver::timer::Interface> timer(std::uint32_t) noexcept override {
        return std::make_unique<driver::timer::Stub>();
    }
    std::unique_ptr<driver::wifi::Interface> wifi(const char*, const char*) noexcept override {
        auto value = std::make_unique<driver::wifi::Stub>(); radio = value.get(); return value;
    }
    void distances(float left, float center, float right) {
        sensors[1]->distance = left; sensors[2]->distance = center; sensors[4]->distance = right;
    }
    void start(unsigned id = 1) {
        char payload[160];
        std::snprintf(payload, sizeof(payload),
            "{\"schema_version\":1,\"request_id\":%u,\"session_id\":\"test-session\",\"command\":\"start\"}", id);
        check(broker->simulateIncoming("cnb/vagrant/command", payload), "queue start");
    }
    void stop() {
        check(broker->simulateIncoming("cnb/vagrant/command",
            R"({"schema_version":1,"request_id":2,"session_id":"test-session","command":"stop"})"), "queue stop");
    }
    void config(const char* style, float stopDistance = 30, float duty = .5F, unsigned rev = 1) {
        char payload[250];
        std::snprintf(payload, sizeof(payload),
            "{\"schema_version\":1,\"revision\":%u,\"stop_distance_cm\":%.2f,\"drive_duty\":%.2f,\"telemetry_interval_ms\":200,\"driver_style\":\"%s\"}",
            rev, stopDistance, duty, style);
        check(broker->simulateIncoming("cnb/vagrant/config/set", payload,
            driver::mqtt::Qos::AtLeastOnce, true), "queue config");
    }
    void output(float f, float b, bool enabled, unsigned servoHz) {
        if (std::fabs(pwms[5]->duty() - f) >= 0.0001F
            || std::fabs(pwms[6]->duty() - b) >= 0.0001F || sleep->read() != enabled) {
            std::fprintf(stderr, "Expected PWM %.2f/%.2f sleep %d; actual %.2f/%.2f sleep %d at tick %u\n",
                static_cast<double>(f), static_cast<double>(b), enabled,
                static_cast<double>(pwms[5]->duty()), static_cast<double>(pwms[6]->duty()),
                sleep->read(), static_cast<unsigned>(host::ticks));
        }
        check(std::fabs(pwms[5]->duty() - f) < 0.0001F, "forward PWM");
        check(std::fabs(pwms[6]->duty() - b) < 0.0001F, "backward PWM");
        check(sleep->read() == enabled, "sleep enable");
        if (pwms[9]->frequencyHz() != servoHz) {
            std::fprintf(stderr, "Servo expected %u Hz; actual %u Hz\n", servoHz, pwms[9]->frequencyHz());
        }
        check(pwms[9]->frequencyHz() == servoHz, "original servo mapping");
    }
};


#include "../firmware/main/source/system/logic/vagrantLogic.cpp"

struct FaultPwm final : driver::pwm::Interface {
    driver::pwm::Stub backing;
    bool fail{false};
    unsigned writes{0};
    bool init() noexcept override { return backing.init(); }
    bool deinit() noexcept override { return backing.deinit(); }
    bool isInitialized() const noexcept override { return backing.isInitialized(); }
    bool setDuty(float value) noexcept override { ++writes; return !fail && backing.setDuty(value); }
    float duty() const noexcept override { return backing.duty(); }
    bool setFrequencyHz(std::uint32_t hz) noexcept override { return backing.setFrequencyHz(hz); }
    std::uint32_t frequencyHz() const noexcept override { return backing.frequencyHz(); }
};

int main() {
    using namespace app::runtime;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    {
        FaultPwm forward, backward;
        driver::motor::MP6550 motor{forward, backward};
        check(motor.init() && motor.setSpeed(.5F), "motor initializes and drives");
        const auto writes = forward.writes + backward.writes;
        check(motor.setSpeed(.5F) && forward.writes + backward.writes == writes, "unchanged motor duty is not rewritten");
        check(!motor.setSpeed(nan) && !motor.setSpeed(1.1F), "invalid duty rejected");
        forward.fail = true;
        check(!motor.setSpeed(.7F), "drive propagates PWM failure");
        check(!motor.stop(driver::motor::StopMode::Brake), "brake propagates PWM failure");
        check(backward.duty() == 1, "second PWM is attempted even when first fails");
        forward.fail = false;
        check(motor.stop(), "motor recovers at driver level");
        forward.deinit();
        check(!motor.stop(), "zero cached duty cannot hide an unavailable PWM driver");
    }
    check(Configuration{}.loopIntervalMs == 20U, "default sensor loop is 20 ms");
    struct Row { std::array<float, 3> distances; float angle; bool blocked; unsigned hz; bool valid{true}; };
    const Row cases[]{
        {{60,60,60},0,false,330}, {{60,35,15},-90,false,500}, {{15,35,60},90,false,215},
        {{20,10,20},0,true,330}, {{30,30,30},0,true,330}, {{31,35,10},-90,false,500},
        {{40,35,40},-90,false,500}, {{40,40,10},-28.9F,false,386}, {{10,40,40},28.9F,false,291},
        {{60,20,60},0,true,330},
        {{nan,nan,nan},0,true,330,false}, {{nan,20,15},0,true,330,false}, {{15,nan,20},0,true,330,false},
        {{-1,0,20},0,true,330,false}, {{31,39,32},90,false,215}, {{39,38,32},-90,false,500},
        {{70,60,15},-20.4F,false,375}, {{15,60,70},20.4F,false,299},
        {{40,40,30},0,false,330}, {{40,40,27},0,false,330},
        {{40,40,26},-1.7F,false,333}, {{26,40,40},1.7F,false,327},
        {{15,40,15},0,false,330}, {{20,40,18},0,false,330},
        {{40,40,0},-30,false,386}};
    for (const auto& row : cases) {
        NavigationState navigation{};
        const auto decision = decide(row.distances, Configuration{}, navigation, 20U);
        if (std::fabs(decision.angle - row.angle) >= 0.001F
            || decision.blocked != row.blocked || decision.valid != row.valid) {
            std::fprintf(stderr, "Decision %.1f/%.1f/%.1f: angle %.1f expected %.1f, blocked %d expected %d, valid %d expected %d\n",
                row.distances[0], row.distances[1], row.distances[2], decision.angle, row.angle,
                decision.blocked, row.blocked, decision.valid, row.valid);
        }
        check(std::fabs(decision.angle - row.angle) < 0.001F && decision.blocked == row.blocked
              && decision.valid == row.valid, "route, wall correction and sensor validity");
    }
    {
        Configuration tuned{};
        tuned.stopDistanceCm = 45.0F;
        tuned.reactionDistanceCm = 52.0F;
        NavigationState navigation{};
        check(decide({52, 52, 52}, tuned, navigation, 20U).angle == 0, "clear forward path");
        check(decide({51, 51, 51}, tuned, navigation, 20U).angle == -90, "equal sides choose a turn");
        check(decide({49, 50, 51}, tuned, navigation, 20U).angle == -90, "small difference keeps turn");
        check(decide({46, 50, 52}, tuned, navigation, 20U).angle == 90, "larger opening changes turn");
        check(decide({60, 50, 100}, tuned, navigation, 20U).angle == 90,
              "turn toward actual right clearance beyond telemetry cap");
        check(decide({100, 50, 60}, tuned, navigation, 20U).angle == -90,
              "turn toward actual left clearance beyond telemetry cap");
        check(std::fabs(decide({60, 48.5F, 100}, tuned, navigation, 20U).dutyScale - 0.825F) < 0.001F,
              "approach duty scales between reaction and stop distance");
        check(decide({60, 45, 100}, tuned, navigation, 20U).escapeRequired,
              "close front triggers escape even with open sides");
        check(decide({60, 46, 100}, tuned, navigation, 20U).blocked,
              "front stop remains latched near its threshold");
        check(!decide({60, 49, 100}, tuned, navigation, 20U).blocked,
              "front stop releases after measurable clearance");
        check(decide({45, 50, 45}, tuned, navigation, 20U).blocked, "no safe side stops early");
        navigation = {};
        check(std::fabs(decide({40, 52, 52}, tuned, navigation, 20U).angle - 3.4F) < 0.001F, "proportional wall correction");
        const float filteredAngle = decide({39, 52, 52}, tuned, navigation, 20U).angle;
        check(filteredAngle > 3.4F && filteredAngle < 5.1F, "side noise is smoothed");
        check(decide({60, 44, 60}, tuned, navigation, 20U).blocked, "front stop bypasses wall filtering");
        navigation = {};
        check(decide({60, 50, 70}, tuned, navigation, 20U).angle == 90, "choose obstacle route");
        check(decide({60, 52, 70}, tuned, navigation, 20U).angle == 90, "reaction threshold noise keeps turn");
        for (unsigned sample = 0; sample < 3; ++sample)
            check(decide({60, 60, 70}, tuned, navigation, 20U).angle == 90, "brief forward clearance keeps turn");
        check(decide({60, 60, 70}, tuned, navigation, 20U).angle == 0, "stable forward clearance releases turn");
        check(decide({60, 50, 70}, tuned, navigation, 1000U).angle == 90, "slow sampling enters avoidance");
        check(decide({60, 60, 70}, tuned, navigation, 1000U).angle == 90, "one slow sample cannot release turn");
        check(decide({60, 60, 70}, tuned, navigation, 1000U).angle == 0, "second slow clear sample releases turn");
    }
    {
        Factory approach; std::atomic<bool> done{false}; host::ticks = 0;
        unsigned tick = 0;
        auto configureDuty = [&](unsigned revision, float duty) {
            char payload[256];
            std::snprintf(payload, sizeof(payload),
                "{\"schema_version\":1,\"revision\":%u,\"stop_distance_cm\":45,\"reaction_distance_cm\":52,\"loop_interval_ms\":20,\"drive_duty\":%.2f,\"telemetry_interval_ms\":200}",
                revision, static_cast<double>(duty));
            check(approach.broker->simulateIncoming("cnb/vagrant/config/set", payload), "queue approach settings");
        };
        host::afterTick = [&] {
            switch (tick) {
            case 0:
                approach.distances(60, 48.5F, 70);
                configureDuty(1U, .44F);
                approach.start();
                break;
            case 1:
                approach.output(.39F, 0, true, 215);
                approach.distances(60, 51, 70);
                break;
            case 3:
                approach.output(.418F, 0, true, 215);
                configureDuty(2U, .2F);
                break;
            case 4:
                approach.output(.2F, 0, true, 215);
                configureDuty(3U, 0);
                break;
            case 5:
                approach.output(0, 0, false, 330);
                approach.stop();
                break;
            case 6: approach.output(0, 0, false, 330); done.store(true); break;
            }
            ++tick;
        };
        app::logic::VagrantLogic{approach}.run(done); host::afterTick = {};
        check(tick == 7, "approach floor preserves lower configured duty and stop");
    }
    // Exercise the real output sequence with fake hardware and time.
    {
        Factory navigation; std::atomic<bool> done{false}; host::ticks = 0;
        unsigned tick = 0;
        host::afterTick = [&] {
            switch (tick) {
            case 0:
                navigation.distances(60, 60, 60);
                check(navigation.broker->simulateIncoming("cnb/vagrant/config/set",
                    R"({"schema_version":1,"revision":1,"stop_distance_cm":30,"reaction_distance_cm":40,"loop_interval_ms":20,"drive_duty":0.5,"telemetry_interval_ms":200})"), "queue navigation settings");
                navigation.start();
                break;
            case 1: navigation.output(.5F, 0, true, 330); break;
            case 6: navigation.output(.5F, 0, true, 330); break;
            case 19: navigation.distances(20, 10, 20); break;
            case 21: navigation.output(1, 1, true, 330); break;
            case 29: navigation.output(0, 0, false, 330); break;
            case 39: navigation.output(0, .4F, true, 330); break;
            case 44: navigation.output(0, .7F, true, 330); break;
            case 49: navigation.output(0, 1, true, 330); break;
            case 50: navigation.distances(60, 20, 15); break;
            case 53: navigation.output(0, 1, true, 330); break;
            case 55: navigation.distances(60, 36, 15); break;
            case 59: navigation.output(0, 0, false, 330); break;
            case 69: navigation.output(0, 0, false, 330); break;
            case 73: navigation.output(.43F, 0, true, 500); break;
            case 74:
                navigation.distances(60, 10, 60);
                check(navigation.broker->simulateIncoming("cnb/vagrant/config/set",
                    R"({"schema_version":1,"revision":2,"stop_distance_cm":30,"reaction_distance_cm":40,"loop_interval_ms":20,"drive_duty":0,"telemetry_interval_ms":200})"), "queue zero drive duty");
                break;
            case 76: navigation.output(0, 0, false, 330); break;
            case 100: navigation.output(0, 0, false, 330); done.store(true); break;
            }
            ++tick;
        };
        app::logic::VagrantLogic{navigation}.run(done); host::afterTick = {};
        check(tick == 101, "navigation timing and zero-duty inhibition exercised");
    }
    {
        Factory narrow; std::atomic<bool> done{false}; host::ticks = 0;
        unsigned tick = 0;
        host::afterTick = [&] {
            switch (tick) {
            case 0: narrow.distances(20, 35, 20); narrow.start(); break;
            case 1: narrow.output(1, 1, true, 330); break;
            case 9: narrow.output(0, 0, false, 330); break;
            case 40: narrow.output(0, 0, false, 330); done.store(true); break;
            }
            ++tick;
        };
        app::logic::VagrantLogic{narrow}.run(done); host::afterTick = {};
        check(tick == 41, "narrow route stops without reversing");
    }
    for (unsigned cancellation = 0; cancellation < 4; ++cancellation) {
        Factory escape; std::atomic<bool> done{false}; host::ticks = 0;
        unsigned tick = 0;
        host::afterTick = [&] {
            switch (tick) {
            case 0: escape.distances(60, 10, 60); escape.start(); break;
            case 20:
                escape.output(0, .46F, true, 330);
                if (cancellation == 0) { escape.stop(); }
                else if (cancellation == 1) { escape.distances(nan, nan, nan); }
                else if (cancellation == 2) {
                    check(escape.broker->simulateIncoming("cnb/vagrant/config/set",
                        R"({"schema_version":1,"revision":1,"stop_distance_cm":30,"reaction_distance_cm":40,"loop_interval_ms":20,"drive_duty":0,"telemetry_interval_ms":200})"), "cancel reverse with zero duty");
                }
                else { escape.distances(60, 60, 60); }
                break;
            case 21:
                if (cancellation == 3) { escape.output(0, .52F, true, 330); }
                else { escape.output(0, 0, false, 330); }
                break;
            case 28: if (cancellation == 3) { escape.output(0, .94F, true, 330); } break;
            case 30: if (cancellation == 3) { escape.output(0, 1, true, 330); } break;
            case 39: escape.output(0, 0, false, 330); break;
            case 40: escape.output(0, 0, false, 330); done.store(true); break;
            }
            ++tick;
        };
        app::logic::VagrantLogic{escape}.run(done); host::afterTick = {};
        check(tick == 41, "active reverse cancellation exercised");
    }
    Control runtime{true};
    ConfigurationRequest request{}; request.revision = 1;
    request.values.stopDistanceCm = 10; request.values.reactionDistanceCm = 20;
    request.values.loopIntervalMs = 20; request.values.driveDuty = 1;
    check(runtime.applyConfiguration(request) == ConfigurationResult::Applied, "custom bounds and full duty");
    request.revision = 2; request.values.reactionDistanceCm = 10;
    check(runtime.applyConfiguration(request) == ConfigurationResult::ReactionDistanceOutOfRange, "reject cap equal to stop");
    request.values.reactionDistanceCm = 40; request.values.loopIntervalMs = 1001;
    check(runtime.applyConfiguration(request) == ConfigurationResult::LoopIntervalOutOfRange, "reject slow loop");
    check(runtime.configurationRevision() == 1, "rejected config is atomic");
    request.values.loopIntervalMs = 250;
    request.values.driverStyle = app::navigation::DriverStyle::SlowLeft;
    check(runtime.applyConfiguration(request) == ConfigurationResult::InvalidDriverStyle, "system mode cannot silently ignore legacy styles");

    Factory f; std::atomic<bool> stop{false}; host::ticks = 0;
    unsigned ticks = 0, telemetryCount = 0;
    auto command = [&](const char* payload, bool retained = false) {
        check(f.broker->simulateIncoming("cnb/vagrant/command", payload, driver::mqtt::Qos::AtLeastOnce, retained), "queue command");
    };
    auto config = [&](const char* payload) {
        check(f.broker->simulateIncoming("cnb/vagrant/config/set", payload), "queue configuration");
    };
    host::afterTick = [&] {
        switch (ticks) {
        case 0:
            f.output(0,0,false,330); f.distances(60,35,15);
            config(R"({"schema_version":1,"revision":1,"stop_distance_cm":30,"reaction_distance_cm":40,"loop_interval_ms":1000,"drive_duty":0.5,"telemetry_interval_ms":200})");
            f.start(1); break;
        case 1:
            f.output(.4125F,0,true,500);
            command(R"({"schema_version":1,"request_id":2,"session_id":"test","command":"servo","angle_deg":30})"); break;
        case 2:
            f.output(0,0,false,291);
            command(R"({"schema_version":1,"request_id":3,"session_id":"test","command":"start"})", true); break;
        case 3: f.output(0,0,false,291); f.distances(15,35,60); f.start(3); break;
        case 4: f.output(.4125F,0,true,215); break;
        case 5:
            config(R"({"schema_version":1,"revision":2,"stop_distance_cm":25,"reaction_distance_cm":50,"loop_interval_ms":20,"drive_duty":1,"telemetry_interval_ms":200})"); break;
        case 6: f.output(.79F,0,true,215); f.distances(20,10,20); break;
        case 8: f.output(1,1,true,330); break;
        case 9: f.distances(nan,nan,nan); break;
        case 10: f.output(0,0,false,330); break; // sensor failure cancels the escape immediately
        case 11:
            config(R"({"schema_version":1,"revision":3,"stop_distance_cm":25,"reaction_distance_cm":50,"loop_interval_ms":1000,"drive_duty":0.7,"telemetry_interval_ms":200})"); break;
        case 12:
            f.output(0,0,false,330);
            command(R"({"schema_version":1,"request_id":4,"session_id":"test","command":"stop"})"); break;
        case 13: f.output(0,0,false,330); break; // stop is not delayed by the 1000ms sensor interval
        case 14: f.distances(60,60,60); f.start(5); break;
        case 15: f.output(.7F,0,true,330); break;
        case 315: f.output(0,0,false,330); break; // heartbeat expired
        case 316: f.start(4); break; // replay must not rearm
        case 317: f.output(0,0,false,330); stop.store(true); break;
        }
        const auto& message = f.broker->lastPublishedMessage();
        if (std::strcmp(message.topic.data(), "cnb/vagrant/telemetry") == 0) {
            cJSON* json = cJSON_Parse(message.payload.data()); check(json != nullptr, "telemetry fits MQTT buffer");
            check(cJSON_IsTrue(cJSON_GetObjectItem(json, "system_test")), "system-test capability");
            check(cJSON_GetObjectItem(json, "decision_distance_cm") != nullptr, "decision distances are published");
            check(cJSON_GetObjectItem(json, "adc_raw") != nullptr, "raw ADC is published");
            if (ticks >= 20) check(cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(json, "distance_cm"), "left")), "fresh sensor reading is published");
            cJSON_Delete(json); ++telemetryCount;
        }
        ++ticks;
    };
    app::logic::VagrantLogic{f}.run(stop);
    host::afterTick = {};
    check(ticks == 318 && telemetryCount > 0, "complete main loop exercised");
    std::puts("PASS: route selection, sensor validity, reverse and cancellation, MQTT commands, telemetry and heartbeat expiry");
}
