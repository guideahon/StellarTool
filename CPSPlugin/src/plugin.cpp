#include <cps_api.h>
#include <cps_log.hpp>
#include <common.hpp>
#include <ue/tables.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
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
    bool consumables = false;
    float consumable_stack = 99.0f;
    bool passive_hp_regen = false;
    float passive_hp_regen_value = 20.0f;
    bool hp_drain = false;
    float hp_drain_rate_percent = 3.0f;
    float hp_drain_fixed = 30.0f;
    bool fishing_power = false;
    float fishing_power_value = 50.0f;
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
    std::string field;
    Number value;
};

struct Snapshot {
    ue::Weak table;
    std::string row = "Player";
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
                     {"consumables", s.consumables},
                     {"consumable_stack", s.consumable_stack},
                     {"passive_hp_regen", s.passive_hp_regen},
                     {"passive_hp_regen_value", s.passive_hp_regen_value},
                     {"hp_drain", s.hp_drain},
                     {"hp_drain_rate_percent", s.hp_drain_rate_percent},
                     {"hp_drain_fixed", s.hp_drain_fixed},
                     {"fishing_power", s.fishing_power},
                     {"fishing_power_value", s.fishing_power_value}};
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
        g_settings.consumables = read_bool(json, "consumables", false);
        g_settings.consumable_stack = clamp_float(json, "consumable_stack", 99.0f, 1.0f, 999.0f);
        g_settings.passive_hp_regen = read_bool(json, "passive_hp_regen", false);
        g_settings.passive_hp_regen_value = clamp_float(json, "passive_hp_regen_value", 20.0f, 0.0f, 200.0f);
        g_settings.hp_drain = read_bool(json, "hp_drain", false);
        g_settings.hp_drain_rate_percent = clamp_float(json, "hp_drain_rate_percent", 3.0f, 0.0f, 25.0f);
        g_settings.hp_drain_fixed = clamp_float(json, "hp_drain_fixed", 30.0f, 0.0f, 500.0f);
        g_settings.fishing_power = read_bool(json, "fishing_power", false);
        g_settings.fishing_power_value = clamp_float(json, "fishing_power_value", 50.0f, 0.0f, 500.0f);
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

std::vector<Target> make_targets(const Settings& settings) {
    std::vector<Target> out;
    if (settings.base_attributes) {
        out.push_back({"MaxHP", Number::from_int(static_cast<int32_t>(std::lround(settings.max_hp)))});
        out.push_back({"MaxShield", Number::from_int(static_cast<int32_t>(std::lround(settings.max_shield)))});
        out.push_back({"DamageReductionPerShieldBock", Number::from_float(settings.shield_damage_reduction / 100.0f)});
    }
    if (settings.shield_regen) {
        out.push_back({"ShieldRegenPerSecond", Number::from_float(settings.shield_regen_normal)});
        out.push_back({"ShieldRegenPerSecondWhenBattle", Number::from_float(settings.shield_regen_combat)});
    }
    if (settings.attack_speed)
        out.push_back({"AttackSpeed", Number::from_float(settings.attack_speed_value)});
    if (settings.gauges) {
        out.push_back({"MaxBetaGauge", Number::from_int(static_cast<int32_t>(std::lround(settings.beta_capacity)))});
        out.push_back({"MaxBurstGauge", Number::from_int(static_cast<int32_t>(std::lround(settings.burst_capacity)))});
        out.push_back({"MaxTachyGauge", Number::from_int(static_cast<int32_t>(std::lround(settings.tachy_capacity)))});
    }
    if (settings.ammunition) {
        const auto stack = static_cast<int32_t>(std::lround(settings.ammunition_stack));
        for (int i = 1; i <= 6; ++i)
            out.push_back({"StackBullet" + std::to_string(i), Number::from_int(stack)});
    }
    if (settings.consumables) {
        const auto stack = static_cast<int32_t>(std::lround(settings.consumable_stack));
        for (int i = 1; i <= 7; ++i)
            out.push_back({"StackConsumable" + std::to_string(i), Number::from_int(stack)});
    }
    if (settings.passive_hp_regen)
        out.push_back({"HPRegenPerSecond", Number::from_float(settings.passive_hp_regen_value)});
    if (settings.hp_drain) {
        out.push_back({"DrainHpByAttackPowerRate", Number::from_float(settings.hp_drain_rate_percent / 100.0f)});
        out.push_back({"DrainHpFixedValue", Number::from_int(static_cast<int32_t>(std::lround(settings.hp_drain_fixed)))});
    }
    if (settings.fishing_power)
        out.push_back({"FishingAttackPower", Number::from_float(settings.fishing_power_value)});
    return out;
}

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

const ue::TableRow* player_row(const TableView& view) {
    for (const auto& row : view.rows)
        if (ue::name(row.name) == "Player")
            return &row;
    return nullptr;
}

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

    for (auto* table : ue::data_tables()) {
        if (ue::name(table) == "CharacterTable") {
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

Target* find_target(std::vector<Target>& targets, const std::string& field) {
    auto it = std::find_if(targets.begin(), targets.end(), [&](const Target& target) { return target.field == field; });
    return it == targets.end() ? nullptr : &*it;
}

bool has_snapshot(ue::UObject* table, const std::string& field) {
    return std::find_if(g_snapshots.begin(), g_snapshots.end(), [&](const Snapshot& snapshot) {
               return snapshot.table.get() == table && snapshot.field == field;
           }) != g_snapshots.end();
}

void reconcile(const Settings& settings) {
    std::vector<Target> targets = make_targets(settings);
    if (targets.empty() && g_snapshots.empty()) {
        set_status({g_save_error ? StatusKind::SaveError : StatusKind::Ready, 0, 0});
        return;
    }
    std::vector<ue::UObject*> candidates;
    auto views = collect_views(candidates);
    const bool ambiguous = candidates.size() > 1;
    TableView* active_view = !ambiguous && !candidates.empty() ? find_view(views, candidates.front()) : nullptr;

    if (ambiguous) {
        std::string detail = "Found multiple CharacterTable objects:";
        for (auto* table : candidates)
            detail += " " + ue::full_name(table);
        log_once("ambiguous-table", detail);
    }

    int changed = 0;
    int skipped = 0;
    std::vector<Snapshot> kept;
    kept.reserve(g_snapshots.size() + targets.size());

    for (auto& snapshot : g_snapshots) {
        auto* table = snapshot.table.get();
        auto* view = find_view(views, table);
        auto* row = view ? player_row(*view) : nullptr;
        if (!table || !view || !row) {
            if (table && view && !view->error.empty())
                log_once("table-read-" + ue::full_name(table), "Could not read " + ue::full_name(table) + ": " + view->error);
            if (table && !row)
                log_once("player-row-" + ue::full_name(table), "Player row is missing in " + ue::full_name(table));
            if (table)
                kept.push_back(std::move(snapshot));
            continue;
        }

        Number current;
        std::string reason;
        if (!read_value(view->row_struct, *row, snapshot.field, snapshot.original.kind, current, reason)) {
            log_once("field-" + snapshot.field, "Skipped CharacterTable / Player / " + snapshot.field + ": " + reason);
            ++skipped;
            kept.push_back(std::move(snapshot));
            continue;
        }

        if (auto* target = find_target(targets, snapshot.field)) {
            if (same_value(current, snapshot.last_written)) {
                if (!same_value(current, target->value)) {
                    if (write_value(view->row_struct, *row, snapshot.field, target->value)) {
                        snapshot.last_written = target->value;
                        ++changed;
                        log(CPS_LOG_INFO, "Applied CharacterTable / Player / " + snapshot.field);
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
                    log(CPS_LOG_WARNING, "Left CharacterTable / Player / " + snapshot.field +
                                               " unchanged because another runtime change replaced the value.");
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
                log_once("restore-" + snapshot.field,
                         "Left CharacterTable / Player / " + snapshot.field +
                             " unchanged during restore because another runtime change replaced it.");
            }
        }
    }
    g_snapshots = std::move(kept);

    if (active_view && !active_view->error.empty()) {
        log_once("table-read-active", "Could not read " + ue::full_name(active_view->object) + ": " + active_view->error);
        skipped += static_cast<int>(targets.size());
    } else if (active_view && !player_row(*active_view)) {
        log_once("missing-player-row", "Player row is missing in " + ue::full_name(active_view->object));
        skipped += static_cast<int>(targets.size());
    } else if (active_view && player_row(*active_view)) {
        const auto* row = player_row(*active_view);
        for (const auto& target : targets) {
            if (has_snapshot(active_view->object, target.field))
                continue;
            Number original;
            std::string reason;
            if (!read_value(active_view->row_struct, *row, target.field, target.value.kind, original, reason)) {
                ++skipped;
                log_once("field-" + target.field,
                         "Skipped CharacterTable / Player / " + target.field + ": " + reason);
                continue;
            }
            Snapshot snapshot;
            snapshot.table = ue::Weak(active_view->object);
            snapshot.field = target.field;
            snapshot.original = original;
            snapshot.last_written = original;
            if (!same_value(original, target.value)) {
                if (!write_value(active_view->row_struct, *row, target.field, target.value)) {
                    ++skipped;
                    log_once("write-" + target.field, "Could not write CharacterTable / Player / " + target.field);
                    continue;
                }
                snapshot.last_written = target.value;
                ++changed;
                log(CPS_LOG_INFO, "Applied CharacterTable / Player / " + target.field);
            }
            g_snapshots.push_back(std::move(snapshot));
        }
    }

    if (ambiguous)
        set_status({StatusKind::Ambiguous, changed, skipped});
    else if (!targets.empty() && !active_view)
        set_status({StatusKind::Searching, changed, skipped});
    else if (!targets.empty())
        set_status({StatusKind::Applied, changed, skipped});
    else if (changed || skipped)
        set_status({StatusKind::Restored, changed, skipped});
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
        auto* row = view ? player_row(*view) : nullptr;
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
            log(CPS_LOG_WARNING, "Could not restore CharacterTable / Player / " + snapshot.field +
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

    if (ui->button(tr("page.disable_all", "Disable all and restore captured values"))) {
        edit.base_attributes = false;
        edit.shield_regen = false;
        edit.attack_speed = false;
        edit.gauges = false;
        edit.ammunition = false;
        edit.consumables = false;
        edit.passive_hp_regen = false;
        edit.hp_drain = false;
        edit.fishing_power = false;
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
        std::snprintf(line, sizeof(line), "%s", tr("page.searching", "Looking for the player data table..."));
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
        std::snprintf(line, sizeof(line), "%s", tr("page.ambiguous", "Found more than one player data table."));
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
    log(CPS_LOG_INFO, "Loaded. Runtime table writes are limited to checked numeric fields on CharacterTable / Player.");
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
