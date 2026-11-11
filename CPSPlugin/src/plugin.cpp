#include <cps_api.h>
#include <cps_log.hpp>
#include <common.hpp>
#include <ue/tables.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
const cps_api* g_api = nullptr;
cps_client g_client = 0;
std::mutex g_state_mutex;
std::mutex g_status_mutex;
std::filesystem::path g_settings_path;

struct Settings {
    bool base_attributes = false;
    float max_hp = 3000.0f;
    float max_shield = 1000.0f;
    float shield_damage_reduction = 20.0f;
    bool shield_regen = false;
    float shield_regen_normal = 120.0f;
    float shield_regen_combat = 30.0f;
    bool attack_speed = false;
    float attack_speed_value = 1.3f;
    bool gauges = false;
    float beta_capacity = 1500.0f;
    float burst_capacity = 2000.0f;
    float tachy_capacity = 18000.0f;
    bool ammunition = false;
    float ammunition_stack = 999.0f;
    bool ammunition_100x = false;
    bool consumables = false;
    float consumable_stack = 99.0f;
    bool passive_hp_regen = false;
    float passive_hp_regen_value = 20.0f;
    bool hp_drain = false;
    float hp_drain_rate_percent = 3.0f;
    float hp_drain_fixed = 30.0f;
    bool fishing_power = false;
    float fishing_power_value = 50.0f;
    bool slower_gauge_gain = false;
    bool lower_gauge_capacity = false;
    bool lower_skill_cooldowns = false;
    bool blaster_damage = false;
    bool forgiving_just = false;
    float forgiving_just_multiplier = 1.5f;
    bool extra_air_dodge = false;
    bool dash_cooldown = false;
    bool tachy_reduction = false;
    bool stronger_gear = false;
    float stronger_gear_multiplier = 2.0f;
    bool tumbler_heal = false;
    float tumbler_heal_value = 60.0f;
    bool shop_prices = false;
    float shop_price_percent = 50.0f;
    bool drop_quantities = false;
    float drop_quantity_percent = 100.0f;
    bool sp_exp = false;
    float sp_exp_percent = 50.0f;
    bool upgrade_costs = false;
    float upgrade_cost_percent = 50.0f;
    bool world_fishing = false;
    float fish_stamina_percent = 50.0f;
    float fish_fight_time_percent = 200.0f;
};

enum class StatusKind { Waiting, Searching, Ready, Applied, Restored, Ambiguous, Error, SaveError };
struct Status {
    StatusKind kind = StatusKind::Waiting;
    int changed = 0;
    int skipped = 0;
};

Settings g_settings;
Status g_status;
uint64_t g_revision = 1;
uint64_t g_processed_revision = 0;
uint64_t g_saved_revision = 1;
float g_scan_elapsed = 0.0f;
bool g_engine_ready = false;
bool g_save_error = false;
std::vector<std::string> g_logged_issues;

enum class NumberKind { Integer, Float };
struct Number {
    NumberKind kind = NumberKind::Integer;
    int32_t integer = 0;
    float real = 0.0f;

    static Number from_int(int32_t value) {
        Number out;
        out.kind = NumberKind::Integer;
        out.integer = value;
        return out;
    }
    static Number from_float(float value) {
        Number out;
        out.kind = NumberKind::Float;
        out.real = value;
        return out;
    }
};

struct Target {
    ue::UObject* table = nullptr;
    std::string row;
    std::string field;
    Number value;
};

struct TableView;

struct Snapshot {
    ue::Weak table;
    std::string row;
    std::string field;
    Number original;
    Number last_written;
    bool conflict_logged = false;
};
std::vector<Snapshot> g_snapshots; // Game-thread only.

const char* tr(const char* id, const char* english) {
    if (!g_api || !g_api->translate)
        return english;
    const char* value = g_api->translate(g_client, id);
    return value == id ? english : value;
}

constexpr const char* mark(const char* id, const char* english) {
    return (void)id, english;
}

void log(int level, const std::string& text) {
    if (g_api && g_api->log)
        g_api->log(g_client, level, text.c_str());
}

void log_once(const std::string& key, const std::string& text, int level = CPS_LOG_WARNING) {
    if (std::find(g_logged_issues.begin(), g_logged_issues.end(), key) == g_logged_issues.end()) {
        g_logged_issues.push_back(key);
        log(level, text);
    }
}

void set_status(Status status) {
    std::lock_guard lock(g_status_mutex);
    g_status = status;
}

Status get_status() {
    std::lock_guard lock(g_status_mutex);
    return g_status;
}

float clamp_float(const cps::Json& json, const char* key, float fallback, float minimum, float maximum) {
    try {
        if (json.contains(key) && json[key].is_number())
            return std::clamp(json[key].get<float>(), minimum, maximum);
    } catch (...) {
    }
    return fallback;
}

bool read_bool(const cps::Json& json, const char* key, bool fallback) {
    try {
        if (json.contains(key) && json[key].is_boolean())
            return json[key].get<bool>();
    } catch (...) {
    }
    return fallback;
}

cps::Json encode_settings(const Settings& s) {
    return cps::Json{{"version", 1},
                     {"base_attributes", s.base_attributes},
                     {"max_hp", s.max_hp},
                     {"max_shield", s.max_shield},
                     {"shield_damage_reduction", s.shield_damage_reduction},
                     {"shield_regen", s.shield_regen},
                     {"shield_regen_normal", s.shield_regen_normal},
                     {"shield_regen_combat", s.shield_regen_combat},
                     {"attack_speed", s.attack_speed},
                     {"attack_speed_value", s.attack_speed_value},
                     {"gauges", s.gauges},
                     {"beta_capacity", s.beta_capacity},
                     {"burst_capacity", s.burst_capacity},
                     {"tachy_capacity", s.tachy_capacity},
                     {"ammunition", s.ammunition},
                     {"ammunition_stack", s.ammunition_stack},
                     {"ammunition_100x", s.ammunition_100x},
                     {"consumables", s.consumables},
                     {"consumable_stack", s.consumable_stack},
                     {"passive_hp_regen", s.passive_hp_regen},
                     {"passive_hp_regen_value", s.passive_hp_regen_value},
                     {"hp_drain", s.hp_drain},
                     {"hp_drain_rate_percent", s.hp_drain_rate_percent},
                     {"hp_drain_fixed", s.hp_drain_fixed},
                     {"fishing_power", s.fishing_power},
                     {"fishing_power_value", s.fishing_power_value},
                     {"slower_gauge_gain", s.slower_gauge_gain},
                     {"lower_gauge_capacity", s.lower_gauge_capacity},
                     {"lower_skill_cooldowns", s.lower_skill_cooldowns},
                     {"blaster_damage", s.blaster_damage},
                     {"forgiving_just", s.forgiving_just},
                     {"forgiving_just_multiplier", s.forgiving_just_multiplier},
                     {"extra_air_dodge", s.extra_air_dodge},
                     {"dash_cooldown", s.dash_cooldown},
                     {"tachy_reduction", s.tachy_reduction},
                     {"stronger_gear", s.stronger_gear},
                     {"stronger_gear_multiplier", s.stronger_gear_multiplier},
                     {"tumbler_heal", s.tumbler_heal},
                     {"tumbler_heal_value", s.tumbler_heal_value},
                     {"shop_prices", s.shop_prices},
                     {"shop_price_percent", s.shop_price_percent},
                     {"drop_quantities", s.drop_quantities},
                     {"drop_quantity_percent", s.drop_quantity_percent},
                     {"sp_exp", s.sp_exp},
                     {"sp_exp_percent", s.sp_exp_percent},
                     {"upgrade_costs", s.upgrade_costs},
                     {"upgrade_cost_percent", s.upgrade_cost_percent},
                     {"world_fishing", s.world_fishing},
                     {"fish_stamina_percent", s.fish_stamina_percent},
                     {"fish_fight_time_percent", s.fish_fight_time_percent}};
}

void load_settings() {
    if (!g_api->config_path)
        return;
    char path[4096]{};
    if (!g_api->config_path(g_client, "settings.json", path, sizeof(path))) {
        log(CPS_LOG_WARNING, "Could not resolve settings.json; defaults will be used.");
        return;
    }
    g_settings_path = cps::utf8_path(path);
    auto raw = cps::read_file(g_settings_path);
    if (raw.empty())
        return;
    try {
        auto json = cps::Json::parse(raw);
        g_settings.base_attributes = read_bool(json, "base_attributes", false);
        g_settings.max_hp = clamp_float(json, "max_hp", 3000.0f, 500.0f, 10000.0f);
        g_settings.max_shield = clamp_float(json, "max_shield", 1000.0f, 0.0f, 5000.0f);
        g_settings.shield_damage_reduction = clamp_float(json, "shield_damage_reduction", 20.0f, 0.0f, 80.0f);
        g_settings.shield_regen = read_bool(json, "shield_regen", false);
        g_settings.shield_regen_normal = clamp_float(json, "shield_regen_normal", 120.0f, 0.0f, 300.0f);
        g_settings.shield_regen_combat = clamp_float(json, "shield_regen_combat", 30.0f, 0.0f, 100.0f);
        g_settings.attack_speed = read_bool(json, "attack_speed", false);
        g_settings.attack_speed_value = clamp_float(json, "attack_speed_value", 1.3f, 0.5f, 2.5f);
        g_settings.gauges = read_bool(json, "gauges", false);
        g_settings.beta_capacity = clamp_float(json, "beta_capacity", 1500.0f, 0.0f, 5000.0f);
        g_settings.burst_capacity = clamp_float(json, "burst_capacity", 2000.0f, 0.0f, 5000.0f);
        g_settings.tachy_capacity = clamp_float(json, "tachy_capacity", 18000.0f, 0.0f, 36000.0f);
        g_settings.ammunition = read_bool(json, "ammunition", false);
        g_settings.ammunition_stack = clamp_float(json, "ammunition_stack", 999.0f, 1.0f, 9999.0f);
        g_settings.ammunition_100x = read_bool(json, "ammunition_100x", false);
        g_settings.consumables = read_bool(json, "consumables", false);
        g_settings.consumable_stack = clamp_float(json, "consumable_stack", 99.0f, 1.0f, 999.0f);
        g_settings.passive_hp_regen = read_bool(json, "passive_hp_regen", false);
        g_settings.passive_hp_regen_value = clamp_float(json, "passive_hp_regen_value", 20.0f, 0.0f, 200.0f);
        g_settings.hp_drain = read_bool(json, "hp_drain", false);
        g_settings.hp_drain_rate_percent = clamp_float(json, "hp_drain_rate_percent", 3.0f, 0.0f, 25.0f);
        g_settings.hp_drain_fixed = clamp_float(json, "hp_drain_fixed", 30.0f, 0.0f, 500.0f);
        g_settings.fishing_power = read_bool(json, "fishing_power", false);
        g_settings.fishing_power_value = clamp_float(json, "fishing_power_value", 50.0f, 0.0f, 500.0f);
        g_settings.slower_gauge_gain = read_bool(json, "slower_gauge_gain", false);
        g_settings.lower_gauge_capacity = read_bool(json, "lower_gauge_capacity", false);
        g_settings.lower_skill_cooldowns = read_bool(json, "lower_skill_cooldowns", false);
        g_settings.blaster_damage = read_bool(json, "blaster_damage", false);
        g_settings.forgiving_just = read_bool(json, "forgiving_just", false);
        g_settings.forgiving_just_multiplier = clamp_float(json, "forgiving_just_multiplier", 1.5f, 1.0f, 3.0f);
        g_settings.extra_air_dodge = read_bool(json, "extra_air_dodge", false);
        g_settings.dash_cooldown = read_bool(json, "dash_cooldown", false);
        g_settings.tachy_reduction = read_bool(json, "tachy_reduction", false);
        g_settings.stronger_gear = read_bool(json, "stronger_gear", false);
        g_settings.stronger_gear_multiplier = clamp_float(json, "stronger_gear_multiplier", 2.0f, 1.0f, 5.0f);
        g_settings.tumbler_heal = read_bool(json, "tumbler_heal", false);
        g_settings.tumbler_heal_value = clamp_float(json, "tumbler_heal_value", 60.0f, 10.0f, 100.0f);
        g_settings.shop_prices = read_bool(json, "shop_prices", false);
        g_settings.shop_price_percent = clamp_float(json, "shop_price_percent", 50.0f, 0.0f, 300.0f);
        g_settings.drop_quantities = read_bool(json, "drop_quantities", false);
        g_settings.drop_quantity_percent = clamp_float(json, "drop_quantity_percent", 100.0f, 0.0f, 1000.0f);
        g_settings.sp_exp = read_bool(json, "sp_exp", false);
        g_settings.sp_exp_percent = clamp_float(json, "sp_exp_percent", 50.0f, 0.0f, 300.0f);
        g_settings.upgrade_costs = read_bool(json, "upgrade_costs", false);
        g_settings.upgrade_cost_percent = clamp_float(json, "upgrade_cost_percent", 50.0f, 0.0f, 300.0f);
        g_settings.world_fishing = read_bool(json, "world_fishing", false);
        g_settings.fish_stamina_percent = clamp_float(json, "fish_stamina_percent", 50.0f, 0.0f, 300.0f);
        g_settings.fish_fight_time_percent = clamp_float(json, "fish_fight_time_percent", 200.0f, 0.0f, 1000.0f);
    } catch (const std::exception& error) {
        log(CPS_LOG_WARNING, std::string("Settings file could not be read; defaults will be used: ") + error.what());
    }
}

void save_settings_if_needed(uint64_t revision, const Settings& settings) {
    if (revision == g_saved_revision || g_settings_path.empty())
        return;
    try {
        cps::atomic_write(g_settings_path, encode_settings(settings));
        g_saved_revision = revision;
        g_save_error = false;
    } catch (const std::exception& error) {
        g_save_error = true;
        log(CPS_LOG_ERROR, std::string("Could not save plugin settings: ") + error.what());
        set_status({StatusKind::SaveError, 0, 0});
    }
}

std::vector<Target> make_targets(const Settings& settings, std::vector<TableView>& views);

bool close_enough(float left, float right) {
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= 0.0001f;
}

bool same_value(const Number& left, const Number& right) {
    if (left.kind != right.kind)
        return false;
    return left.kind == NumberKind::Integer ? left.integer == right.integer : close_enough(left.real, right.real);
}

bool read_value(const ue::UStruct* row_struct, const ue::TableRow& row, const std::string& field,
                NumberKind expected, Number& out, std::string& reason) {
    auto property = ue::find_property(row_struct, field);
    if (!property) {
        reason = "property is missing";
        return false;
    }
    const auto type = ue::property_type(property);
    const auto size = ue::property_size(property);
    if (expected == NumberKind::Integer && type == "IntProperty" && size == sizeof(int32_t)) {
        if (auto value = ue::row_field<int32_t>(row_struct, row, field)) {
            out = Number::from_int(*value);
            return true;
        }
    } else if (expected == NumberKind::Float && type == "FloatProperty" && size == sizeof(float)) {
        if (auto value = ue::row_field<float>(row_struct, row, field); value && std::isfinite(*value)) {
            out = Number::from_float(*value);
            return true;
        }
    }
    reason = "unexpected property type " + type + " (size " + std::to_string(size) + ")";
    return false;
}

bool write_value(const ue::UStruct* row_struct, const ue::TableRow& row, const std::string& field,
                 const Number& value) {
    auto property = ue::find_property(row_struct, field);
    if (!property || ue::property_offset(property) < 0)
        return false;
    if (value.kind == NumberKind::Integer && ue::property_type(property) == "IntProperty" &&
        ue::property_size(property) == sizeof(int32_t)) {
        if (auto ptr = ue::row_field<int32_t>(row_struct, row, field)) {
            *const_cast<int32_t*>(ptr) = value.integer;
            return true;
        }
    } else if (value.kind == NumberKind::Float && ue::property_type(property) == "FloatProperty" &&
               ue::property_size(property) == sizeof(float) && std::isfinite(value.real)) {
        if (auto ptr = ue::row_field<float>(row_struct, row, field)) {
            *const_cast<float*>(ptr) = value.real;
            return true;
        }
    }
    return false;
}

struct TableView {
    ue::UObject* object = nullptr;
    ue::UStruct* row_struct = nullptr;
    std::vector<ue::TableRow> rows;
    std::string error;
};

const ue::TableRow* find_row(const TableView& view, const std::string& name) {
    for (const auto& row : view.rows)
        if (ue::name(row.name) == name)
            return &row;
    return nullptr;
}

const ue::TableRow* player_row(const TableView& view) { return find_row(view, "Player"); }

TableView make_view(ue::UObject* table) {
    TableView view;
    view.object = table;
    view.row_struct = ue::table_struct(table);
    if (!view.row_struct) {
        view.error = "row struct is missing";
        return view;
    }
    view.rows = ue::table_rows(table, view.error);
    return view;
}

std::vector<TableView> collect_views(std::vector<ue::UObject*>& candidates) {
    std::vector<TableView> views;
    auto add = [&](ue::UObject* object) {
        if (!object || std::find_if(views.begin(), views.end(), [&](const TableView& v) { return v.object == object; }) !=
                           views.end())
            return;
        views.push_back(make_view(object));
    };

    const std::vector<std::string> wanted = {"CharacterTable", "SkillTable", "EffectTable", "ShopItemTable",
                                             "RewardGroupTable", "SPLevelTable", "CharacterLevelTable", "ItemFishTable"};
    for (auto* table : ue::data_tables()) {
        if (std::find(wanted.begin(), wanted.end(), ue::name(table)) != wanted.end()) {
            candidates.push_back(table);
            add(table);
        }
    }
    for (const auto& snapshot : g_snapshots)
        add(snapshot.table.get());
    return views;
}

TableView* find_view(std::vector<TableView>& views, ue::UObject* table) {
    auto it = std::find_if(views.begin(), views.end(), [&](const TableView& view) { return view.object == table; });
    return it == views.end() ? nullptr : &*it;
}

Target* find_target(std::vector<Target>& targets, ue::UObject* table, const std::string& row,
                    const std::string& field) {
    auto it = std::find_if(targets.begin(), targets.end(), [&](const Target& target) {
        return target.table == table && target.row == row && target.field == field;
    });
    return it == targets.end() ? nullptr : &*it;
}

Snapshot* find_snapshot(ue::UObject* table, const std::string& row, const std::string& field) {
    auto it = std::find_if(g_snapshots.begin(), g_snapshots.end(), [&](const Snapshot& snapshot) {
        return snapshot.table.get() == table && snapshot.row == row && snapshot.field == field;
    });
    return it == g_snapshots.end() ? nullptr : &*it;
}

bool has_snapshot(ue::UObject* table, const std::string& row, const std::string& field) {
    return std::find_if(g_snapshots.begin(), g_snapshots.end(), [&](const Snapshot& snapshot) {
               return snapshot.table.get() == table && snapshot.row == row && snapshot.field == field;
           }) != g_snapshots.end();
}

bool base_number(const TableView& view, const ue::TableRow& row, const std::string& row_name,
                 const std::string& field, Number& out) {
    if (auto* snapshot = find_snapshot(view.object, row_name, field)) {
        out = snapshot->original;
        return true;
    }
    auto property = ue::find_property(view.row_struct, field);
    if (!property)
        return false;
    const auto type = ue::property_type(property);
    std::string reason;
    if (type == "IntProperty")
        return read_value(view.row_struct, row, field, NumberKind::Integer, out, reason);
    if (type == "FloatProperty")
        return read_value(view.row_struct, row, field, NumberKind::Float, out, reason);
    return false;
}

void push_target(std::vector<Target>& out, ue::UObject* table, const std::string& row,
                 const std::string& field, Number value) {
    auto it = std::find_if(out.begin(), out.end(), [&](const Target& target) {
        return target.table == table && target.row == row && target.field == field;
    });
    if (it == out.end())
        out.push_back({table, row, field, value});
    else
        it->value = value;
}

Number scale_number(const Number& base, float percent, int minimum = 0, int maximum = 0) {
    if (base.kind == NumberKind::Integer) {
        if (base.integer <= 0)
            return base;
        int32_t value = static_cast<int32_t>(std::lround(static_cast<float>(base.integer) * percent / 100.0f));
        if (percent > 0.0f)
            value = std::max(value, minimum);
        if (maximum > 0)
            value = std::min(value, maximum);
        return Number::from_int(value);
    }
    if (base.real <= 0.0f)
        return base;
    float value = base.real * percent / 100.0f;
    if (percent > 0.0f)
        value = std::max(value, static_cast<float>(minimum));
    if (maximum > 0)
        value = std::min(value, static_cast<float>(maximum));
    return Number::from_float(value);
}

std::vector<Target> make_targets(const Settings& settings, std::vector<TableView>& views) {
    std::vector<Target> out;
    auto for_table = [&](const std::string& name, const auto& fn) {
        TableView* selected = nullptr;
        int matches = 0;
        for (auto& view : views) {
            if (ue::name(view.object) == name) {
                selected = &view;
                ++matches;
            }
        }
        if (matches == 1 && selected && selected->error.empty())
            fn(*selected);
        else if (matches > 1)
            log_once("ambiguous-" + name, "Found multiple " + name + " objects; skipped runtime edits for this table.");
    };
    auto direct = [&](TableView& view, const std::string& row, const std::string& field, Number value) {
        push_target(out, view.object, row, field, value);
    };
    auto scaled = [&](TableView& view, const ue::TableRow& row, const std::string& row_name,
                      const std::string& field, float multiplier, int min = 0, int max = 0) {
        Number base;
        if (!base_number(view, row, row_name, field, base) ||
            (base.kind == NumberKind::Integer ? base.integer <= 0 : base.real <= 0.0f))
            return;
        push_target(out, view.object, row_name, field, scale_number(base, multiplier * 100.0f, min, max));
    };
    auto set_original_type = [&](TableView& view, const ue::TableRow& row, const std::string& row_name,
                                 const std::string& field, float value) {
        Number base;
        if (!base_number(view, row, row_name, field, base))
            return;
        if (base.kind == NumberKind::Integer)
            direct(view, row_name, field, Number::from_int(static_cast<int32_t>(std::lround(value))));
        else
            direct(view, row_name, field, Number::from_float(value));
    };

    for_table("CharacterTable", [&](TableView& view) {
        const auto* player = player_row(view);
        if (!player)
            return;
        const std::string row = "Player";
        if (settings.slower_gauge_gain)
            direct(view, row, "BetaGaugeAdditiveRate", Number::from_float(-0.5f));
        if (settings.lower_gauge_capacity) {
            direct(view, row, "MaxBetaGauge", Number::from_int(400));
            direct(view, row, "MaxBurstGauge", Number::from_int(800));
        }
        if (settings.base_attributes) {
            direct(view, row, "MaxHP", Number::from_int(static_cast<int32_t>(std::lround(settings.max_hp))));
            direct(view, row, "MaxShield", Number::from_int(static_cast<int32_t>(std::lround(settings.max_shield))));
            direct(view, row, "DamageReductionPerShieldBock", Number::from_float(settings.shield_damage_reduction / 100.0f));
        }
        if (settings.shield_regen) {
            direct(view, row, "ShieldRegenPerSecond", Number::from_float(settings.shield_regen_normal));
            direct(view, row, "ShieldRegenPerSecondWhenBattle", Number::from_float(settings.shield_regen_combat));
        }
        if (settings.attack_speed)
            direct(view, row, "AttackSpeed", Number::from_float(settings.attack_speed_value));
        if (settings.gauges) {
            direct(view, row, "MaxBetaGauge", Number::from_int(static_cast<int32_t>(std::lround(settings.beta_capacity))));
            direct(view, row, "MaxBurstGauge", Number::from_int(static_cast<int32_t>(std::lround(settings.burst_capacity))));
            direct(view, row, "MaxTachyGauge", Number::from_int(static_cast<int32_t>(std::lround(settings.tachy_capacity))));
        }
        if (settings.ammunition)
            for (int i = 1; i <= 6; ++i)
                direct(view, row, "StackBullet" + std::to_string(i), Number::from_int(static_cast<int32_t>(std::lround(settings.ammunition_stack))));
        if (settings.ammunition_100x) {
            const int32_t values[] = {3000, 300, 1600, 1200, 6000, 800};
            for (int i = 1; i <= 6; ++i)
                direct(view, row, "StackBullet" + std::to_string(i), Number::from_int(values[i - 1]));
        }
        if (settings.consumables)
            for (int i = 1; i <= 7; ++i)
                direct(view, row, "StackConsumable" + std::to_string(i), Number::from_int(static_cast<int32_t>(std::lround(settings.consumable_stack))));
        if (settings.passive_hp_regen)
            direct(view, row, "HPRegenPerSecond", Number::from_float(settings.passive_hp_regen_value));
        if (settings.hp_drain) {
            direct(view, row, "DrainHpByAttackPowerRate", Number::from_float(settings.hp_drain_rate_percent / 100.0f));
            direct(view, row, "DrainHpFixedValue", Number::from_int(static_cast<int32_t>(std::lround(settings.hp_drain_fixed))));
        }
        if (settings.fishing_power)
            direct(view, row, "FishingAttackPower", Number::from_float(settings.fishing_power_value));
    });

    for_table("SkillTable", [&](TableView& view) {
        const std::vector<std::pair<const char*, float>> beta = {
            {"P_Eve_Sword_Beta_ChainStab1_1", 400.0f}, {"P_Eve_Sword_Beta_ChargeSlash1_1", 400.0f},
            {"P_Eve_Sword_Beta_ChargeSlash2_1", 400.0f}, {"P_Eve_Sword_Beta_SwordAura1_1", 400.0f},
            {"P_Eve_Sword_Beta_ShieldBreak1_1", 400.0f}, {"P_Eve_Sword_Beta_ShieldBreak2_1", 400.0f},
            {"P_Eve_Sword_Beta_ChainStab1Ex", 400.0f}, {"P_Eve_Sword_Beta_ChargeSlash1Ex", 400.0f},
            {"P_Eve_Sword_Beta_SwordAura1Ex_1", 400.0f}, {"P_Eve_Sword_Beta_ShieldBreak1Ex_1", 400.0f}};
        const std::vector<std::pair<const char*, float>> burst = {
            {"P_Eve_Sword_Burst_SpinAttack1_1", 600.0f}, {"P_Eve_Sword_Burst_SpinAttack2_1", 600.0f},
            {"P_Eve_Sword_Burst_FinishAttack1_1", 600.0f}, {"P_Eve_Sword_Burst_FinishAttack2_1", 600.0f},
            {"P_Eve_Sword_Burst_FinishAttack3_1", 600.0f}, {"P_Eve_Sword_Burst_MassiveAttack1_1", 800.0f},
            {"P_Eve_Sword_Burst_MassiveAttack2_1", 800.0f}, {"P_Eve_Sword_Burst_MeleeCombatBuff1_1", 600.0f}};
        if (settings.lower_skill_cooldowns) {
            for (const auto& [name, cost] : beta) {
                if (auto* row = find_row(view, name)) {
                    direct(view, name, "CoolTime", Number::from_float(3.0f));
                    direct(view, name, "UseEnergyAmount", Number::from_float(cost));
                }
            }
            for (const auto& [name, cost] : burst) {
                if (auto* row = find_row(view, name)) {
                    direct(view, name, "CoolTime", Number::from_float(3.0f));
                    direct(view, name, "UseEnergyAmount", Number::from_float(cost));
                }
            }
        }
        if (settings.forgiving_just || settings.extra_air_dodge || settings.dash_cooldown || settings.blaster_damage) {
            for (const auto& row_data : view.rows) {
                const std::string name = ue::name(row_data.name);
                if (settings.forgiving_just && name.starts_with("P_Eve_"))
                    scaled(view, row_data, name, "JustActionTime", settings.forgiving_just_multiplier);
                if (settings.extra_air_dodge && name.starts_with("P_Eve_") && name.find("Air") != std::string::npos &&
                    name.find("Evade") != std::string::npos) {
                    Number count;
                    if (base_number(view, row_data, name, "UsableCount", count) && count.kind == NumberKind::Integer && count.integer > 0 && count.integer < 2)
                        direct(view, name, "UsableCount", Number::from_int(2));
                }
                if (settings.dash_cooldown && (name == "P_Eve_Sword_Normal_DashAttack1_1" ||
                    name == "P_Eve_Sword_Normal_DashAttack3_1" || name == "P_Eve_Sword_Normal_DashAttack4_1"))
                    direct(view, name, "CoolTime", Number::from_float(4.0f));
                if (settings.blaster_damage && name.starts_with("P_Eve_Gun_ShootRailgun")) {
                    scaled(view, row_data, name, "AttackDamageRate", 2.0f);
                    scaled(view, row_data, name, "ShieldAttackDamageRate", 2.0f);
                }
            }
        }
    });

    for_table("EffectTable", [&](TableView& view) {
        for (const auto& row_data : view.rows) {
            const std::string name = ue::name(row_data.name);
            if (settings.tachy_reduction && name == "P_Eve_SkillTree_TachyGaugeReduceConsumeRate")
                set_original_type(view, row_data, name, "CalculationValue", 0.6f);
            if (settings.tumbler_heal && name == "Item_HP_RPotion")
                set_original_type(view, row_data, name, "CalculationValue", settings.tumbler_heal_value);
            if (settings.stronger_gear && name.starts_with("Gear_") && name.find("_HitDmgUp") == std::string::npos) {
                Number base;
                if (base_number(view, row_data, name, "CalculationValue", base) &&
                    (base.kind == NumberKind::Integer ? base.integer > 0 : base.real > 0.0f))
                    scaled(view, row_data, name, "CalculationValue", settings.stronger_gear_multiplier);
            }
        }
    });

    for_table("ShopItemTable", [&](TableView& view) {
        if (!settings.shop_prices)
            return;
        for (const auto& row_data : view.rows) {
            const std::string name = ue::name(row_data.name);
            for (int i = 1; i <= 4; ++i) {
                const auto field = "MoneyItemCount" + std::to_string(i);
                const auto discount = "Discount_" + field;
                Number value;
                if (base_number(view, row_data, name, field, value))
                    push_target(out, view.object, name, field, scale_number(value, settings.shop_price_percent, 1));
                if (base_number(view, row_data, name, discount, value))
                    push_target(out, view.object, name, discount, scale_number(value, settings.shop_price_percent, 1));
            }
        }
    });

    for_table("RewardGroupTable", [&](TableView& view) {
        if (!settings.drop_quantities)
            return;
        for (const auto& row_data : view.rows) {
            const std::string name = ue::name(row_data.name);
            for (const auto* field : {"ItemMinCount", "ItemMaxCount"}) {
                Number value;
                if (base_number(view, row_data, name, field, value))
                    push_target(out, view.object, name, field, scale_number(value, settings.drop_quantity_percent, 1));
            }
        }
    });

    for_table("SPLevelTable", [&](TableView& view) {
        if (!settings.sp_exp)
            return;
        for (const auto& row_data : view.rows) {
            const std::string name = ue::name(row_data.name);
            Number value;
            if (base_number(view, row_data, name, "RequiredSPExp", value))
                push_target(out, view.object, name, "RequiredSPExp", scale_number(value, settings.sp_exp_percent, 1));
        }
    });

    for_table("CharacterLevelTable", [&](TableView& view) {
        if (!settings.upgrade_costs)
            return;
        for (const auto& row_data : view.rows) {
            const std::string name = ue::name(row_data.name);
            for (const auto* field : {"RequiredItemAmount1", "RequiredItemAmount2"}) {
                Number value;
                if (base_number(view, row_data, name, field, value))
                    push_target(out, view.object, name, field, scale_number(value, settings.upgrade_cost_percent, 1));
            }
        }
    });

    for_table("ItemFishTable", [&](TableView& view) {
        if (!settings.world_fishing)
            return;
        for (const auto& row_data : view.rows) {
            const std::string name = ue::name(row_data.name);
            for (const auto& [field, pct] : {std::pair{"Stamina", settings.fish_stamina_percent},
                                             std::pair{"FightingTime", settings.fish_fight_time_percent}}) {
                Number value;
                if (base_number(view, row_data, name, field, value))
                    push_target(out, view.object, name, field, scale_number(value, pct, 1));
            }
        }
    });
    return out;
}

bool any_option_enabled(const Settings& s) {
    return s.base_attributes || s.shield_regen || s.attack_speed || s.gauges || s.ammunition || s.ammunition_100x || s.consumables ||
           s.passive_hp_regen || s.hp_drain || s.fishing_power || s.slower_gauge_gain || s.lower_gauge_capacity ||
           s.lower_skill_cooldowns || s.blaster_damage || s.forgiving_just || s.extra_air_dodge || s.dash_cooldown ||
           s.tachy_reduction || s.stronger_gear || s.tumbler_heal ||
           s.shop_prices || s.drop_quantities || s.sp_exp || s.upgrade_costs || s.world_fishing;
}

void reconcile(const Settings& settings) {
    std::vector<ue::UObject*> candidates;
    auto views = collect_views(candidates);
    std::unordered_map<std::string, int> table_counts;
    for (auto* table : candidates)
        ++table_counts[ue::name(table)];
    auto targets = make_targets(settings, views);
    int changed = 0;
    int skipped = 0;
    bool ambiguous = false;
    for (const auto& [name, count] : table_counts) {
        if (count > 1) {
            ambiguous = true;
            log_once("ambiguous-" + name, "Found multiple " + name + " objects; runtime edits for that table were skipped.");
        }
    }

    std::vector<Snapshot> kept;
    kept.reserve(g_snapshots.size() + targets.size());
    for (auto& snapshot : g_snapshots) {
        auto* table = snapshot.table.get();
        auto* view = find_view(views, table);
        auto* row = view ? find_row(*view, snapshot.row) : nullptr;
        const auto table_name = table ? ue::name(table) : "<unloaded>";
        const auto label = table_name + " / " + snapshot.row + " / " + snapshot.field;
        if (!table || !view || !row) {
            if (table && view && !view->error.empty())
                log_once("table-read-" + table_name, "Could not read " + ue::full_name(table) + ": " + view->error);
            if (table)
                kept.push_back(std::move(snapshot));
            continue;
        }

        Number current;
        std::string reason;
        if (!read_value(view->row_struct, *row, snapshot.field, snapshot.original.kind, current, reason)) {
            log_once("field-" + label, "Skipped " + label + ": " + reason);
            ++skipped;
            kept.push_back(std::move(snapshot));
            continue;
        }

        if (auto* target = find_target(targets, table, snapshot.row, snapshot.field)) {
            if (same_value(current, snapshot.last_written)) {
                if (!same_value(current, target->value)) {
                    if (write_value(view->row_struct, *row, snapshot.field, target->value)) {
                        snapshot.last_written = target->value;
                        ++changed;
                        log(CPS_LOG_INFO, "Applied " + label);
                    } else {
                        ++skipped;
                    }
                }
            } else if (same_value(current, target->value)) {
                snapshot.last_written = target->value;
            } else {
                ++skipped;
                if (!snapshot.conflict_logged) {
                    snapshot.conflict_logged = true;
                    log(CPS_LOG_WARNING, "Left " + label + " unchanged because another runtime change replaced the value.");
                }
            }
            kept.push_back(std::move(snapshot));
        } else {
            if (same_value(current, snapshot.last_written)) {
                if (write_value(view->row_struct, *row, snapshot.field, snapshot.original))
                    ++changed;
                else
                    ++skipped;
            } else {
                ++skipped;
                log_once("restore-" + label, "Left " + label + " unchanged during restore because another runtime change replaced it.");
            }
        }
    }
    g_snapshots = std::move(kept);

    for (const auto& target : targets) {
        const auto table_name = ue::name(target.table);
        if (table_counts[table_name] != 1 || has_snapshot(target.table, target.row, target.field))
            continue;
        auto* view = find_view(views, target.table);
        auto* row = view ? find_row(*view, target.row) : nullptr;
        const auto label = table_name + " / " + target.row + " / " + target.field;
        if (!view || !row || !view->error.empty()) {
            ++skipped;
            log_once("missing-row-" + label, "Could not resolve " + label + " in the loaded data table.");
            continue;
        }
        Number original;
        std::string reason;
        if (!read_value(view->row_struct, *row, target.field, target.value.kind, original, reason)) {
            ++skipped;
            log_once("field-" + label, "Skipped " + label + ": " + reason);
            continue;
        }
        Snapshot snapshot;
        snapshot.table = ue::Weak(target.table);
        snapshot.row = target.row;
        snapshot.field = target.field;
        snapshot.original = original;
        snapshot.last_written = original;
        if (!same_value(original, target.value)) {
            if (!write_value(view->row_struct, *row, target.field, target.value)) {
                ++skipped;
                log_once("write-" + label, "Could not write " + label);
                continue;
            }
            snapshot.last_written = target.value;
            ++changed;
            log(CPS_LOG_INFO, "Applied " + label);
        }
        g_snapshots.push_back(std::move(snapshot));
    }

    if (ambiguous)
        set_status({StatusKind::Ambiguous, changed, skipped});
    else if (!targets.empty())
        set_status({StatusKind::Applied, changed, skipped});
    else if (changed || skipped)
        set_status({StatusKind::Restored, changed, skipped});
    else if (any_option_enabled(settings))
        set_status({StatusKind::Searching, 0, 0});
    else if (g_save_error)
        set_status({StatusKind::SaveError, 0, 0});
    else
        set_status({StatusKind::Ready, 0, 0});
}

void restore_all() {
    if (!g_engine_ready || g_snapshots.empty())
        return;
    std::vector<ue::UObject*> candidates;
    auto views = collect_views(candidates);
    int restored = 0;
    int skipped = 0;
    for (const auto& snapshot : g_snapshots) {
        auto* table = snapshot.table.get();
        auto* view = find_view(views, table);
        auto* row = view ? find_row(*view, snapshot.row) : nullptr;
        if (!table || !view || !row) {
            ++skipped;
            continue;
        }
        Number current;
        std::string reason;
        if (read_value(view->row_struct, *row, snapshot.field, snapshot.original.kind, current, reason) &&
            same_value(current, snapshot.last_written) &&
            write_value(view->row_struct, *row, snapshot.field, snapshot.original)) {
            ++restored;
        } else {
            ++skipped;
            log(CPS_LOG_WARNING, "Could not restore " + ue::name(table) + " / " + snapshot.row + " / " + snapshot.field +
                                     "; it may have changed elsewhere.");
        }
    }
    g_snapshots.clear();
    log(CPS_LOG_INFO, "Cleanup restored " + std::to_string(restored) + " field(s); left " +
                         std::to_string(skipped) + " field(s) untouched.");
}

void on_tick(void*, float delta_seconds) {
    cps::log::translate_structured_exceptions();
    try {
        if (!g_engine_ready) {
            std::string error;
            if (!ue::init(g_api->engine(), error) || !ue::verify_runtime(error)) {
                set_status({StatusKind::Waiting, 0, 0});
                return;
            }
            g_engine_ready = true;
            log(CPS_LOG_INFO, "Unreal reflection is ready: " + ue::status().detail);
        }

        Settings settings;
        uint64_t revision = 0;
        {
            std::lock_guard lock(g_state_mutex);
            settings = g_settings;
            revision = g_revision;
        }
        save_settings_if_needed(revision, settings);

        g_scan_elapsed += std::max(0.0f, delta_seconds);
        const bool rescan = g_scan_elapsed >= 2.0f;
        if (rescan)
            g_scan_elapsed = 0.0f;
        if (revision != g_processed_revision || rescan) {
            if (revision != g_processed_revision)
                g_logged_issues.clear();
            reconcile(settings);
            g_processed_revision = revision;
        }
    } catch (const std::exception& error) {
        log(CPS_LOG_ERROR, std::string("Game-thread update failed safely: ") + error.what());
        set_status({StatusKind::Error, 0, 0});
    } catch (...) {
        log(CPS_LOG_ERROR, "Game-thread update failed with an unknown exception.");
        set_status({StatusKind::Error, 0, 0});
    }
}

void draw_page(void*, const cps_ui* ui, const cps_rect*) {
    Settings edit;
    {
        std::lock_guard lock(g_state_mutex);
        edit = g_settings;
    }
    bool changed = false;

    ui->heading(tr("tab.title", "Gameplay"));
    ui->text_dim(tr("page.intro", "Change supported player values while the game is running."));
    auto toggle = [&](const char* key, const char* fallback, bool& value) {
        int enabled = value ? 1 : 0;
        if (ui->toggle(tr(key, fallback), &enabled)) {
            value = enabled != 0;
            changed = true;
        }
    };
    auto slider = [&](const char* key, const char* fallback, float& value, float min, float max, const char* format) {
        if (ui->slider(tr(key, fallback), &value, min, max, format))
            changed = true;
    };
    ui->rule();
    ui->heading(tr("page.attributes", "Player attributes"));
    int base = edit.base_attributes ? 1 : 0;
    if (ui->toggle(tr("page.base_attributes", "Enable custom health, shield, and damage reduction"), &base)) {
        edit.base_attributes = base != 0;
        changed = true;
    }
    if (ui->slider(tr("page.max_hp", "Maximum health"), &edit.max_hp, 500.0f, 10000.0f, "%.0f"))
        changed = true;
    if (ui->slider(tr("page.max_shield", "Maximum shield"), &edit.max_shield, 0.0f, 5000.0f, "%.0f"))
        changed = true;
    if (ui->slider(tr("page.shield_reduction", "Damage reduction while shielded (%)"),
                   &edit.shield_damage_reduction, 0.0f, 80.0f, "%.0f%%"))
        changed = true;

    int regen = edit.shield_regen ? 1 : 0;
    if (ui->toggle(tr("page.shield_regen", "Enable custom shield regeneration"), &regen)) {
        edit.shield_regen = regen != 0;
        changed = true;
    }
    if (ui->slider(tr("page.shield_regen_normal", "Shield regeneration outside combat"),
                   &edit.shield_regen_normal, 0.0f, 300.0f, "%.0f"))
        changed = true;
    if (ui->slider(tr("page.shield_regen_combat", "Shield regeneration in combat"),
                   &edit.shield_regen_combat, 0.0f, 100.0f, "%.0f"))
        changed = true;

    int attack = edit.attack_speed ? 1 : 0;
    if (ui->toggle(tr("page.attack_speed", "Enable custom attack speed"), &attack)) {
        edit.attack_speed = attack != 0;
        changed = true;
    }
    if (ui->slider(tr("page.attack_speed_value", "Attack speed"), &edit.attack_speed_value,
                   0.5f, 2.5f, "%.2fx"))
        changed = true;

    ui->rule();
    ui->heading(tr("page.gauges", "Gauge capacity"));
    int gauges = edit.gauges ? 1 : 0;
    if (ui->toggle(tr("page.enable_gauges", "Enable custom Beta, Burst and Tachy capacity"), &gauges)) {
        edit.gauges = gauges != 0;
        changed = true;
    }
    if (ui->slider(tr("page.beta", "Maximum Beta gauge"), &edit.beta_capacity, 0.0f, 5000.0f, "%.0f"))
        changed = true;
    if (ui->slider(tr("page.burst", "Maximum Burst gauge"), &edit.burst_capacity, 0.0f, 5000.0f, "%.0f"))
        changed = true;
    if (ui->slider(tr("page.tachy", "Maximum Tachy gauge"), &edit.tachy_capacity, 0.0f, 36000.0f, "%.0f"))
        changed = true;

    ui->rule();
    ui->heading(tr("page.inventory", "Inventory limits"));
    int ammo = edit.ammunition ? 1 : 0;
    if (ui->toggle(tr("page.ammo", "Enable custom ammunition stacks"), &ammo)) {
        edit.ammunition = ammo != 0;
        changed = true;
    }
    if (ui->slider(tr("page.ammo_value", "Ammunition stack size"), &edit.ammunition_stack,
                   1.0f, 9999.0f, "%.0f"))
        changed = true;
    toggle("page.ammo_100x", "Use individual 100x ammunition stacks", edit.ammunition_100x);
    int consumables = edit.consumables ? 1 : 0;
    if (ui->toggle(tr("page.consumables", "Enable custom consumable stacks"), &consumables)) {
        edit.consumables = consumables != 0;
        changed = true;
    }
    if (ui->slider(tr("page.consumables_value", "Consumable stack size"), &edit.consumable_stack,
                   1.0f, 999.0f, "%.0f"))
        changed = true;

    ui->rule();
    ui->heading(tr("page.player_extras", "Additional player settings"));
    int hp_regen = edit.passive_hp_regen ? 1 : 0;
    if (ui->toggle(tr("page.hp_regen_toggle", "Enable passive health regeneration"), &hp_regen)) {
        edit.passive_hp_regen = hp_regen != 0;
        changed = true;
    }
    if (ui->slider(tr("page.hp_regen", "Health regenerated per second"), &edit.passive_hp_regen_value,
                   0.0f, 200.0f, "%.1f"))
        changed = true;

    int drain = edit.hp_drain ? 1 : 0;
    if (ui->toggle(tr("page.hp_drain_toggle", "Enable healing on hit"), &drain)) {
        edit.hp_drain = drain != 0;
        changed = true;
    }
    if (ui->slider(tr("page.hp_drain_rate", "Healing from attack power (%)"),
                   &edit.hp_drain_rate_percent, 0.0f, 25.0f, "%.1f%%"))
        changed = true;
    if (ui->slider(tr("page.hp_drain_fixed", "Healing per hit"), &edit.hp_drain_fixed,
                   0.0f, 500.0f, "%.0f"))
        changed = true;

    int fishing = edit.fishing_power ? 1 : 0;
    if (ui->toggle(tr("page.fishing_toggle", "Enable custom fishing power"), &fishing)) {
        edit.fishing_power = fishing != 0;
        changed = true;
    }
    if (ui->slider(tr("page.fishing_power", "Fishing attack power"), &edit.fishing_power_value,
                   0.0f, 500.0f, "%.0f"))
        changed = true;

    ui->rule();
    ui->heading(tr("page.builder_combat", "Builder: combat and skills"));
    toggle("page.slower_gain", "Reduce Beta gauge gain", edit.slower_gauge_gain);
    toggle("page.lower_capacity", "Use lower Beta and Burst capacity", edit.lower_gauge_capacity);
    toggle("page.skill_cooldowns", "Use Stellar Souls skill cooldowns and costs", edit.lower_skill_cooldowns);
    toggle("page.blaster_damage", "Double Railgun damage", edit.blaster_damage);
    toggle("page.forgiving_just", "Increase perfect parry and dodge window", edit.forgiving_just);
    slider("page.forgiving_just_multiplier", "Perfect window multiplier", edit.forgiving_just_multiplier,
           1.0f, 3.0f, "%.2fx");
    toggle("page.extra_air_dodge", "Allow an extra aerial dodge", edit.extra_air_dodge);
    toggle("page.dash_cooldown", "Set dash attack cooldown to 4 seconds", edit.dash_cooldown);
    toggle("page.tachy_reduction", "Reduce Tachy gauge consumption", edit.tachy_reduction);
    toggle("page.stronger_gear", "Increase positive gear effects", edit.stronger_gear);
    slider("page.stronger_gear_multiplier", "Gear effect multiplier", edit.stronger_gear_multiplier,
           1.0f, 5.0f, "%.2fx");
    toggle("page.tumbler_heal", "Set Tumbler healing", edit.tumbler_heal);
    slider("page.tumbler_heal_value", "Tumbler healing value", edit.tumbler_heal_value,
           10.0f, 100.0f, "%.0f");

    ui->rule();
    ui->heading(tr("page.builder_world", "Builder: world and progression"));
    toggle("page.shop_prices", "Change shop prices", edit.shop_prices);
    slider("page.shop_price_percent", "Price percent of original", edit.shop_price_percent,
           0.0f, 300.0f, "%.0f%%");
    toggle("page.drop_quantities", "Change item drop quantities", edit.drop_quantities);
    slider("page.drop_quantity_percent", "Drop quantity percent of original", edit.drop_quantity_percent,
           0.0f, 1000.0f, "%.0f%%");
    toggle("page.sp_exp", "Change SP experience requirements", edit.sp_exp);
    slider("page.sp_exp_percent", "SP experience percent of original", edit.sp_exp_percent,
           0.0f, 300.0f, "%.0f%%");
    toggle("page.upgrade_costs", "Change upgrade material costs", edit.upgrade_costs);
    slider("page.upgrade_cost_percent", "Upgrade cost percent of original", edit.upgrade_cost_percent,
           0.0f, 300.0f, "%.0f%%");
    toggle("page.world_fishing", "Change fishing difficulty and fight time", edit.world_fishing);
    slider("page.fish_stamina_percent", "Fish stamina percent of original", edit.fish_stamina_percent,
           0.0f, 300.0f, "%.0f%%");
    slider("page.fish_fight_time_percent", "Fish fight time percent of original", edit.fish_fight_time_percent,
           0.0f, 1000.0f, "%.0f%%");

    ui->rule();
    ui->heading(tr("page.builder_static", "Builder options that still need generated mods"));
    ui->text_dim(tr("page.builder_static_details",
                    "Combat profiles and damage presets; SkinSuit/outfit and CNS helper; mini-bosses, enemy variety and overworld bosses; environment-death changes and automatic gauge effects; chance-based drops; and challenge/moveset asset packages remain desktop Builder features. They require row creation, enums/arrays/references, spawn changes, assets, or helper files that CPS runtime reflection cannot safely reproduce."));

    if (ui->button(tr("page.disable_all", "Disable all and restore captured values"))) {
        edit.base_attributes = false;
        edit.shield_regen = false;
        edit.attack_speed = false;
        edit.gauges = false;
        edit.ammunition = false;
        edit.ammunition_100x = false;
        edit.consumables = false;
        edit.passive_hp_regen = false;
        edit.hp_drain = false;
        edit.fishing_power = false;
        edit.slower_gauge_gain = false;
        edit.lower_gauge_capacity = false;
        edit.lower_skill_cooldowns = false;
        edit.blaster_damage = false;
        edit.forgiving_just = false;
        edit.extra_air_dodge = false;
        edit.dash_cooldown = false;
        edit.tachy_reduction = false;
        edit.stronger_gear = false;
        edit.tumbler_heal = false;
        edit.shop_prices = false;
        edit.drop_quantities = false;
        edit.sp_exp = false;
        edit.upgrade_costs = false;
        edit.world_fishing = false;
        changed = true;
    }

    if (changed) {
        std::lock_guard lock(g_state_mutex);
        g_settings = edit;
        ++g_revision;
        set_status({StatusKind::Waiting, 0, 0});
    }

    const auto status = get_status();
    char line[192]{};
    switch (status.kind) {
    case StatusKind::Waiting:
        std::snprintf(line, sizeof(line), "%s", tr("page.pending", "Waiting for the game table..."));
        break;
    case StatusKind::Searching:
        std::snprintf(line, sizeof(line), "%s", tr("page.searching", "Waiting for the game data tables..."));
        break;
    case StatusKind::Ready:
        std::snprintf(line, sizeof(line), "%s", tr("page.ready", "Ready. No runtime changes are active."));
        break;
    case StatusKind::Applied:
        std::snprintf(line, sizeof(line), tr("page.applied", "Updated %d field(s); %d could not be changed."),
                      status.changed, status.skipped);
        break;
    case StatusKind::Restored:
        std::snprintf(line, sizeof(line), tr("page.restored", "Restored %d field(s); %d changed elsewhere and were left untouched."),
                      status.changed, status.skipped);
        break;
    case StatusKind::Ambiguous:
        std::snprintf(line, sizeof(line), "%s", tr("page.ambiguous", "Found duplicate data table objects; affected tables were skipped."));
        break;
    case StatusKind::Error:
        std::snprintf(line, sizeof(line), "%s", tr("page.error", "Could not initialize game reflection API."));
        break;
    case StatusKind::SaveError:
        std::snprintf(line, sizeof(line), "%s", tr("page.save_error", "Could not save plugin settings."));
        break;
    }
    ui->rule();
    ui->text(line);
}
} // namespace

extern "C" __declspec(dllexport) int cps_plugin_load(const cps_api* api, const cps_plugin* plugin) {
    g_api = api;
    g_client = plugin->client;
    cps::log::bind(api, g_client);
    cps::log::translate_structured_exceptions();
    load_settings();

    if (!api->on_game_tick(g_client, on_tick, nullptr)) {
        log(CPS_LOG_ERROR, "Could not register the game tick callback.");
        return 0;
    }
    static const cps_tab page{sizeof(cps_tab), "gameplay", mark("tab.title", "Gameplay"), nullptr,
                              CPS_TAB_SIDEBAR, draw_page, nullptr, nullptr};
    if (!api->add_tab(g_client, &page)) {
        log(CPS_LOG_ERROR, "Could not register the gameplay page.");
        return 0;
    }
    log(CPS_LOG_INFO, "Loaded. Runtime edits use checked integer/float fields on explicitly selected rows in supported Stellar Souls tables.");
    return 1;
}

extern "C" __declspec(dllexport) void cps_plugin_unload() {
    cps::log::translate_structured_exceptions();
    try {
        restore_all();
        Settings settings;
        uint64_t revision = 0;
        {
            std::lock_guard lock(g_state_mutex);
            settings = g_settings;
            revision = g_revision;
        }
        save_settings_if_needed(revision, settings);
    } catch (const std::exception& error) {
        log(CPS_LOG_ERROR, std::string("Plugin cleanup failed: ") + error.what());
    } catch (...) {
        log(CPS_LOG_ERROR, "Plugin cleanup failed with an unknown exception.");
    }
    g_snapshots.clear();
    g_client = 0;
    g_api = nullptr;
}
