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
#include "system/logic/logic.h"
#include "driver/adc/stub.h"
#include "driver/gpio/stub.h"
#include "driver/mqtt/stub.h"
#include "driver/pwm/stub.h"
#include "driver/motor/mp6550.h"
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

struct Sensor final : driver::ir_sensor::Interface {
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
    std::unique_ptr<driver::ir_sensor::Interface> ir_sensor(driver::adc::Interface& a) noexcept override {
        for (unsigned pin = 0; pin < adcs.size(); ++pin) {
            if (adcs[pin] == &a) {
                auto value = std::make_unique<Sensor>(a, reads, static_cast<int>(pin));
                sensors[pin] = value.get(); return value;
            }
        }
        std::abort();
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
        check(pwms[5]->duty() == f, "forward PWM");
        check(pwms[6]->duty() == b, "backward PWM");
        check(sleep->read() == enabled, "sleep enable");
        if (pwms[9]->frequencyHz() != servoHz) {
            std::fprintf(stderr, "Servo expected %u Hz; actual %u Hz\n", servoHz, pwms[9]->frequencyHz());
        }
        check(pwms[9]->frequencyHz() == servoHz, "original servo mapping");
    }
};

void scenario(const char* name, unsigned steps,
              const std::function<void(Factory&)>& prepare,
              const std::function<void(Factory&, unsigned)>& inspect) {
    host::ticks = 0;
    Factory factory;
    app::logic::Logic logic(factory);
    factory.output(0, 0, false, 300);
    prepare(factory);
    std::atomic<bool> stop{false};
    unsigned tick = 0;
    host::afterTick = [&] {
        inspect(factory, tick);
        check(factory.reads.size() == 3, "exactly three reads per tick");
        check(factory.reads == std::vector<int>({2, 1, 4}), "SCRUM-16 forward/left/right read order");
        factory.reads.clear();
        if (++tick == steps) { stop.store(true); }
    };
    logic.run(stop);
    host::afterTick = {};
    check(!factory.sleep->read() && factory.pwms[5]->duty() == 0 && factory.pwms[6]->duty() == 0,
          "shutdown disables both outputs");
    std::printf("PASS: %s\n", name);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    scenario("boot cannot drive", 2, [](Factory& f) { f.distances(80, 100, 15); },
        [](Factory& f, unsigned) { f.output(0, 0, false, 300); });
    scenario("original route selection, finite check, braking and recovery", 7,
        [](Factory& f) { f.distances(80, 357, 15); f.start(); },
        [](Factory& f, unsigned tick) {
            switch (tick) {
                case 0: f.output(.5F, 0, true, 300); f.distances(78, 69, 26); break;
                case 1: f.output(.5F, 0, true, 500); f.distances(25, 20, 70); break;
                case 2: f.output(.5F, 0, true, 215); f.distances(20, 20, 20); break;
                case 3: f.output(1, 1, true, 215); f.distances(60, std::numeric_limits<float>::quiet_NaN(), 60); break;
                case 4: f.output(1, 1, true, 300); f.distances(-1, 70, 20); break;
                // Original SCRUM-16 checks finite values only, not > 0 for all sensors.
                case 5: f.output(.5F, 0, true, 300); f.distances(30, 30, 30); break;
                case 6: f.output(.5F, 0, true, 215); break;
            }
        });
    scenario("MQTT config, gradual sweep, obstacle and stop", 4,
        [](Factory& f) { f.distances(60, 60, 60); f.config("gradual_sweep", 30, .2F); f.start(); },
        [](Factory& f, unsigned tick) {
            if (tick == 0) { f.output(.2F, 0, true, 500); f.distances(60, 60, 15); }
            if (tick == 1) { f.output(1, 1, true, 488); f.distances(60, 60, 60); }
            if (tick == 2) { f.output(.2F, 0, true, 477); f.stop(); }
            if (tick == 3) { f.output(0, 0, false, 477); }
        });
    scenario("duty above 1 rejected atomically", 1,
        [](Factory& f) { f.distances(60, 80, 60); f.config("slow_left", 30, 1.01F); f.start(); },
        [](Factory& f, unsigned) { f.output(.5F, 0, true, 300); });
    scenario("invalid stop distance rejected atomically", 1,
        [](Factory& f) { f.distances(31, 32, 33); f.config("slow_left", 29); f.start(); },
        [](Factory& f, unsigned) { f.output(.5F, 0, true, 215); });
    scenario("MQTT stop distance and full duty range affect original Logic", 5,
        [](Factory& f) { f.distances(15, 40, 15); f.config("decide_action", 35, .8F); f.start(); },
        [](Factory& f, unsigned tick) {
            if (tick == 0) { f.output(.8F, 0, true, 300); f.distances(15, 34, 15); }
            if (tick == 1) { f.output(1, 1, true, 300); f.config("decide_action", 30, 1, 2); }
            if (tick == 2) { f.output(1, 0, true, 300); f.config("decide_action", 30, 0, 3); }
            if (tick >= 3) { f.output(0, 0, true, 300); }
        });
    scenario("SlowLeft uses configured duty", 1,
        [](Factory& f) { f.distances(60, 60, 60); f.config("slow_left", 30, .7F); f.start(); },
        [](Factory& f, unsigned) { f.output(.7F, 0, true, 500); });
    scenario("SlowRight uses configured duty and any-sensor stop", 2,
        [](Factory& f) { f.distances(60, 60, 60); f.config("slow_right", 40, 1); f.start(); },
        [](Factory& f, unsigned tick) {
            if (tick == 0) { f.output(1, 0, true, 215); f.distances(39, 60, 60); }
            else { f.output(1, 1, true, 215); }
        });
    scenario("heartbeat timeout", 61,
        [](Factory& f) { f.distances(60, 80, 60); f.start(); },
        [](Factory& f, unsigned tick) { f.output(tick < 60 ? .5F : 0, 0, tick < 60, 300); });
    scenario("MQTT disconnect requires a new start", 3,
        [](Factory& f) { f.distances(60, 80, 60); f.start(); },
        [](Factory& f, unsigned tick) {
            f.output(tick == 0 ? .5F : 0, 0, tick == 0, 300);
            if (tick == 0) { f.broker->disconnect(); }
        });
    scenario("ADC telemetry mapping and speed", 5,
        [](Factory& f) { f.distances(61, 81, 41); f.config("decide_action"); f.start(); },
        [](Factory& f, unsigned tick) {
            if (tick != 4) { return; }
            const auto& message = f.broker->lastPublishedMessage();
            check(std::strcmp(message.topic.data(), "cnb/vagrant/telemetry") == 0, "telemetry topic");
            cJSON* root = cJSON_Parse(message.payload.data());
            check(root != nullptr, "valid telemetry JSON");
            const auto* adc = cJSON_GetObjectItemCaseSensitive(root, "adc_raw");
            check(cJSON_GetObjectItemCaseSensitive(adc, "left")->valueint == 100, "left ADC mapping");
            check(cJSON_GetObjectItemCaseSensitive(adc, "center")->valueint == 200, "center ADC mapping");
            check(cJSON_GetObjectItemCaseSensitive(adc, "right")->valueint == 400, "right ADC mapping");
            cJSON_Delete(root);
        });
    std::puts("All experiment integration tests passed (no physical hardware or real MQTT broker used).");
}
