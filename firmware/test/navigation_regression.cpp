// Reuse the host hardware fakes without executing the old steering expectations.
#define main previousIntegrationMain
#include "../../tests/system_test.cpp"
#undef main

void kickstartRespectsSafety()
{
    Factory factory;
    std::atomic<bool> stop{false};
    host::ticks = 0U;
    host::afterTick = [&]() {
        const auto now = host::ticks;
        if (now == 0U)
        {
            factory.broker->simulateIncoming("cnb/vagrant/config/set",
                R"({"schema_version":1,"revision":1,"drive_duty":0.44,"stop_distance_cm":45,"reaction_distance_cm":52,"loop_interval_ms":20,"telemetry_interval_ms":200})");
            factory.distances(70, 70, 90);
            factory.start();
        }
        const float forward = factory.pwms[5]->duty();
        const float backward = factory.pwms[6]->duty();
        if ((now >= 10U && now <= 30U) || (now >= 100U && now < 200U))
        { check(forward == 1.0F && backward == 0.0F, "forward starts with full duty for 100 ms"); }
        if (now == 30U) { factory.stop(); }
        if (now >= 40U && now <= 90U)
        { check(forward == 0.0F && backward == 0.0F, "MQTT Stop interrupts an active kick immediately"); }
        if (now == 90U) { factory.start(3U); }
        if (now >= 200U && now <= 210U)
        { check(std::abs(forward - .44F) < .0001F, "100 ms kick returns to configured duty without retriggering"); }
        if (now == 210U) { factory.distances(10, 10, 10); }
        if (now >= 220U)
        { check(!(forward > 0.0F && backward == 0.0F), "obstacle handling always overrides forward kickstart"); }
        if (now == 230U) { factory.distances(70, std::numeric_limits<float>::quiet_NaN(), 90); }
        if (now >= 240U)
        { check(forward == 0.0F && backward == 0.0F, "sensor fault disables automatic propulsion"); }
        if (now >= 260U) { stop.store(true); }
    };
    app::logic::VagrantLogic{factory}.run(stop);
    host::afterTick = {};
}

void recoveryUsesCurrentRoute(bool frontIsBest)
{
    Factory factory;
    std::atomic<bool> stop{false};
    host::ticks = 0U;
    bool reversed{false}, resumed{false};
    host::afterTick = [&]() {
        const auto now = host::ticks;
        if (now == 0U)
        {
            factory.broker->simulateIncoming("cnb/vagrant/config/set",
                R"({"schema_version":1,"revision":1,"drive_duty":0.44,"stop_distance_cm":45,"reaction_distance_cm":52,"loop_interval_ms":20,"telemetry_interval_ms":200})");
            factory.distances(44, 40, 20);
            factory.start();
        }
        const float forward = factory.pwms[5]->duty();
        const float backward = factory.pwms[6]->duty();
        if (backward > 0 && forward == 0) { reversed = true; }
        if (now == 350U)
        { factory.distances(20, frontIsBest ? 80 : 49, frontIsBest ? 20 : 80); }
        if (reversed && forward > 0 && backward == 0)
        {
            resumed = true;
            check(frontIsBest ? factory.pwms[9]->frequencyHz() == 330U
                              : factory.pwms[9]->frequencyHz() < 330U,
                "recovery follows current best route, not the original left exit");
        }
        if (now >= 1800U) { stop.store(true); }
    };
    app::logic::VagrantLogic{factory}.run(stop);
    host::afterTick = {};
    check(reversed && resumed, "reverse recovers into the newly open route");
}

void normalSteeringKeepsDriving()
{
    Factory factory;
    std::atomic<bool> stop{false};
    host::ticks = 0U;
    host::afterTick = [&]() {
        const auto now = host::ticks;
        if (now == 0U)
        {
            factory.broker->simulateIncoming("cnb/vagrant/config/set",
                R"({"schema_version":1,"revision":1,"drive_duty":0.44,"stop_distance_cm":45,"reaction_distance_cm":52,"loop_interval_ms":20,"telemetry_interval_ms":200})");
            factory.distances(70, 70, 90);
            factory.start();
        }
        if (now == 100U) { factory.distances(70, 50, 90); }
        if (now == 400U) { factory.distances(100, 50, 70); }
        if (now >= 120U)
        {
            if (std::abs(factory.pwms[5]->duty() - .44F) >= .0001F)
            { std::fprintf(stderr, "normal turn at %u: %.3f/%.3f\n", static_cast<unsigned>(now),
                static_cast<double>(factory.pwms[5]->duty()), static_cast<double>(factory.pwms[6]->duty())); }
            check(std::abs(factory.pwms[5]->duty() - .44F) < .0001F,
                "ordinary large steering changes do not interrupt propulsion");
            check(factory.pwms[6]->duty() == 0.0F, "ordinary turns never request electrical braking");
        }
        if (now >= 700U) { stop.store(true); }
    };
    app::logic::VagrantLogic{factory}.run(stop);
    host::afterTick = {};
}

void simpleReverse(bool leftExit)
{
    Factory factory;
    std::atomic<bool> stop{false};
    host::ticks = 0U;
    bool reversed{false}, resumed{false};
    std::uint32_t resumedAt{0U};
    host::afterTick = [&]() {
        const auto now = host::ticks;
        if (now == 0U)
        {
            factory.broker->simulateIncoming("cnb/vagrant/config/set",
                R"({"schema_version":1,"revision":1,"drive_duty":0.44,"stop_distance_cm":45,"reaction_distance_cm":52,"loop_interval_ms":20,"telemetry_interval_ms":200})");
            factory.distances(leftExit ? 44 : 20, 40, leftExit ? 20 : 44);
            factory.start();
        }
        const auto forward = factory.pwms[5]->duty();
        const auto backward = factory.pwms[6]->duty();
        const auto hz = factory.pwms[9]->frequencyHz();
        check(!(forward > 0 && backward > 0) || (forward == 1 && backward == 1),
            "both active outputs are only electrical braking");
        if (backward > 0 && forward == 0)
        {
            reversed = true;
            check(backward == 1.0F, "reverse starts at full duty");
            check(leftExit ? hz < 330U : hz > 330U, "reverse steers opposite the selected exit");
        }
        if (now == 350U) { factory.distances(leftExit ? 80 : 20, 49, leftExit ? 20 : 80); }
        if (reversed && forward > 0 && backward == 0)
        {
            if (!resumed) { resumedAt = now; }
            resumed = true;
            if (now - resumedAt >= 100U)
            { check(std::abs(forward - .44F) < .0001F, "forward duty stays configured after the single Start kick"); }
            check(leftExit ? hz > 330U : hz < 330U, "forward resumes toward the currently freer side");
        }
        if (now >= 1800U) { stop.store(true); }
    };
    app::logic::VagrantLogic{factory}.run(stop);
    host::afterTick = {};
    check(reversed && resumed, "short reverse returns to ordinary navigation without a recovery latch");
}

void fullSteeringOnRepeatedBlockage()
{
    for (float exitAngle : {-90.0F, 90.0F})
    {
        ReverseManeuver reverse;
        reverse.update(0U, true, exitAngle, false, false);
        check(reverse.reverseAngle() == -std::copysign(60.0F, exitAngle),
            "first reverse uses 60 degrees opposite the exit");
        reverse.update(80U, true, exitAngle, true, false);
        reverse.update(180U, true, exitAngle, true, false);
        reverse.update(480U, true, exitAngle, true, false);
        reverse.update(580U, true, exitAngle, true, true);
        reverse.update(590U, true, exitAngle, false, false);
        check(reverse.reverseAngle() == -std::copysign(90.0F, exitAngle),
            "continued blockage uses full 90 degree reverse steering");
        check(reverse.update(670U, true, exitAngle, false, false) == ReverseManeuver::Phase::Preparing,
            "full steering still requires preparation before reverse");
        check(reverse.update(770U, true, exitAngle, false, false) == ReverseManeuver::Phase::Preparing,
            "reverse waits until full steering is ready");
        reverse.reset();
        reverse.update(800U, true, exitAngle, false, false);
        check(std::abs(reverse.reverseAngle()) == 60.0F, "new Start resets escalation");

        reverse.update(880U, true, exitAngle, true, false);
        reverse.update(980U, true, exitAngle, true, false);
        reverse.update(1280U, false, exitAngle, true, false);
        reverse.update(1380U, false, exitAngle, true, true);
        reverse.update(1390U, false, exitAngle, true, true);
        reverse.update(1400U, true, exitAngle, false, false);
        check(std::abs(reverse.reverseAngle()) == 60.0F, "clear route resets escalation");
    }
}

void persistentFrontObstacle()
{
    Factory factory;
    std::atomic<bool> stop{false};
    host::ticks = 0U;
    unsigned pulses{0U};
    bool wasReversing{false};
    host::afterTick = [&]() {
        const auto now = host::ticks;
        if (now == 0U)
        {
            factory.broker->simulateIncoming("cnb/vagrant/config/set",
                R"({"schema_version":1,"revision":1,"drive_duty":0.44,"stop_distance_cm":45,"reaction_distance_cm":52,"loop_interval_ms":20,"telemetry_interval_ms":200})");
            factory.distances(20, 40, 44);
            factory.start();
        }
        const float forward = factory.pwms[5]->duty();
        const float backward = factory.pwms[6]->duty();
        check(!(forward > 0 && backward == 0), "front stop never permits forward propulsion");
        const bool reversing = backward > 0 && forward == 0;
        if (reversing && !wasReversing) { ++pulses; }
        wasReversing = reversing;
        if (now >= 2900U) { stop.store(true); }
    };
    app::logic::VagrantLogic{factory}.run(stop);
    host::afterTick = {};
    check(pulses >= 3U, "persistent front obstacle repeats short pulses instead of entering a permanent recovery latch");
}

void screenshotScenario()
{
    Factory factory;
    std::atomic<bool> stop{false};
    host::ticks = 0U;
    host::afterTick = [&]() {
        const auto now = host::ticks;
        if (now == 0U)
        {
            factory.broker->simulateIncoming("cnb/vagrant/config/set",
                R"({"schema_version":1,"revision":1,"drive_duty":0.46,"stop_distance_cm":40,"reaction_distance_cm":52,"loop_interval_ms":20,"telemetry_interval_ms":200})");
            factory.distances(94302.8F, 38.3F, 15.7F);
        }
        const float forward = factory.pwms[5]->duty();
        const float backward = factory.pwms[6]->duty();
        if (now <= 100U)
        { check(forward == 0 && backward == 0, "open left side never bypasses DISARMED authorization"); }
        if (now == 100U) { factory.start(); }
        if (now >= 110U)
        {
            check(backward == 0 && factory.pwms[9]->frequencyHz() > 330U,
                "screenshot distances produce left steering and no reversing once armed");
            check(std::abs(forward - (now < 210U ? 1.0F : .46F)) < .0001F,
                "one Start kick is followed by steady configured duty in screenshot scenario");
        }
        if (now >= 700U) { stop.store(true); }
    };
    app::logic::VagrantLogic{factory}.run(stop);
    host::afterTick = {};
}

int main()
{
    app::runtime::Configuration config{};
    config.stopDistanceCm = 45.0F;
    config.reactionDistanceCm = 52.0F;
    NavigationState state{};
    auto d = decide({60, 50, 100}, config, state, 20U);
    check(d.distances == std::array<float, 3U>{52, 50, 52}, "all values beyond reaction distance are capped");
    const auto cappedDecision = d;
    state = {};
    d = decide({6000, 50, 10000}, config, state, 20U);
    check(d.angle == cappedDecision.angle && d.blocked == cappedDecision.blocked,
        "raw distances beyond the horizon cannot affect route choice");
    state = {};
    d = decide({60, 60, 10000}, config, state, 20U);
    check(d.angle == 0 && !d.blocked, "equally clear capped directions prefer straight ahead");
    state = {};
    check(decide({20, 50, 80}, config, state, 20U).angle > 0, "freer right side gives right steering");
    state = {};
    check(decide({80, 50, 20}, config, state, 20U).angle < 0, "freer left side gives left steering");
    state = {};
    d = decide({20, 70, 70}, config, state, 20U);
    check(d.angle > 0 && d.angle <= 45 && !d.blocked, "bounded P correction steers away from the left wall");
    state = {};
    check(decide({70, 70, 20}, config, state, 20U).angle < 0, "small P correction steers away from right wall");
    check(decide({20, 70, 70}, config, state, 20U).angle > 0,
        "wall correction changes sign immediately when the nearer wall changes side");
    check(decide({70, 70, 70}, config, state, 20U).angle == 0,
        "clear road removes stale wall correction");
    state = {};
    const auto smallTurn = decide({52, 51.9F, 30}, config, state, 20U);
    const auto largeTurn = decide({52, 40, 30}, config, state, 20U);
    check(smallTurn.angle < 0 && std::abs(smallTurn.angle) < 2.0F
        && std::abs(largeTurn.angle) > std::abs(smallTurn.angle),
        "side steering scales with advantage instead of jumping to 30 degrees");
    for (float left : {10.0F, 20.0F, 40.0F, 51.0F, 80.0F})
    for (float front : {10.0F, 20.0F, 40.0F, 51.0F, 80.0F})
    for (float right : {10.0F, 20.0F, 40.0F, 51.0F, 80.0F})
    {
        const auto route = decide({left, front, right}, config, state, 20U);
        const auto& capped = route.distances;
        if (capped[0] > capped[1] && capped[0] > capped[2])
        { check(route.angle < 0, "uniquely longest left always selects left"); }
        if (capped[2] > capped[1] && capped[2] > capped[0])
        { check(route.angle > 0, "uniquely longest right always selects right"); }
        if (capped[1] >= capped[0] && capped[1] >= capped[2])
        { check(std::abs(route.angle) <= 45.0F, "front-best route only uses bounded wall corrections"); }
    }
    state = {};
    d = decide({50, 50, 50}, config, state, 20U);
    check(d.angle == 0 && !d.blocked, "no arbitrary side-opening threshold stops an equal-clearance corridor");
    state = {};
    d = decide({15, 50, 20}, config, state, 20U);
    check(!d.blocked && d.angle > 0 && d.angle <= 45, "narrow corridor uses bounded wall correction rather than reverse");
    state = {};
    check(decide({44, 45, 20}, config, state, 20U).blocked, "all blocked directions trigger reverse");
    check(decide({44, 46, 20}, config, state, 20U).blocked, "best-route hysteresis suppresses threshold noise");
    check(!decide({80, 38, 20}, config, state, 20U).blocked, "open side permits turning even below front stop threshold");
    config.stopDistanceCm = 40;
    state = {};
    d = decide({94302.8F, 38.3F, 15.7F}, config, state, 20U);
    check(d.distances[0] == 52 && d.angle < 0 && !d.blocked,
        "screenshot scenario commands left without reverse when authorized");
    d = decide({80, std::numeric_limits<float>::quiet_NaN(), 20}, config, state, 20U);
    check(!d.valid && d.blocked, "invalid sensors stop propulsion");

    normalSteeringKeepsDriving();
    kickstartRespectsSafety();
    simpleReverse(true);
    simpleReverse(false);
    recoveryUsesCurrentRoute(true);
    recoveryUsesCurrentRoute(false);
    persistentFrontObstacle();
    fullSteeringOnRepeatedBlockage();
    screenshotScenario();
    std::puts("PASS: capped route choice, proportional correction, kickstart, Stop and simple reverse");
}
