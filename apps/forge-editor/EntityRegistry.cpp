#include "EntityRegistry.hpp"

#include <algorithm>

namespace forge::editor {

// ─── Global instance ─────────────────────────────────────────────────────────

EntityRegistry& globalRegistry() noexcept {
    static EntityRegistry reg;
    return reg;
}

// ─── Registry methods ─────────────────────────────────────────────────────────

void EntityRegistry::add(EntityClassDef def) noexcept {
    defs_.push_back(std::move(def));
}

void EntityRegistry::clear() noexcept { defs_.clear(); }

const EntityClassDef* EntityRegistry::find(const std::string& cn) const noexcept {
    for (const auto& d : defs_)
        if (d.classname == cn) return &d;
    return nullptr;
}

std::vector<const EntityClassDef*>
EntityRegistry::inCategory(const std::string& cat) const noexcept {
    std::vector<const EntityClassDef*> out;
    for (const auto& d : defs_)
        if (d.category == cat) out.push_back(&d);
    return out;
}

std::vector<std::string> EntityRegistry::categories() const noexcept {
    std::vector<std::string> cats;
    for (const auto& d : defs_) {
        if (std::ranges::find(cats, d.category) == cats.end())
            cats.push_back(d.category);
    }
    std::ranges::sort(cats);
    return cats;
}

// ─── Built-in definitions ─────────────────────────────────────────────────────

void EntityRegistry::loadBuiltins() noexcept {
    defs_.clear();

    // ── Spawns ────────────────────────────────────────────────────────────────
    {
        EntityClassDef e;
        e.classname   = "info_player_start";
        e.category    = "Spawns";
        e.description = "Default player spawn point. Exactly one per map.";
        e.editorColor = { 0.2f, 0.9f, 0.2f };
        e.properties  = {
            { "angle", PropType::Float, "0",
              "Direction the player faces on spawn (degrees, 0=+X).", {} },
        };
        defs_.push_back(std::move(e));
    }
    {
        EntityClassDef e;
        e.classname   = "info_player_deathmatch";
        e.category    = "Spawns";
        e.description = "Deathmatch spawn point.";
        e.editorColor = { 0.2f, 0.8f, 0.4f };
        e.properties  = {
            { "angle", PropType::Float, "0", "Facing direction.", {} },
        };
        defs_.push_back(std::move(e));
    }

    // ── Lights ────────────────────────────────────────────────────────────────
    {
        EntityClassDef e;
        e.classname   = "light";
        e.category    = "Lights";
        e.description = "Omnidirectional point light.";
        e.editorColor = { 1.f, 0.9f, 0.3f };
        e.properties  = {
            { "light",   PropType::Float,  "300",
                            "Brightness (game units squared).", {} },
            { "_color",  PropType::Color,  "1.0 0.95 0.8",
                            "Light colour as normalised RGB.", {} },
            { "radius",  PropType::Float,  "512",
                            "Hard attenuation radius (game units).", {} },
            { "target",  PropType::String, "",
                            "Name of entity this light aims at (spot light).", {} },
        };
        defs_.push_back(std::move(e));
    }
    {
        EntityClassDef e;
        e.classname   = "light_spot";
        e.category    = "Lights";
        e.description = "Cone (spot) light.";
        e.editorColor = { 1.f, 0.7f, 0.2f };
        e.properties  = {
            { "light",   PropType::Float,  "300", "Brightness.", {} },
            { "_color",  PropType::Color,  "1.0 0.95 0.8", "Colour.", {} },
            { "radius",  PropType::Float,  "512", "Attenuation radius.", {} },
            { "angle",   PropType::Float,  "40",  "Cone half-angle (degrees).", {} },
            { "wait",    PropType::Float,  "0.5", "Penumbra falloff exponent.", {} },
        };
        defs_.push_back(std::move(e));
    }

    // ── Triggers ──────────────────────────────────────────────────────────────
    {
        EntityClassDef e;
        e.classname   = "trigger_once";
        e.kind        = EntityKind::Brush;
        e.category    = "Triggers";
        e.description = "Fires its targets once when the player enters.";
        e.editorColor = { 0.8f, 0.3f, 0.3f };
        e.properties  = {
            { "target",  PropType::String, "", "Targetname to activate.", {} },
            { "delay",   PropType::Float,  "0", "Delay before firing (seconds).", {} },
            { "message", PropType::String, "", "HUD message displayed on entry.", {} },
        };
        defs_.push_back(std::move(e));
    }
    {
        EntityClassDef e;
        e.classname   = "trigger_multiple";
        e.kind        = EntityKind::Brush;
        e.category    = "Triggers";
        e.description = "Fires repeatedly whenever the player is inside.";
        e.editorColor = { 0.9f, 0.4f, 0.2f };
        e.properties  = {
            { "target",  PropType::String, "", "Targetname to activate.", {} },
            { "wait",    PropType::Float,  "0.2", "Minimum re-fire interval.", {} },
        };
        defs_.push_back(std::move(e));
    }

    // ── Func entities ─────────────────────────────────────────────────────────
    {
        EntityClassDef e;
        e.classname   = "func_door";
        e.kind        = EntityKind::Brush;
        e.category    = "Functional";
        e.description = "A sliding door triggered by player proximity or target.";
        e.editorColor = { 0.4f, 0.6f, 0.9f };
        e.properties  = {
            { "angle",   PropType::Float,  "0",   "Opening direction.", {} },
            { "speed",   PropType::Float,  "100", "Movement speed (units/sec).", {} },
            { "wait",    PropType::Float,  "3",   "Seconds open before closing.", {} },
            { "lip",     PropType::Float,  "8",   "Lip remaining after opening.", {} },
            { "targetname", PropType::String, "", "Name for triggering.", {} },
        };
        defs_.push_back(std::move(e));
    }
    {
        EntityClassDef e;
        e.classname   = "func_plat";
        e.kind        = EntityKind::Brush;
        e.category    = "Functional";
        e.description = "Elevator platform.";
        e.editorColor = { 0.5f, 0.4f, 0.8f };
        e.properties  = {
            { "height", PropType::Float, "0",  "Travel height (0 = auto).", {} },
            { "speed",  PropType::Float, "150","Movement speed.", {} },
            { "wait",   PropType::Float, "1",  "Seconds at top before returning.", {} },
            { "targetname", PropType::String, "", "Name for triggering.", {} },
        };
        defs_.push_back(std::move(e));
    }
    {
        EntityClassDef e;
        e.classname   = "func_rotating";
        e.kind        = EntityKind::Brush;
        e.category    = "Functional";
        e.description = "Continuously rotating brush entity.";
        e.editorColor = { 0.3f, 0.7f, 0.7f };
        e.properties  = {
            { "speed",  PropType::Float, "100", "Rotation speed (degrees/sec).", {} },
            { "targetname", PropType::String, "", "Name for triggering/toggling.", {} },
        };
        defs_.push_back(std::move(e));
    }

    // ── Sounds ────────────────────────────────────────────────────────────────
    {
        EntityClassDef e;
        e.classname   = "target_speaker";
        e.category    = "Sound";
        e.description = "Plays a sound at its origin.";
        e.editorColor = { 0.5f, 0.8f, 0.9f };
        e.properties  = {
            { "noise",  PropType::String, "", "Sound file path.", {} },
            { "volume", PropType::Float,  "1", "Playback volume (0–1).", {} },
        };
        defs_.push_back(std::move(e));
    }

    // ── Items / pickups ───────────────────────────────────────────────────────
    {
        EntityClassDef e;
        e.classname   = "item_health";
        e.category    = "Items";
        e.description = "Health pickup.";
        e.editorColor = { 0.9f, 0.2f, 0.2f };
        e.properties  = {
            { "amount", PropType::Int, "25", "Health granted.", {} },
        };
        defs_.push_back(std::move(e));
    }
    {
        EntityClassDef e;
        e.classname   = "item_armor";
        e.category    = "Items";
        e.description = "Armour pickup.";
        e.editorColor = { 0.6f, 0.6f, 0.9f };
        e.properties  = {
            { "amount", PropType::Int, "50", "Armour granted.", {} },
        };
        defs_.push_back(std::move(e));
    }

    // ── Misc ──────────────────────────────────────────────────────────────────
    {
        EntityClassDef e;
        e.classname   = "info_null";
        e.category    = "Misc";
        e.description = "Generic named target / anchor point.";
        e.editorColor = { 0.5f, 0.5f, 0.5f };
        e.properties  = {
            { "targetname", PropType::String, "", "Name used by triggers.", {} },
        };
        defs_.push_back(std::move(e));
    }
    {
        EntityClassDef e;
        e.classname   = "misc_model";
        e.category    = "Misc";
        e.description = "Places a static mesh asset in the world.";
        e.editorColor = { 0.7f, 0.5f, 0.9f };
        e.properties  = {
            { "model",  PropType::String, "", "Relative path to GLTF/OBJ asset.", {} },
            { "skin",   PropType::Int,    "0", "Skin/material variant index.", {} },
            { "angle",  PropType::Float,  "0", "Yaw rotation (degrees).", {} },
        };
        defs_.push_back(std::move(e));
    }
}

} // namespace forge::editor
