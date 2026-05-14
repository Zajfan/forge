#include <catch2/catch_test_macros.hpp>

#include <forge/runtime.hpp>
#include <forge/scene.hpp>
#include <forge/geo/Primitives.hpp>

namespace {

forge::scene::Scene makeTriggerScene() {
    using namespace forge;

    scene::Scene s;

    scene::PointEntity spawn;
    spawn.name = "spawn";
    spawn.classname = "info_player_start";
    spawn.transform = scene::Transform::fromTranslation({-200.0, 16.0, 0.0});
    spawn.set<std::string>("team", "blue");
    s.addEntity(std::move(spawn));

    scene::BrushEntity trigger;
    trigger.name = "t_once_per_entity";
    trigger.classname = "trigger_multiple";
    trigger.solid = false;
    trigger.brushes = { geo::makeBox({-32.0, 0.0, -32.0}, {32.0, 72.0, 32.0}) };
    trigger.set<float>("wait", 0.0f);
    trigger.set<bool>("once_per_entity", true);
    trigger.set<std::string>("filter_team", "blue");
    s.addEntity(std::move(trigger));

    return s;
}

const forge::runtime::GameRuntime::TriggerDebugInfo* firstTrigger(
    const std::vector<forge::runtime::GameRuntime::TriggerDebugInfo>& triggers) {
    if (triggers.empty()) return nullptr;
    return &triggers.front();
}

} // namespace

TEST_CASE("GameRuntime trigger enter/exit honors once_per_entity", "[runtime][trigger]") {
    forge::runtime::GameRuntime runtime;
    forge::scene::Scene scene = makeTriggerScene();

    REQUIRE(runtime.init(scene, {-200.f, 16.f, 0.f}));
    REQUIRE(runtime.playerTeam() == "blue");

    forge::gfx::InputState input{};

    runtime.update(1.0f / 60.0f, input);
    {
        const auto triggers = runtime.triggerDebugSnapshot();
        REQUIRE(triggers.size() == 1);
        const auto* t = firstTrigger(triggers);
        REQUIRE(t != nullptr);
        CHECK(t->inside == false);
        CHECK(t->teamFilterPass == true);
        CHECK(t->enterFireCount == 0);
        CHECK(t->exitFireCount == 0);
    }

    runtime.teleportPlayer({0.f, 16.f, 0.f});
    runtime.update(1.0f / 60.0f, input);
    {
        const auto triggers = runtime.triggerDebugSnapshot();
        const auto* t = firstTrigger(triggers);
        REQUIRE(t != nullptr);
        CHECK(t->inside == true);
        CHECK(t->firedForPlayer == true);
        CHECK(t->enterFireCount == 1);
        CHECK(t->exitFireCount == 0);
    }

    runtime.teleportPlayer({200.f, 16.f, 0.f});
    runtime.update(1.0f / 60.0f, input);
    {
        const auto triggers = runtime.triggerDebugSnapshot();
        const auto* t = firstTrigger(triggers);
        REQUIRE(t != nullptr);
        CHECK(t->inside == false);
        CHECK(t->enterFireCount == 1);
        CHECK(t->exitFireCount == 1);
    }

    runtime.teleportPlayer({0.f, 16.f, 0.f});
    runtime.update(1.0f / 60.0f, input);
    {
        const auto triggers = runtime.triggerDebugSnapshot();
        const auto* t = firstTrigger(triggers);
        REQUIRE(t != nullptr);
        CHECK(t->inside == true);
        CHECK(t->firedForPlayer == true);
        CHECK(t->enterFireCount == 1);
        CHECK(t->exitFireCount == 1);
    }

    runtime.shutdown();
}
