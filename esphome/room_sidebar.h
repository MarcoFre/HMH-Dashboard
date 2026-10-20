#include <type_traits>
#include "esphome/core/helpers.h"
#pragma once
#include "lvgl.h"
#include "hmh_layout.h"
#include "hmh_tile_grid.h"
// lvgl.h does not export the image-cache API in this LVGL build.
// LVGL 9.3+ keeps the cache instances in a dedicated subdirectory.
#if __has_include("src/misc/cache/instance/lv_image_cache.h")
#include "src/misc/cache/instance/lv_image_cache.h"
#elif __has_include("src/misc/cache/lv_image_cache.h")
#include "src/misc/cache/lv_image_cache.h"
#else
#error "HMH standby: LVGL image-cache header not found"
#endif
#include "esphome/components/json/json_util.h"
#include <string>
#include <vector>
#include <functional>
#include <utility>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cctype>


static const lv_font_t *hmh_chip_text_14 = &lv_font_montserrat_16;
static const lv_font_t *hmh_runtime_text_16 = &lv_font_montserrat_16;
static const lv_font_t *hmh_runtime_text_20 = &lv_font_montserrat_20;
static const lv_font_t *hmh_runtime_text_28 = &lv_font_montserrat_28;
static void hmh_set_text_fonts(const lv_font_t *small, const lv_font_t *normal, const lv_font_t *large) {
  // ESPHome owns these mutable fonts; keep their actual identity and glyph callbacks.
  const_cast<lv_font_t *>(small)->fallback=&lv_font_montserrat_16;
  const_cast<lv_font_t *>(normal)->fallback=&lv_font_montserrat_20;
  const_cast<lv_font_t *>(large)->fallback=&lv_font_montserrat_28;
  hmh_runtime_text_16=small;hmh_runtime_text_20=normal;hmh_runtime_text_28=large;
}
// Returns -1 for left, +1 for right, 0 for a tap/vertical/control gesture.
static int hmh_swipe_direction(int dx, int dy, bool allowed) {
  if (!allowed || std::abs(dx) < 80 || std::abs(dx) <= 2 * std::abs(dy)) return 0;
  return dx < 0 ? -1 : 1;
}
static std::string hmh_history_client="dashboard";
static bool hmh_navigation_blocked = false;
static bool hmh_status_busy = false;
static bool hmh_raw_touch_enabled=false,hmh_raw_pressed=false;
static uint32_t hmh_raw_sequence=0,hmh_raw_pressed_at=0;
static lv_point_t hmh_raw_point{},hmh_raw_origin{};
static void hmh_touch_start(int x,int y) {
  hmh_raw_touch_enabled=true;hmh_raw_pressed=true;++hmh_raw_sequence;
  hmh_raw_point.x=x;hmh_raw_point.y=y;hmh_raw_origin=hmh_raw_point;hmh_raw_pressed_at=lv_tick_get();
}
static void hmh_touch_update(int x,int y) {hmh_raw_point.x=x;hmh_raw_point.y=y;}
static void hmh_touch_end() {hmh_raw_pressed=false;}
static void hmh_check_glyphs(const lv_font_t *font,const char *text) {
  const auto *p=reinterpret_cast<const unsigned char *>(text);
  while(*p) {
    uint32_t cp=*p++;
    if(cp>=0xC0 && cp<0xE0 && *p) {cp=(cp&31)<<6;cp|=*p++&63;}
    else if(cp>=0xE0 && cp<0xF0 && p[0] && p[1]) {cp=(cp&15)<<12;cp|=(p[0]&63)<<6;cp|=p[1]&63;p+=2;}
    else if(cp>=0xF0 && cp<0xF8 && p[0] && p[1] && p[2]) {cp=(cp&7)<<18;cp|=(p[0]&63)<<12;cp|=(p[1]&63)<<6;cp|=p[2]&63;p+=3;}
    if(cp<32 || (cp>=0xE000 && cp<=0xF8FF) || cp>=0xF0000) continue;
    lv_font_glyph_dsc_t dsc{};
    if(!lv_font_get_glyph_dsc(font,&dsc,cp,0) || dsc.is_placeholder)
      ESP_LOGW("ha_dashboard","Glifo mancante U+%04X",static_cast<unsigned>(cp));
  }
}

static bool hmh_dark = true; // Gaia's initial dark theme
static const lv_font_t *hmh_mdi_font = hmh_runtime_text_20;
struct HmhPalette { uint32_t source, light, dark; };
static const HmhPalette hmh_palette[] = {
  {0x0B1220,0xFFFFFF,0x071423}, {0x111827,0xF5F5F5,0x0A1C30},
  {0x1E293B,0xF8F9FA,0x142B43}, {0x0F172A,0xF3F4F6,0x0D2137},
  {0x334155,0xE5E7EB,0x1B3D59}, {0xEAF2FF,0x202124,0xEDF8FF},
  {0xF1F5F9,0x202124,0xDCEFFC}, {0xCBD5E1,0x60656B,0xB4D4E8},
  {0x94A3B8,0x555B63,0x87ADC6}, {0x4A3D22,0xFFEBD0,0x49341D},
  {0x164452,0xE0F2F1,0x164452}, {0x24465A,0xF0F0F0,0x303030},
  {0xFBBF24,0xFF9800,0xFFB347}, {0x64748B,0x9CA3AF,0x73777C}
};
static lv_color_t hmh_color(uint32_t rgb) {
  for (const auto &p : hmh_palette) if (p.source == rgb)
    return lv_color_hex(hmh_dark ? p.dark : p.light);
  return lv_color_hex(rgb);
}
static bool hmh_remap(lv_color_t color, lv_color_t &result) {
  uint32_t rgb = lv_color_to_u32(color) & 0xFFFFFF;
  // Read the previous theme's colors, then write the new theme.
  for (const auto &p : hmh_palette) if (rgb == (hmh_dark ? p.light : p.dark)) {
    result = lv_color_hex(hmh_dark ? p.dark : p.light); return true;
  }
  for (const auto &p : hmh_palette) if (rgb == p.source) {
    result = lv_color_hex(hmh_dark ? p.dark : p.light); return true;
  }
  return false;
}
static void hmh_retheme(lv_obj_t *obj) {
  lv_color_t color;
  if (hmh_remap(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), color))
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
  if (lv_obj_check_type(obj, &lv_label_class)) {
    lv_obj_set_style_text_color(obj, hmh_color(0xEAF2FF), 0);
    lv_obj_set_style_text_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  }
  if (hmh_remap(lv_obj_get_style_border_color(obj, LV_PART_MAIN), color))
    lv_obj_set_style_border_color(obj, color, LV_PART_MAIN);
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) hmh_retheme(lv_obj_get_child(obj, i));
}

// Il prefisso impedisce a HA di convertire il JSON in un dizionario nativo.
static std::string hmh_json(const std::string &value) {
  return value.compare(0, 5, "json:") == 0 ? value.substr(5) : value;
}

// All instances use the same default RAM policy and can release each other's blocks.
template<class T> class HmhRAMAllocator : public esphome::RAMAllocator<T> {
 public:
  using value_type = T;
  using is_always_equal = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  template<class U> struct rebind { using other = HmhRAMAllocator<U>; };
  HmhRAMAllocator() = default;
  template<class U> HmhRAMAllocator(const HmhRAMAllocator<U> &) {}
  template<class U> bool operator==(const HmhRAMAllocator<U> &) const noexcept { return true; }
  template<class U> bool operator!=(const HmhRAMAllocator<U> &) const noexcept { return false; }
};

// Avoid invalidating/redrawing LVGL labels when the rendered text is unchanged.
static void hmh_label_set_if_changed(lv_obj_t *label, const char *text) {
  if (!label) return;
  if (!text) text = "";
  const char *current = lv_label_get_text(label);
  if (current && std::strcmp(current, text) == 0) return;
  lv_label_set_text(label, text);
}

// Fast path: accept ONLY changes in the JSON "state" values, with every
// other byte identical. Any structural/attribute change falls back to parsing.
static bool hmh_state_only_delta(const std::string &before, const std::string &after,
                                 std::vector<std::string> &states) {
  static const std::string key = "\"state\":\"";
  size_t a = 0, b = 0;
  while (true) {
    const size_t ka = before.find(key, a), kb = after.find(key, b);
    if ((ka == std::string::npos) != (kb == std::string::npos)) return false;
    const size_t ea = ka == std::string::npos ? before.size() : ka;
    const size_t eb = kb == std::string::npos ? after.size() : kb;
    if (ea-a != eb-b || before.compare(a,ea-a,after,b,eb-b) != 0) return false;
    if (ka == std::string::npos) return !states.empty();
    a = ka + key.size(); b = kb + key.size();
    // Any escaped JSON value is handled by the full parser instead.
    const size_t end_a = before.find('"', a), end_b = after.find('"', b);
    if (end_a == std::string::npos || end_b == std::string::npos) return false;
    if (before.find('\\',a) < end_a || after.find('\\',b) < end_b) return false;
    states.emplace_back(after.substr(b,end_b-b));
    a = end_a; b = end_b;
  }
}

static uint64_t hmh_catalog_signature(const std::string &value) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; }
  return hash;
}

struct HmhBatteryWidget {
  lv_obj_t *root=nullptr,*outline=nullptr,*fill=nullptr,*text=nullptr;
  int body_width=0,body_height=0;
  void create(lv_obj_t *parent,int x,int y,bool compact) {
    root=lv_obj_create(parent);lv_obj_set_pos(root,x,y);lv_obj_set_size(root,compact?66:230,compact?14:30);
    lv_obj_set_style_pad_all(root,0,0);lv_obj_set_style_border_width(root,0,0);lv_obj_set_style_bg_opa(root,LV_OPA_TRANSP,0);
    lv_obj_clear_flag(root,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    body_width=compact?23:64;body_height=compact?12:28;
    outline=lv_obj_create(root);lv_obj_set_pos(outline,0,1);lv_obj_set_size(outline,body_width,body_height);
    lv_obj_set_style_pad_all(outline,0,0);lv_obj_set_style_border_width(outline,1,0);lv_obj_set_style_radius(outline,3,0);lv_obj_set_style_bg_opa(outline,LV_OPA_TRANSP,0);
    lv_obj_clear_flag(outline,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(outline,LV_OBJ_FLAG_SCROLLABLE);
    auto *tip=lv_obj_create(root);lv_obj_set_pos(tip,body_width,body_height/3);lv_obj_set_size(tip,compact?2:4,body_height/3);
    lv_obj_set_style_border_width(tip,0,0);lv_obj_set_style_bg_color(tip,hmh_color(0x94A3B8),0);lv_obj_clear_flag(tip,LV_OBJ_FLAG_CLICKABLE);
    fill=lv_obj_create(outline);lv_obj_set_pos(fill,2,2);lv_obj_set_size(fill,1,body_height-4);lv_obj_set_style_border_width(fill,0,0);lv_obj_set_style_radius(fill,1,0);
    lv_obj_clear_flag(fill,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(fill,LV_OBJ_FLAG_SCROLLABLE);
    text=lv_label_create(root);lv_obj_set_pos(text,body_width+(compact?5:12),0);lv_obj_set_width(text,compact?37:148);
    lv_obj_set_style_text_font(text,compact?hmh_chip_text_14:hmh_runtime_text_20,0);lv_label_set_long_mode(text,LV_LABEL_LONG_DOT);lv_obj_clear_flag(text,LV_OBJ_FLAG_CLICKABLE);
  }
  static int band(float level,bool known,bool low) {
    if(!known) return -1;
    if(!std::isfinite(level)) return low?0:2;
    return level<=20?0:level<=40?1:2;
  }
  void update(float level,bool known,bool low) {
    int state=band(level,known,low);uint32_t color=state<0?0x64748B:state==0?0xEF4444:state==1?0xF59E0B:0x22C55E;
    lv_obj_set_style_border_color(outline,lv_color_hex(color),0);lv_obj_set_style_bg_color(fill,lv_color_hex(color),0);lv_obj_set_style_text_color(text,lv_color_hex(color),0);
    if(known && std::isfinite(level) && level>0) {lv_obj_clear_flag(fill,LV_OBJ_FLAG_HIDDEN);lv_obj_set_width(fill,std::max(1,static_cast<int>((body_width-4)*std::clamp(level,0.0f,100.0f)/100)));}
    else lv_obj_add_flag(fill,LV_OBJ_FLAG_HIDDEN);
    char value[24];if(known && std::isfinite(level)) std::snprintf(value,sizeof(value),"%.0f%%",static_cast<double>(level));
    else std::snprintf(value,sizeof(value),"%s",!known?"--":low?"Bassa":"OK");
    lv_label_set_text(text,value);
  }
};
// Compact, bounded user preferences; independent from the live HA catalog.
class HmhWidgetLayout {
 public:
  struct RoomLayout { std::string area; std::string order = "0123456789:"; unsigned collapsed = 0; };
  std::vector<RoomLayout> rooms;
  std::vector<std::string> entities;
  std::vector<std::string> closed_devices;
  bool device_closed(const std::string &key) const { return std::find(closed_devices.begin(),closed_devices.end(),key)!=closed_devices.end(); }
  std::function<void(const std::string &)> on_saved;
  RoomLayout &room(const std::string &area) {
    for (auto &r : rooms) if (r.area == area) return r;
    rooms.push_back({area}); return rooms.back();
  }
  bool collapsed(const std::string &area, int group) const {
    for (const auto &r : rooms) if (r.area == area) return (r.collapsed & (1u << group)) != 0;
    return false;
  }
  int category_rank(const std::string &area, int group) const {
    for (const auto &r : rooms) if (r.area == area) {
      auto i = r.order.find(static_cast<char>('0' + group));
      if (i != std::string::npos) return static_cast<int>(i);
    }
    return group;
  }
  size_t entity_rank(const std::string &id) const {
    for (size_t i = 0; i < entities.size(); ++i) if (entities[i] == id) return i;
    return entities.size();
  }
  void load(const std::string &payload) {
    if (payload.empty()) return;
    esphome::json::parse_json(payload, [&](JsonObject root) {
      if (!root["r"].is<JsonArray>() || !root["e"].is<JsonArray>()) return false;
      rooms.clear(); entities.clear(); closed_devices.clear();
      for(JsonVariant key:root["g"].as<JsonArray>()) if(key.is<const char *>()) closed_devices.emplace_back(key.as<const char *>());
      for (JsonArray item : root["r"].as<JsonArray>()) {
        if (!item[0].is<const char *>() || !item[1].is<const char *>()) continue;
        std::string order = item[1].as<const char *>(), sorted = order;
        std::sort(sorted.begin(), sorted.end());
        if (sorted == "01234567") { order += "8"; sorted += "8"; }
        if (sorted == "012345678") {order += "9";sorted += "9";}
        if(sorted=="0123456789") {order+=":";sorted+=":";}
        if (sorted != "0123456789:") continue;
        rooms.push_back({item[0].as<const char *>(), order, (item[2] | 0u) & 2047u});
      }
      for (JsonVariant id : root["e"].as<JsonArray>()) if (id.is<const char *>()) {
        std::string value = id.as<const char *>();
        if (std::find(entities.begin(), entities.end(), value) == entities.end()) entities.push_back(value);
      }
      return true;
    });
  }
  bool save() {
    JsonDocument doc;
    auto r = doc["r"].to<JsonArray>();
    for (const auto &room : rooms) { auto item = r.add<JsonArray>(); item.add(room.area); item.add(room.order); item.add(room.collapsed); }
    auto e = doc["e"].to<JsonArray>(); for (const auto &id : entities) e.add(id);
    auto g=doc["g"].to<JsonArray>(); for(const auto &key:closed_devices) g.add(key);
    if (measureJson(doc) >= 4096) return false;
    std::string payload; serializeJson(doc, payload);
    if (on_saved) on_saved(payload);
    return true;
  }
};
static HmhWidgetLayout hmh_widget_layout;

class RoomDevices {
 public:
  struct Entity {
    std::string id, area, name, domain, state, device_class, unit, device, device_name;
    std::string power_state, power_unit, weather_condition, chip_glyph;
    bool has_power = false, chip = false;
    bool has_battery=false,battery_known=false,battery_low=false,battery_only=false,battery_entity=false;float battery_level=NAN;
    std::vector<std::string> alarm_modes;std::string code_format;
    bool code_arm_required=true,code_disarm_required=false;
    std::vector<std::string> hvac_modes;
    std::string hvac_action;
    float current_temperature = NAN, temperature = NAN, low = NAN, high = NAN;
    float min_temp = 7, max_temp = 35, step = 0.5;
    float number_min=NAN,number_max=NAN,number_step=NAN;
    int group_section=-1;
    int percentage = -1;
    bool speed_supported = false, purifier = false;
    std::string pm25_state, pm25_unit;
    int position = -1;
    bool cover_open = false, cover_close = false, cover_stop = false;
    bool target_supported = false, range_supported = false;
    bool brightness_supported = false, color_supported = false;
    int brightness = 0, red = 255, green = 255, blue = 255;
  };
  struct Card {
    lv_obj_t *icon_tile = nullptr, *icon = nullptr, *rgb_dot = nullptr, *slider = nullptr;
    HmhBatteryWidget battery;
    std::vector<std::pair<lv_obj_t *,std::string>> alarm_buttons;
    std::string entity_id;
    bool uniform_composite = false;
    bool energy_composite = false;
    int width = 0;
    lv_obj_t *minus = nullptr, *plus = nullptr, *mode = nullptr, *target = nullptr;
    float requested = NAN;
    uint32_t requested_at = 0;
    lv_obj_t *room_label = nullptr, *pm25_label = nullptr, *speed_bar = nullptr;
    std::vector<std::pair<std::string, lv_obj_t *>> composite_metrics;
    std::vector<std::pair<std::string, lv_obj_t *>> composite_controls;
    std::vector<std::pair<std::string, lv_obj_t *>> composite_targets;
    lv_obj_t *gaia_scale = nullptr, *gaia_scale_caption = nullptr;
    lv_obj_t *obj = nullptr, *name = nullptr, *status = nullptr, *lamp = nullptr, *lamp_base = nullptr;
  };
  std::function<void(const std::string &, const std::string &)> on_command;

  std::function<void(const std::string &, float)> on_temperature;
  std::function<void(const std::string &, float, float)> on_temperature_range;
  std::function<void(const std::string &, const std::string &)> on_hvac_mode;
  std::function<void(const std::string &, const std::string &)> on_cover;
  std::function<void(const std::string &, int, const std::string &)> on_history;
  std::function<void(const std::string &, const std::string &, const std::string &)> on_alarm;
  std::function<void(const std::string &, float)> on_number;
  std::function<void(const std::string &, int)> on_fan_percentage;
  std::function<void(const std::string &, int)> on_brightness;
  std::function<void(const std::string &, int, int, int)> on_color;

  std::function<std::string(const std::string &)> area_name;
  bool matches_view(const Entity &e) const {
    if(area_.rfind("@battery:",0)==0) return e.battery_entity && (area_=="@battery:all" || e.area==area_.substr(9));
    if(e.battery_only) return false;
    if(e.domain=="weather" && area_.rfind("@category:",0)==0) return false;
    if (area_.rfind("@category:", 0) == 0)
      return section(e) == std::atoi(area_.c_str() + 10);
    return e.area == area_;
  }
  unsigned category_mask() const {
    unsigned mask = 0;
    for (const auto &e : entities_) if(e.domain!="weather" && !e.battery_only) mask |= 1u << section(e);
    return mask;
  }
  std::vector<std::string> battery_areas() const {
    std::vector<std::string> result;for(const auto &e:entities_) if(e.battery_entity && std::find(result.begin(),result.end(),e.area)==result.end()) result.push_back(e.area);
    std::sort(result.begin(),result.end());return result;
  }
  std::function<void(int,const std::string &)> on_energy;
  void load_energy(const std::string &payload) {
    esphome::json::parse_json(hmh_json(payload),[this](JsonObject root){
      if(std::string(root["request"] | "")!=energy_request_) return false;
      energy_pending_=false;energy_series_.clear();energy_hours_.clear();
      energy_date_=root["date"] | "";energy_note_=root["error"] | (root["note"] | "");
      for(JsonVariant hour:root["hours"].as<JsonArray>()) {if(energy_hours_.size()<25) energy_hours_.push_back(hour.as<std::string>());}
      for(JsonObject row:root["series"].as<JsonArray>()) {
        if(energy_series_.size()>=65) break;
        EnergySeries series;series.id=row["id"] | "";series.name=row["name"] | "";
        for(JsonVariant value:row["values"].as<JsonArray>()) {if(series.values.size()>=25) break;series.values.push_back(value.isNull()?-1:value.as<double>());}
        energy_series_.push_back(std::move(series));
      }
      energy_active_hours_=root["active"] | 25;energy_dirty_=true;if(area_=="@energy" && visible_) refresh_energy();return true;
    });
  }
  void poll_energy() {
    if(area_!="@energy" || !visible_) return;
    if(energy_pending_ && lv_tick_get()-energy_at_>60000) {energy_pending_=false;energy_note_="Nessuna risposta: verifica integrazione HA e Recorder";energy_dirty_=true;refresh_energy();}
    else if(!energy_pending_ && lv_tick_get()-energy_at_>300000) request_energy();
  }
  void load_history(const std::string &payload) {render_history(payload);}
  void poll_history() {if(history_pending_ && history_note_ && lv_tick_get()-history_started_>30000) {history_pending_=false;lv_label_set_text(history_note_,"Nessuna risposta: verifica integrazione HA e Recorder");}}
  bool detail_open() const { return dialog_ != nullptr; }
  bool chip_target(lv_obj_t *target) const {
    for(;target;target=lv_obj_get_parent(target)) if(target==chip_row_) return true;
    return false;
  }
  bool chip_at(const lv_point_t &point) const {
    if(!chip_row_ || lv_obj_has_flag(chip_row_,LV_OBJ_FLAG_HIDDEN)) return false;
    lv_area_t r;lv_obj_get_coords(chip_row_,&r);
    return point.x>=r.x1 && point.x<=r.x2 && point.y>=r.y1 && point.y<=r.y2;
  }
  bool dragging() const { return dragging_; }
  void cancel_drag() {
    if (!dragging_) return;
    lv_timer_delete(drag_timer_); drag_timer_ = nullptr;
    lv_obj_delete(drag_ghost_); drag_ghost_ = nullptr;
    restore_preview();
    lv_obj_delete(drop_marker_); drop_marker_ = nullptr;
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    dragging_ = false; layout_dirty_ = true; refresh(false);
  }
  void refresh_theme() { refresh(true); }
  void set_width(int width) {
    if (width == layout_width_) return;
    layout_width_ = width;
    layout_dirty_ = true;
    refresh(true);
  }
  void attach(lv_obj_t *list, lv_obj_t *hint) {
    list_ = list; hint_ = hint;
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
  }
  void set_area(const std::string &area) {
    bool changed = area_ != area;
    if (changed) close_detail();
    area_ = area;
    if(changed && area_=="@energy") request_energy();
    show(true);
    refresh(changed);
  }
  void show(bool visible) {
    visible_ = visible;
    if (!visible) close_detail();
    if (!list_) return;
    if(!visible && chip_row_) lv_obj_add_flag(chip_row_,LV_OBJ_FLAG_HIDDEN);
    if (visible) lv_obj_clear_flag(list_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
  }
  void set_online(bool online) {
    if (online_ == online) return;
    online_ = online;
    if(area_=="@energy") {if(online_) request_energy();else {energy_pending_=false;energy_note_="API non connessa";energy_dirty_=true;}}
    refresh(false);
  }
  void load(const std::string &payload) {
    if (!list_ || dragging_) return;
    const uint32_t perf_start = millis();
    const uint64_t signature = hmh_catalog_signature(payload);
    if (has_payload_ && payload.size() == payload_size_ && signature == payload_signature_) return;
    if (has_payload_ && !previous_payload_.empty() && entities_.size() > 0) {
      std::vector<std::string> states;
      if (hmh_state_only_delta(previous_payload_, payload, states) && states.size() == entities_.size()) {
        bool relevant = false;
        for (size_t i = 0; i < states.size(); ++i) {
          if (entities_[i].state == states[i]) continue;
          if (matches_view(entities_[i])) relevant = true;
          entities_[i].state = std::move(states[i]);
        }
        previous_payload_ = payload;
        payload_size_ = payload.size();
        payload_signature_ = signature;
        if (relevant) refresh(false);
        const uint32_t elapsed = millis() - perf_start;
        if (elapsed >= 25) ESP_LOGW("hmh_perf", "Delta stati: %u ms", static_cast<unsigned>(elapsed));
        return;
      }
    }
    std::vector<Entity, HmhRAMAllocator<Entity>> incoming;
    bool valid = esphome::json::parse_json(hmh_json(payload), [&](JsonObject root) -> bool {
      if (!root["entities"].is<JsonArray>()) return false;
      JsonArray entities_array = root["entities"].as<JsonArray>();
      // Reserve once in PSRAM, instead of repeatedly doubling an internal-RAM vector.
      const size_t count = entities_array.size();
      if (count > incoming.max_size() || count > incoming.get_allocator().get_max_free_block_size() / sizeof(Entity)) {
        ESP_LOGE("ha_dashboard", "Memoria insufficiente per %u entita", static_cast<unsigned>(count));
        return false;
      }
      incoming.reserve(count);
      for (JsonObject item : entities_array) {
        if (!item["id"].is<const char *>() || !item["area"].is<const char *>() ||
            !item["name"].is<const char *>() || !item["state"].is<const char *>()) return false;
        Entity e;
        e.id = item["id"].as<const char *>();
        e.area = item["area"].as<const char *>();
        e.name = item["name"].as<const char *>();
        e.state = item["state"].as<const char *>();
        e.code_format=item["code_format"] | "";e.code_arm_required=item["code_arm_required"] | true;e.code_disarm_required=item["code_disarm_required"] | false;
        JsonArray alarm_modes_array=item["alarm_modes"].as<JsonArray>();
        for(JsonVariant mode:alarm_modes_array) if(mode.is<const char *>()) e.alarm_modes.push_back(mode.as<const char *>());
        e.has_battery=item["has_battery"] | false;e.battery_known=item["battery_known"] | false;e.battery_low=item["battery_low"] | false;
        e.battery_only=item["battery_only"] | false;e.battery_entity=item["battery_entity"] | false;e.battery_level=item["battery_level"].is<float>()?item["battery_level"].as<float>():NAN;
        e.chip=item["chip"] | false;e.group_section=item["group_section"] | -1;
        e.weather_condition=item["condition"] | "";e.chip_glyph=item["glyph"] | "";
        e.device=item["device"] | ""; e.device_name=item["device_name"] | "";
        e.domain = e.id.substr(0, e.id.find('.'));
        if (item["device_class"].is<const char *>()) e.device_class = item["device_class"].as<const char *>();
        if (item["unit"].is<const char *>()) e.unit = item["unit"].as<const char *>();
        e.has_power = item["power_entity"].is<const char *>();
        if (item["power_state"].is<const char *>()) e.power_state = item["power_state"].as<const char *>();
        if (item["power_unit"].is<const char *>()) e.power_unit = item["power_unit"].as<const char *>();
        if (e.domain != "light" && e.domain != "switch" && e.domain != "input_boolean" &&
            e.domain != "binary_sensor" && e.domain != "button" && e.domain != "sensor" && e.domain != "climate" && e.domain != "cover" && e.domain != "fan" && e.domain != "alarm_control_panel" && e.domain != "number" && e.domain != "input_number" && !(e.domain=="weather" && e.chip)) continue;
        for (const auto &existing : incoming) if (existing.id == e.id) return false;
        e.current_temperature = item["current_temperature"].is<float>() ? item["current_temperature"].as<float>() : NAN;
        e.temperature = item["temperature"].is<float>() ? item["temperature"].as<float>() : NAN;
        e.low = item["target_temp_low"].is<float>() ? item["target_temp_low"].as<float>() : NAN;
        e.high = item["target_temp_high"].is<float>() ? item["target_temp_high"].as<float>() : NAN;
        e.min_temp = item["min_temp"] | 7.0f; e.max_temp = item["max_temp"] | 35.0f;
        e.step = item["target_temp_step"] | 0.5f;
        if (!std::isfinite(e.step) || e.step <= 0) e.step = 0.5;
        e.position = item["position"].is<float>() ? static_cast<int>(item["position"].as<float>()) : -1;
        e.cover_open = item["cover_open"] | false; e.cover_close = item["cover_close"] | false; e.cover_stop = item["cover_stop"] | false;
        e.target_supported = item["target_supported"] | false;
        e.range_supported = item["range_supported"] | false;
        if (item["hvac_action"].is<const char *>()) e.hvac_action = item["hvac_action"].as<const char *>();
        if (item["hvac_modes"].is<JsonArray>()) {
          JsonArray modes = item["hvac_modes"].as<JsonArray>();
          for (JsonVariant mode : modes) if (mode.is<const char *>()) e.hvac_modes.push_back(mode.as<const char *>());
        }
        e.number_min=item["number_min"].is<float>()?item["number_min"].as<float>():NAN;
        e.number_max=item["number_max"].is<float>()?item["number_max"].as<float>():NAN;
        e.number_step=item["number_step"].is<float>()?item["number_step"].as<float>():NAN;
        e.purifier=item["purifier"] | false;e.pm25_state=item["pm25_state"] | "";e.pm25_unit=item["pm25_unit"] | "";
        e.percentage = item["percentage"].is<float>() ? static_cast<int>(std::round(item["percentage"].as<float>())) : -1;
        e.percentage = e.percentage < 0 ? -1 : std::min(e.percentage, 100);
        e.speed_supported = item["speed_supported"] | false;
        e.brightness_supported = item["brightness_supported"] | false;
        e.color_supported = item["color_supported"] | false;
        e.brightness = item["brightness"] | 0;
        if (item["rgb"].is<JsonArray>() && item["rgb"].as<JsonArray>().size() >= 3) {
          e.red = item["rgb"][0] | 255;
          e.green = item["rgb"][1] | 255;
          e.blue = item["rgb"][2] | 255;
        }
        incoming.push_back(std::move(e));
      }
      return true;
    });
    if (!valid) {
      if (visible_) lv_label_set_text(hint_, "Catalogo entita' non valido: controlla il package HA.");
      return;
    }
    const uint32_t perf_parsed = millis();
    entities_ = std::move(incoming);
    previous_payload_ = payload;
    payload_signature_ = signature; payload_size_ = payload.size(); has_payload_ = true;
    received_ = true;
    refresh(false);
    const uint32_t perf_total = millis() - perf_start;
    if (perf_total >= 25) {
      ESP_LOGW("hmh_perf", "Catalogo: parsing=%u ms, vista=%u ms, totale=%u ms, entita=%u",
               static_cast<unsigned>(perf_parsed - perf_start),
               static_cast<unsigned>(millis() - perf_parsed),
               static_cast<unsigned>(perf_total),
               static_cast<unsigned>(entities_.size()));
    }
  }

 private:
  struct Heading { int group; lv_obj_t *obj; };
  struct DeviceHeading { int group; std::string key; lv_obj_t *obj; };
  struct DeviceBlock { std::string key; int group; std::vector<const Entity *> items; bool grouped=false; };
  std::vector<DeviceHeading> device_headings_;
  std::vector<DeviceBlock> device_blocks_;
  std::string drag_device_,drop_device_,pending_device_,groups_signature_;
  static std::string device_key(const Entity &e) {
    if(e.device.empty()) return "";
    return e.area+"/"+e.device;
  }
  std::string block_key(const Entity &e) const {
    auto key=device_key(e);
    for(const auto &block:device_blocks_) if(block.grouped && block.key==key) return key;
    return "@"+e.id;
  }
  std::vector<Heading> headings_;
  bool dragging_ = false, drag_category_ = false, drop_after_ = false;
  int drag_group_ = -1, drop_group_ = -1, pending_group_ = -1;
  std::string drag_entity_, drop_entity_;
  lv_obj_t *drag_ghost_ = nullptr, *drop_marker_ = nullptr;
  std::vector<std::pair<lv_obj_t *,lv_point_t>> preview_original_;
  std::string preview_target_;
  bool preview_after_=false;
  void restore_preview() {
    for(const auto &entry:preview_original_)
      lv_obj_set_pos(entry.first,entry.second.x,entry.second.y);
    preview_original_.clear();preview_target_.clear();
  }
  void preview_reflow() {
    if(drag_category_ || !drag_device_.empty() || drag_entity_.empty() || drop_entity_.empty()) return;
    if(preview_target_==drop_entity_ && preview_after_==drop_after_) return;
    restore_preview();
    std::vector<Card *> ordered;
    for(auto &card:cards_) {
      auto *e=find(card.entity_id);
      if(e && section(*e)==drag_group_) ordered.push_back(&card);
    }
    auto source=std::find_if(ordered.begin(),ordered.end(),[&](const Card *card){return card->entity_id==drag_entity_;});
    auto target=std::find_if(ordered.begin(),ordered.end(),[&](const Card *card){return card->entity_id==drop_entity_;});
    if(source==ordered.end() || target==ordered.end() || ordered.size()<2) return;
    // Preserve the actual current positions for a reversible, commit-free preview.
    std::sort(ordered.begin(),ordered.end(),[](const Card *a,const Card *b){
      const int ay=lv_obj_get_y(a->obj),by=lv_obj_get_y(b->obj);
      return ay==by?lv_obj_get_x(a->obj)<lv_obj_get_x(b->obj):ay<by;
    });
    const int y0=lv_obj_get_y(ordered.front()->obj);
    for(auto *card:ordered)
      preview_original_.push_back({card->obj,{static_cast<lv_coord_t>(lv_obj_get_x(card->obj)),static_cast<lv_coord_t>(lv_obj_get_y(card->obj))}});
    auto it=std::find_if(ordered.begin(),ordered.end(),[&](const Card *card){return card->entity_id==drag_entity_;});
    Card *moving=*it;ordered.erase(it);
    auto dest=std::find_if(ordered.begin(),ordered.end(),[&](const Card *card){return card->entity_id==drop_entity_;});
    if(dest!=ordered.end() && drop_after_) ++dest;
    ordered.insert(dest,moving);
    const HmhLayout layout=HmhLayout::current();
    const int gap=layout.size(8),inner_width=std::max(1,layout_width_-layout.size(14));
    const int cols=std::clamp((inner_width+gap)/(layout.size(255)+gap),1,3)*2;
    const int tile_height=layout.size(40);
    const int cw=std::max(1,(inner_width-(cols-1)*gap)/cols);
    HmhTileGrid grid(inner_width,cols,tile_height,gap);grid.reset(y0);
    for(auto *card:ordered) {
      const int w=lv_obj_get_width(card->obj),h=lv_obj_get_height(card->obj);
      auto place=grid.place(std::clamp((w+gap)/(cw+gap),1,cols),
                           std::max(1,(h+gap)/(tile_height+gap)));
      lv_obj_set_pos(card->obj,place.x,place.y);
    }
    preview_target_=drop_entity_;preview_after_=drop_after_;
  }
  lv_timer_t *drag_timer_ = nullptr;
  lv_indev_t *drag_input_ = nullptr;
  static void layout_event(lv_event_t *event) {
    auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(event));
    auto *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
    auto code = lv_event_get_code(event);
    if (code == LV_EVENT_LONG_PRESSED && !self->dragging_ && !hmh_navigation_blocked) {
      for(const auto &h:self->device_headings_) if(h.obj==target) { self->start_drag(h.group,"",h.key); return; }
    for (const auto &h : self->headings_) if (h.obj == target) { self->start_drag(h.group, ""); return; }
      for (const auto &card : self->cards_) if (card.name == target || card.icon_tile == target) {
        const Entity *e = self->find(card.entity_id);
        if (e) self->start_drag(section(*e), e->id);
        return;
      }
    }
    if (code != LV_EVENT_CLICKED || self->dragging_ || hmh_navigation_blocked) return;
    for(const auto &card:self->cards_) if(card.name==target || card.icon_tile==target) {
      const auto *e=self->find(card.entity_id);if(e && e->domain=="sensor") {self->open_history(*e);return;}
    }
      for(const auto &h:self->device_headings_) if(h.obj==target) {
      self->pending_device_=h.key;
      lv_async_call([](void *data) {
        auto *d=static_cast<RoomDevices *>(data);
        auto previous=hmh_widget_layout.closed_devices;
        auto &closed=hmh_widget_layout.closed_devices;
        auto found=std::find(closed.begin(),closed.end(),d->pending_device_);
        if(found==closed.end()) closed.push_back(d->pending_device_); else closed.erase(found);
        bool saved=hmh_widget_layout.save();
        if(!saved) closed=std::move(previous);
        d->layout_dirty_=true;d->refresh(false);
        if(!saved) lv_label_set_text(d->hint_,"Spazio impostazioni esaurito: modifica non salvata.");
      },self);return;
    }
    for (const auto &h : self->headings_) if (h.obj == target) {
      self->pending_group_ = h.group;
      lv_async_call([](void *data) {
        auto *d = static_cast<RoomDevices *>(data);
        auto previous = hmh_widget_layout.rooms;
        hmh_widget_layout.room(d->area_).collapsed ^= 1u << d->pending_group_;
        bool saved = hmh_widget_layout.save();
        if (!saved) hmh_widget_layout.rooms = std::move(previous);
        d->layout_dirty_ = true; d->refresh(false);
        if (!saved) lv_label_set_text(d->hint_, "Spazio impostazioni esaurito: modifica non salvata.");
      }, self);
      return;
    }
    for (const auto &card : self->cards_) if (card.name == target || card.icon_tile == target) {
      self->command(card.obj); return;
    }
  }
  void start_drag(int group, const std::string &entity, const std::string &device="") {
    if (!list_ || dialog_ || dragging_) return;
    drag_input_ = lv_indev_active();
    if (!drag_input_) return;
    dragging_ = true; drag_group_ = group; drag_entity_ = entity;
    drag_device_=device;drop_device_=device;
    drag_category_ = entity.empty() && device.empty(); drop_group_ = group; drop_entity_ = entity;
    hmh_navigation_blocked = true;
    lv_indev_wait_release(drag_input_);
    lv_obj_clear_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    drag_ghost_ = lv_obj_create(lv_layer_top());
    const Card *drag_card=nullptr;
    for(const auto &card:cards_) if(card.entity_id==entity) {drag_card=&card;break;}
    lv_obj_set_size(drag_ghost_,drag_card?drag_card->width:240,
        drag_card?lv_obj_get_height(drag_card->obj):52);
    lv_obj_set_style_pad_all(drag_ghost_, 8, 0);
    lv_obj_set_style_radius(drag_ghost_, 12, 0);
    lv_obj_set_style_bg_color(drag_ghost_, hmh_color(0x10273D), 0);
    lv_obj_set_style_bg_opa(drag_ghost_,LV_OPA_70,0);
    lv_obj_set_style_border_width(drag_ghost_, 2, 0);
    lv_obj_set_style_border_color(drag_ghost_, lv_color_hex(0x22D3EE), 0);
    lv_obj_clear_flag(drag_ghost_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(drag_ghost_, LV_OBJ_FLAG_SCROLLABLE);
    const Entity *e = find(entity);
    auto *caption = make_label(drag_ghost_, 0, 0, 218, e ? e->name.c_str() : device.empty() ? "Sposta categoria" : "Sposta dispositivo");
    lv_obj_set_style_text_color(caption, lv_color_hex(0xFFFFFF), 0);
    drop_marker_ = lv_obj_create(list_);
    lv_obj_set_size(drop_marker_, (drag_category_ || !drag_device_.empty()) ? layout_width_ - 20 : (layout_width_ - 26) / 2, 4);
    lv_obj_set_style_bg_color(drop_marker_, lv_color_hex(0x22D3EE), 0);
    lv_obj_set_style_bg_opa(drop_marker_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(drop_marker_, 0, 0);
    lv_obj_clear_flag(drop_marker_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(drop_marker_, LV_OBJ_FLAG_SCROLLABLE);
    drag_timer_ = lv_timer_create([](lv_timer_t *timer) {
      auto *self = static_cast<RoomDevices *>(lv_timer_get_user_data(timer));
      if (lv_indev_get_state(self->drag_input_) == LV_INDEV_STATE_RELEASED) { self->update_drag(); self->finish_drag(); }
      else self->update_drag();
    }, 30, this);
    update_drag();
  }
  void update_drag() {
    lv_point_t point; lv_indev_get_point(drag_input_, &point);
    lv_obj_set_pos(drag_ghost_,
        std::clamp(static_cast<int>(point.x)-lv_obj_get_width(drag_ghost_)/2,0,
          std::max(0,800-static_cast<int>(lv_obj_get_width(drag_ghost_)))),
        std::clamp(static_cast<int>(point.y)-44,0,428));
    lv_area_t bounds; lv_obj_get_coords(list_, &bounds);
    if (point.y < bounds.y1 + 35) lv_obj_scroll_by(list_, 0, 10, LV_ANIM_OFF);
    else if (point.y > bounds.y2 - 35) lv_obj_scroll_by(list_, 0, -10, LV_ANIM_OFF);
    lv_obj_update_layout(list_);
    lv_obj_t *nearest = nullptr;
    long best = 0x7FFFFFFF;
    if (drag_category_) {
      for (const auto &h : headings_) {
        lv_area_t r; lv_obj_get_coords(h.obj, &r);
        long distance = std::abs(static_cast<int>(point.y) - (r.y1 + r.y2) / 2);
        if (distance < best) { best = distance; nearest = h.obj; drop_group_ = h.group; drop_after_ = point.y > (r.y1 + r.y2) / 2; }
      }
    } else if(!drag_device_.empty()) {
      for(const auto &h:device_headings_) if(h.group==drag_group_) {
        lv_area_t r;lv_obj_get_coords(h.obj,&r);
        long distance=std::abs(static_cast<int>(point.y)-(r.y1+r.y2)/2);
        if(distance<best) {best=distance;nearest=h.obj;drop_device_=h.key;drop_after_=point.y>(r.y1+r.y2)/2;}
      }
      for(const auto &card:cards_) {
        const Entity *e=find(card.entity_id);if(!e || section(*e)!=drag_group_) continue;
        auto key=block_key(*e);if(key[0]!='@') continue;
        lv_area_t r;lv_obj_get_coords(card.obj,&r);
        long distance=std::abs(static_cast<int>(point.y)-(r.y1+r.y2)/2);
        if(distance<best) {best=distance;nearest=card.obj;drop_device_=key;drop_after_=point.y>(r.y1+r.y2)/2;}
      }
    } else {
      for (const auto &card : cards_) {
        const Entity *e = find(card.entity_id);
        if (!e || section(*e) != drag_group_) continue;
        const auto *source=find(drag_entity_);
        if(!source) continue;
        auto source_key=block_key(*source),target_key=block_key(*e);
        if(source_key[0]!='@' || target_key[0]!='@') { if(source_key!=target_key) continue; }
        lv_area_t r; lv_obj_get_coords(card.obj, &r);
        long dx = point.x - (r.x1 + r.x2) / 2, dy = point.y - (r.y1 + r.y2) / 2;
        long distance = dx * dx + dy * dy;
        if (distance < best) {
          best = distance; nearest = card.obj; drop_entity_ = e->id;
          drop_after_ = std::abs(dy) > (r.y2 - r.y1) / 3 ? dy > 0 : dx > 0;
        }
      }
    }
    if(nearest && !drag_category_ && drag_device_.empty()) {
      preview_reflow();
      lv_obj_add_flag(drop_marker_,LV_OBJ_FLAG_HIDDEN);
    } else if(nearest) {
      lv_obj_clear_flag(drop_marker_,LV_OBJ_FLAG_HIDDEN);
      int marker_y=lv_obj_get_y(nearest)+(drop_after_?lv_obj_get_height(nearest):-4);
      if(!drag_device_.empty() && drop_after_) {
        for(const auto &card:cards_) {
          const auto *e=find(card.entity_id);
          if(e && block_key(*e)==drop_device_) marker_y=std::max(marker_y,static_cast<int>(lv_obj_get_y(card.obj)+lv_obj_get_height(card.obj)));
        }
      }
      lv_obj_set_pos(drop_marker_,lv_obj_get_x(nearest),marker_y);
      lv_obj_move_foreground(drop_marker_);
    }
  }
  void finish_drag() {
    auto previous_rooms = hmh_widget_layout.rooms;
    auto previous_entities = hmh_widget_layout.entities;
    // Normalize this view's category into contiguous blocks before moving a sensor.
    // This keeps a device in place even when its first sensor changes position.
    if(!drag_category_) {
      std::vector<std::string> visible;
      for(const auto &block:device_blocks_) if(block.group==drag_group_)
        for(const auto *e:block.items) visible.push_back(e->id);
      auto &order=hmh_widget_layout.entities;
      for(const auto &id:visible) if(std::find(order.begin(),order.end(),id)==order.end()) order.push_back(id);
      size_t index=0;
      for(auto &id:order) if(std::find(visible.begin(),visible.end(),id)!=visible.end()) id=visible[index++];
    }
    if (drag_category_ && drag_group_ != drop_group_) {
      auto &order = hmh_widget_layout.room(area_).order;
      char source = static_cast<char>('0' + drag_group_), target = static_cast<char>('0' + drop_group_);
      order.erase(std::remove(order.begin(), order.end(), source), order.end());
      auto position = order.find(target);
      order.insert(position + (drop_after_ ? 1 : 0), 1, source);
    } else if(!drag_device_.empty() && drag_device_!=drop_device_) {
      const DeviceBlock *source=nullptr,*target=nullptr;
      for(const auto &block:device_blocks_) {
        if(block.key==drag_device_) source=&block;
        if(block.key==drop_device_) target=&block;
      }
      if(source && target) {
        auto &order=hmh_widget_layout.entities;
        // Seed in the current visible block order before moving the whole device.
        for(const auto &block:device_blocks_) for(const auto *e:block.items)
          if(std::find(order.begin(),order.end(),e->id)==order.end()) order.push_back(e->id);
        for(const auto *e:source->items) order.erase(std::remove(order.begin(),order.end(),e->id),order.end());
        size_t position=drop_after_?0:order.size();
        for(const auto *e:target->items) {
          auto found=std::find(order.begin(),order.end(),e->id);
          if(found!=order.end()) {
            size_t at=std::distance(order.begin(),found);
            position=drop_after_?std::max(position,at+1):std::min(position,at);
          }
        }
        for(const auto *e:source->items) order.insert(order.begin()+position++,e->id);
      }
    } else if (!drag_category_ && drag_device_.empty() && drag_entity_ != drop_entity_) {
      // Seed all entities in this category, preserving existing saved order.
      std::vector<const Entity *> group;
      for (const auto &e : entities_) if (section(e) == drag_group_) group.push_back(&e);
      std::stable_sort(group.begin(), group.end(), [](const Entity *a, const Entity *b) {
        return hmh_widget_layout.entity_rank(a->id) < hmh_widget_layout.entity_rank(b->id);
      });
      auto &order = hmh_widget_layout.entities;
      for (const auto *e : group) if (std::find(order.begin(), order.end(), e->id) == order.end()) order.push_back(e->id);
      order.erase(std::remove(order.begin(), order.end(), drag_entity_), order.end());
      auto position = std::find(order.begin(), order.end(), drop_entity_);
      if (position != order.end()) order.insert(position + (drop_after_ ? 1 : 0), drag_entity_);
    }
    bool saved = hmh_widget_layout.save();
    if (!saved) { hmh_widget_layout.rooms = std::move(previous_rooms); hmh_widget_layout.entities = std::move(previous_entities); }
    lv_timer_delete(drag_timer_); drag_timer_ = nullptr;
    lv_obj_delete(drag_ghost_); drag_ghost_ = nullptr;
    lv_obj_delete(drop_marker_); drop_marker_ = nullptr;
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    int scroll_y = lv_obj_get_scroll_y(list_);
    dragging_ = false; layout_dirty_ = true; refresh(false);
    lv_obj_scroll_to_y(list_, scroll_y, LV_ANIM_OFF);
    if (!saved) lv_label_set_text(hint_, "Spazio impostazioni esaurito: modifica non salvata.");
  }
  lv_obj_t *history_plot_=nullptr,*history_note_=nullptr,*history_value_=nullptr;
  std::vector<std::pair<lv_obj_t *,int>> history_period_buttons_;
  int history_hours_=0;uint32_t history_counter_=0,history_started_=0;bool history_pending_=false;
  std::string history_request_;
  std::vector<std::vector<lv_point_precise_t>> history_segments_;
  void request_history(int hours) {
    if(!dialog_ || !history_plot_) return;
    history_started_=lv_tick_get();history_pending_=true;
    for(const auto &button:history_period_buttons_) lv_obj_set_style_bg_color(button.first,hmh_color(button.second==hours?0x0891B2:0x334155),0);
    history_hours_=hours;history_request_=hmh_history_client+"-"+std::to_string(++history_counter_)+"-"+std::to_string(lv_tick_get());
    lv_obj_clean(history_plot_);history_segments_.clear();
    lv_label_set_text(history_note_,online_?"Caricamento storico da Home Assistant...":"API non connessa");
    if(online_ && on_history) on_history(detail_entity_,hours,history_request_);
  }
  void open_history(const Entity &e) {
    if(hmh_navigation_blocked || dragging_ || !on_history || e.domain!="sensor") return;
    std::string entity=e.id,name=e.name,value=status_text(e);close_detail();detail_entity_=entity;
    dialog_=lv_obj_create(lv_layer_top());lv_obj_set_pos(dialog_,0,0);lv_obj_set_size(dialog_,800,480);
    lv_obj_set_style_pad_all(dialog_,0,0);lv_obj_set_style_border_width(dialog_,0,0);lv_obj_set_style_bg_color(dialog_,hmh_color(0x111827),0);lv_obj_clear_flag(dialog_,LV_OBJ_FLAG_SCROLLABLE);
    auto *history_title=make_label(dialog_,24,16,670,name.c_str());lv_obj_set_style_text_color(history_title,hmh_color(0xEAF2FF),0);
    history_value_=make_label(dialog_,24,46,670,value.c_str());lv_obj_set_style_text_font(history_value_,hmh_runtime_text_28,0);lv_obj_set_style_text_color(history_value_,lv_color_hex(0x22D3EE),0);
    auto *close=detail_button(dialog_,730,12,48,40,"X");lv_obj_add_event_cb(close,[](lv_event_t *event){static_cast<RoomDevices *>(lv_event_get_user_data(event))->close_detail();},LV_EVENT_CLICKED,this);
    const int periods[]={1,24,168};const char *labels[]={"1 ora","24 ore","7 giorni"};
    for(int i=0;i<3;++i) {auto *button=detail_button(dialog_,24+i*150,90,138,36,labels[i]);
      history_period_buttons_.push_back({button,periods[i]});
      lv_obj_set_user_data(button,reinterpret_cast<void *>(static_cast<intptr_t>(periods[i])));
      lv_obj_add_event_cb(button,[](lv_event_t *event){auto *self=static_cast<RoomDevices *>(lv_event_get_user_data(event));auto *button=static_cast<lv_obj_t *>(lv_event_get_target(event));self->request_history(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button))));},LV_EVENT_CLICKED,this);
    }
    history_plot_=lv_obj_create(dialog_);lv_obj_set_pos(history_plot_,24,145);lv_obj_set_size(history_plot_,752,245);
    lv_obj_set_style_pad_all(history_plot_,0,0);lv_obj_set_style_border_width(history_plot_,0,0);lv_obj_set_style_bg_color(history_plot_,hmh_color(0x0B1220),0);lv_obj_clear_flag(history_plot_,LV_OBJ_FLAG_SCROLLABLE);
    history_note_=make_label(dialog_,24,402,752,"");lv_obj_set_style_text_color(history_note_,hmh_color(0xEAF2FF),0);lv_obj_set_height(history_note_,62);lv_label_set_long_mode(history_note_,LV_LABEL_LONG_WRAP);
    request_history(24);
  }
  void render_history(const std::string &payload) {
    if(!dialog_ || !history_hours_ || !history_plot_) return;
    esphome::json::parse_json(hmh_json(payload),[&](JsonObject root)->bool {
      if(std::string(root["request"] | "")!=history_request_ || std::string(root["id"] | "")!=detail_entity_) return false;
      history_pending_=false;
      lv_obj_clean(history_plot_);history_segments_.clear();
      if(root["error"].is<const char *>()) {lv_label_set_text(history_note_,root["error"].as<const char *>());return true;}
      JsonArray points=root["points"].as<JsonArray>();if(points.size()>200) return false;
      float minimum=INFINITY,maximum=-INFINITY;
      for(JsonArray point:points) if(point[1].is<float>()) {float value=point[1].as<float>();if(std::isfinite(value)) {minimum=std::min(minimum,value);maximum=std::max(maximum,value);}}
      if(!std::isfinite(minimum)) {lv_label_set_text(history_note_,"Nessun dato numerico nel periodo");return true;}
      float bottom=minimum,top=maximum;if(top==bottom) {bottom-=std::max(1.0f,std::fabs(bottom)*0.05f);top+=std::max(1.0f,std::fabs(top)*0.05f);}
      double duration=root["duration"] | static_cast<double>(history_hours_*3600);if(!std::isfinite(duration) || duration<=0) return false;
      history_segments_.emplace_back();
      for(JsonArray point:points) {
        if(!point[1].is<float>() || !std::isfinite(point[1].as<float>())) continue;
        double offset=point[0] | 0.0;float value=point[1].as<float>();if(!std::isfinite(offset)) continue;
        history_segments_.back().push_back({static_cast<lv_value_precise_t>(60+std::clamp(offset/duration,0.0,1.0)*676),static_cast<lv_value_precise_t>(12+(top-value)/(top-bottom)*204)});
      }
      for(int i=0;i<4;++i) {auto *grid=lv_obj_create(history_plot_);lv_obj_set_pos(grid,60,12+i*68);lv_obj_set_size(grid,676,1);lv_obj_set_style_border_width(grid,0,0);lv_obj_set_style_bg_color(grid,hmh_color(0x334155),0);}
      char bound[32];std::snprintf(bound,sizeof(bound),"%.2f",static_cast<double>(maximum));auto *upper=make_label(history_plot_,2,2,58,bound);lv_obj_set_style_text_color(upper,hmh_color(0xEAF2FF),0);std::snprintf(bound,sizeof(bound),"%.2f",static_cast<double>(minimum));auto *lower=make_label(history_plot_,2,205,58,bound);lv_obj_set_style_text_color(lower,hmh_color(0xEAF2FF),0);
      for(auto &segment:history_segments_) {
        if(segment.empty()) continue;
        if(segment.size()==1) {auto *dot=lv_obj_create(history_plot_);lv_obj_set_pos(dot,segment[0].x-3,segment[0].y-3);lv_obj_set_size(dot,6,6);lv_obj_set_style_border_width(dot,0,0);lv_obj_set_style_radius(dot,3,0);lv_obj_set_style_bg_color(dot,lv_color_hex(0x22D3EE),0);continue;}
        auto *line=lv_line_create(history_plot_);lv_line_set_points(line,segment.data(),segment.size());lv_obj_set_style_line_width(line,3,0);lv_obj_set_style_line_color(line,lv_color_hex(0x22D3EE),0);lv_obj_clear_flag(line,LV_OBJ_FLAG_CLICKABLE);
      }
      char summary[200];std::snprintf(summary,sizeof(summary),"%s  —  %s\nMin %.2f  |  Max %.2f  |  %u campioni",root["start"] | "",root["end"] | "",static_cast<double>(minimum),static_cast<double>(maximum),static_cast<unsigned>(points.size()));lv_label_set_text(history_note_,summary);return true;
    });
  }
  std::string alarm_code_,alarm_action_;
  lv_obj_t *alarm_pin_label_=nullptr;
  std::vector<std::pair<lv_obj_t *,std::string>> alarm_keys_;
  void wipe_alarm_code() {std::fill(alarm_code_.begin(),alarm_code_.end(),'\0');alarm_code_.clear();}
  static const char *alarm_action_name(const std::string &action) {
    if(action=="alarm_arm_home") return "Casa";
    if(action=="alarm_arm_away") return "Fuori";
    if(action=="alarm_arm_night") return "Notte";
    if(action=="alarm_arm_vacation") return "Vacanza";
    if(action=="alarm_arm_custom_bypass") return "Parziale";
    return "Disinserisci";
  }
  static bool alarm_needs_code(const Entity &e,const std::string &action) {
    return action=="alarm_disarm"?e.code_disarm_required:e.code_arm_required && !e.code_format.empty();
  }
  bool alarm_supported(const Entity &e,const std::string &action) const {
    return action=="alarm_disarm" || std::find(e.alarm_modes.begin(),e.alarm_modes.end(),action)!=e.alarm_modes.end();
  }
  void alarm_action(const Entity &e,const std::string &action) {
    if(hmh_navigation_blocked || !online_ || !available(e) || !on_alarm || !alarm_supported(e,action)) return;
    if(!alarm_needs_code(e,action)) {on_alarm(e.id,action,"");return;}
    if(e.code_format!="number") return;
    std::string entity=e.id;close_detail();detail_entity_=entity;alarm_action_=action;
    dialog_=lv_obj_create(lv_layer_top());lv_obj_set_pos(dialog_,0,0);lv_obj_set_size(dialog_,800,480);
    lv_obj_set_style_pad_all(dialog_,0,0);lv_obj_set_style_bg_color(dialog_,hmh_color(0x111827),0);lv_obj_clear_flag(dialog_,LV_OBJ_FLAG_SCROLLABLE);
    make_label(dialog_,220,16,360,(std::string(alarm_action_name(action))+" - PIN").c_str());
    alarm_pin_label_=make_label(dialog_,220,48,360,"");
    const char *tokens[]={"1","2","3","4","5","6","7","8","9","Cancella","0","Annulla"};
    for(int i=0;i<12;++i) {
      auto *key=detail_button(dialog_,220+(i%3)*122,82+(i/3)*70,114,60,tokens[i]);alarm_keys_.push_back({key,tokens[i]});
    }
    auto *confirm=detail_button(dialog_,220,372,358,60,"Conferma");alarm_keys_.push_back({confirm,"Conferma"});
    for(const auto &key:alarm_keys_) lv_obj_add_event_cb(key.first,[](lv_event_t *event){
      auto *self=static_cast<RoomDevices *>(lv_event_get_user_data(event));
      auto *target=static_cast<lv_obj_t *>(lv_event_get_target(event));
      std::string token;for(const auto &key:self->alarm_keys_) if(key.first==target) {token=key.second;break;}
      self->alarm_key(token);
    },LV_EVENT_CLICKED,this);
  }
  void alarm_key(const std::string &token) {
    if(hmh_navigation_blocked || !dialog_) return;
    if(token=="Annulla") {close_detail();return;}
    if(token=="Conferma") {
      const auto *e=find(detail_entity_);
      if(e && online_ && available(*e) && on_alarm && !alarm_code_.empty() && alarm_supported(*e,alarm_action_)) on_alarm(e->id,alarm_action_,alarm_code_);
      close_detail();return;
    }
    if(token=="Cancella") {if(!alarm_code_.empty()) {alarm_code_.back()='\0';alarm_code_.pop_back();}}
    else if(token.size()==1 && token[0]>='0' && token[0]<='9' && alarm_code_.size()<16) alarm_code_+=token;
    std::string masked(alarm_code_.size(),'*');lv_label_set_text(alarm_pin_label_,masked.c_str());
  }
  void close_detail() {
    if(history_plot_) lv_obj_clean(history_plot_);
    history_plot_=history_note_=history_value_=nullptr;history_request_.clear();history_segments_.clear();history_period_buttons_.clear();history_hours_=0;history_pending_=false;
    wipe_alarm_code();alarm_action_.clear();alarm_pin_label_=nullptr;alarm_keys_.clear();
    if (dialog_) lv_obj_del_async(dialog_);
    dialog_ = nullptr;
    detail_status_ = detail_toggle_ = brightness_slider_ = brightness_label_ = nullptr;
    for (int i = 0; i < 3; ++i) rgb_sliders_[i] = rgb_labels_[i] = nullptr;
    detail_entity_.clear();
    range_low_label_ = range_high_label_ = nullptr;
    climate_mode_buttons_.clear();
    detail_modes_.clear();
  }
  lv_obj_t *detail_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text) {
    auto *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, hmh_color(0x0891B2), 0);
    lv_obj_set_style_radius(button, 10, 0);
    auto *label = make_label(button, 0, 0, w - 16, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
  }
  lv_obj_t *detail_slider(lv_obj_t *parent, int y, int value, uint32_t color) {
    auto *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, 150, y);
    lv_obj_set_size(slider, 396, 16);
    lv_slider_set_range(slider, 0, 255);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, hmh_color(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, hmh_color(color), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, [](lv_event_t *event) {
      auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(event));
      self->detail_slider_event(static_cast<lv_obj_t *>(lv_event_get_target(event)), lv_event_get_code(event));
    }, LV_EVENT_ALL, this);
    return slider;
  }
  void update_slider_labels() {
    if (brightness_slider_) {
      std::string text = "Luminosita'\n" + std::to_string((lv_slider_get_value(brightness_slider_) * 100 + 127) / 255) + "%";
      lv_label_set_text(brightness_label_, text.c_str());
    }
    const char *names[] = {"Rosso", "Verde", "Blu"};
    for (int i = 0; i < 3; ++i) if (rgb_sliders_[i]) {
      std::string text = std::string(names[i]) + "\n" + std::to_string(lv_slider_get_value(rgb_sliders_[i]));
      lv_label_set_text(rgb_labels_[i], text.c_str());
    }
  }
  void detail_slider_event(lv_obj_t *target, lv_event_code_t code) {
    if (!dialog_) return;
    if (code == LV_EVENT_VALUE_CHANGED) update_slider_labels();
    if (code != LV_EVENT_RELEASED) return;
    const Entity *e = find(detail_entity_);
    if (!e || !online_ || !available(*e)) return;
    if (target == brightness_slider_ && e->brightness_supported && on_brightness) {
      on_brightness(e->id, lv_slider_get_value(brightness_slider_));
    } else if (e->color_supported && rgb_sliders_[0] && on_color) {
      for (int i = 0; i < 3; ++i) if (target == rgb_sliders_[i]) {
        on_color(e->id, lv_slider_get_value(rgb_sliders_[0]),
            lv_slider_get_value(rgb_sliders_[1]), lv_slider_get_value(rgb_sliders_[2]));
        break;
      }
    }
  }
  void sync_detail_status() {
    if (!dialog_) return;
    const Entity *e = find(detail_entity_);
    if(history_hours_) {
      if(!e || e->domain!="sensor") {close_detail();return;}
      lv_label_set_text(history_value_,status_text(*e).c_str());return;
    }
    if(!alarm_action_.empty()) {if(!e || !online_ || !available(*e)) close_detail();return;}
    if (e && e->domain == "climate") {
      if (detail_modes_ != e->hvac_modes) { close_detail(); return; }
      for (size_t i = 0; i < climate_mode_buttons_.size(); ++i) {
        lv_obj_set_style_bg_color(climate_mode_buttons_[i], hmh_color(detail_modes_[i] == e->state ? 0x0891B2 : 0x334155), 0);
        if (online_ && available(*e)) lv_obj_clear_state(climate_mode_buttons_[i], LV_STATE_DISABLED);
        else lv_obj_add_state(climate_mode_buttons_[i], LV_STATE_DISABLED);
      }
      lv_label_set_text(detail_status_, status_text(*e).c_str());
      if (range_low_label_) lv_label_set_text(range_low_label_, ("Min: " + temperature_text(e->low, e->unit)).c_str());
      if (range_high_label_) lv_label_set_text(range_high_label_, ("Max: " + temperature_text(e->high, e->unit)).c_str());
      return;
    }
    if (!e || e->domain != "light" ||
        e->brightness_supported != (brightness_slider_ != nullptr) ||
        e->color_supported != (rgb_sliders_[0] != nullptr)) {
      close_detail();
      return;
    }
    std::string text = "Stato HA: " + status_text(*e);
    if (!online_) text += " | API offline";
    lv_label_set_text(detail_status_, text.c_str());
    lv_label_set_text(lv_obj_get_child(detail_toggle_, 0), e->state == "on" ? "SPEGNI" : "ACCENDI");
    bool enabled = online_ && available(*e);
    if (enabled) lv_obj_clear_state(detail_toggle_, LV_STATE_DISABLED);
    else lv_obj_add_state(detail_toggle_, LV_STATE_DISABLED);
    if (brightness_slider_) {
      if (enabled) lv_obj_clear_state(brightness_slider_, LV_STATE_DISABLED);
      else lv_obj_add_state(brightness_slider_, LV_STATE_DISABLED);
      if (!lv_obj_has_state(brightness_slider_, LV_STATE_PRESSED))
        lv_slider_set_value(brightness_slider_, e->brightness, LV_ANIM_OFF);
    }
    int values[] = {e->red, e->green, e->blue};
    bool any_pressed = false;
    for (auto *slider : rgb_sliders_) if (slider && lv_obj_has_state(slider, LV_STATE_PRESSED)) any_pressed = true;
    for (int i = 0; i < 3; ++i) if (rgb_sliders_[i]) {
      if (enabled) lv_obj_clear_state(rgb_sliders_[i], LV_STATE_DISABLED);
      else lv_obj_add_state(rgb_sliders_[i], LV_STATE_DISABLED);
      if (!any_pressed) lv_slider_set_value(rgb_sliders_[i], values[i], LV_ANIM_OFF);
    }
    update_slider_labels();
  }
  void open_detail(const Entity &e) {
    close_detail();
    detail_entity_ = e.id;
    dialog_ = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(dialog_, 0, 0);
    lv_obj_set_size(dialog_, 800, 480);
    lv_obj_set_style_bg_color(dialog_, hmh_color(0x020617), 0);
    lv_obj_set_style_bg_opa(dialog_, LV_OPA_90, 0);
    lv_obj_set_style_radius(dialog_, 0, 0);
    lv_obj_set_style_pad_all(dialog_, 0, 0);
    lv_obj_set_style_border_width(dialog_, 0, 0);
    lv_obj_clear_flag(dialog_, LV_OBJ_FLAG_SCROLLABLE);
    auto *panel = lv_obj_create(dialog_);
    lv_obj_set_pos(panel, 110, 40);
    lv_obj_set_size(panel, 580, 400);
    lv_obj_set_style_bg_color(panel, hmh_color(0x111827), 0);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, hmh_color(0x22D3EE), 0);
    lv_obj_set_style_text_color(panel, hmh_color(0xEAF2FF), 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    auto *title = make_label(panel, 16, 16, 482, e.name.c_str());
    lv_obj_set_style_text_font(title, hmh_runtime_text_20, 0);
    auto *close = detail_button(panel, 522, 8, 44, 36, "X");
    lv_obj_add_event_cb(close, [](lv_event_t *event) {
      static_cast<RoomDevices *>(lv_event_get_user_data(event))->close_detail();
    }, LV_EVENT_CLICKED, this);
    detail_status_ = make_label(panel, 16, 50, 548, "Stato HA: ...");
    int row_y = 88;
    if (e.brightness_supported) {
      brightness_label_ = make_label(panel, 16, row_y - 10, 124, "Luminosita'");
      lv_label_set_long_mode(brightness_label_, LV_LABEL_LONG_WRAP);
      brightness_slider_ = detail_slider(panel, row_y, e.brightness, 0xFBBF24);
      row_y += 60;
    }
    if (e.color_supported) {
      uint32_t colors[] = {0xEF4444, 0x22C55E, 0x3B82F6};
      int values[] = {e.red, e.green, e.blue};
      for (int i = 0; i < 3; ++i) {
        rgb_labels_[i] = make_label(panel, 16, row_y - 10, 124, "Colore");
        lv_label_set_long_mode(rgb_labels_[i], LV_LABEL_LONG_WRAP);
        rgb_sliders_[i] = detail_slider(panel, row_y, values[i], colors[i]);
        row_y += 60;
      }
    }
    detail_toggle_ = detail_button(panel, 16, 342, 548, 42, "ON/OFF");
    lv_obj_add_event_cb(detail_toggle_, [](lv_event_t *event) {
      auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(event));
      const Entity *entity = self->find(self->detail_entity_);
      if (entity && self->online_ && available(*entity) && self->on_command) {
        self->on_command(entity->state == "on" ? "light.turn_off" : "light.turn_on", entity->id);
      }
    }, LV_EVENT_CLICKED, this);
    sync_detail_status();
  }

  static std::string temperature_text(float value, const std::string &unit) {
    if (!std::isfinite(value)) return "--";
    char buffer[32]; std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(value));
    std::string text(buffer);
    if (text.size() > 2 && text.substr(text.size() - 2) == ".0") text.resize(text.size() - 2);
    return text + (unit.empty() ? "" : " " + unit);
  }
  static std::string mode_name(const std::string &mode) {
    if (mode == "off") return "Spento";
    if (mode == "heat") return "Riscaldamento";
    if (mode == "cool") return "Raffrescamento";
    if (mode == "heat_cool") return "Caldo/freddo";
    if (mode == "auto") return "Auto";
    if (mode == "dry") return "Deumidifica";
    if (mode == "fan_only") return "Ventilazione";
    return mode;
  }
  void climate_card_action(lv_obj_t *target) {
    for (auto &card : cards_) {
      if (target != card.minus && target != card.plus && target != card.mode) continue;
      const auto *e = find(card.entity_id);
      if (!e || !online_ || !available(*e)) return;
      if (target == card.mode) { open_climate(*e); return; }
      if (!e->target_supported || !std::isfinite(e->temperature) || !on_temperature) return;
      float base = std::isfinite(card.requested) && lv_tick_get() - card.requested_at <= 5000 ? card.requested : e->temperature;
      float value = std::max(e->min_temp, std::min(e->max_temp, base + (target == card.plus ? e->step : -e->step)));
      if (std::fabs(value - base) < 0.001f) return;
      card.requested = value; card.requested_at = lv_tick_get();
      lv_label_set_text(card.target, (temperature_text(value, "") + "...").c_str());
      on_temperature(e->id, value);
      auto *timeout = lv_timer_create([](lv_timer_t *timer) {
        static_cast<RoomDevices *>(lv_timer_get_user_data(timer))->refresh(false);
      }, 5100, this);
      lv_timer_set_repeat_count(timeout, 1);
      return;
    }
  }
  void open_climate(const Entity &e) {
    close_detail(); detail_entity_ = e.id;
    detail_modes_ = e.hvac_modes;
    dialog_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(dialog_, 800, 480); lv_obj_set_pos(dialog_, 0, 0);
    lv_obj_set_style_bg_color(dialog_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dialog_, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(dialog_, 0, 0); lv_obj_set_style_border_width(dialog_, 0, 0);
    lv_obj_clear_flag(dialog_, LV_OBJ_FLAG_SCROLLABLE);
    auto *panel = lv_obj_create(dialog_); lv_obj_set_pos(panel, 90, 35); lv_obj_set_size(panel, 620, 410);
    lv_obj_set_style_pad_all(panel, 0, 0); lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_bg_color(panel, hmh_color(0x1E293B), 0);
    lv_obj_set_style_border_color(panel, hmh_color(0x334155), 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    make_label(panel, 16, 18, 520, e.name.c_str());
    auto *close = detail_button(panel, 558, 8, 44, 36, "X");
    lv_obj_add_event_cb(close, [](lv_event_t *ev) { static_cast<RoomDevices *>(lv_event_get_user_data(ev))->close_detail(); }, LV_EVENT_CLICKED, this);
    detail_status_ = make_label(panel, 16, 54, 588, status_text(e).c_str());
    int y = 98;
    if (e.range_supported && std::isfinite(e.low) && std::isfinite(e.high)) {
      range_low_label_ = make_label(panel, 16, y + 10, 310, ("Min: " + temperature_text(e.low, e.unit)).c_str());
      range_high_label_ = make_label(panel, 16, y + 58, 310, ("Max: " + temperature_text(e.high, e.unit)).c_str());
      for (int i = 0; i < 4; ++i) {
        auto *button = detail_button(panel, 400 + (i % 2) * 96, y + (i / 2) * 48, 84, 40, i % 2 ? "+" : "-");
        lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<size_t>(i)));
        lv_obj_add_event_cb(button, [](lv_event_t *ev) {
          auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(ev));
          const auto *entity = self->find(self->detail_entity_);
          if (!entity || !self->online_ || !available(*entity) || !self->on_temperature_range) return;
          size_t op = reinterpret_cast<size_t>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(ev))));
          float low = entity->low, high = entity->high;
          if (!std::isfinite(low) || !std::isfinite(high)) return;
          float delta = op % 2 ? entity->step : -entity->step;
          if (op < 2) low = std::max(entity->min_temp, std::min(high, low + delta));
          else high = std::min(entity->max_temp, std::max(low, high + delta));
          self->on_temperature_range(entity->id, low, high);
        }, LV_EVENT_CLICKED, this);
      }
      y += 106;
    } else if (e.target_supported && std::isfinite(e.temperature)) {
      make_label(panel, 16, y, 588, ("Impostata: " + temperature_text(e.temperature, e.unit)).c_str()); y += 42;
    }
    for (size_t i = 0; i < e.hvac_modes.size(); ++i) {
      auto *button = detail_button(panel, 16 + static_cast<int>(i % 3) * 198, y + static_cast<int>(i / 3) * 48, 186, 40, mode_name(e.hvac_modes[i]).c_str());
      climate_mode_buttons_.push_back(button);
      lv_obj_set_user_data(button, reinterpret_cast<void *>(i));
      lv_obj_set_style_bg_color(button, hmh_color(e.hvac_modes[i] == e.state ? 0x0891B2 : 0x334155), 0);
      lv_obj_add_event_cb(button, [](lv_event_t *ev) {
        auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(ev));
        const auto *entity = self->find(self->detail_entity_);
        size_t index = reinterpret_cast<size_t>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(ev))));
        if (entity && self->online_ && available(*entity) && index < entity->hvac_modes.size() && self->on_hvac_mode)
          self->on_hvac_mode(entity->id, entity->hvac_modes[index]);
      }, LV_EVENT_CLICKED, this);
    }
  }
  lv_obj_t *range_low_label_ = nullptr, *range_high_label_ = nullptr;
  std::vector<lv_obj_t *> climate_mode_buttons_;
  std::vector<std::string> detail_modes_;
  static const char *binary_icon(const Entity &e) {
    const bool on = e.state == "on";
    if (e.device_class == "window") return on ? "\xF3\xB0\x96\xB1" : "\xF3\xB0\x96\xAE";
    if (e.device_class == "door" || e.device_class == "opening" || e.device_class == "garage_door")
      return on ? "\xF3\xB0\xA0\x9C" : "\xF3\xB0\xA0\x9B";
    if (e.device_class == "motion") return "\xF3\xB0\xB6\x91";
    if (e.device_class == "occupancy" || e.device_class == "presence") return "\xF3\xB0\x80\x84";
    if (e.device_class == "battery") return "\xF3\xB0\x82\x83";
    if (e.device_class == "smoke" || e.device_class == "gas") return "\xF3\xB0\x8E\x92";
    if (e.device_class == "moisture") return "\xF3\xB1\x94\x82";
    return "\xF3\xB0\x88\x88";
  }
  static const char *cover_icon(const Entity &e) {
    bool opened = e.state != "closed";
    if (e.device_class == "shutter") return opened ? "\xF3\xB1\x84\x9E" : "\xF3\xB1\x84\x9C";
    if (e.device_class == "blind") return opened ? "\xF3\xB1\x80\x91" : "\xF3\xB0\x82\xAC";
    if (e.device_class == "shade") return opened ? "\xF3\xB1\xA9\xAB" : "\xF3\xB1\xA9\xAC";
    if (e.device_class == "curtain") return opened ? "\xF3\xB1\xA1\x86" : "\xF3\xB1\xA1\x87";
    if (e.device_class == "garage") return opened ? "\xF3\xB0\x9B\x9A" : "\xF3\xB0\x9B\x99";
    if (e.device_class == "gate") return opened ? "\xF3\xB1\x85\xAA" : "\xF3\xB0\x8A\x99";
    if (e.device_class == "door") return opened ? "\xF3\xB0\xA0\x9C" : "\xF3\xB0\xA0\x9B";
    if (e.device_class == "window") return opened ? "\xF3\xB0\x96\xB1" : "\xF3\xB0\x96\xAE";
    if (e.device_class == "awning") return opened ? "\xF3\xB1\xAE\x87" : "\xF3\xB1\xAE\x87";
    if (e.device_class == "damper") return opened ? "\xF3\xB1\x81\xA8" : "\xF3\xB1\x81\xA7";
    return opened ? "\xF3\xB1\x84\x9E" : "\xF3\xB1\x84\x9C";
  }
  static bool is_number(const Entity &e) {return e.domain=="number" || e.domain=="input_number";}
  static bool number_range(const Entity &e) {
    return is_number(e) && std::isfinite(e.number_min) && std::isfinite(e.number_max) && std::isfinite(e.number_step) && e.number_max>e.number_min && e.number_step>0;
  }
  static float number_value(const Entity &e) {
    char *end=nullptr;float value=std::strtof(e.state.c_str(),&end);
    return end && end!=e.state.c_str() && *end=='\0' && std::isfinite(value)?value:NAN;
  }
  static float number_from_slider(const Entity &e,int position) {
    double raw=e.number_min+(e.number_max-e.number_min)*(position/1000.0);
    double rounded=e.number_min+std::round((raw-e.number_min)/e.number_step)*e.number_step;
    return std::clamp(static_cast<float>(rounded),e.number_min,e.number_max);
  }
  static bool tall_card(const Entity &e) {return !is_contact(e) && (e.domain=="sensor" || e.domain=="binary_sensor" || e.domain=="light" || e.domain=="climate" || e.domain=="cover" || e.domain=="fan" || is_number(e));}
  static bool is_contact(const Entity &e) {return e.domain=="binary_sensor" && (e.device_class=="door" || e.device_class=="window" || e.device_class=="opening" || e.device_class=="garage_door");}
  static int section(const Entity &e) {
    if(e.group_section>=0 && e.group_section<=10) return e.group_section;
    if(e.domain=="alarm_control_panel") return 10;
    if(is_number(e)) return 3;
    if (e.domain == "binary_sensor" && (e.device_class == "door" || e.device_class == "window" ||
        e.device_class == "opening" || e.device_class == "garage_door")) return 8;
    if (e.domain == "fan") return e.purifier ? 9 : 7;
    if (e.domain == "light") return 0;
    if (e.domain == "switch" || e.domain == "input_boolean") return 1;
    if (e.domain == "climate") return 2;
    if (e.domain == "sensor" && (e.device_class == "power" || e.device_class == "energy")) return 5;
    if (e.domain == "binary_sensor" || e.domain == "sensor") return 3;
    if (e.domain == "cover") return 4;
    return 6;
  }
  static bool available(const Entity &e) {
    if (e.state == "unavailable") return false;
    if (e.domain == "cover") return e.state == "open" || e.state == "closed" || e.state == "opening" || e.state == "closing";
    if (e.domain == "climate" || e.domain=="alarm_control_panel") return !e.state.empty() && e.state != "unknown";
    if (e.domain == "sensor" || is_number(e)) {
      if (e.state.empty() || e.state == "unknown") return false;
      char *end = nullptr;
      float value = std::strtof(e.state.c_str(), &end);
      return end && end != e.state.c_str() && *end == '\0' && std::isfinite(value);
    }
    // Per button 'unknown' e' normale prima della prima pressione.
    return e.domain == "button" || e.state == "on" || e.state == "off";
  }
  static int sensor_decimals(const Entity &e) {
    if (e.device_class == "power" || e.device_class == "energy" ||
        e.device_class == "current" || e.device_class == "voltage") return 2;
    if (e.device_class == "temperature" || e.device_class == "humidity" || e.device_class == "wind_speed") return 1;
    return 2;
  }
  static const char *sensor_icon(const Entity &e) {
    if (e.device_class == "temperature") return "\xF3\xB0\x94\x8F";
    if (e.device_class == "humidity") return "\xF3\xB0\x96\x8E";
    if (e.device_class == "power") return "\xF3\xB1\x90\x8B";
    if (e.device_class == "energy") return "\xF3\xB0\x86\x99";
    if (e.device_class == "current") return "\xF3\xB1\x92\x80";
    if (e.device_class == "voltage") return "\xF3\xB0\x89\x81";
    if (e.device_class == "wind_speed") return "\xF3\xB0\x96\x9D";
    if (e.device_class == "pressure") return "\xF3\xB0\x8A\x9A";
    if (e.device_class == "atmospheric_pressure") return "\xF3\xB0\x8A\x9A";
    if (e.device_class == "battery") return "\xF3\xB0\x81\xB9";
    if (e.device_class == "illuminance") return "\xF3\xB0\x83\x9E";
    if (e.device_class == "carbon_dioxide") return "\xF3\xB0\x9F\xA4";
    return "\xF3\xB0\x8A\x9A";
  }
  static std::string status_text(const Entity &e) {
    if(e.domain=="alarm_control_panel") {
      if(e.state=="disarmed") return "Disinserito";
      if(e.state=="armed_home") return "Inserito in casa";
      if(e.state=="armed_away") return "Inserito fuori";
      if(e.state=="armed_night") return "Inserito notte";
      if(e.state=="armed_vacation") return "Inserito vacanza";
      if(e.state=="armed_custom_bypass") return "Inserito parziale";
      if(e.state=="arming") return "Inserimento...";
      if(e.state=="disarming") return "Disinserimento...";
      if(e.state=="pending") return "Ingresso in attesa";
      if(e.state=="triggered") return "ALLARME!";
      return "Non disponibile";
    }
    if(is_number(e)) {
      float value=number_value(e);if(!std::isfinite(value)) return "Non disponibile";
      char text[48];std::snprintf(text,sizeof(text),"%.2f",static_cast<double>(value));
      return std::string(text)+(e.unit.empty()?"":" "+e.unit);
    }
    if (e.domain == "fan") {
      if (!available(e)) return "Non disponibile";
      std::string text = e.state == "on" ? "Acceso" : "Spento";
      if (e.percentage >= 0 && e.speed_supported) text += " - " + std::to_string(e.state == "on" ? e.percentage : 0) + "%";
      return text;
    }
    if (e.state == "unavailable") return "Non disponibile";
    if (e.domain == "climate") return available(e) ? mode_name(e.state) + " - " + temperature_text(e.current_temperature, e.unit) : "Non disponibile";
    if (e.domain == "sensor") {
      if (!available(e)) return "Non disponibile";
      std::string value = e.state;
      char formatted[64];
      double numeric = std::strtod(e.state.c_str(), nullptr);
      int count = std::snprintf(formatted, sizeof(formatted), "%.*f", sensor_decimals(e), numeric);
      if (count >= 0 && static_cast<size_t>(count) < sizeof(formatted)) value = formatted;
      return value + (e.unit.empty() ? "" : " " + e.unit);
    }
    if (e.domain == "cover") {
      if (!available(e)) return "Non disponibile";
      std::string text = e.state == "closed" ? "Chiusa" : e.state == "opening" ? "In apertura" : e.state == "closing" ? "In chiusura" : "Aperta";
      if (e.position >= 0 && e.position <= 100) text += " - " + std::to_string(e.position) + "%";
      return text;
    }
    if (e.domain == "button") return "PREMI";
    if (e.state != "on" && e.state != "off") return "Stato sconosciuto";
    bool on = e.state == "on";
    if (e.domain == "binary_sensor") {
      const auto &c = e.device_class;
      if (c == "door" || c == "window" || c == "opening" || c == "garage_door") return on ? "Aperto" : "Chiuso";
      if (c == "motion" || c == "occupancy" || c == "presence") return on ? "Rilevato" : "Non rilevato";
      if (c == "connectivity") return on ? "Connesso" : "Disconnesso";
      if (c == "problem" || c == "smoke" || c == "gas" || c == "moisture" || c == "safety") return on ? "Allarme" : "Normale";
      if (c == "battery") return on ? "Batteria bassa" : "Batteria OK";
      return on ? "Attivo" : "Inattivo";
    }
    return on ? "ON" : "OFF";
  }
  const Entity *find(const std::string &entity_id) const {
    for (const auto &e : entities_) if (e.id == entity_id) return &e;
    return nullptr;
  }
  void command(lv_obj_t *target) {
    if (hmh_navigation_blocked) return;
    if (!online_) return;
    for (const auto &card : cards_) {
      if (card.obj != target) continue;
      const Entity *e = find(card.entity_id);
      if(e && e->domain=="sensor") {open_history(*e);return;}
      if (!e || !available(*e) || (e->domain == "binary_sensor" || e->domain == "sensor") || !on_command) return;
      if (e->domain == "cover" || e->domain=="alarm_control_panel" || is_number(*e)) return;
      if (e->domain == "climate") { open_climate(*e); return; }
      if (e->domain == "light" && (e->brightness_supported || e->color_supported)) {
        open_detail(*e);
        return;
      }
      // Usa lo stato reale HA: non imposta uno stato ottimistico sul display.
      std::string action = e->domain == "button" ? "button.press" :
          e->domain + (e->state == "on" ? ".turn_off" : ".turn_on");
      on_command(action, e->id);
      return;
    }
  }
  static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int width, const char *text) {
    auto *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, hmh_runtime_text_16, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    hmh_check_glyphs(hmh_runtime_text_16,text);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
  }
  // Lampadina disegnata con oggetti LVGL: non richiede font o immagini esterne.
  static lv_obj_t *lamp_part(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
    auto *part = lv_obj_create(parent);
    lv_obj_set_pos(part, x, y);
    lv_obj_set_size(part, w, h);
    lv_obj_set_style_radius(part, radius, 0);
    lv_obj_set_style_border_width(part, 0, 0);
    lv_obj_set_style_pad_all(part, 0, 0);
    lv_obj_clear_flag(part, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    return part;
  }
  struct Chip {std::string id;lv_obj_t *obj,*name,*value,*icon;HmhBatteryWidget battery;};
  lv_obj_t *chip_row_=nullptr;
  std::vector<Chip> chips_;
  static std::string chip_value(const Entity &e) {
    if(e.state.empty() || e.state=="unknown" || e.state=="unavailable") return "—";
    if(e.domain=="weather") {
      std::string value=e.weather_condition.empty()?e.state:e.weather_condition;
      if(std::isfinite(e.temperature)) value+=" · "+temperature_text(e.temperature,e.unit);
      return value;
    }
    if(e.domain=="light" || e.domain=="binary_sensor" || e.state=="on" || e.state=="off") return "";
    if(e.domain!="sensor") return status_text(e);
    char *end=nullptr;double number=std::strtod(e.state.c_str(),&end);
    if(end && end!=e.state.c_str() && *end=='\0') {
      if(!std::isfinite(number)) return "—";
      return status_text(e);
    }
    return e.state;
  }
  void refresh_chips(bool force) {
    std::vector<const Entity *> selected;
    for(const auto &e:entities_) if(e.chip && matches_view(e)) selected.push_back(&e);
    std::stable_sort(selected.begin(),selected.end(),[](const Entity *a,const Entity *b){return hmh_widget_layout.entity_rank(a->id)<hmh_widget_layout.entity_rank(b->id);});
    if(selected.empty()) {
      lv_obj_set_y(list_,76);lv_obj_set_height(list_,392);
      if(chip_row_) lv_obj_add_flag(chip_row_,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(hint_,LV_OBJ_FLAG_HIDDEN);return;
    }
    if(!chip_row_) {
      chip_row_=lv_obj_create(lv_obj_get_parent(list_));
      lv_obj_set_style_pad_all(chip_row_,0,0);lv_obj_set_style_border_width(chip_row_,0,0);
      lv_obj_set_style_bg_opa(chip_row_,LV_OPA_TRANSP,0);
      lv_obj_set_scroll_dir(chip_row_,LV_DIR_VER);lv_obj_set_scrollbar_mode(chip_row_,LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_set_pos(chip_row_,lv_obj_get_x(list_)+4,62);
    const int columns=layout_width_>600?3:2;
    const int rows=(selected.size()+columns-1)/columns;
    const int list_y=std::min(376,76+rows*46);
    lv_obj_set_y(list_,list_y);lv_obj_set_height(list_,480-list_y-12);
    lv_obj_set_size(chip_row_,layout_width_-8,std::min(rows*46,list_y-66));
    lv_obj_clear_flag(chip_row_,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(hint_,LV_OBJ_FLAG_HIDDEN);
    bool rebuild=force || selected.size()!=chips_.size();
    if(!rebuild) for(size_t i=0;i<selected.size();++i) if(selected[i]->id!=chips_[i].id || selected[i]->has_battery!=(chips_[i].battery.root!=nullptr)) {rebuild=true;break;}
    if(rebuild) {
      int scroll=lv_obj_get_scroll_x(chip_row_);lv_obj_clean(chip_row_);chips_.clear();
      for(size_t i=0;i<selected.size();++i) {
        const auto &e=*selected[i];Chip chip;chip.id=e.id;
        chip.obj=lv_btn_create(chip_row_);lv_obj_set_pos(chip.obj,(i%columns)*218,(i/columns)*46);lv_obj_set_size(chip.obj,210,40);
        lv_obj_set_style_pad_all(chip.obj,0,0);lv_obj_set_style_radius(chip.obj,18,0);
        lv_obj_set_style_border_width(chip.obj,1,0);lv_obj_clear_flag(chip.obj,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(chip.obj,LV_OBJ_FLAG_SCROLLABLE);
        chip.icon=make_label(chip.obj,6,6,30,"");lv_obj_set_style_text_font(chip.icon,hmh_mdi_font,0);
        lv_obj_set_style_text_align(chip.icon,LV_TEXT_ALIGN_CENTER,0);
        chip.name=make_label(chip.obj,42,2,158,e.name.c_str());
        lv_obj_set_style_text_font(chip.name,hmh_chip_text_14,0);
        chip.value=make_label(chip.obj,42,20,158,"");
        lv_obj_set_style_text_font(chip.value,hmh_chip_text_14,0);
        if(e.has_battery) {chip.battery.create(chip.obj,144,13,true);lv_obj_set_width(chip.name,96);lv_obj_set_width(chip.value,96);}
        lv_obj_add_event_cb(chip.obj,[](lv_event_t *event){
          auto *self=static_cast<RoomDevices *>(lv_event_get_user_data(event));
          if(hmh_navigation_blocked || !self->online_) return;
          auto *target=static_cast<lv_obj_t *>(lv_event_get_target(event));
          for(const auto &chip:self->chips_) if(chip.obj==target) {
            const auto *e=self->find(chip.id);
            if(e && e->domain=="light" && available(*e) && self->on_command) self->on_command("light.toggle",e->id);
            else if(e && e->domain=="sensor") self->open_history(*e);
            break;
          }
        },LV_EVENT_CLICKED,this);
        chips_.push_back(std::move(chip));
      }
      lv_obj_scroll_to_x(chip_row_,scroll,LV_ANIM_OFF);
    }
    lv_obj_update_layout(chip_row_);
    for(size_t i=0;i<selected.size();++i) {
      const auto &e=*selected[i];auto &chip=chips_[i];
      lv_label_set_text(chip.name,e.name.c_str());
      if(chip.battery.root) chip.battery.update(e.battery_level,e.battery_known,e.battery_low);
      std::string value=chip_value(e);lv_label_set_text(chip.value,value.c_str());
      if(value.empty()) {lv_obj_add_flag(chip.value,LV_OBJ_FLAG_HIDDEN);lv_obj_set_y(chip.name,12);}
      else {lv_obj_clear_flag(chip.value,LV_OBJ_FLAG_HIDDEN);lv_obj_set_y(chip.name,2);}
      lv_label_set_text(chip.icon,e.domain=="weather"?e.chip_glyph.c_str():e.domain=="light"?"\xF3\xB0\x8C\xB5":e.domain=="binary_sensor"?binary_icon(e):sensor_icon(e));
      bool light_available=online_ && ((e.domain=="light" && available(e)) || (e.domain=="sensor" && on_history));
      if(light_available) lv_obj_add_flag(chip.obj,LV_OBJ_FLAG_CLICKABLE);else lv_obj_clear_flag(chip.obj,LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_bg_color(chip.obj,hmh_color(0x1E293B),0);
      lv_obj_set_style_border_color(chip.obj,hmh_color(0x334155),0);
      lv_obj_set_style_text_color(chip.name,hmh_color(0x94A3B8),0);
      lv_obj_set_style_text_color(chip.value,hmh_color(0xEAF2FF),0);
      bool available=e.state!="unknown" && e.state!="unavailable" && !e.state.empty();
      lv_obj_set_style_text_color(chip.icon,available && ((e.domain!="binary_sensor" && e.domain!="light") || e.state=="on")?lv_color_hex(e.domain=="light"?0xFBBF24:0x22D3EE):hmh_color(0x64748B),0);
      if(is_contact(e)) {
        bool known=e.state=="on" || e.state=="off";
        lv_color_t color=known?lv_color_hex(e.state=="on"?0xF59E0B:0x22C55E):hmh_color(0x64748B);
        lv_obj_set_style_text_color(chip.icon,color,0);
        lv_obj_set_style_border_color(chip.obj,color,0);
      }
    }
  }
  struct BatteryCard {std::string id;lv_obj_t *obj,*name,*room;HmhBatteryWidget widget;};
  std::vector<BatteryCard> battery_cards_;bool battery_view_=false;
  struct EnergySeries {std::string id,name;std::vector<double> values;bool enabled=true;};
  std::vector<EnergySeries> energy_series_;
  std::vector<std::string> energy_hours_;
  std::string energy_request_,energy_date_,energy_note_;
  int energy_day_=0,energy_width_=0,energy_active_hours_=25;uint32_t energy_at_=0,energy_counter_=0;
  bool energy_pending_=false,energy_dirty_=true,energy_view_=false;
  void request_energy() {
    energy_at_=lv_tick_get();energy_pending_=online_ && static_cast<bool>(on_energy);
    energy_request_=hmh_history_client+"-energy-"+std::to_string(++energy_counter_)+"-"+std::to_string(energy_at_);
    energy_note_=energy_pending_?"Caricamento consumi da Home Assistant...":"API non connessa";
    energy_series_.clear();energy_date_.clear();energy_dirty_=true;
    if(energy_pending_) on_energy(energy_day_,energy_request_);
  }
  void refresh_energy() {
    if(!list_ || !visible_) return;
    if(chip_row_) lv_obj_add_flag(chip_row_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(list_,110);lv_obj_set_height(list_,358);lv_obj_set_y(hint_,78);
    if(energy_view_ && !energy_dirty_ && energy_width_==layout_width_) return;
    energy_view_=true;energy_dirty_=false;energy_width_=layout_width_;
    lv_obj_clean(list_);cards_.clear();headings_.clear();device_headings_.clear();battery_cards_.clear();battery_view_=false;
    lv_label_set_text(hint_,energy_note_.c_str());lv_obj_set_style_text_font(hint_,hmh_chip_text_14,0);
    lv_obj_clear_flag(hint_,LV_OBJ_FLAG_HIDDEN);
    const int width=layout_width_-8;
    auto label=[&](int x,int y,int w,const std::string &text) {auto *l=make_label(list_,x,y,w,text.c_str());lv_obj_set_style_text_font(l,hmh_chip_text_14,0);lv_obj_set_style_text_color(l,hmh_color(0xEAF2FF),0);return l;};
    label(92,7,width-240,energy_date_.empty()?(energy_day_==0?"Oggi":"Caricamento..."):energy_date_);
    for(int i=0;i<3;++i) {
      auto *button=detail_button(list_,i==0?0:i==1?width-138:width-70,0,i==0?80:62,32,i==0?"<":i==1?">":"Oggi");
      lv_obj_set_user_data(button,reinterpret_cast<void *>(static_cast<intptr_t>(i+1)));
      if(i==1 && energy_day_==0) lv_obj_add_state(button,LV_STATE_DISABLED);
      lv_obj_add_event_cb(button,[](lv_event_t *event){auto *self=static_cast<RoomDevices *>(lv_event_get_user_data(event));int action=static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(event)))));self->energy_day_=action==3?0:std::clamp(self->energy_day_+(action==1?-1:1),-30,0);self->request_energy();lv_async_call([](void *data){auto *self=static_cast<RoomDevices *>(data);if(self->area_=="@energy") self->refresh_energy();},self);},LV_EVENT_CLICKED,this);
    }
    const int plot_x=48,plot_y=64,plot_w=width-56,plot_h=170;
    double maximum=0,total=0;bool missing=false;
    for(size_t h=0;h<energy_hours_.size();++h) {double sum=0;for(const auto &series:energy_series_) if(series.enabled && h<series.values.size()) {double value=series.values[h];if(value>=0 && std::isfinite(value)) sum+=value;else if(static_cast<int>(h)<energy_active_hours_) missing=true;}maximum=std::max(maximum,sum);total+=sum;}
    maximum=maximum>0?maximum*1.12:1;
    char text[100];std::snprintf(text,sizeof(text),"%.2f kWh%s",total,missing?" · dati parziali":"");label(0,38,width,text);
    for(int j=0;j<4;++j) {int y=plot_y+j*plot_h/3;auto *grid=lv_obj_create(list_);lv_obj_set_pos(grid,plot_x,y);lv_obj_set_size(grid,plot_w,1);lv_obj_set_style_border_width(grid,0,0);lv_obj_set_style_bg_color(grid,hmh_color(0x334155),0);lv_obj_clear_flag(grid,LV_OBJ_FLAG_CLICKABLE);std::snprintf(text,sizeof(text),"%.2f",maximum*(3-j)/3);label(0,y-8,46,text);}
    static const uint32_t palette[]={0x06B6D4,0xF59E0B,0x8B5CF6,0x22C55E,0xEF4444,0xEC4899,0x3B82F6,0x84CC16,0xF97316,0x14B8A6,0xA78BFA,0xEAB308};
    int n=static_cast<int>(energy_hours_.size());
    for(int h=0;h<n;++h) {
      int top=plot_y+plot_h;double stacked=0;
      for(size_t j=0;j<energy_series_.size();++j) {const auto &series=energy_series_[j];if(!series.enabled || h>=static_cast<int>(series.values.size()) || series.values[h]<=0 || !std::isfinite(series.values[h])) continue;stacked+=series.values[h];int next=plot_y+plot_h-static_cast<int>(stacked/maximum*plot_h);int height=top-next;
        if(height>0) {auto *bar=lv_obj_create(list_);lv_obj_set_pos(bar,plot_x+h*plot_w/n+1,next);lv_obj_set_size(bar,std::max(2,plot_w/n-3),height);lv_obj_set_style_border_width(bar,0,0);lv_obj_set_style_radius(bar,0,0);lv_obj_set_style_bg_color(bar,lv_color_hex(series.id=="@unmonitored"?0x94A3B8:palette[j%12]),0);lv_obj_clear_flag(bar,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(bar,LV_OBJ_FLAG_SCROLLABLE);}top=next;
      }
      if(h%3==0) label(plot_x+h*plot_w/n,plot_y+plot_h+6,30,energy_hours_[h]);
    }
    for(size_t j=0;j<energy_series_.size();++j) {
      auto &series=energy_series_[j];int y=plot_y+plot_h+32+static_cast<int>(j/2)*36,x=static_cast<int>(j%2)*(width/2);
      auto *button=detail_button(list_,x,y,width/2-6,30,series.name.c_str());lv_obj_set_style_text_font(lv_obj_get_child(button,0),hmh_chip_text_14,0);lv_label_set_long_mode(lv_obj_get_child(button,0),LV_LABEL_LONG_DOT);lv_obj_set_style_bg_color(button,hmh_color(series.enabled?0x1E293B:0x0B1220),0);lv_obj_set_style_border_width(button,2,0);lv_obj_set_style_border_color(button,lv_color_hex(series.id=="@unmonitored"?0x94A3B8:palette[j%12]),0);lv_obj_set_user_data(button,reinterpret_cast<void *>(static_cast<intptr_t>(j+1)));
      lv_obj_add_event_cb(button,[](lv_event_t *event){auto *self=static_cast<RoomDevices *>(lv_event_get_user_data(event));size_t index=static_cast<size_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_target(event)))))-1;if(index<self->energy_series_.size()) {self->energy_series_[index].enabled=!self->energy_series_[index].enabled;self->energy_dirty_=true;lv_async_call([](void *data){auto *self=static_cast<RoomDevices *>(data);if(self->area_=="@energy") self->refresh_energy();},self);}},LV_EVENT_CLICKED,this);
    }
  }
  void refresh_batteries(bool force) {
    std::vector<const Entity *> items;for(const auto &e:entities_) if(matches_view(e) && e.battery_entity) items.push_back(&e);
    std::stable_sort(items.begin(),items.end(),[](const Entity *a,const Entity *b){
      if(a->area!=b->area) return a->area<b->area;
      int ba=HmhBatteryWidget::band(a->battery_level,a->battery_known,a->battery_low),bb=HmhBatteryWidget::band(b->battery_level,b->battery_known,b->battery_low);
      if(ba<0) ba=3;if(bb<0) bb=3;
      if(ba!=bb) return ba<bb;return a->name<b->name;
    });
    int counts[4]={};for(const auto *e:items) ++counts[HmhBatteryWidget::band(e->battery_level,e->battery_known,e->battery_low)+1];
    char totals[160];std::snprintf(totals,sizeof(totals),"Scariche: %d  |  Quasi scariche: %d  |  Cariche: %d  |  N/D: %d",counts[1],counts[2],counts[3],counts[0]);
    lv_obj_clear_flag(hint_,LV_OBJ_FLAG_HIDDEN);lv_label_set_text(hint_,totals);lv_obj_set_style_text_font(hint_,hmh_chip_text_14,0);
    if(chip_row_) lv_obj_add_flag(chip_row_,LV_OBJ_FLAG_HIDDEN);
    bool rebuild=force || !battery_view_ || battery_cards_.size()!=items.size();
    if(!rebuild) for(size_t i=0;i<items.size();++i) if(items[i]->id!=battery_cards_[i].id) {rebuild=true;break;}
    if(rebuild) {
      lv_obj_clean(list_);cards_.clear();headings_.clear();device_headings_.clear();battery_cards_.clear();
      int width=(layout_width_-26)/2;
      for(size_t i=0;i<items.size();++i) {
        const auto &e=*items[i];BatteryCard card;card.id=e.id;card.obj=lv_obj_create(list_);
        lv_obj_set_pos(card.obj,(i%2)*(width+12),(i/2)*126);lv_obj_set_size(card.obj,width,114);
        lv_obj_set_style_pad_all(card.obj,0,0);lv_obj_set_style_radius(card.obj,18,0);lv_obj_set_style_border_width(card.obj,1,0);
        lv_obj_clear_flag(card.obj,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(card.obj,LV_OBJ_FLAG_SCROLLABLE);
        card.name=make_label(card.obj,12,10,width-24,e.name.c_str());lv_obj_set_height(card.name,38);lv_label_set_long_mode(card.name,LV_LABEL_LONG_WRAP);
        card.room=make_label(card.obj,12,46,width-24,"");card.widget.create(card.obj,12,74,false);battery_cards_.push_back(card);
      }
    }
    battery_view_=true;lv_obj_update_layout(list_);
    for(size_t i=0;i<items.size();++i) {
      const auto &e=*items[i];auto &card=battery_cards_[i];lv_label_set_text(card.name,e.name.c_str());
      std::string room=area_name?area_name(e.area):e.area;lv_label_set_text(card.room,room.c_str());
      card.widget.update(e.battery_level,e.battery_known,e.battery_low);
      lv_obj_set_style_bg_color(card.obj,hmh_color(0x1E293B),0);lv_obj_set_style_border_color(card.obj,hmh_color(0x334155),0);
      lv_obj_set_style_text_color(card.name,hmh_color(0xEAF2FF),0);lv_obj_set_style_text_color(card.room,hmh_color(0x94A3B8),0);
    }
  }
  void refresh(bool force_rebuild) {
    if (!list_ || !visible_ || dragging_) return;
    if(area_=="@energy") {refresh_energy();return;}
    if(energy_view_) {energy_view_=false;lv_obj_set_y(hint_,128);force_rebuild=true;}
    if(area_.rfind("@battery:",0)==0) {lv_obj_set_y(list_,76);lv_obj_set_height(list_,392);refresh_batteries(force_rebuild);return;}
    if(battery_view_) {battery_cards_.clear();battery_view_=false;force_rebuild=true;}
    lv_obj_set_style_text_font(hint_,hmh_runtime_text_16,0);
    refresh_chips(force_rebuild);
    std::vector<const Entity *> selected;
    const bool categories = area_.rfind("@category:", 0) == 0;
    for (const auto &e : entities_)
      if (matches_view(e) && !e.chip) selected.push_back(&e);
    device_blocks_.clear();
    for(const auto *e:selected) {
      auto key=device_key(*e);if(key.empty()) key="@"+e->id;
      auto found=std::find_if(device_blocks_.begin(),device_blocks_.end(),[&](const DeviceBlock &b){return b.key==key;});
      if(found==device_blocks_.end()) {device_blocks_.push_back({key,section(*e),{e},false});}
      else found->items.push_back(e);
    }
    for(auto &block:device_blocks_) {
      block.grouped=block.key[0]!='@' && block.items.size()>1;
      if(!block.grouped) block.key="@"+block.items[0]->id;
      std::stable_sort(block.items.begin(),block.items.end(),[](const Entity *a,const Entity *b){
        return hmh_widget_layout.entity_rank(a->id)<hmh_widget_layout.entity_rank(b->id);
      });
    }
    // Gaia 0.8 pilot: one purifier card per real HA device. Only sensor
    // children join it; unrelated domains stay individually controllable.
    for(auto &block:device_blocks_) {
      auto primary=std::find_if(block.items.begin(),block.items.end(),
          [](const Entity *e){return e->domain=="fan" && e->purifier;});
      if(primary==block.items.end()) primary=std::find_if(block.items.begin(),block.items.end(),
          [](const Entity *e){return e->domain=="light" || e->domain=="switch" ||
              e->domain=="fan" || e->domain=="climate" || e->domain=="cover";});
      if(primary!=block.items.end() && block.grouped) {
        const Entity *fan=*primary;
        block.items.erase(primary);
        block.items.insert(block.items.begin(),fan);
        block.group=section(*fan);
      }
    }
    std::stable_sort(device_blocks_.begin(),device_blocks_.end(),[&](const DeviceBlock &a,const DeviceBlock &b){
      if(a.group!=b.group) return categories?a.group<b.group:
        hmh_widget_layout.category_rank(area_,a.group)<hmh_widget_layout.category_rank(area_,b.group);
      return hmh_widget_layout.entity_rank(a.items[0]->id)<hmh_widget_layout.entity_rank(b.items[0]->id);
    });
    selected.clear();std::string signature;
    for(const auto &block:device_blocks_) {
      signature+=block.key+"|"+block.items[0]->device_name+";";
      const bool composite=block.grouped && !block.items.empty();
      for(const auto *e:block.items) {
        // Sensor children are drawn inside the parent's card.
        if(!composite || e==block.items[0]) selected.push_back(e);
        signature+=e->id+"|"+e->name+"|"+e->device_class+"|"+e->unit+";";signature+=e->has_battery?"B":"N";
        for(const auto &mode:e->alarm_modes) signature+=mode+",";
      }
    }
    if(signature!=groups_signature_) {layout_dirty_=true;groups_signature_=signature;} // Device metric changes force a complete layout recalculation.
    auto all_selected = selected;
    selected.erase(std::remove_if(selected.begin(), selected.end(), [&](const Entity *e) {
      return (!categories && hmh_widget_layout.collapsed(area_,section(*e))) ||
        hmh_widget_layout.device_closed(block_key(*e));
    }),selected.end());
    bool rebuild = layout_dirty_ || force_rebuild || selected.size() != cards_.size();
    if (!rebuild) for (size_t i = 0; i < selected.size(); ++i) {
      if (selected[i]->id != cards_[i].entity_id) { rebuild = true; break; }
    }
    if (rebuild) {
      layout_dirty_ = false;
      lv_obj_clean(list_);
      cards_.clear();
      headings_.clear();device_headings_.clear();
      std::string previous_device;
      int layout_y = 0, previous_section = -1;
      const HmhLayout responsive = HmhLayout::current();
      const int gap = responsive.size(8);
      const int minimum_half_card_width = responsive.size(255);
      const int inner_width = std::max(1,layout_width_-responsive.size(14));
      // Each ordinary card spans two logical tiles. The grid grows in 2-tile
      // pairs as the screen becomes wider, while large widgets may span more.
      const int card_columns=std::clamp((inner_width+gap)/(minimum_half_card_width+gap),1,3);
      const int tile_columns=card_columns*2;
      HmhTileGrid tile_grid(inner_width,tile_columns,responsive.size(40),gap);
      tile_grid.reset(layout_y);
      for (size_t i = 0; i < all_selected.size(); ++i) {
        const auto &e = *all_selected[i];
        int group = section(e);
        if (group != previous_section) {
          layout_y = tile_grid.bottom();
          tile_grid.reset(layout_y);
          const char *title = group == 0 ? "LUCI" : group == 1 ? "INTERRUTTORI" : group == 2 ? "TERMOSTATI" : group == 3 ? "SENSORI" : group == 4 ? "APERTURE" : group == 5 ? "ENERGIA" : group == 7 ? "VENTILATORI" : group == 10 ? "ALLARME" : group == 9 ? "PURIFICATORI" : group == 8 ? "PORTE E FINESTRE" : "PULSANTI";
          size_t count = 0; for (const auto *item : all_selected) if (section(*item) == group) ++count;
          bool closed = !categories && hmh_widget_layout.collapsed(area_, group);
          std::string caption = std::string(categories ? "" : closed ? "+ " : "- ") + title + " · " + std::to_string(count);
          auto *heading = make_label(list_, 2, layout_y, layout_width_ - 18, caption.c_str());
          lv_obj_set_height(heading, 24);
          lv_obj_set_style_text_color(heading, hmh_color(0x22D3EE), 0);
          if (!categories) {
            lv_obj_add_flag(heading, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(heading, layout_event, LV_EVENT_ALL, this);
          }
          headings_.push_back({group, heading});
          layout_y += 26;
          previous_section = group;
          tile_grid.reset(layout_y);
        }
        if (!categories && hmh_widget_layout.collapsed(area_, group)) continue;
        const auto key=block_key(e);
        if(key!=previous_device) {
          if(key[0]!='@' || (!previous_device.empty() && previous_device[0]!='@')) {layout_y=tile_grid.bottom();tile_grid.reset(layout_y);}
          previous_device=key;
          if(key[0]!='@') {
            size_t count=0;for(const auto &block:device_blocks_) if(block.key==key) count=block.items.size();
            std::string caption=std::string(hmh_widget_layout.device_closed(key)?"+ ":"- ")+
              (e.device_name.empty()?e.device:e.device_name)+" · "+std::to_string(count);
            if(categories && area_name) caption+=" · "+area_name(e.area);
            auto *heading=make_label(list_,12,layout_y,layout_width_-30,caption.c_str());
            lv_obj_set_style_text_font(heading,hmh_runtime_text_20,0);
            lv_obj_set_style_text_color(heading,hmh_color(0xCBD5E1),0);
            lv_obj_set_height(heading,25);
            lv_obj_add_flag(heading,LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(heading,layout_event,LV_EVENT_ALL,this);
            device_headings_.push_back({group,key,heading});layout_y+=27;tile_grid.reset(layout_y);
          }
        }
        if(hmh_widget_layout.device_closed(key)) continue;
        // Gaia 0.7.1: footprints depend on actual controls and content.
        // A simple barometer has the same footprint as other numerical sensors.
        // Expand only for controls that truly require more space.
        const bool is_alarm=e.domain=="alarm_control_panel";
        const bool has_control=e.domain=="climate" || e.domain=="cover" ||
            (e.domain=="light" && e.brightness_supported) ||
            (e.domain=="fan" && e.speed_supported) ||
            (is_number(e) && number_range(e));
        const auto composite_key=device_key(e);
        const auto composite_block=std::find_if(device_blocks_.begin(),device_blocks_.end(),
            [&](const DeviceBlock &b){return b.grouped && b.key==composite_key &&
                !b.items.empty() && b.items[0]==&e;});
        const bool composite_purifier=composite_block!=device_blocks_.end();
        const bool sensor_only_composite=composite_purifier &&
            std::all_of(composite_block->items.begin(),composite_block->items.end(),
                [](const Entity *item){return item->domain=="sensor" || item->domain=="binary_sensor";});
        const bool switch_only_composite=composite_purifier &&
            std::all_of(composite_block->items.begin(),composite_block->items.end(),
                [](const Entity *item){return item->domain=="switch" || item->domain=="input_boolean";});
        const bool uniform_composite=sensor_only_composite || switch_only_composite;
        const int requested_columns=(is_alarm || composite_purifier)?4:2;
        // Layout is driven by the content kind, not a fixed composite height.
        // Numerical values need a taller cell than status-only switches.
        // Metric tiles are sourced exclusively from labelled catalog entities.
        const bool energy_composite=false;
        const int composite_count=composite_purifier?
            static_cast<int>(composite_block->items.size())-(uniform_composite?0:1):0;
        const bool all_switches=uniform_composite && switch_only_composite;
        const int metric_cols=(composite_count==3 && (all_switches || energy_composite))?3:2;
        const int metric_rows=(composite_count+metric_cols-1)/metric_cols;
        const bool numeric_metrics=sensor_only_composite || energy_composite;
        const int metric_height=responsive.size(numeric_metrics?76:all_switches?60:68);
        const bool compact_header=composite_purifier && !uniform_composite &&
            (e.domain=="switch" || e.domain=="input_boolean" ||
             e.domain=="light" || e.domain=="fan") &&
            !e.purifier;
        // Purifier header and speed control need less vertical space than
        // other multi-control hero cards. Reserve 8px below the speed bar.
        const bool purifier_composite=composite_purifier && e.purifier && e.domain=="fan";
        const int metrics_top=uniform_composite?responsive.size(12):
            compact_header?responsive.size(76):
            purifier_composite?responsive.size(112):responsive.size(145);
        const int metrics_bottom=metrics_top+metric_rows*metric_height+
            std::max(0,metric_rows-1)*responsive.size(8);
        // Slider has to sit below the metrics, not overlap or leave a blank band.
        const bool slider_below_metrics=composite_purifier && has_control &&
            (e.domain=="light" || e.domain=="fan") && compact_header;
        const int content_bottom=metrics_bottom+responsive.size(slider_below_metrics?56:12);
        const int tile_pitch=responsive.size(40)+gap;
        const int composite_rows=std::max(2,
            (content_bottom+gap+tile_pitch-1)/tile_pitch);
        // Alarm actions start at y=78/88, with 42 px row pitch and 34 px buttons.
        // Include the final disarm row, then reserve a small bottom breathing space.
        const int alarm_action_rows=(static_cast<int>(e.alarm_modes.size())+2)/3;
        const int alarm_bottom=(categories?88:78)+alarm_action_rows*42+34+12;
        const int alarm_rows=std::max(3,(alarm_bottom+gap+tile_pitch-1)/tile_pitch);
        const int requested_rows=composite_purifier?composite_rows:is_alarm?alarm_rows
            :has_control?3:(e.domain=="sensor" || categories || tall_card(e))?3:2;
        const auto slot=tile_grid.place(requested_columns,requested_rows);
        const int card_width=slot.w;
        const int card_height=slot.h;
        Card card;
        card.entity_id = e.id;
        card.uniform_composite = uniform_composite;
        card.energy_composite = energy_composite;
        card.width = card_width;
        card.obj = lv_btn_create(list_);
        lv_obj_set_pos(card.obj,slot.x,slot.y);
        lv_obj_set_size(card.obj,card_width,card_height);
        lv_obj_set_style_radius(card.obj, 16, 0);
        lv_obj_set_style_pad_all(card.obj, 0, 0);
        lv_obj_set_style_shadow_width(card.obj, 0, 0);
        lv_obj_set_style_border_width(card.obj, 1, 0);
        lv_obj_set_style_border_color(card.obj, lv_color_hex(0x245377), 0);
        lv_obj_set_style_bg_color(card.obj, hmh_color(0x24465A), LV_STATE_PRESSED);
        auto *icon_tile = lamp_part(card.obj, 12, 17, 46, 46, LV_RADIUS_CIRCLE);
        card.icon_tile = icon_tile;
        lv_obj_set_style_bg_color(icon_tile, lv_color_hex(0x254560), 0);
        if (e.domain == "light") {
          card.lamp = lamp_part(icon_tile, 12, 8, 22, 24, LV_RADIUS_CIRCLE);
          card.lamp_base = lamp_part(icon_tile, 18, 30, 10, 7, 2);
        } else {
          const char *symbol = e.domain=="alarm_control_panel" ? "\xF3\xB0\x92\x98" : is_number(e) ? "\xF3\xB0\x98\xAE" : e.purifier ? "\xF3\xB0\xB5\x84" : e.domain == "fan" ? "\xF3\xB0\x88\x90" : e.domain == "climate" ? "\xF3\xB0\x8E\x93" : e.domain == "sensor" ? "\xF3\xB0\x94\x8F" : e.domain == "button" ? LV_SYMBOL_OK :
              e.domain == "binary_sensor" ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_POWER;
          auto *icon = make_label(icon_tile, 0, 0, 46, symbol);
          card.icon = icon;
          lv_obj_set_style_text_font(icon, (e.domain == "sensor" || e.domain == "climate" || e.domain == "binary_sensor" || e.domain == "cover" || e.domain == "fan" || e.domain=="alarm_control_panel" || is_number(e)) ? hmh_mdi_font : hmh_runtime_text_20, 0);
          lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
          lv_obj_center(icon);
          lv_obj_set_style_text_color(icon, hmh_color(0x22D3EE), 0);
        }
        card.name = make_label(card.obj, 70, 19, card_width - 82, e.name.c_str());
        lv_obj_set_style_text_color(card.name, lv_color_hex(0xE8F6FF), 0);
        card.status = make_label(card.obj, 70, 44, card_width - 82, "...");
        lv_obj_set_style_text_color(card.status, hmh_color(0x94A3B8), 0);
        if (categories && !is_contact(e)) {
          card.room_label = make_label(card.obj, 70, 61, card_width - 82, "");
          lv_obj_set_style_text_color(card.room_label, hmh_color(0x94A3B8), 0);
        }
        if (e.domain == "sensor" || e.domain == "binary_sensor") {
          // Gaia 0.7.2: place each section relative to the tile slot, not
          // to a fixed 130px card. Reserve header, value and optional footer.
          const HmhLayout cell_metrics=HmhLayout::current();
          const int inset=cell_metrics.size(12);
          const int header_height=cell_metrics.size(40);
          const int value_y=inset+header_height+cell_metrics.size(5);
          lv_obj_set_pos(card.icon_tile,inset,inset);
          lv_obj_set_size(card.icon_tile,cell_metrics.size(38),cell_metrics.size(38));
          if(card.icon) {
            lv_obj_set_width(card.icon,cell_metrics.size(38));
            lv_obj_center(card.icon);
          }
          lv_obj_set_pos(card.name,inset+cell_metrics.size(46),inset);
          lv_obj_set_width(card.name,std::max(30,card_width-inset*2-cell_metrics.size(46)));
          lv_obj_set_height(card.name,header_height);
          lv_label_set_long_mode(card.name, LV_LABEL_LONG_DOT);
          lv_obj_set_pos(card.status,inset,value_y);
          lv_obj_set_width(card.status,std::max(30,card_width-inset*2));
          lv_obj_set_height(card.status,cell_metrics.size(33));
          lv_obj_set_style_text_font(card.status,
              (e.domain=="sensor" && card_width>=cell_metrics.size(225))?hmh_runtime_text_28:hmh_runtime_text_20,0);
          lv_obj_set_style_text_color(card.status, lv_color_hex(0xD6F3FF), 0);
          if (e.domain == "sensor") {
            const bool percentage=(e.unit=="%" && (e.device_class=="humidity" || e.device_class=="battery"));
            const bool barometer=(e.device_class=="atmospheric_pressure" || e.unit=="hPa" || e.unit=="mbar");
            if (percentage || barometer) {
              card.gaia_scale=lv_bar_create(card.obj);
              lv_obj_set_pos(card.gaia_scale,inset,card_height-cell_metrics.size(10));
              lv_obj_set_size(card.gaia_scale,card_width-inset*2,cell_metrics.size(4));
              lv_bar_set_range(card.gaia_scale,0,1000);
              lv_obj_set_style_radius(card.gaia_scale,LV_RADIUS_CIRCLE,LV_PART_MAIN);
              lv_obj_set_style_radius(card.gaia_scale,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
              lv_obj_set_style_bg_color(card.gaia_scale,lv_color_hex(0x29455B),LV_PART_MAIN);
              lv_obj_set_style_bg_color(card.gaia_scale,lv_color_hex(barometer?0x4DAEED:0x30C9DD),LV_PART_INDICATOR);
              if (barometer) {
                card.gaia_scale_caption=make_label(card.obj,inset,card_height-cell_metrics.size(31),card_width-inset*2,"Pressione relativa");
                lv_obj_set_style_text_font(card.gaia_scale_caption,hmh_chip_text_14,0);
                lv_obj_set_style_text_color(card.gaia_scale_caption,lv_color_hex(0x91B3CA),0);
              }
            }
          }
        }
        if (e.domain == "light") {
          card.rgb_dot = lamp_part(card.obj, 70, 45, 16, 16, LV_RADIUS_CIRCLE);
          lv_obj_set_style_border_width(card.rgb_dot, 1, 0);
          lv_obj_set_style_border_color(card.rgb_dot, hmh_color(0x94A3B8), 0);
          lv_obj_add_flag(card.rgb_dot, LV_OBJ_FLAG_HIDDEN);
        }
        if (e.domain == "cover") {
          const int touch_gap=responsive.size(8);
          const int touch_pad=responsive.size(12);
          const int touch_height=responsive.size(36);
          const int controls_y=card_height-touch_height-responsive.size(10);
          int w = std::max(responsive.size(32),(card_width-2*touch_pad-2*touch_gap)/3);
          card.minus = detail_button(card.obj,touch_pad,controls_y,w,touch_height,LV_SYMBOL_DOWN);
          card.mode = detail_button(card.obj,touch_pad+w+touch_gap,controls_y,w,touch_height,LV_SYMBOL_PAUSE);
          card.plus = detail_button(card.obj,touch_pad+2*(w+touch_gap),controls_y,w,touch_height,LV_SYMBOL_UP);
          for (auto *button : {card.minus, card.mode, card.plus}) {
            lv_obj_set_style_bg_color(button, hmh_color(0x334155), 0);
            lv_obj_clear_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_add_event_cb(button, [](lv_event_t *event) {
              auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(event));
              auto *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
              for (const auto &card : self->cards_) {
                const Entity *e = self->find(card.entity_id);
                if (!e || e->domain != "cover" || !self->online_ || !available(*e) || !self->on_cover) continue;
                if (target == card.minus && e->cover_close && e->state != "closed") self->on_cover(e->id, "close_cover");
                if (target == card.mode && e->cover_stop) self->on_cover(e->id, "stop_cover");
                if (target == card.plus && e->cover_open && (e->state != "open" || (e->position >= 0 && e->position < 100))) self->on_cover(e->id, "open_cover");
              }
            }, LV_EVENT_CLICKED, this);
          }
        }
        if (e.domain == "climate") {
          const int control_h=responsive.size(36);
          const int control_y=card_height-control_h-responsive.size(10);
          const int control_pad=responsive.size(12);
          const int control_w=responsive.size(40);
          card.minus = detail_button(card.obj,control_pad,control_y,control_w,control_h,"-");
          card.plus = detail_button(card.obj,card_width-control_pad-control_w*2-responsive.size(8),control_y,control_w,control_h,"+");
          card.mode = detail_button(card.obj,card_width-control_pad-control_w,control_y,control_w,control_h,LV_SYMBOL_SETTINGS);
          card.target = make_label(card.obj,control_pad+control_w+responsive.size(4),control_y+responsive.size(8),std::max(30,card_width-2*control_pad-3*control_w-responsive.size(16)),"--");
          lv_obj_set_style_text_align(card.target, LV_TEXT_ALIGN_CENTER, 0);
          for (auto *button : {card.minus, card.plus, card.mode}) {
            lv_obj_set_style_bg_color(button, hmh_color(0x334155), 0);
            lv_obj_clear_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_add_event_cb(button, [](lv_event_t *event) {
              auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(event));
              self->climate_card_action(static_cast<lv_obj_t *>(lv_event_get_target(event)));
            }, LV_EVENT_CLICKED, this);
          }
        }
        if ((e.domain == "light" && e.brightness_supported) || (e.domain == "fan" && e.speed_supported)) {
          card.slider = lv_slider_create(card.obj);
          lv_obj_set_pos(card.slider,responsive.size(12),card_height-responsive.size(46));
          lv_obj_set_size(card.slider,card_width-responsive.size(24),responsive.size(36));
          lv_slider_set_range(card.slider, 0, e.domain == "fan" ? 100 : 255);
          lv_obj_set_style_radius(card.slider, 12, LV_PART_MAIN);
          lv_obj_set_style_radius(card.slider, 12, LV_PART_INDICATOR);
          lv_obj_set_style_bg_opa(card.slider, LV_OPA_COVER, LV_PART_MAIN);
          lv_obj_set_style_bg_color(card.slider, hmh_color(0x4A3D22), LV_PART_MAIN);
          lv_obj_set_style_bg_color(card.slider, hmh_color(0xFBBF24), LV_PART_INDICATOR);
          lv_obj_set_style_bg_opa(card.slider, LV_OPA_COVER, LV_PART_INDICATOR);
          lv_obj_set_style_bg_opa(card.slider, LV_OPA_TRANSP, LV_PART_KNOB);
          lv_obj_set_style_border_width(card.slider, 0, LV_PART_KNOB);
          lv_obj_set_style_shadow_width(card.slider, 0, LV_PART_KNOB);
          lv_obj_set_style_pad_all(card.slider, 0, LV_PART_KNOB);
          lv_obj_clear_flag(card.slider, LV_OBJ_FLAG_EVENT_BUBBLE);
          lv_obj_add_event_cb(card.slider, [](lv_event_t *event) {
            auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(event));
            auto *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
            for (const auto &c : self->cards_) if (c.slider == target) {
              const auto *entity = self->find(c.entity_id);
              if (entity && self->online_ && available(*entity)) {
                if (entity->domain == "fan" && entity->speed_supported && self->on_fan_percentage)
                  self->on_fan_percentage(entity->id, lv_slider_get_value(target));
                else if (entity->domain == "light" && self->on_brightness)
                  self->on_brightness(entity->id, lv_slider_get_value(target));
              }
              break;
            }
          }, LV_EVENT_RELEASED, this);
        }
        if(e.domain=="alarm_control_panel") {
          std::vector<std::string> actions=e.alarm_modes;actions.push_back("alarm_disarm");
          for(size_t i=0;i<actions.size();++i) {
            int w=(card_width-32)/3;
            const bool disarm=actions[i]=="alarm_disarm";
            int x=disarm?8:8+(i%3)*(w+8);
            int y=(categories?88:78)+(disarm?((e.alarm_modes.size()+2)/3)*42:(i/3)*42);
            auto *button=detail_button(card.obj,x,y,disarm?card_width-16:w,34,alarm_action_name(actions[i]));
            auto *caption=lv_obj_get_child(button,0);
            lv_obj_set_style_text_font(caption,hmh_chip_text_14,0);
            lv_label_set_long_mode(caption,LV_LABEL_LONG_CLIP);
            card.alarm_buttons.push_back({button,actions[i]});
            lv_obj_add_event_cb(button,[](lv_event_t *event){
              auto *self=static_cast<RoomDevices *>(lv_event_get_user_data(event));
              auto *target=static_cast<lv_obj_t *>(lv_event_get_target(event));
              for(const auto &card:self->cards_) for(const auto &button:card.alarm_buttons) if(button.first==target) {
                auto *e=self->find(card.entity_id);if(e) self->alarm_action(*e,button.second);return;
              }
            },LV_EVENT_CLICKED,this);
          }
        }
        if(is_number(e) && number_range(e)) {
          card.slider=lv_slider_create(card.obj);lv_obj_set_pos(card.slider,12,categories?88:78);
          lv_obj_set_size(card.slider,card_width-24,30);lv_slider_set_range(card.slider,0,1000);
          lv_obj_set_style_radius(card.slider,12,LV_PART_MAIN);lv_obj_set_style_radius(card.slider,12,LV_PART_INDICATOR);
          lv_obj_set_style_bg_color(card.slider,hmh_color(0x233B60),LV_PART_MAIN);
          lv_obj_set_style_bg_color(card.slider,lv_color_hex(0x3B82F6),LV_PART_INDICATOR);
          lv_obj_set_style_bg_opa(card.slider,LV_OPA_COVER,LV_PART_MAIN);
          lv_obj_set_style_bg_opa(card.slider,LV_OPA_TRANSP,LV_PART_KNOB);
          lv_obj_clear_flag(card.slider,LV_OBJ_FLAG_EVENT_BUBBLE);
          lv_obj_add_event_cb(card.slider,[](lv_event_t *event){
            auto *self=static_cast<RoomDevices *>(lv_event_get_user_data(event));
            if(hmh_navigation_blocked || !self->online_ || !self->on_number) return;
            auto *target=static_cast<lv_obj_t *>(lv_event_get_target(event));
            for(const auto &card:self->cards_) if(card.slider==target) {
              const auto *e=self->find(card.entity_id);
              if(e && available(*e) && number_range(*e)) self->on_number(e->id,number_from_slider(*e,lv_slider_get_value(target)));
              break;
            }
          },LV_EVENT_RELEASED,this);
        }
        if(e.purifier) {
          card.pm25_label=make_label(card.obj,12,categories?80:68,card_width-24,"");
          lv_obj_set_height(card.pm25_label,20);
          if(composite_purifier) lv_obj_add_flag(card.pm25_label,LV_OBJ_FLAG_HIDDEN);
          const int speed_y=purifier_composite?responsive.size(74):
              composite_purifier?responsive.size(100):(categories?103:90);
          const int speed_h=purifier_composite?responsive.size(30):
              composite_purifier?responsive.size(30):(categories?18:30);
          if(card.slider) {lv_obj_set_y(card.slider,speed_y);lv_obj_set_height(card.slider,speed_h);}
          else {
            card.speed_bar=lv_bar_create(card.obj);lv_obj_set_pos(card.speed_bar,12,speed_y);
            lv_obj_set_size(card.speed_bar,card_width-24,speed_h);lv_bar_set_range(card.speed_bar,0,100);
            lv_obj_clear_flag(card.speed_bar,LV_OBJ_FLAG_CLICKABLE);
          }
          auto *bar=card.slider?card.slider:card.speed_bar;
          lv_obj_set_style_radius(bar,12,LV_PART_MAIN);lv_obj_set_style_radius(bar,12,LV_PART_INDICATOR);
          lv_obj_set_style_bg_color(bar,hmh_color(0x392347),LV_PART_MAIN);
          lv_obj_set_style_bg_color(bar,lv_color_hex(0x9333B5),LV_PART_INDICATOR);
          lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_MAIN);
          lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_INDICATOR);
        }
        if(uniform_composite) {
          lv_obj_add_flag(card.icon_tile,LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(card.name,LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(card.status,LV_OBJ_FLAG_HIDDEN);
          lv_obj_clear_flag(card.obj,LV_OBJ_FLAG_CLICKABLE);
        }
        if(composite_purifier) {
          // Dedicated metric region, above the bottom fan slider.
          const int inset=responsive.size(12);
          const int metric_top=metrics_top;
          const int cell_gap=responsive.size(8);
          const int cell_w=(card_width-2*inset-(metric_cols-1)*cell_gap)/metric_cols;
          const int cell_h=metric_height;
          int index=0;
          std::vector<const Entity *> metric_items;
          for(const Entity *metric:composite_block->items) {
            if(metric==&e && !uniform_composite) continue;
            metric_items.push_back(metric);
          }
          for(const Entity *metric:metric_items) {
            const bool power_tile=energy_composite && metric==&e;
            const bool last_single=metric_cols==2 && (composite_count%2)==1 && index==composite_count-1;
            const int mx=inset+(index%metric_cols)*(cell_w+cell_gap);
            const int my=metric_top+(index/metric_cols)*(cell_h+cell_gap);
            auto *tile=lv_obj_create(card.obj);
            lv_obj_set_pos(tile,mx,my);
            lv_obj_set_size(tile,last_single?card_width-2*inset:cell_w,cell_h);
            lv_obj_set_style_pad_all(tile,responsive.size(6),0);
            lv_obj_set_style_radius(tile,responsive.size(9),0);
            lv_obj_set_style_border_width(tile,0,0);
            lv_obj_set_style_bg_color(tile,lv_color_hex(0x19354D),0);
            lv_obj_clear_flag(tile,LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(tile,LV_OBJ_FLAG_CLICKABLE);
            // Distinct semantic icon and two-line typography.
            const bool temp=metric->device_class=="temperature";
            const bool humid=metric->device_class=="humidity";
            const bool particulate=metric->device_class=="pm25" ||
                metric->name.find("PM2")!=std::string::npos ||
                metric->name.find("pm2")!=std::string::npos;
            const uint32_t accent=temp?0xF4B35C:humid?0x55C9EE:particulate?0x8FCB93:0x91B5E8;
            const bool lux=metric->device_class=="illuminance";
            const bool pressure=metric->device_class=="atmospheric_pressure" ||
                metric->device_class=="pressure";
            const bool wind=metric->device_class=="speed" ||
                metric->name.find("vento")!=std::string::npos;
            const bool mdi_metric=temp || humid || particulate;
            const char *glyph=temp?"\xF3\xB0\x94\x90":
                humid?"\xF3\xB0\x96\x8E":
                particulate?"\xF3\xB0\xB5\x83":
                lux?"L":
                pressure?"P":
                wind?"V":
                metric->domain=="switch" || metric->domain=="input_boolean"?LV_SYMBOL_POWER:
                metric->domain=="light"?LV_SYMBOL_EYE_OPEN:metric->domain=="button"?LV_SYMBOL_OK:
                metric->domain=="fan"?LV_SYMBOL_REFRESH:LV_SYMBOL_SETTINGS;
            auto *symbol=make_label(tile,0,responsive.size(12),responsive.size(25),glyph);
            lv_obj_set_style_text_font(symbol,mdi_metric?hmh_mdi_font:hmh_runtime_text_20,0);
            lv_obj_set_style_text_color(symbol,lv_color_hex(accent),0);
            std::string short_name=metric->name;
            if(!metric->device_name.empty() &&
               short_name.size()>metric->device_name.size() &&
               short_name.compare(0,metric->device_name.size(),metric->device_name)==0) {
              short_name.erase(0,metric->device_name.size());
              const auto begin=short_name.find_first_not_of(" -_:|/");
              if(begin!=std::string::npos) short_name.erase(0,begin);
              if(short_name.empty()) short_name=metric->name;
            }
            auto *title=make_label(tile,responsive.size(28),responsive.size(2),(last_single?card_width-2*inset:cell_w)-responsive.size(42),
                power_tile?"Potenza":temp?"Temperatura":humid?"Umidita'":particulate?"PM2.5":short_name.c_str());
            lv_obj_set_style_text_font(title,hmh_chip_text_14,0);
            lv_obj_set_style_text_color(title,lv_color_hex(0x94B7CE),0);
            lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
            lv_obj_set_height(title,responsive.size(20));
            const int value_y=responsive.size(numeric_metrics?31:25);
            auto *value=make_label(tile,responsive.size(28),value_y,
                (last_single?card_width-2*inset:cell_w)-responsive.size(42),"--");
            lv_obj_set_height(value,cell_h-value_y-responsive.size(6));
            lv_obj_set_style_text_font(value,
                (metric->domain=="switch" && !power_tile) || metric->domain=="input_boolean" ||
                metric->domain=="fan" || metric->domain=="light" ?
                    hmh_runtime_text_16:hmh_runtime_text_20,0);
            lv_obj_set_style_text_color(value,lv_color_hex(0xF2F8FE),0);
            lv_label_set_long_mode(value,LV_LABEL_LONG_DOT);
            card.composite_metrics.push_back({metric->id,value});
            // Each visible sub-tile owns its touch target: numerical readings
            // open their history; commands retain their existing behavior.
            card.composite_targets.push_back({metric->id,tile});
            lv_obj_add_flag(tile,LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(tile,LV_OBJ_FLAG_EVENT_BUBBLE);
            if(metric->domain=="switch" || metric->domain=="light" ||
               metric->domain=="input_boolean" || metric->domain=="button" ||
               metric->domain=="fan" || metric->domain=="climate") {
              card.composite_controls.push_back({metric->id,tile});
            }
            lv_obj_add_event_cb(tile,[](lv_event_t *event) {
              auto *self=static_cast<RoomDevices *>(lv_event_get_user_data(event));
              if(hmh_navigation_blocked) return;
              lv_obj_t *target=static_cast<lv_obj_t *>(lv_event_get_target(event));
              for(const auto &card:self->cards_) for(const auto &item:card.composite_targets) {
                if(item.second!=target) continue;
                const Entity *child=self->find(item.first);
                if(!child) return;
                if(child->domain=="sensor") {self->open_history(*child);return;}
                if(!self->online_ || !self->available(*child)) return;
                if(child->domain=="climate") {self->open_climate(*child);return;}
                if(child->domain=="light" && (child->brightness_supported || child->color_supported)) {
                  self->open_detail(*child);return;
                }
                if(child->domain=="binary_sensor") return;
                if(!self->on_command) return;
                const std::string action=child->domain=="button"?"button.press":
                    child->domain+(child->state=="on"?".turn_off":".turn_on");
                self->on_command(action,child->id);
                return;
              }
            },LV_EVENT_CLICKED,this);
            ++index;
          }
        }
        if(slider_below_metrics && card.slider)
          lv_obj_set_y(card.slider,metrics_bottom+responsive.size(8));
        if(e.has_battery) {card.battery.create(card.obj,e.battery_entity?12:card_width-72,e.battery_entity?78:2,!e.battery_entity);if(e.battery_entity) lv_obj_add_flag(card.status,LV_OBJ_FLAG_HIDDEN);}
        lv_obj_add_event_cb(card.obj, [](lv_event_t *event) {
          auto *self = static_cast<RoomDevices *>(lv_event_get_user_data(event));
          self->command(static_cast<lv_obj_t *>(lv_event_get_target(event)));
        }, LV_EVENT_CLICKED, this);
        for (auto *handle : {card.name, card.icon_tile}) {
          lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_add_event_cb(handle, layout_event, LV_EVENT_ALL, this);
        }
        cards_.push_back(std::move(card));
      }
      if (force_rebuild) lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
    }
    std::string hint;
    if (!received_) hint = "In attesa delle entita' da HA...";
    else if (all_selected.empty()) hint = "Nessuna entità con etichetta HMH Dashboard in questa vista.";
    else hint = std::to_string(all_selected.size()) + (all_selected.size() == 1 ? " dispositivo" : " dispositivi");
    if (!online_) hint += " | API offline";
    hmh_label_set_if_changed(hint_, hint.c_str());
    // The count overlaps the category heading when the list starts high.
    // Keep it accessible for empty/offline states; normal views use heading counts.
    if (received_ && !all_selected.empty() && online_)
      lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_clear_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    for (size_t i = 0; i < selected.size(); ++i) {
      auto &card = cards_[i];
      const auto &e = *selected[i];
      // Aggiorna i widget esistenti senza perdere lo scorrimento.
      hmh_label_set_if_changed(card.name, e.name.c_str());
      if (card.room_label) {
        std::string name = area_name ? area_name(e.area) : e.area;
        hmh_label_set_if_changed(card.room_label, name.c_str());
      }
      bool active = (e.domain == "cover" && available(e) && e.state != "closed") || e.state == "on" || (e.domain == "climate" && e.state != "off" && available(e));
      bool can_command = online_ && available(e) && e.domain != "binary_sensor" && e.domain != "sensor";
      const bool read_only = e.domain == "sensor" || e.domain == "binary_sensor";
      if (can_command || read_only) lv_obj_clear_state(card.obj, LV_STATE_DISABLED);
      else lv_obj_add_state(card.obj, LV_STATE_DISABLED);
      if (read_only && e.domain!="sensor") lv_obj_clear_flag(card.obj, LV_OBJ_FLAG_CLICKABLE);
      else lv_obj_add_flag(card.obj, LV_OBJ_FLAG_CLICKABLE);
      if(card.uniform_composite && !card.composite_controls.empty() && (e.domain=="switch" || e.domain=="input_boolean"))
        lv_obj_clear_flag(card.obj,LV_OBJ_FLAG_CLICKABLE);
      std::string status = status_text(e);
      if (available(e)) {
        if (e.domain == "light") {
          status = e.brightness_supported ?
              std::to_string(active ? (e.brightness * 100 + 127) / 255 : 0) + "%" : "";
        } else if (e.domain == "switch" || e.domain == "input_boolean") {
          // Do not expose a power sensor's value through another entity's card.
          status.clear();
        }
      }
      if(is_contact(e)) status.clear();
      hmh_label_set_if_changed(card.status, status.c_str());
      if(card.uniform_composite && !card.composite_metrics.empty()) {
        lv_obj_add_flag(card.name,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(card.status,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(card.icon_tile,LV_OBJ_FLAG_HIDDEN);
      }
      // State-driven backgrounds for every interactive metric tile.
      // Read-only numerical tiles keep their neutral background.
      for(const auto &control:card.composite_controls) {
        const Entity *child=find(control.first);
        if(!child) continue;
        const bool online_state=available(*child);
        const bool on_state=online_state &&
            (child->state=="on" ||
             (child->domain=="climate" && child->state!="off") ||
             (child->domain=="fan" && child->state=="on"));
        const uint32_t fill=on_state?0x24563D:0x19354D;
        lv_obj_set_style_bg_color(control.second,lv_color_hex(fill),0);
        lv_obj_set_style_border_width(control.second,on_state?1:0,0);
        lv_obj_set_style_border_color(control.second,lv_color_hex(0x34D399),0);
        lv_obj_set_style_bg_opa(control.second,online_state?LV_OPA_COVER:LV_OPA_60,0);
      }
      for(const auto &metric:card.composite_metrics) {
        const Entity *child=find(metric.first);
        if(!child) continue;
        std::string caption=status_text(*child);
        if(!(card.energy_composite && metric.first==card.entity_id) &&
           (child->domain=="switch" || child->domain=="light" || child->domain=="input_boolean"))
          caption=child->state=="on"?"Acceso":"Spento";
        hmh_label_set_if_changed(metric.second,caption.c_str());
      }
      // Gaia 0.8.15: colour switch tiles according to live HA state.
      // The tile owns icon, title and value, in that order. Style only switch
      // and input_boolean children; keep sensors and other controls unchanged.
      for(const auto &control:card.composite_controls) {
        const Entity *child=find(control.first);
        if(!child || (child->domain!="switch" && child->domain!="input_boolean")) continue;
        lv_obj_t *tile=control.second;
        const bool active=child->state=="on";
        const bool known=available(*child);
        const uint32_t icon_color=!known?0x8195A5:active?0xF4C46D:0x8195A5;
        const uint32_t tile_color=!known?0x1A2937:active?0x20433F:0x19354D;
        lv_obj_set_style_bg_color(tile,lv_color_hex(tile_color),0);
        lv_obj_set_style_border_width(tile,active && known?1:0,0);
        lv_obj_set_style_border_color(tile,lv_color_hex(0x477D66),0);
        if(lv_obj_get_child_cnt(tile)>=3) {
          lv_obj_set_style_text_color(lv_obj_get_child(tile,0),lv_color_hex(icon_color),0);
          lv_obj_set_style_text_color(lv_obj_get_child(tile,2),
              lv_color_hex(!known?0x8195A5:active?0xF5F9FC:0xB6C6D3),0);
        }
      }
      lv_obj_set_style_text_color(card.status, hmh_color(0xCBD5E1), 0);
      if (e.domain == "cover") {
        hmh_label_set_if_changed(card.icon, cover_icon(e));
        lv_obj_clear_flag(card.obj, LV_OBJ_FLAG_CLICKABLE);
        const bool enabled[] = {can_command && e.cover_close && e.state != "closed", can_command && e.cover_stop, can_command && e.cover_open && (e.state != "open" || (e.position >= 0 && e.position < 100))};
        lv_obj_t *buttons[] = {card.minus, card.mode, card.plus};
        for (int j = 0; j < 3; ++j) {
          bool supported = j == 0 ? e.cover_close : j == 1 ? e.cover_stop : e.cover_open;
          if (supported) lv_obj_clear_flag(buttons[j], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(buttons[j], LV_OBJ_FLAG_HIDDEN);
          if (enabled[j]) lv_obj_clear_state(buttons[j], LV_STATE_DISABLED); else lv_obj_add_state(buttons[j], LV_STATE_DISABLED);
        }
      }
      if (e.domain == "climate") {
        if (card.icon) hmh_label_set_if_changed(card.icon, (e.hvac_action == "cooling" || e.state == "cool") ? "\xF3\xB0\x9C\x97" :
            (e.hvac_action == "heating" || e.state == "heat") ? "\xF3\xB0\x88\xB8" : "\xF3\xB0\x8E\x93");
        if (std::isfinite(card.requested) && (std::fabs(e.temperature - card.requested) < 0.01f ||
            lv_tick_get() - card.requested_at > 5000)) card.requested = NAN;
        bool single = e.target_supported && std::isfinite(e.temperature) &&
            !(e.range_supported && (e.state == "heat_cool" || e.state == "auto") && std::isfinite(e.low) && std::isfinite(e.high));
        std::string target = single ? temperature_text(std::isfinite(card.requested) ? card.requested : e.temperature, "") :
            e.range_supported && std::isfinite(e.low) && std::isfinite(e.high) ? "Intervallo" : "--";
        if (std::isfinite(card.requested)) target += "...";
        hmh_label_set_if_changed(card.target, target.c_str());
        for (auto *button : {card.minus, card.plus}) {
          if (can_command && single) lv_obj_clear_state(button, LV_STATE_DISABLED);
          else lv_obj_add_state(button, LV_STATE_DISABLED);
        }
        if (can_command) lv_obj_clear_state(card.mode, LV_STATE_DISABLED);
        else lv_obj_add_state(card.mode, LV_STATE_DISABLED);
      }
      lv_obj_set_y(card.name, (e.domain == "sensor" || e.domain == "binary_sensor") ? 14 : status.empty() && !(e.domain == "light" && e.color_supported) ? 30 : 19);
      if (e.domain == "sensor" || e.domain == "binary_sensor") {
        lv_obj_set_style_text_font(card.status, available(e) ? (e.domain == "sensor" ? hmh_runtime_text_28 : hmh_runtime_text_16) : hmh_runtime_text_16, 0);
        lv_obj_set_style_text_color(card.status, hmh_color(0xEAF2FF), 0);
      } else if(e.domain=="switch" || e.domain=="input_boolean" || e.domain=="light" ||
                e.domain=="fan" || e.domain=="cover" || e.domain=="climate") {
        lv_obj_set_style_text_font(card.status,
            e.domain=="switch" && e.has_power && !card.energy_composite ?
            hmh_runtime_text_20 : hmh_runtime_text_16,0);
      }
      if (e.domain == "sensor" && card.icon) {
        hmh_label_set_if_changed(card.icon, sensor_icon(e));
      }
      if (e.domain == "binary_sensor" && card.icon)
        hmh_label_set_if_changed(card.icon, binary_icon(e));
      // The primary control has the same active-state language as nested tiles.
      // Status-only and numerical sensor cards keep their existing appearance.
      if(card.icon_tile &&
         (e.domain=="switch" || e.domain=="input_boolean" ||
          e.domain=="light" || e.domain=="fan" || e.domain=="climate" ||
          e.domain=="cover")) {
        const bool highlighted=available(e) && active;
        lv_obj_set_style_bg_color(card.icon_tile,
            lv_color_hex(highlighted?0x24563D:0x254560),0);
        lv_obj_set_style_border_width(card.icon_tile,highlighted?2:0,0);
        lv_obj_set_style_border_color(card.icon_tile,lv_color_hex(0x34D399),0);
      }
      const lv_color_t icon_color = available(e) && active ?
          hmh_color(0xFBBF24) : hmh_color(0x64748B);
      if (card.icon) lv_obj_set_style_text_color(card.icon,
          (e.domain == "sensor" || e.domain == "climate") && available(e) ?
          (e.domain == "climate" && !active ? hmh_color(0x64748B) : e.domain == "climate" && (e.hvac_action == "cooling" || e.state == "cool") ? lv_color_hex(0x3B82F6) : hmh_color(0xFBBF24)) : e.domain == "button" && available(e) ? hmh_color(0x22D3EE) : icon_color, 0);
      if (card.slider) {
        if (!lv_obj_has_state(card.slider, LV_STATE_PRESSED))
          lv_slider_set_value(card.slider, active ? (e.domain == "fan" ? std::max(e.percentage, 0) : e.brightness) : 0, LV_ANIM_OFF);
        if (can_command) lv_obj_clear_state(card.slider, LV_STATE_DISABLED);
        else lv_obj_add_state(card.slider, LV_STATE_DISABLED);
      }
      if(e.domain=="alarm_control_panel") {
        lv_obj_clear_flag(card.obj,LV_OBJ_FLAG_CLICKABLE);
        bool enabled=online_ && available(e) && e.state!="arming" && e.state!="disarming";
        const bool alarm_available=available(e);
        const bool alarm_triggered=e.state=="triggered";
        const uint32_t alarm_active_color=alarm_triggered?0xEF4444:
            e.state=="disarmed"?0x3B82F6:0xF59E0B;
        for(const auto &button:card.alarm_buttons) {
          bool needed=alarm_needs_code(e,button.second);
          bool allowed=enabled && (!needed || e.code_format=="number");
          if(allowed) lv_obj_clear_state(button.first,LV_STATE_DISABLED);else lv_obj_add_state(button.first,LV_STATE_DISABLED);
          const bool selected=alarm_available &&
              ((button.second=="alarm_disarm" && e.state=="disarmed") ||
               (button.second=="alarm_arm_home" && e.state=="armed_home") ||
               (button.second=="alarm_arm_away" && e.state=="armed_away") ||
               (button.second=="alarm_arm_night" && e.state=="armed_night") ||
               (button.second=="alarm_arm_vacation" && e.state=="armed_vacation") ||
               (button.second=="alarm_arm_custom_bypass" && e.state=="armed_custom_bypass"));
          lv_obj_set_style_bg_color(button.first,
              lv_color_hex(selected?0x365B62:0x233B53),0);
          lv_obj_set_style_border_width(button.first,selected?2:0,0);
          lv_obj_set_style_border_color(button.first,lv_color_hex(alarm_active_color),0);
          if(lv_obj_get_child_cnt(button.first)>0)
            lv_obj_set_style_text_color(lv_obj_get_child(button.first,0),
                lv_color_hex(selected?0xFFFFFF:0xCBD5E1),0);
        }
        lv_obj_set_style_text_color(card.status,
            alarm_available?lv_color_hex(alarm_active_color):hmh_color(0x64748B),0);
        if(card.icon) lv_obj_set_style_text_color(card.icon,available(e)?lv_color_hex(e.state=="triggered"?0xEF4444:e.state=="disarmed"?0x3B82F6:0xF59E0B):hmh_color(0x64748B),0);
      }
      if(is_number(e)) {
        lv_obj_clear_flag(card.obj,LV_OBJ_FLAG_CLICKABLE);
        if(card.slider) {
          float value=number_value(e);bool enabled=can_command && number_range(e) && std::isfinite(value);
          if(enabled) lv_obj_clear_state(card.slider,LV_STATE_DISABLED);else lv_obj_add_state(card.slider,LV_STATE_DISABLED);
          if(!lv_obj_has_state(card.slider,LV_STATE_PRESSED)) lv_slider_set_value(card.slider,enabled?static_cast<int>(std::round(std::clamp((value-e.number_min)/(e.number_max-e.number_min),0.0f,1.0f)*1000)):0,LV_ANIM_OFF);
        }
        if(card.icon) lv_obj_set_style_text_color(card.icon,available(e)?lv_color_hex(0x3B82F6):hmh_color(0x64748B),0);
      }
      if(e.purifier && card.pm25_label) {
        char *end=nullptr;double value=std::strtod(e.pm25_state.c_str(),&end);
        bool valid=end && end!=e.pm25_state.c_str() && *end=='\0' && std::isfinite(value) && value>=0;
        char formatted[32];std::snprintf(formatted,sizeof(formatted),"%.1f",value);
        std::string text=valid?std::string("PM2.5: ")+formatted+" "+e.pm25_unit:"PM2.5: non disponibile";
        hmh_label_set_if_changed(card.pm25_label,text.c_str());
        lv_obj_set_style_text_color(card.pm25_label,hmh_color(0xCBD5E1),0);
        if(card.speed_bar) lv_bar_set_value(card.speed_bar,active?std::max(e.percentage,0):0,LV_ANIM_OFF);
        if(card.icon) lv_obj_set_style_text_color(card.icon,active?lv_color_hex(0xC084FC):hmh_color(0x64748B),0);
      }
      if (card.rgb_dot) {
        const bool show_rgb = available(e) && e.color_supported;
        if (show_rgb) {
          lv_obj_clear_flag(card.rgb_dot, LV_OBJ_FLAG_HIDDEN);
          lv_obj_set_style_bg_color(card.rgb_dot, lv_color_make(e.red, e.green, e.blue), 0);
        } else lv_obj_add_flag(card.rgb_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(card.status, show_rgb ? 96 : 70);
        lv_obj_set_width(card.status, card.width - (show_rgb ? 108 : 82));
      }
      lv_obj_set_style_bg_color(card.obj, lv_color_hex(0x10273D), 0);
      lv_obj_set_style_bg_opa(card.obj, LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(card.obj, hmh_color(0x334155), 0);
      lv_obj_set_style_text_opa(card.name, LV_OPA_COVER, 0);
      lv_obj_set_style_text_opa(card.status, LV_OPA_COVER, 0);
      if (card.gaia_scale) {
        const bool barometer=(e.device_class=="atmospheric_pressure" || e.unit=="hPa" || e.unit=="mbar");
        const float reading=std::strtof(e.state.c_str(),nullptr);
        const bool numeric=!e.state.empty() && std::isfinite(reading) && (std::isdigit(static_cast<unsigned char>(e.state[0])) || e.state[0]=='-' || e.state[0]=='+');
        // Percent values have a natural 0..100 range; pressure is mapped to a
        // barometric 980..1040 hPa reference band, NOT a rain forecast.
        const float pressure_hpa=e.unit=="kPa"?reading*10.0f:reading;
        const float fraction=barometer?(pressure_hpa-980.0f)/60.0f:reading/100.0f;
        if(numeric) {
          lv_bar_set_value(card.gaia_scale,std::clamp(static_cast<int>(std::lround(fraction*1000.0f)),0,1000),LV_ANIM_OFF);
          lv_obj_clear_flag(card.gaia_scale,LV_OBJ_FLAG_HIDDEN);
        } else lv_obj_add_flag(card.gaia_scale,LV_OBJ_FLAG_HIDDEN);
        if(card.gaia_scale_caption) {
          hmh_label_set_if_changed(card.gaia_scale_caption, numeric?(pressure_hpa<1000?"Bassa pressione":pressure_hpa>1020?"Alta pressione":"Pressione intermedia"):"Pressione non disponibile");
        }
      }
      // Binary sensor resta una scheda di sola lettura ma a luminosita' piena.
      lv_obj_set_style_bg_opa(card.obj, LV_OPA_COVER, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(card.obj, hmh_color(0x1E293B), LV_STATE_DISABLED);
      lv_obj_set_style_text_opa(card.obj, LV_OPA_COVER, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(card.icon_tile, hmh_color(available(e) && (active || e.domain == "sensor") ? 0x4A3D22 : 0x334155), 0);
      if(is_contact(e)) {
        lv_obj_set_y(card.name,17);lv_obj_set_height(card.name,46);
        lv_obj_add_flag(card.status,LV_OBJ_FLAG_HIDDEN);
        const bool known=e.state=="on" || e.state=="off";
        lv_color_t color=known?lv_color_hex(e.state=="on"?0xF59E0B:0x22C55E):hmh_color(0x64748B);
        if(card.icon) lv_obj_set_style_text_color(card.icon,color,0);
        lv_obj_set_style_bg_color(card.icon_tile,known?hmh_color(e.state=="on"?0x49321D:0x163D2B):hmh_color(0x334155),0);
      }
      if(card.battery.root) card.battery.update(e.battery_level,e.battery_known,e.battery_low);
      if (card.lamp) {
        lv_color_t color = icon_color;
        lv_obj_set_style_bg_color(card.lamp, color, 0);
        lv_obj_set_style_bg_color(card.lamp_base, color, 0);
      }
    }
    sync_detail_status();
  }
  lv_obj_t *list_ = nullptr, *hint_ = nullptr;
  std::vector<Entity, HmhRAMAllocator<Entity>> entities_;
  std::vector<Card> cards_;
  std::string area_;
  std::string previous_payload_;
  uint64_t payload_signature_ = 0;
  size_t payload_size_ = 0;
  bool has_payload_ = false;
  int layout_width_ = 574;
  bool layout_dirty_ = false;
  bool online_ = false, visible_ = true, received_ = false;
  lv_obj_t *dialog_ = nullptr, *detail_status_ = nullptr, *detail_toggle_ = nullptr;
  lv_obj_t *brightness_slider_ = nullptr, *brightness_label_ = nullptr;
  lv_obj_t *rgb_sliders_[3] = {nullptr, nullptr, nullptr};
  lv_obj_t *rgb_labels_[3] = {nullptr, nullptr, nullptr};
  std::string detail_entity_;
};
static RoomDevices room_devices;


// Calendar events are supplied by the optional Home Assistant package.
// Dates use ISO local YYYY-MM-DD; end is exclusive for all-day events.
class HmhCalendar {
 public:
  struct Event {std::string start,end,title,source;bool all_day=false;};
  std::function<void(int,int)> request;
  bool enabled=false;
  int year=0,month=0,selected=0,today=0,today_year=0,today_month=0;
  int now_hour=0,now_minute=0;bool now_valid=false;
  std::vector<Event> events;
  lv_obj_t *panel=nullptr,*heading=nullptr,*list=nullptr,*days[42]={};
  int cell_day[42]={};
  void attach(lv_obj_t *parent) {
    panel=lv_obj_create(parent);lv_obj_set_pos(panel,214,70);lv_obj_set_size(panel,574,395);
    lv_obj_set_style_bg_color(panel,hmh_color(0x0B1220),0);lv_obj_set_style_border_width(panel,0,0);
    lv_obj_set_style_pad_all(panel,4,0);lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    auto button=[&](int x,int y,int w,const char *caption,int action) {
      auto *b=lv_btn_create(panel);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,35);
      lv_obj_set_style_bg_color(b,hmh_color(0x1E293B),0);lv_obj_set_style_radius(b,8,0);
      auto *l=lv_label_create(b);lv_label_set_text(l,caption);lv_obj_center(l);
      lv_obj_set_user_data(b,reinterpret_cast<void *>(static_cast<intptr_t>(action)));
      lv_obj_add_event_cb(b,[](lv_event_t *ev){auto *self=static_cast<HmhCalendar *>(lv_event_get_user_data(ev));auto *b=static_cast<lv_obj_t *>(lv_event_get_target(ev));int a=static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(b)));if(a==0)self->choose_today();else self->move_month(a);},LV_EVENT_CLICKED,this);
    };
    button(4,0,40,"<",-1);button(525,0,40,">",1);button(445,0,76,"Oggi",0);
    heading=lv_label_create(panel);lv_obj_set_pos(heading,65,6);lv_obj_set_width(heading,365);
    lv_obj_set_style_text_font(heading,hmh_runtime_text_20,0);
    lv_obj_set_style_text_color(heading,lv_color_hex(0xFFFFFF),0);
    const char *week[]={"Lun","Mar","Mer","Gio","Ven","Sab","Dom"};
    for(int i=0;i<7;i++){auto *l=lv_label_create(panel);lv_obj_set_pos(l,13+i*79,43);lv_obj_set_width(l,76);lv_label_set_text(l,week[i]);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(l,hmh_runtime_text_16,0);lv_obj_set_style_text_color(l,lv_color_hex(0xCBD5E1),0);}    
    for(int i=0;i<42;i++){
      auto *b=lv_btn_create(panel);days[i]=b;lv_obj_set_pos(b,5+(i%7)*79,65+(i/7)*33);lv_obj_set_size(b,74,31);
      lv_obj_set_style_radius(b,7,0);lv_obj_set_style_border_width(b,0,0);lv_obj_set_style_shadow_width(b,0,0);
      lv_obj_set_style_pad_all(b,0,0);lv_obj_set_user_data(b,reinterpret_cast<void *>(static_cast<intptr_t>(i+1)));
      auto *l=lv_label_create(b);lv_obj_center(l);lv_obj_set_style_text_font(l,hmh_runtime_text_16,0);
      lv_obj_add_event_cb(b,[](lv_event_t *ev){auto *self=static_cast<HmhCalendar *>(lv_event_get_user_data(ev));auto *b=static_cast<lv_obj_t *>(lv_event_get_target(ev));int i=static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(b)))-1;if(i>=0&&i<42&&self->cell_day[i]){self->selected=self->cell_day[i];self->render();}},LV_EVENT_CLICKED,this);
    }
    list=lv_obj_create(panel);lv_obj_set_pos(list,4,270);lv_obj_set_size(list,560,117);
    lv_obj_set_style_border_width(list,0,0);lv_obj_set_style_bg_opa(list,LV_OPA_TRANSP,0);lv_obj_set_style_pad_all(list,3,0);
    lv_obj_set_scroll_dir(list,LV_DIR_VER);lv_obj_set_scrollbar_mode(list,LV_SCROLLBAR_MODE_AUTO);
    // Position each event manually: Flex layout is disabled in this LVGL build.
    lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
  }
  static int days_in_month(int y,int m){static const int d[]={0,31,28,31,30,31,30,31,31,30,31,30,31};return d[m]+(m==2&&y%4==0&&(y%100!=0||y%400==0));}
  static int weekday(int y,int m,int d){if(m<3){m+=12;y--;}int k=y%100,j=y/100;return ((d+13*(m+1)/5+k+k/4+j/4+5*j+5)%7);}// Mon=0
  static std::string date(int y,int m,int d){char b[16];snprintf(b,sizeof(b),"%04d-%02d-%02d",y,m,d);return b;}
  static bool parse_iso(const std::string &s,int &y,int &m,int &d,int &hh,int &mm,bool &has_time){
    y=m=d=hh=mm=0;has_time=false;
    if(s.size()<10) return false;
    if(std::sscanf(s.c_str(),"%d-%d-%d",&y,&m,&d)!=3) return false;
    if(s.size()>=16){if(std::sscanf(s.c_str()+11,"%d:%d",&hh,&mm)==2)has_time=true;}
    return true;
  }
  static time_t make_time(int y,int m,int d,int hh=0,int mm=0,int ss=0){std::tm t={};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=hh;t.tm_min=mm;t.tm_sec=ss;t.tm_isdst=-1;return std::mktime(&t);}  
  static int day_diff(int y1,int m1,int d1,int y2,int m2,int d2){
    time_t a=make_time(y1,m1,d1,12,0,0),b=make_time(y2,m2,d2,12,0,0);
    return static_cast<int>((a-b)/86400);
  }
  static std::string weekday_short_it(int y,int m,int d){
    static const char *wd[] = {"Dom","Lun","Mar","Mer","Gio","Ven","Sab"};
    std::tm t={};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=12;t.tm_isdst=-1;std::mktime(&t);
    int idx=t.tm_wday; if(idx<0||idx>6) idx=0; return wd[idx];
  }
  void set_now(int y,int m,int d,int hh,int mm){
    if(!y||!m||!d)return;
    bool change=today_year!=y||today_month!=m||today!=d;
    bool minute_change=!now_valid||now_hour!=hh||now_minute!=mm;
    today_year=y;today_month=m;today=d;now_hour=hh;now_minute=mm;now_valid=true;
    if(!year){year=y;month=m;selected=0;fetch();render();}
    else if((change || minute_change) && year==y && month==m && panel && !lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN))render();
  }
  void set_today(int y,int m,int d){set_now(y,m,d,now_hour,now_minute);}  
  void choose_today(){selected=0;render();}
  void move_month(int delta){month+=delta;if(month<1){month=12;year--;}else if(month>12){month=1;year++;}selected=0;events.clear();render();fetch();}
  void fetch(){if(request&&enabled&&year&&month)request(year,month);}  
  void set_enabled(bool e){if(enabled==e)return;enabled=e;if(!e){events.clear();if(panel)lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);}else fetch();}
  void show(bool visible){if(!panel)return;if(visible&&enabled){lv_obj_clear_flag(panel,LV_OBJ_FLAG_HIDDEN);render();fetch();}else lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);}  
  void load(const std::string &payload){
    std::vector<Event> incoming;int ry=0,rm=0;bool ok=esphome::json::parse_json(hmh_json(payload),[&](JsonObject root)->bool{
      ry=root["year"]|0;rm=root["month"]|0;
      if(!root["events"].is<JsonArray>())return false;
      JsonArray events_array=root["events"].as<JsonArray>();
      for(JsonObject v:events_array){
        Event e;e.start=v["s"].as<std::string>();e.end=v["e"].as<std::string>();e.title=v["t"].as<std::string>();e.source=v["c"].as<std::string>();e.all_day=v["a"]|false;
        if(!e.start.empty()&&incoming.size()<120)incoming.push_back(std::move(e));
      }return true;
    });
    if(!ok||ry!=year||rm!=month)return;
    events=std::move(incoming);render();
  }
  bool includes(const Event &e,int d)const{
    const auto dt=date(year,month,d);
    if(e.start.substr(0,10)>dt)return false;
    if(e.end.empty())return e.start.substr(0,10)==dt;
    if(e.all_day)return dt<e.end.substr(0,10);
    if(e.end.substr(0,10)>dt)return true;
    if(e.end.substr(0,10)==dt)return e.end.size()>10&&e.end.substr(11,5)>"00:00";
    return false;
  }
  int start_day_diff(const Event &e) const {
    int y,m,d,hh,mm;bool has_time;
    if(!parse_iso(e.start,y,m,d,hh,mm,has_time)) return 9999;
    return day_diff(y,m,d,today_year,today_month,today);
  }
  bool is_today_past(const Event &e) const {
    int y,m,d,hh,mm;bool has_time;
    if(!now_valid || !parse_iso(e.start,y,m,d,hh,mm,has_time)) return false;
    if(y!=today_year || m!=today_month || d!=today || e.all_day || !has_time) return false;
    return (hh*60+mm) < (now_hour*60+now_minute);
  }
  bool is_upcoming_window(const Event &e) const {
    if(!today_year || !today_month || !today) return true;
    int diff = start_day_diff(e);
    if(diff>=0 && diff<7) return true;
    return includes(e,today);
  }
  lv_color_t event_color(const Event &e) const {
    int diff = start_day_diff(e);
    if(diff>0) return lv_color_hex(0xFACC15);      // future days -> yellow
    if(diff<0) return lv_color_hex(0xEF4444);      // past days -> red
    if(is_today_past(e)) return lv_color_hex(0xEF4444); // earlier today -> red
    return lv_color_hex(0xFFFFFF);                 // later today / all-day today -> white
  }
  std::string italian_prefix(const Event &e) const {
    int y,m,d,hh,mm;bool has_time;
    if(!parse_iso(e.start,y,m,d,hh,mm,has_time)) return e.start;
    char b[64];
    std::string wd=weekday_short_it(y,m,d);
    if(e.all_day || !has_time) std::snprintf(b,sizeof(b),"%s %02d/%02d • Tutto il giorno",wd.c_str(),d,m);
    else std::snprintf(b,sizeof(b),"%s %02d/%02d • %02d:%02d",wd.c_str(),d,m,hh,mm);
    return b;
  }
  std::string event_line(const Event &e) const {
    std::string txt=italian_prefix(e);
    txt+="  ";txt+=e.title;
    if(!e.source.empty()) txt+="  ("+e.source+")";
    return txt;
  }
  bool standby_summary(std::string &text, lv_color_t &color) const {
    text.clear();
    const Event *best=nullptr; time_t best_ts=0;
    for(const auto &e:events){
      if(!is_upcoming_window(e)) continue;
      int y,m,d,hh,mm;bool has_time;
      if(!parse_iso(e.start,y,m,d,hh,mm,has_time)) continue;
      time_t ts=make_time(y,m,d,e.all_day?0:hh,e.all_day?0:mm,0);
      if(start_day_diff(e)==0 && is_today_past(e)) continue;
      if(!best || ts<best_ts){best=&e;best_ts=ts;}
    }
    if(!best) return false;
    color=event_color(*best);
    text="Prossimo appuntamento\n"+event_line(*best);
    return true;
  }
  void render(){
    if(!panel||!year||!month)return;
    static const char *months[]={"","Gennaio","Febbraio","Marzo","Aprile","Maggio","Giugno","Luglio","Agosto","Settembre","Ottobre","Novembre","Dicembre"};
    std::string ttl=std::string(months[month])+" "+std::to_string(year);lv_label_set_text(heading,ttl.c_str());
    int offset=weekday(year,month,1);int maxday=days_in_month(year,month);
    for(int i=0;i<42;i++){
      int d=i-offset+1;auto *b=days[i];auto *l=lv_obj_get_child(b,0);
      if(d<1||d>maxday){cell_day[i]=0;lv_obj_add_flag(b,LV_OBJ_FLAG_HIDDEN);continue;}
      cell_day[i]=d;lv_obj_clear_flag(b,LV_OBJ_FLAG_HIDDEN);bool star=false;for(const auto &e:events)if(includes(e,d)){star=true;break;}
      std::string label=std::to_string(d)+(star?" *":"");lv_label_set_text(l,label.c_str());lv_obj_center(l);
      bool chosen=(selected?selected==d:today==d);lv_obj_set_style_bg_color(b,chosen?lv_color_hex(0x0891B2):hmh_color(0x1E293B),0);
      lv_obj_set_style_text_color(l,chosen?lv_color_hex(0xFFFFFF):hmh_color(0xEAF2FF),0);
    }
    lv_obj_clean(list);
    int shown=0;
    int row_y=3;
    for(const auto &e:events){
      bool match=selected?includes(e,selected):is_upcoming_window(e);
      if(!match)continue;
      auto *l=lv_label_create(list);lv_obj_set_width(l,535);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);
      std::string txt=event_line(e);
      lv_label_set_text(l,txt.c_str());
      lv_obj_set_style_text_font(l,hmh_runtime_text_16,0);
      lv_obj_set_style_text_color(l,event_color(e),0);
      lv_obj_update_layout(l);
      lv_obj_set_pos(l,3,row_y);
      row_y += lv_obj_get_height(l)+7;
      shown++;
    }
    if(!shown){
      auto *l=lv_label_create(list);
      lv_label_set_text(l,events.empty()?"Nessun evento caricato":"Nessun appuntamento nei prossimi 7 giorni");
      lv_obj_set_style_text_font(l,hmh_runtime_text_16,0);
      lv_obj_set_style_text_color(l,lv_color_hex(0xFFFFFF),0);
      lv_obj_set_pos(l,3,3);
    }
  }
};
static HmhCalendar hmh_calendar;

// Modulo indipendente dall'hardware. I widget vengono creati a runtime.
class RoomSidebar {
 public:
  struct Room {
    std::string id, name;
    lv_obj_t *button = nullptr;
    std::string glyph;
    lv_obj_t *icon_label = nullptr, *name_label = nullptr;
    bool enabled = true;
  };
  // Punto di collegamento per il successivo modulo dispositivi.
  std::function<void(const std::string &)> on_selected;
  std::function<void()> on_settings;
  std::function<void(int)> on_width_changed;
  std::function<void(const std::string &)> on_preferences_changed;
  std::function<void(int)> on_theme_changed;
  std::function<void(int)> on_night_brightness_changed;
  void set_night_brightness(int value) { night_brightness_=std::clamp(value,55,100); }
  std::function<void()> on_theme_applied;
  bool has_rooms() const { return has_payload_ && !all_rooms_.empty(); }
  bool categories_mode() const { return categories_; }
  bool navigation_area(const lv_point_t &point) const {
    if(!devices_) return false;
    const auto metrics=HmhLayout::current();
    return point.x>=lv_obj_get_x(devices_) && point.x<metrics.width && point.y>=metrics.status_height() && point.y<metrics.height;
  }
  int page_mode() const {return calendar_?4:energy_?3:batteries_?2:categories_?1:0;}
  bool settings_open() const { return settings_; }
  bool dragging() const { return setup_dragging_; }
  void cancel_drag() {
    if(!setup_dragging_) return;
    cleanup_setup_drag();
    render_settings();
  }
  std::string area_name(const std::string &id) const {
    for (const auto &room : all_rooms_) if (room.id == id) return room.name;
    return id;
  }
  void sync_categories(unsigned mask) {
    if (mask == category_mask_) return;
    category_mask_ = mask;
    if (categories_ && !settings_) rebuild_sidebar();
  }
  void sync_batteries(const std::vector<std::string> &areas) {
    if(areas==battery_areas_) return;battery_areas_=areas;if(batteries_ && !settings_) rebuild_sidebar();
  }
  void set_categories(bool enabled) {set_page(enabled?1:0);}
  void set_page(int page) {
    page=std::clamp(page,0,hmh_calendar.enabled?4:3);if(page_mode()==page || settings_) return;
    if(energy_) {} else if(batteries_) saved_battery_=selected_;else if(categories_) saved_category_=selected_;else saved_room_=selected_;
    calendar_=page==4;energy_=page==3;batteries_=page==2;categories_=page==1;selected_=calendar_?"@calendar":energy_?"@energy":batteries_?saved_battery_:categories_?saved_category_:saved_room_;
    lv_label_set_text(heading_,calendar_?"Calendario":energy_?"Energia":batteries_?"Batterie":categories_?"Categorie":"Stanze");rebuild_sidebar();
  }
  void set_theme_mode(int mode) { theme_mode_ = mode >= 0 && mode <= 2 ? mode : 0; }
  void update_theme(int hour, bool valid) {
    last_hour_ = hour; time_valid_ = valid;
    bool dark = true; // Gaia preview keeps a navy palette
    if (theme_applied_ && dark == hmh_dark) return;
    hmh_dark = dark; theme_applied_ = true;
    auto *root = lv_obj_get_parent(sidebar_);
    hmh_retheme(root);
    lv_obj_set_style_bg_color(root, hmh_color(0x0B1220), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sidebar_, hmh_color(0x111827), 0);
    lv_obj_set_style_text_color(title_, hmh_color(0xEAF2FF), 0);
    lv_obj_set_style_text_color(hint_, hmh_color(0x94A3B8), 0);
    update_settings_button();
    lv_label_set_text(toggle_label_, collapsed_ ? ">" : "<");
    lv_obj_set_style_text_color(toggle_label_, hmh_color(0xEAF2FF), 0);
    lv_obj_set_style_text_opa(toggle_label_, LV_OPA_COVER, 0);
    for (const auto &room : rooms_) {
      lv_obj_set_style_bg_color(room.button, hmh_color(0x1E293B), 0);
      lv_obj_set_style_text_color(room.name_label, hmh_color(0xEAF2FF), 0);
      lv_obj_set_style_text_color(room.icon_label, hmh_color(0xEAF2FF), 0);
      lv_obj_set_style_text_color(room.name_label, lv_color_hex(0xFFFFFF), LV_STATE_CHECKED);
      // Child labels do not inherit their parent's checked state.
      if (room.id == selected_ && !settings_) {
        lv_obj_set_style_text_color(room.name_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(room.icon_label, lv_color_hex(0xFFFFFF), 0);
      }
    }
    if (on_theme_applied) on_theme_applied();
    if (settings_ && !setup_dragging_) render_settings();
  }
  void set_preferences(const std::string &value) { preferences_ = value; }
  const std::string &selected_area_id() const { return selected_; }

  void attach(lv_obj_t *list, lv_obj_t *title, lv_obj_t *hint) {
    list_ = list; title_ = title; hint_ = hint;
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
  }

  void bind_layout(lv_obj_t *sidebar, lv_obj_t *heading, lv_obj_t *settings,
      lv_obj_t *settings_label, lv_obj_t *toggle_label, lv_obj_t *devices, const lv_font_t *icon_font) {
    sidebar_ = sidebar; heading_ = heading; settings_button_ = settings;
    settings_label_ = settings_label; toggle_label_ = toggle_label; devices_ = devices;
    icon_font_ = icon_font;
    hmh_mdi_font = icon_font;
    update_settings_button();
    toggle_collapsed(); // Gaia: compact sidebar on boot
  }
  void toggle_collapsed() {
    if (!sidebar_) return;
    collapsed_ = true; // Gaia: always show icons without expanded labels
    const HmhLayout metrics = HmhLayout::current();
    const int sidebar_width = metrics.sidebar_width();
    const int right_x = metrics.content_left();
    const int right_width = metrics.content_width();
    lv_obj_set_width(sidebar_, sidebar_width);
    lv_obj_set_width(list_, sidebar_width - metrics.size(16));
    lv_obj_set_style_pad_all(list_, 0, 0);
    if (collapsed_) lv_obj_add_flag(heading_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(heading_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(settings_button_, metrics.size(8));
    lv_obj_set_width(settings_button_, metrics.size(44));
    lv_obj_set_style_pad_all(settings_button_, 0, 0);
    lv_obj_align(settings_label_, LV_ALIGN_CENTER, 0, 0);
    update_settings_button();
    lv_label_set_text(toggle_label_, collapsed_ ? ">" : "<");
    lv_obj_add_flag(lv_obj_get_parent(toggle_label_), LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(title_, right_x + 8);
    lv_obj_set_width(title_, right_width - 8);
    lv_obj_set_x(hint_, right_x + 8);
    lv_obj_set_width(hint_, right_width - 8);
    lv_obj_set_x(devices_, right_x);
    lv_obj_set_width(devices_, right_width);
    for (auto &room : rooms_) {
      lv_obj_set_width(room.button, metrics.size(44));
      if (collapsed_) lv_obj_add_flag(room.name_label, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_clear_flag(room.name_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(room.icon_label, collapsed_ ? LV_ALIGN_CENTER : LV_ALIGN_LEFT_MID, collapsed_ ? 0 : 10, 0);
    }
    if (settings_panel_) {
      lv_obj_set_x(settings_panel_, right_x);
      lv_obj_set_width(settings_panel_, right_width);
      if (settings_ && !setup_dragging_) render_settings();
    }
    if (on_width_changed) on_width_changed(right_width);
  }

  void load(const std::string &payload) {
    if (!list_ || setup_dragging_) return;
    const uint64_t signature = hmh_catalog_signature(payload);
    if (has_payload_ && payload.size() == payload_size_ && signature == payload_signature_) return;
    std::vector<Room> incoming;
    bool valid = esphome::json::parse_json(hmh_json(payload), [&](JsonObject root) -> bool {
      if (!root["areas"].is<JsonArray>()) return false;
      JsonArray areas_array = root["areas"].as<JsonArray>();
      for (JsonObject area : areas_array) {
        if (!area["id"].is<const char *>() || !area["name"].is<const char *>()) return false;
        std::string area_id = area["id"].as<const char *>();
        std::string name = area["name"].as<const char *>();
        if (area_id.empty() || name.empty()) return false;
        for (const auto &existing : incoming) if (existing.id == area_id) return false;
        std::string glyph = area["icon_glyph"].is<const char *>() ? area["icon_glyph"].as<const char *>() : "\xF3\xB0\x9A\xA1";
        incoming.push_back({area_id, name, nullptr, glyph});
      }
      return true;
    });
    if (!valid) {
      if (rooms_.empty()) lv_label_set_text(hint_, "Catalogo stanze non valido. Verifica il sensore HA.");
      return; // Conserva l'ultimo elenco valido.
    }
    all_rooms_ = std::move(incoming);
    apply_preferences();
    rebuild_sidebar();
    payload_signature_ = signature; payload_size_ = payload.size(); has_payload_ = true;
  }

  void rebuild_sidebar() {
    lv_obj_clean(list_);
    rooms_.clear();
    if(calendar_) {rooms_.push_back({"@calendar","Calendario",nullptr,"\xF3\xB0\x83\xAD"});}
    else if(energy_) {rooms_.push_back({"@energy","Consumi",nullptr,"\xF3\xB0\x89\x81"});}
    else if(batteries_) {
      rooms_.push_back({"@battery:all","Tutti",nullptr,"\xF3\xB0\x81\xB9"});
      for(const auto &room:all_rooms_) if(std::find(battery_areas_.begin(),battery_areas_.end(),room.id)!=battery_areas_.end())
        rooms_.push_back({"@battery:"+room.id,room.name,nullptr,room.glyph});
    } else if (categories_) {
      static const char *names[] = {"Luci", "Interruttori", "Termostati", "Sensori", "Aperture", "Energia", "Pulsanti", "Ventilatori", "Porte e finestre", "Purificatori", "Allarme"};
      static const char *icons[] = {"\xF3\xB0\x8C\xB5", "\xF3\xB0\x90\xA5", "\xF3\xB0\x94\x8F", "\xF3\xB0\x8A\x9A", "\xF3\xB0\x84\x9A", "\xF3\xB0\x89\x81", "\xF3\xB0\x85\x9D", "\xF3\xB0\x88\x90", "\xF3\xB0\xA0\x9B", "\xF3\xB0\xB5\x84", "\xF3\xB0\x92\x98"};
      for (int i = 0; i < 11; ++i) if (category_mask_ & (1u << i))
        rooms_.push_back({"@category:" + std::to_string(i), names[i], nullptr, icons[i]});
    } else {
      for (const auto &room : all_rooms_) if (room.enabled) rooms_.push_back(room);
    }
    int y = 0;
    for (auto &room : rooms_) {
      room.button = lv_btn_create(list_);
      lv_obj_set_pos(room.button, 0, y);
      lv_obj_set_size(room.button, HmhLayout::current().size(44), HmhLayout::current().size(48));
      lv_obj_set_style_pad_all(room.button, 0, 0);
      lv_obj_set_style_radius(room.button, 12, 0);
      lv_obj_set_style_bg_color(room.button, hmh_color(0x1E293B), 0);
      lv_obj_set_style_bg_color(room.button, hmh_color(0x0891B2), LV_STATE_CHECKED);
      lv_obj_set_style_border_width(room.button, 0, 0);
      lv_obj_set_style_shadow_width(room.button, 0, 0);
      room.icon_label = lv_label_create(room.button);
      lv_obj_set_style_text_font(room.icon_label, icon_font_, 0);
      lv_obj_set_style_text_color(room.icon_label, hmh_color(0xEAF2FF), 0);
      lv_label_set_text(room.icon_label, room.glyph.c_str());
      lv_obj_clear_flag(room.icon_label, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_align(room.icon_label, collapsed_ ? LV_ALIGN_CENTER : LV_ALIGN_LEFT_MID, collapsed_ ? 0 : 10, 0);
      room.name_label = lv_label_create(room.button);
      lv_obj_set_width(room.name_label, 114);
      lv_label_set_long_mode(room.name_label, LV_LABEL_LONG_DOT);
      lv_label_set_text(room.name_label, room.name.c_str());
      lv_obj_set_style_text_font(room.name_label, hmh_runtime_text_16, 0);
      lv_obj_set_style_text_color(room.name_label, hmh_color(0xEAF2FF), 0);
      lv_obj_clear_flag(room.name_label, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_align(room.name_label, LV_ALIGN_LEFT_MID, 48, 0);
      if (collapsed_) lv_obj_add_flag(room.name_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_event_cb(room.button, [](lv_event_t *event) {
        auto *self = static_cast<RoomSidebar *>(lv_event_get_user_data(event));
        auto *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
        for (const auto &entry : self->rooms_) {
          if (entry.button == target) { self->select(entry.id); break; }
        }
      }, LV_EVENT_CLICKED, this);
      y += HmhLayout::current().size(56);
    }
    bool found = false;
    for (const auto &room : rooms_) if (room.id == selected_) found = true;
    if (!found) selected_ = rooms_.empty() ? "" : rooms_.front().id;
    if (rooms_.empty()) {
      selected_.clear();
      lv_obj_t *empty = lv_label_create(list_);
      lv_obj_set_width(empty, HmhLayout::current().size(38));
      lv_label_set_text(empty, "—");
      lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(empty, hmh_runtime_text_20, 0);
      lv_label_set_text(title_, categories_ ? "Categorie" : "Nessuna stanza");
      lv_label_set_text(hint_, categories_ ? "Nessuna entità HMH Dashboard disponibile." : "Scegli le stanze nelle Impostazioni.");
      if (on_selected) on_selected(selected_);
      if (settings_) show_settings();
    } else if (settings_) {
      show_settings();
    } else {
      select(selected_);
    }
  }

  void select(const std::string &area_id) {
    // Copia prima di aggiornare selected_: area_id puo' riferirsi a selected_.
    selected_ = area_id;
    settings_ = false;
    lv_obj_add_flag(title_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hint_,LV_OBJ_FLAG_HIDDEN);
    if (settings_panel_) lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    for (const auto &room : rooms_) {
      if (room.id == selected_) {
        lv_obj_add_state(room.button, LV_STATE_CHECKED);
        lv_label_set_text(title_, room.name.c_str());
        lv_obj_set_style_text_color(room.name_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(room.icon_label, lv_color_hex(0xFFFFFF), 0);

        lv_obj_scroll_to_view(room.button, LV_ANIM_OFF);
      } else {
        lv_obj_clear_state(room.button, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(room.name_label, hmh_color(0xEAF2FF), 0);
        lv_obj_set_style_text_color(room.icon_label, hmh_color(0xEAF2FF), 0);
      }
    }
    if (on_selected) on_selected(selected_);
  }

  void show_settings() {
    settings_ = true;
    draft_ = all_rooms_;
    draft_theme_ = theme_mode_;
    draft_night_brightness_=night_brightness_;
    for (const auto &room : rooms_) {
      lv_obj_clear_state(room.button, LV_STATE_CHECKED);
      lv_obj_set_style_text_color(room.name_label, hmh_color(0xEAF2FF), 0);
      lv_obj_set_style_text_color(room.icon_label, hmh_color(0xEAF2FF), 0);
    }
    settings_appearance_=false;
    lv_obj_clear_flag(title_,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(title_, "Impostazioni");
    lv_label_set_text(hint_, "Tieni premuto sul nome di una stanza per spostarla.");
    if (on_settings) on_settings();
    if (!settings_panel_) {
      settings_panel_ = lv_obj_create(lv_obj_get_parent(devices_));
      lv_obj_set_style_pad_all(settings_panel_, 0, 0);
      lv_obj_set_style_border_width(settings_panel_, 0, 0);
      lv_obj_set_style_bg_opa(settings_panel_, LV_OPA_TRANSP, 0);
      lv_obj_clear_flag(settings_panel_, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_flag(hint_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(settings_panel_,lv_obj_get_x(devices_),116);
    lv_obj_set_size(settings_panel_,lv_obj_get_width(devices_),352);
    lv_obj_clear_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    render_settings();
  }

 private:
  void update_settings_button() {
    if (!settings_button_ || !settings_label_) return;
    if (!settings_icon_) {
      settings_icon_ = lv_label_create(settings_button_);
      lv_obj_clear_flag(settings_icon_, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_label_set_text(settings_icon_, "\xF3\xB0\x92\x93");
    lv_obj_set_style_text_font(settings_icon_, icon_font_, 0);
    lv_obj_set_style_text_color(settings_icon_, hmh_color(0xEAF2FF), 0);
    lv_obj_set_style_text_opa(settings_icon_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(settings_icon_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(settings_button_, 0, 0);
    lv_obj_set_style_bg_color(settings_button_, hmh_color(0x334155), 0);
    lv_obj_set_style_bg_opa(settings_button_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(settings_label_, hmh_color(0xEAF2FF), 0);
    lv_obj_set_style_text_opa(settings_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(settings_label_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(settings_label_, hmh_runtime_text_16, 0);
    lv_label_set_text(settings_label_, "Impostazioni");
    if (collapsed_) {
      lv_obj_add_flag(settings_label_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_center(settings_icon_);
    } else {
      lv_obj_clear_flag(settings_label_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_width(settings_label_, 124);
      lv_obj_align(settings_icon_, LV_ALIGN_LEFT_MID, 8, 0);
      lv_obj_align(settings_label_, LV_ALIGN_LEFT_MID, 40, 0);
    }
  }
  lv_obj_t *settings_icon_ = nullptr;

  static lv_obj_t *settings_text(lv_obj_t *parent, const char *text, int x, int y, int width) {
    auto *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y); lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, hmh_runtime_text_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
  }
  void apply_preferences() {
    std::vector<Room> ordered;
    size_t start = 0;
    while (start < preferences_.size()) {
      size_t end = preferences_.find('\n', start);
      if (end == std::string::npos) end = preferences_.size();
      std::string token = preferences_.substr(start, end - start);
      const bool enabled = token.empty() || token[0] != '!';
      if (!enabled) token.erase(0, 1);
      for (const auto &room : all_rooms_) if (room.id == token) {
        bool duplicate = false;
        for (const auto &r : ordered) if (r.id == token) duplicate = true;
        if (!duplicate) { ordered.push_back(room); ordered.back().enabled = enabled; }
      }
      start = end + 1;
    }
    for (const auto &room : all_rooms_) {
      bool found = false;
      for (const auto &r : ordered) if (r.id == room.id) found = true;
      if (!found) ordered.push_back(room); // Nuove aree visibili in fondo.
    }
    all_rooms_ = std::move(ordered);
  }
  lv_obj_t *setting_button(lv_obj_t *parent, const char *text, int x, int y, int width, size_t action) {
    auto *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y); lv_obj_set_size(button, width, 40);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x334155), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_user_data(button, reinterpret_cast<void *>(action));
    auto *label = settings_text(button, text, 0, 0, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, [](lv_event_t *event) {
      auto *self = static_cast<RoomSidebar *>(lv_event_get_user_data(event));
      auto *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
      if(self->setup_dragging_ || hmh_navigation_blocked) return;
      self->settings_action(reinterpret_cast<size_t>(lv_obj_get_user_data(target)));
    }, LV_EVENT_CLICKED, this);
    return button;
  }
  void render_settings() {
    if (!settings_panel_ || !settings_ || setup_dragging_) return;
    setup_rows_.clear();
    lv_obj_set_style_bg_color(settings_panel_,lv_color_hex(0x0B1220),0);
    lv_obj_set_style_bg_opa(settings_panel_,LV_OPA_COVER,0);
    int scroll = settings_rows_ ? lv_obj_get_scroll_y(settings_rows_) : 0;
    lv_obj_clean(settings_panel_);
    lv_obj_update_layout(settings_panel_);
    int width = lv_obj_get_width(settings_panel_);
    int height = lv_obj_get_height(settings_panel_);
    settings_rows_=nullptr;
    auto *rooms_tab=setting_button(settings_panel_,"Stanze",0,0,(width-12)/2,TAB_ACTION);
    auto *appearance_tab=setting_button(settings_panel_,"Aspetto",(width+12)/2,0,(width-12)/2,TAB_ACTION+1);
    lv_obj_set_style_bg_color(rooms_tab,lv_color_hex(!settings_appearance_?0x0891B2:0x334155),0);
    lv_obj_set_style_bg_color(appearance_tab,lv_color_hex(settings_appearance_?0x0891B2:0x334155),0);
    if(settings_appearance_) {
      settings_text(settings_panel_,"Tema",0,54,width);
      const char *themes[]={"Chiaro","Scuro","Auto"};
      int theme_width=(width-24)/3;
      for(size_t i=0;i<3;++i) {
        auto *button=setting_button(settings_panel_,themes[i],i*(theme_width+8),80,theme_width,THEME_ACTION+i);
        lv_obj_set_style_bg_color(button,lv_color_hex(draft_theme_==static_cast<int>(i)?0x0891B2:0x334155),0);
      }
      settings_text(settings_panel_,"Auto: scuro 20:00 - 07:00",0,127,width);
      settings_text(settings_panel_,"Luminosità notte: 22:00 - 07:00",0,168,width);
      const char *levels[]={"55%","70%","85%","100%"};const int values[]={55,70,85,100};
      int button_width=(width-24)/4;
      for(size_t i=0;i<4;++i) {
        auto *button=setting_button(settings_panel_,levels[i],i*(button_width+8),198,button_width,BRIGHTNESS_ACTION+i);
        lv_obj_set_style_bg_color(button,lv_color_hex(draft_night_brightness_==values[i]?0x0891B2:0x334155),0);
      }
    } else {
    settings_rows_=lv_obj_create(settings_panel_);
    lv_obj_set_pos(settings_rows_,0,50);lv_obj_set_size(settings_rows_,width,height-102);
    lv_obj_set_style_pad_all(settings_rows_, 0, 0);
    lv_obj_set_style_border_width(settings_rows_, 0, 0);
    lv_obj_set_style_bg_opa(settings_rows_, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(settings_rows_, LV_DIR_VER);
    for (size_t i = 0; i < draft_.size(); ++i) {
      int y = static_cast<int>(i) * 48;
      auto *toggle=lv_checkbox_create(settings_rows_);
      lv_checkbox_set_text(toggle,"");lv_obj_set_pos(toggle,0,y);lv_obj_set_size(toggle,42,40);
      lv_obj_set_style_pad_all(toggle,8,0);
      lv_obj_set_style_text_font(toggle,&lv_font_montserrat_20,LV_PART_INDICATOR);
      lv_obj_set_style_radius(toggle,5,LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(toggle,lv_color_hex(0x1E293B),LV_PART_INDICATOR);
      lv_obj_set_style_border_color(toggle,lv_color_hex(0x94A3B8),LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(toggle,lv_color_hex(0x0891B2),static_cast<lv_style_selector_t>(LV_PART_INDICATOR)|static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
      lv_obj_set_style_text_color(toggle,lv_color_hex(0xFFFFFF),static_cast<lv_style_selector_t>(LV_PART_INDICATOR)|static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
      if(draft_[i].enabled) lv_obj_add_state(toggle,LV_STATE_CHECKED);
      lv_obj_set_user_data(toggle,reinterpret_cast<void *>(i*3));
      lv_obj_add_event_cb(toggle,[](lv_event_t *event){
        auto *self=static_cast<RoomSidebar *>(lv_event_get_user_data(event));
        auto *target=static_cast<lv_obj_t *>(lv_event_get_target(event));
        if(self->setup_dragging_ || hmh_navigation_blocked) return;
        self->settings_action(reinterpret_cast<size_t>(lv_obj_get_user_data(target)));
      },LV_EVENT_VALUE_CHANGED,this);
      auto *handle=lv_btn_create(settings_rows_);
      lv_obj_set_pos(handle,54,y);lv_obj_set_size(handle,width-54,40);
      lv_obj_set_style_radius(handle,10,0);lv_obj_set_style_pad_all(handle,0,0);
      lv_obj_set_style_bg_color(handle,lv_color_hex(0x1E293B),0);
      lv_obj_set_style_shadow_width(handle,0,0);
      auto *name=settings_text(handle,draft_[i].name.c_str(),12,10,width-110);
      (void)name;
      for(int line=0;line<3;++line) {
        auto *bar=lv_obj_create(handle);lv_obj_set_pos(bar,width-86,13+line*6);lv_obj_set_size(bar,18,2);
        lv_obj_set_style_pad_all(bar,0,0);lv_obj_set_style_border_width(bar,0,0);
        lv_obj_set_style_radius(bar,0,0);lv_obj_set_style_bg_color(bar,lv_color_hex(0xFFFFFF),0);
        lv_obj_clear_flag(bar,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(bar,LV_OBJ_FLAG_SCROLLABLE);
      }
      lv_obj_add_event_cb(handle,setup_drag_event,LV_EVENT_ALL,this);
      setup_rows_.push_back({draft_[i].id,handle});
    }
    }
    setting_button(settings_panel_, "Annulla", 0, height - 44, (width - 12) / 2, CANCEL_ACTION);
    setting_button(settings_panel_, "Salva", (width + 12) / 2, height - 44, (width - 12) / 2, SAVE_ACTION);
    if(settings_rows_) lv_obj_scroll_to_y(settings_rows_,scroll,LV_ANIM_OFF);
  }
  void settings_action(size_t action) {
    if (settings_timer_ || setup_dragging_ || hmh_navigation_blocked) return;
    pending_action_ = action;
    // Ricostruisci dopo il callback touch: non cancellare il suo target attivo.
    settings_timer_ = lv_timer_create([](lv_timer_t *timer) {
      auto *self = static_cast<RoomSidebar *>(lv_timer_get_user_data(timer));
      self->settings_timer_ = nullptr;
      if (!self->settings_) return;
      size_t action = self->pending_action_;
      if (action == SAVE_ACTION) {
        std::string value;
        for (const auto &room : self->draft_) value += (room.enabled ? "" : "!") + room.id + "\n";
        if (value.size() > 2047) {
          lv_label_set_text(self->hint_, "Troppi dati da salvare: riduci il numero di stanze."); return;
        }
        self->preferences_ = value;
        if (self->on_preferences_changed) self->on_preferences_changed(value);
        self->all_rooms_ = self->draft_;
        self->night_brightness_=self->draft_night_brightness_;
        if(self->on_night_brightness_changed) self->on_night_brightness_changed(self->night_brightness_);
        self->theme_mode_ = self->draft_theme_;
        if (self->on_theme_changed) self->on_theme_changed(self->theme_mode_);
        self->settings_ = false;
        lv_obj_clear_flag(self->hint_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(self->settings_panel_, LV_OBJ_FLAG_HIDDEN);
        self->rebuild_sidebar();
        self->update_theme(self->last_hour_, self->time_valid_);
      } else if (action == CANCEL_ACTION) {
        self->settings_ = false;
        lv_obj_clear_flag(self->hint_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(self->settings_panel_, LV_OBJ_FLAG_HIDDEN);
        self->rebuild_sidebar();
      } else if(action==TAB_ACTION || action==TAB_ACTION+1) {
        self->settings_appearance_=action==TAB_ACTION+1;self->render_settings();
      } else if(action>=BRIGHTNESS_ACTION && action<BRIGHTNESS_ACTION+4) {
        const int values[]={55,70,85,100};
        self->draft_night_brightness_=values[action-BRIGHTNESS_ACTION];
        self->render_settings();
      } else if (action >= THEME_ACTION && action < THEME_ACTION + 3) {
        self->draft_theme_ = static_cast<int>(action - THEME_ACTION);
        self->render_settings();
      } else {
        size_t index = action / 3, op = action % 3;
        if (index >= self->draft_.size()) return;
        if (op == 0) self->draft_[index].enabled = !self->draft_[index].enabled;
        else if (op == 1 && index > 0) std::swap(self->draft_[index], self->draft_[index - 1]);
        else if (op == 2 && index + 1 < self->draft_.size()) std::swap(self->draft_[index], self->draft_[index + 1]);
        self->render_settings();
      }
    }, 1, this);
    lv_timer_set_repeat_count(settings_timer_, 1);
  }
  struct SetupRow {std::string id;lv_obj_t *obj;};
  std::vector<SetupRow> setup_rows_;
  bool setup_dragging_=false,setup_after_=false;
  std::string setup_source_,setup_target_;
  lv_obj_t *setup_ghost_=nullptr,*setup_marker_=nullptr;
  lv_indev_t *setup_input_=nullptr;
  lv_timer_t *setup_drag_timer_=nullptr;
  static void setup_drag_event(lv_event_t *event) {
    auto *self=static_cast<RoomSidebar *>(lv_event_get_user_data(event));
    if(lv_event_get_code(event)!=LV_EVENT_LONG_PRESSED || self->setup_dragging_ || hmh_navigation_blocked) return;
    auto *target=static_cast<lv_obj_t *>(lv_event_get_target(event));
    for(const auto &row:self->setup_rows_) if(row.obj==target) {self->start_setup_drag(row.id);return;}
  }
  void start_setup_drag(const std::string &id) {
    if(!settings_ || !settings_rows_ || setup_dragging_) return;
    setup_input_=lv_indev_active();if(!setup_input_) return;
    setup_dragging_=true;setup_source_=setup_target_=id;setup_after_=false;
    hmh_navigation_blocked=true;lv_indev_wait_release(setup_input_);
    lv_obj_clear_flag(settings_rows_,LV_OBJ_FLAG_SCROLLABLE);
    setup_ghost_=lv_obj_create(lv_layer_top());lv_obj_set_size(setup_ghost_,260,48);
    lv_obj_set_style_pad_all(setup_ghost_,8,0);lv_obj_set_style_radius(setup_ghost_,12,0);
    lv_obj_set_style_bg_color(setup_ghost_,lv_color_hex(0x0891B2),0);
    lv_obj_set_style_border_width(setup_ghost_,2,0);
    lv_obj_set_style_border_color(setup_ghost_,lv_color_hex(0x22D3EE),0);
    lv_obj_clear_flag(setup_ghost_,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(setup_ghost_,LV_OBJ_FLAG_SCROLLABLE);
    std::string name=id;for(const auto &room:draft_) if(room.id==id) name=room.name;
    settings_text(setup_ghost_,name.c_str(),0,0,240);
    setup_marker_=lv_obj_create(settings_rows_);lv_obj_set_size(setup_marker_,lv_obj_get_width(settings_rows_),4);
    lv_obj_set_style_bg_color(setup_marker_,lv_color_hex(0x22D3EE),0);
    lv_obj_set_style_bg_opa(setup_marker_,LV_OPA_COVER,0);lv_obj_set_style_border_width(setup_marker_,0,0);
    lv_obj_clear_flag(setup_marker_,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(setup_marker_,LV_OBJ_FLAG_SCROLLABLE);
    setup_drag_timer_=lv_timer_create([](lv_timer_t *timer){
      auto *self=static_cast<RoomSidebar *>(lv_timer_get_user_data(timer));
      self->update_setup_drag();
      if(lv_indev_get_state(self->setup_input_)==LV_INDEV_STATE_RELEASED) self->finish_setup_drag();
    },30,this);
    update_setup_drag();
  }
  void update_setup_drag() {
    lv_point_t point;lv_indev_get_point(setup_input_,&point);
    lv_obj_set_pos(setup_ghost_,std::clamp(static_cast<int>(point.x)-130,0,540),std::clamp(static_cast<int>(point.y)-60,0,432));
    lv_area_t bounds;lv_obj_get_coords(settings_rows_,&bounds);
    if(point.y<bounds.y1+30) lv_obj_scroll_by(settings_rows_,0,10,LV_ANIM_OFF);
    else if(point.y>bounds.y2-30) lv_obj_scroll_by(settings_rows_,0,-10,LV_ANIM_OFF);
    lv_obj_update_layout(settings_rows_);
    long best=0x7FFFFFFF;lv_obj_t *nearest=nullptr;
    for(const auto &row:setup_rows_) {
      lv_area_t r;lv_obj_get_coords(row.obj,&r);
      long distance=std::abs(static_cast<int>(point.y)-(r.y1+r.y2)/2);
      if(distance<best) {best=distance;nearest=row.obj;setup_target_=row.id;setup_after_=point.y>(r.y1+r.y2)/2;}
    }
    if(nearest) {
      lv_obj_set_pos(setup_marker_,0,lv_obj_get_y(nearest)+(setup_after_?lv_obj_get_height(nearest):-4));
      lv_obj_move_foreground(setup_marker_);
    }
  }
  void cleanup_setup_drag() {
    if(setup_drag_timer_) lv_timer_delete(setup_drag_timer_);setup_drag_timer_=nullptr;
    if(setup_ghost_) lv_obj_delete(setup_ghost_);setup_ghost_=nullptr;
    if(setup_marker_) lv_obj_delete(setup_marker_);setup_marker_=nullptr;
    lv_obj_add_flag(settings_rows_,LV_OBJ_FLAG_SCROLLABLE);setup_dragging_=false;
  }
  void finish_setup_drag() {
    int scroll=lv_obj_get_scroll_y(settings_rows_);
    if(setup_source_!=setup_target_) {
      auto source=std::find_if(draft_.begin(),draft_.end(),[&](const Room &r){return r.id==setup_source_;});
      if(source!=draft_.end()) {
        Room moving=*source;draft_.erase(source);
        auto target=std::find_if(draft_.begin(),draft_.end(),[&](const Room &r){return r.id==setup_target_;});
        if(target!=draft_.end()) draft_.insert(target+(setup_after_?1:0),std::move(moving));
      }
    }
    cleanup_setup_drag();render_settings();lv_obj_scroll_to_y(settings_rows_,scroll,LV_ANIM_OFF);
  }
  static constexpr size_t THEME_ACTION = 100000;
  static constexpr size_t BRIGHTNESS_ACTION = 100010;
  static constexpr size_t TAB_ACTION=100020;
  bool settings_appearance_=false;
  int night_brightness_=55,draft_night_brightness_=55;
  int theme_mode_ = 0, draft_theme_ = 0, last_hour_ = 12;
  bool time_valid_ = false, theme_applied_ = false;
  static constexpr size_t SAVE_ACTION = static_cast<size_t>(-1);
  static constexpr size_t CANCEL_ACTION = static_cast<size_t>(-2);
  std::vector<Room> all_rooms_, draft_;
  std::string preferences_;
  lv_obj_t *settings_panel_ = nullptr, *settings_rows_ = nullptr;
  lv_timer_t *settings_timer_ = nullptr;
  size_t pending_action_ = 0;

  lv_obj_t *list_ = nullptr, *title_ = nullptr, *hint_ = nullptr;
  bool categories_ = false,batteries_=false,energy_=false,calendar_=false;
  std::vector<std::string> battery_areas_;std::string saved_battery_;
  unsigned category_mask_ = 0;
  std::string saved_room_, saved_category_;
  std::vector<Room> rooms_;
  std::string selected_;
  uint64_t payload_signature_ = 0;
  size_t payload_size_ = 0;
  bool has_payload_ = false;
  bool settings_ = false, collapsed_ = false;
  lv_obj_t *sidebar_ = nullptr, *heading_ = nullptr, *settings_button_ = nullptr;
  lv_obj_t *settings_label_ = nullptr, *toggle_label_ = nullptr, *devices_ = nullptr;
  const lv_font_t *icon_font_ = hmh_runtime_text_20;
};
static RoomSidebar room_sidebar;

// Shared standby surface: optional downloaded image behind clock/date/weather.
static std::string hmh_weather_text, hmh_weather_details, hmh_weather_glyph;
static void hmh_load_weather(const std::string &payload) {
 static uint64_t last_signature=0;static size_t last_size=0;static bool loaded=false;
 const auto signature=hmh_catalog_signature(payload);
 if(loaded && last_size==payload.size() && last_signature==signature) return;
 bool valid=esphome::json::parse_json(hmh_json(payload), [](JsonObject root) -> bool {
  hmh_weather_text=root["weather"]["text"] | "";
  hmh_weather_details=root["weather"]["details"] | "";
  hmh_weather_glyph=root["weather"]["glyph"] | "";
  return true;
 });
 if(valid) {loaded=true;last_size=payload.size();last_signature=signature;}
}
class HmhNavigation {
 public:
  std::function<void(std::string &, std::string &)> clock_text;
  std::function<void()> on_wake;
  // Detach from LVGL before online_image releases or changes its pixel buffer.
  void clear_background() {
    if (overlay_) lv_obj_set_style_bg_image_src(overlay_, nullptr, 0);
    if (background_source_) lv_image_cache_drop(background_source_);
    background_source_ = nullptr;
  }
  void set_background(const lv_image_dsc_t *source) {
    clear_background();
    if (!overlay_ || !source || !source->data) return;
    background_source_ = source;
    lv_image_cache_drop(source);
    lv_obj_set_style_bg_image_src(overlay_, source, 0);
    // Dim only the image, keeping the existing clock and weather legible.
    lv_obj_set_style_bg_image_opa(overlay_, LV_OPA_50, 0);
    lv_obj_invalidate(overlay_);
  }
  bool is_standby() const { return standby_; }
  void show_page_dots() {
    if(!page_indicator_ || standby_ || hmh_status_busy || room_sidebar.settings_open()) return;
    dots_at_=lv_tick_get();dots_visible_=true;
    lv_obj_clear_flag(page_indicator_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(page_indicator_);
    update_page_dots();
  }
  void check_direct_touch() { if(overlay_ && hmh_raw_touch_enabled) poll_raw_touch(); }
  void set_weather_icon_font(const lv_font_t *font) { if(weather_icon_) lv_obj_set_style_text_font(weather_icon_,font,0); }
  void attach(const lv_font_t *clock_font) {
    last_activity_ = lv_tick_get();
    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(overlay_, 0, 0);
    lv_obj_set_size(overlay_, 800, 480);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);
    time_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(time_, clock_font, 0);
    lv_obj_set_style_text_color(time_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(time_, "--:--");
    lv_obj_align(time_, LV_ALIGN_CENTER, 0, -30);
    date_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(date_, hmh_runtime_text_28, 0);
    lv_obj_set_style_text_color(date_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(date_, "In attesa dell'ora da HA");
    lv_obj_align(date_, LV_ALIGN_CENTER, 0, 85);
    next_event_ = lv_label_create(overlay_);
    lv_obj_set_width(next_event_, 620);
    lv_obj_set_style_text_font(next_event_, hmh_runtime_text_20, 0);
    lv_obj_set_style_text_align(next_event_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(next_event_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(next_event_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(next_event_, 58);
    lv_label_set_text(next_event_, "");
    lv_obj_set_pos(next_event_, 90, 268);
    lv_obj_clear_flag(time_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(date_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(next_event_, LV_OBJ_FLAG_CLICKABLE);
    weather_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(weather_,hmh_runtime_text_28,0);
    lv_obj_set_style_text_color(weather_,lv_color_hex(0xCBD5E1),0);
    lv_obj_set_width(weather_,340); lv_obj_set_style_text_align(weather_,LV_TEXT_ALIGN_LEFT,0);
    weather_icon_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(weather_icon_,hmh_mdi_font,0);
    lv_obj_set_style_text_color(weather_icon_,lv_color_hex(0x38BDF8),0);
    lv_obj_clear_flag(weather_,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(weather_icon_,LV_OBJ_FLAG_CLICKABLE);
    weather_condition_=lv_label_create(overlay_);
    lv_obj_set_style_text_font(weather_condition_,hmh_runtime_text_20,0);
    lv_obj_set_style_text_color(weather_condition_,lv_color_hex(0xCBD5E1),0);
    lv_obj_set_width(weather_condition_,280);
    lv_obj_set_style_text_align(weather_condition_,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_clear_flag(weather_condition_,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    page_indicator_=lv_obj_create(lv_layer_top());
    lv_obj_set_pos(page_indicator_,350,451);lv_obj_set_size(page_indicator_,124,24);
    lv_obj_set_style_pad_all(page_indicator_,0,0);lv_obj_set_style_border_width(page_indicator_,0,0);
    lv_obj_set_style_radius(page_indicator_,12,0);lv_obj_set_style_bg_opa(page_indicator_,LV_OPA_80,0);
    lv_obj_clear_flag(page_indicator_,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(page_indicator_,LV_OBJ_FLAG_SCROLLABLE);
    for(int i=0;i<5;++i) {
      page_dots_[i]=lv_obj_create(page_indicator_);
      lv_obj_set_style_border_width(page_dots_[i],0,0);lv_obj_set_style_pad_all(page_dots_[i],0,0);
      lv_obj_set_style_radius(page_dots_[i],LV_RADIUS_CIRCLE,0);
      lv_obj_clear_flag(page_dots_[i],LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(page_dots_[i],LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_flag(page_indicator_,LV_OBJ_FLAG_HIDDEN);
    bind_inputs();
    swipe_timer_=lv_timer_create([](lv_timer_t *timer){static_cast<HmhNavigation *>(lv_timer_get_user_data(timer))->poll_swipe();},20,this);
  }
  void tick() {
    bind_inputs();
    if (!overlay_) return;
    if (hmh_status_busy) { last_activity_ = lv_tick_get(); return; }
    bool pressed = false;
    for (lv_indev_t *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
      if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER && lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED) pressed = true;
    if (pressed) last_activity_ = lv_tick_get();
    if (!standby_ && !pressed && lv_tick_get() - last_activity_ >= 60000) {
      standby_ = true;
      hmh_navigation_blocked = true;
      lv_obj_move_foreground(overlay_);
      lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    if (standby_) {
      std::string time = "--:--", date = "In attesa dell'ora da HA";
      if (clock_text) clock_text(time, date);
      std::string next_event_text; lv_color_t next_event_color=lv_color_hex(0xFACC15);
      const bool has_next_event=hmh_calendar.standby_summary(next_event_text,next_event_color);
      const bool meteo=!hmh_weather_text.empty();
      // Do not invalidate the full standby background on every 1-second timer tick.
      std::string standby_key=time+"|"+date+"|"+next_event_text+"|"+hmh_weather_text+"|"+hmh_weather_details+"|"+hmh_weather_glyph;
      if(standby_key != standby_render_key_) {
        standby_render_key_=standby_key;
        lv_label_set_text(time_, time.c_str());
        lv_label_set_text(date_, date.c_str());
      lv_obj_align(time_, LV_ALIGN_CENTER, 0, has_next_event ? (meteo ? -105 : -70) : (meteo ? -80 : -30));
      lv_obj_align(date_, LV_ALIGN_CENTER, 0, has_next_event ? (meteo ? -5 : 35) : (meteo ? 30 : 85));
      if(has_next_event) {
        lv_label_set_text(next_event_, next_event_text.c_str());
        // Standby uses the same fixed white color as the date (0xFFFFFF).
        lv_obj_set_pos(next_event_, 90, 268); // 58px high, positioned between date and weather
        lv_obj_clear_flag(next_event_, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(next_event_, "");
        lv_obj_add_flag(next_event_, LV_OBJ_FLAG_HIDDEN);
      }
      if(meteo) {
        std::string text=hmh_weather_details;
        size_t separator=0;
        while((separator=text.find(" · ",separator))!=std::string::npos) {
          text.replace(separator,std::strlen(" · "),"\n"); ++separator;
        }
        lv_label_set_text(weather_,text.c_str());
        lv_obj_set_pos(weather_,410,340);
        lv_label_set_text(weather_icon_,hmh_weather_glyph.c_str());
        lv_obj_align(weather_icon_,LV_ALIGN_TOP_LEFT,230,337);
        lv_label_set_text(weather_condition_,hmh_weather_text.c_str());
        lv_obj_set_pos(weather_condition_,120,414);
        lv_obj_clear_flag(weather_condition_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(weather_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(weather_icon_,LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(weather_condition_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(weather_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(weather_icon_,LV_OBJ_FLAG_HIDDEN);
      }
      }
    }
  }
 private:
  std::string standby_render_key_;
  const lv_image_dsc_t *background_source_ = nullptr;
  lv_obj_t *page_indicator_=nullptr,*page_dots_[5]={nullptr,nullptr,nullptr,nullptr,nullptr};
  uint32_t dots_at_=0;bool dots_visible_=false;
  void update_page_dots() {
    if(!page_indicator_) return;
    if(standby_ || hmh_status_busy || room_sidebar.settings_open() || !dots_visible_ || lv_tick_get()-dots_at_>=3000) {
      lv_obj_add_flag(page_indicator_,LV_OBJ_FLAG_HIDDEN);dots_visible_=false;return;
    }
    lv_obj_set_style_bg_color(page_indicator_,hmh_color(0x0F172A),0);
    if(page_dots_[4])lv_obj_add_flag(page_dots_[4],LV_OBJ_FLAG_HIDDEN);
    if(hmh_calendar.enabled && page_dots_[4])lv_obj_clear_flag(page_dots_[4],LV_OBJ_FLAG_HIDDEN);
    for(int i=0;i<(hmh_calendar.enabled?5:4);++i) {
      bool selected=room_sidebar.page_mode()==i;int size=selected?10:5;
      lv_obj_set_size(page_dots_[i],size,size);
      lv_obj_set_pos(page_dots_[i],(hmh_calendar.enabled?11:18)+i*22-size/2,12-size/2);
      lv_obj_set_style_bg_color(page_dots_[i],selected?lv_color_hex(0x22D3EE):hmh_color(0x64748B),0);
    }
  }
  std::vector<lv_indev_t *> bound_inputs_;
  void bind_inputs() {
    for(auto *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input)) {
      if(lv_indev_get_type(input)!=LV_INDEV_TYPE_POINTER || std::find(bound_inputs_.begin(),bound_inputs_.end(),input)!=bound_inputs_.end()) continue;
      lv_indev_add_event_cb(input,input_event,LV_EVENT_ALL,this);bound_inputs_.push_back(input);
    }
  }
  uint32_t raw_seen_sequence_=0,raw_wake_sequence_=0;
  bool raw_allow_swipe_=false;
  static bool slider_at(lv_obj_t *obj,const lv_point_t &point) {
    if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)) return false;
    if(lv_obj_check_type(obj,&lv_slider_class)) {
      lv_area_t r;lv_obj_get_coords(obj,&r);
      if(point.x>=r.x1-12 && point.x<=r.x2+12 && point.y>=r.y1-12 && point.y<=r.y2+12) return true;
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i) if(slider_at(lv_obj_get_child(obj,i),point)) return true;
    return false;
  }
  void poll_raw_touch() {
    if(raw_seen_sequence_!=hmh_raw_sequence) {
      raw_seen_sequence_=hmh_raw_sequence;last_activity_=lv_tick_get();
      show_page_dots();
      raw_allow_swipe_=!standby_ && !hmh_status_busy && raw_wake_sequence_!=hmh_raw_sequence &&
        !room_sidebar.settings_open() && room_sidebar.navigation_area(hmh_raw_origin) && !room_devices.detail_open() && !slider_at(lv_screen_active(),hmh_raw_origin) && !room_devices.chip_at(hmh_raw_origin);
      if(!standby_ && !hmh_status_busy && raw_wake_sequence_!=hmh_raw_sequence) hmh_navigation_blocked=false;
    }
    if(!hmh_raw_pressed || !raw_allow_swipe_ || hmh_navigation_blocked || room_devices.dragging() || room_sidebar.dragging()) return;
    int direction=hmh_swipe_direction(hmh_raw_point.x-hmh_raw_origin.x,hmh_raw_point.y-hmh_raw_origin.y,true);
    if(!direction) return;
    hmh_navigation_blocked=true;raw_allow_swipe_=false;next_page_=(room_sidebar.page_mode()+(direction<0?1:(hmh_calendar.enabled?4:3)))%(hmh_calendar.enabled?5:4);
    for(auto *input:bound_inputs_) lv_indev_wait_release(input);
    ESP_LOGI("ha_dashboard","Swipe rilevato: %s",next_page_==3?"energia":next_page_==2?"batterie":next_page_==1?"categorie":"stanze");
    lv_async_call([](void *data){auto *nav=static_cast<HmhNavigation *>(data);room_sidebar.set_page(nav->next_page_);nav->show_page_dots();},this);
  }
  void poll_swipe() {
    update_page_dots();
    if(hmh_raw_touch_enabled) {poll_raw_touch();return;}
    if(gesture_input_ && lv_indev_get_state(gesture_input_)==LV_INDEV_STATE_RELEASED) {gesture_tracking_=false;return;}
    if(!gesture_input_ || standby_ || hmh_status_busy || hmh_navigation_blocked ||
       room_devices.dragging() || room_sidebar.dragging() || !allow_swipe_) return;
    if(lv_indev_get_state(gesture_input_)!=LV_INDEV_STATE_PRESSED) return;
    lv_point_t point;lv_indev_get_point(gesture_input_,&point);
    int dx=point.x-start_.x,dy=point.y-start_.y;
    if(hmh_swipe_direction(dx,dy,true)==0) return;
    hmh_navigation_blocked=true;lv_indev_wait_release(gesture_input_);
    next_page_=(room_sidebar.page_mode()+(dx<0?1:(hmh_calendar.enabled?4:3)))%(hmh_calendar.enabled?5:4);
    lv_async_call([](void *data){auto *nav=static_cast<HmhNavigation *>(data);room_sidebar.set_page(nav->next_page_);nav->show_page_dots();},this);
  }
  bool gesture_tracking_=false;
  lv_indev_t *gesture_input_=nullptr;
  lv_timer_t *swipe_timer_=nullptr;
  static bool slider_target(lv_obj_t *obj) {
    for (; obj; obj = lv_obj_get_parent(obj))
      if (lv_obj_check_type(obj, &lv_slider_class)) return true;
    return false;
  }
  static void input_event(lv_event_t *event) {
    auto *self = static_cast<HmhNavigation *>(lv_event_get_user_data(event));
    auto *input = static_cast<lv_indev_t *>(lv_event_get_current_target(event));
    const auto code = lv_event_get_code(event);
    if (hmh_status_busy) {
      if (code == LV_EVENT_PRESSED) lv_indev_wait_release(input);
      lv_indev_stop_processing(input);
      return;
    }
    if (code == LV_EVENT_PRESSED) {
      self->last_activity_ = lv_tick_get();
      self->show_page_dots();
      if (self->standby_) {
        self->raw_wake_sequence_=hmh_raw_sequence;
        hmh_navigation_blocked = true;
        lv_indev_stop_processing(input);
        lv_indev_wait_release(input);
        // Hide after this input dispatch, retaining the input's wait-release latch.
        lv_async_call([](void *data) {
          auto *nav = static_cast<HmhNavigation *>(data);
          nav->standby_ = false;
          if (nav->on_wake) nav->on_wake();
          nav->last_activity_ = lv_tick_get();
          lv_obj_add_flag(nav->overlay_, LV_OBJ_FLAG_HIDDEN);
          nav->show_page_dots();
        }, self);
        return;
      }
      if (room_devices.dragging() || room_sidebar.dragging()) { lv_indev_stop_processing(input); return; }
      if(hmh_raw_touch_enabled) {self->gesture_tracking_=false;return;}
      if(!self->gesture_tracking_) {
        hmh_navigation_blocked = false;
        self->gesture_tracking_=true;self->gesture_input_=input;
        lv_indev_get_point(input, &self->start_);
        self->allow_swipe_ = room_sidebar.navigation_area(self->start_) && !slider_target(static_cast<lv_obj_t *>(lv_event_get_param(event))) &&
            !room_devices.chip_target(static_cast<lv_obj_t *>(lv_event_get_param(event))) &&
            !room_devices.detail_open() && !room_sidebar.settings_open();
      }
    } else if (code == LV_EVENT_RELEASED) {
      if(hmh_raw_touch_enabled) {self->gesture_tracking_=false;self->last_activity_=lv_tick_get();return;}
      if(lv_indev_get_state(input)==LV_INDEV_STATE_RELEASED) self->gesture_tracking_=false;
      self->last_activity_ = lv_tick_get();
      lv_point_t end;
      lv_indev_get_point(input, &end);
      int dx = end.x - self->start_.x, dy = end.y - self->start_.y;
      if (!hmh_navigation_blocked && hmh_swipe_direction(dx, dy, self->allow_swipe_) != 0) {
        hmh_navigation_blocked = true;
        lv_indev_stop_processing(input);
        self->next_page_=(room_sidebar.page_mode()+(dx<0?1:(hmh_calendar.enabled?4:3)))%(hmh_calendar.enabled?5:4);
        lv_async_call([](void *data) {
          auto *nav = static_cast<HmhNavigation *>(data);
          room_sidebar.set_page(nav->next_page_);nav->show_page_dots();
        }, self);
      }
    } else if (hmh_navigation_blocked && (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED ||
               code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT)) {
      lv_indev_stop_processing(input);
    }
  }
  lv_obj_t *overlay_ = nullptr, *time_ = nullptr, *date_ = nullptr, *next_event_ = nullptr, *weather_=nullptr, *weather_icon_=nullptr, *weather_condition_=nullptr;
  lv_point_t start_{};
  uint32_t last_activity_ = 0;
  bool standby_ = false, allow_swipe_ = false, next_categories_ = false;
  int next_page_=0;
};
static HmhNavigation hmh_navigation;


// OTA overlay is created only when an update starts.
class HmhBootOTA {
 public:
  enum class Mode { HIDDEN, OTA, ERROR, COMPLETE };
  void attach(const lv_font_t *title_font, const lv_font_t *icon_font) {
    title_font_=title_font; icon_font_=icon_font;
  }
  void create_overlay() {
    if (overlay_) return;
    const lv_font_t *title_font=title_font_, *icon_font=icon_font_;
    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(overlay_,0,0); lv_obj_set_size(overlay_,800,480);
    lv_obj_set_style_radius(overlay_,0,0);lv_obj_set_style_border_width(overlay_,0,0);
    lv_obj_set_style_pad_all(overlay_,0,0);
    lv_obj_set_style_bg_color(overlay_,lv_color_hex(0x0B1220),0);
    lv_obj_set_style_bg_opa(overlay_,LV_OPA_COVER,0);
    lv_obj_clear_flag(overlay_,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay_,LV_OBJ_FLAG_CLICKABLE);
    icon_ = label(108, icon_font, "\xF3\xB0\x9F\x90", 0x22D3EE);
    title_ = label(156,title_font,"HMH Dashboard",0xEAF2FF);
    status_ = label(232,hmh_runtime_text_20,"AGGIORNAMENTO IN CORSO",0x94A3B8);
    percent_ = label(282,title_font,"0%",0x22D3EE);
    lv_obj_add_flag(percent_,LV_OBJ_FLAG_HIDDEN);
    bar_ = lv_bar_create(overlay_);
    lv_obj_set_pos(bar_,170,350);lv_obj_set_size(bar_,460,22);
    lv_bar_set_range(bar_,0,100);
    lv_obj_set_style_radius(bar_,11,LV_PART_MAIN);
    lv_obj_set_style_radius(bar_,11,LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_,lv_color_hex(0x1E293B),LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_,lv_color_hex(0x0EA5E9),LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_,LV_OPA_COVER,LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_,LV_OPA_COVER,LV_PART_INDICATOR);
    lv_obj_add_flag(bar_,LV_OBJ_FLAG_HIDDEN);
    footer_ = label(410,hmh_runtime_text_16,"HackMyHome",0x64748B);
    lv_obj_add_flag(overlay_,LV_OBJ_FLAG_HIDDEN);
    timer_=lv_timer_create([](lv_timer_t *timer) {
      static_cast<HmhBootOTA *>(lv_timer_get_user_data(timer))->tick();
    },150,this);
  }
  void tick() {
    if (!overlay_) return;
    if (mode_==Mode::ERROR && lv_tick_get()-error_at_>=1800) hide();
  }

  void begin() {
    create_overlay();
    room_devices.cancel_drag();
    room_sidebar.cancel_drag();
    mode_=Mode::OTA;hmh_status_busy=true;hmh_navigation_blocked=true;
    if(!overlay_) return;
    lv_obj_clear_flag(overlay_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(overlay_);
    lv_obj_clear_flag(percent_,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(bar_,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_,"AGGIORNAMENTO IN CORSO");
    lv_label_set_text(footer_,"Non spegnere il dispositivo");
    progress(0);
  }
  void progress(float value) {
    if(!overlay_ || mode_!=Mode::OTA) return;
    int percentage=std::isfinite(value)?std::clamp(static_cast<int>(std::round(value)),0,100):0;
    char text[12];std::snprintf(text,sizeof(text),"%d%%",percentage);
    lv_label_set_text(percent_,text);lv_bar_set_value(bar_,percentage,LV_ANIM_OFF);
    lv_refr_now(nullptr);
  }
  void complete() {
    progress(100);mode_=Mode::COMPLETE;
    if(!overlay_) return;
    lv_label_set_text(status_,"AGGIORNAMENTO COMPLETATO");
    lv_label_set_text(footer_,"Riavvio...");lv_refr_now(nullptr);
  }
  void error(int code) {
    mode_=Mode::ERROR;error_at_=lv_tick_get();
    hmh_status_busy=true;hmh_navigation_blocked=true;
    if(!overlay_) {hmh_status_busy=false;return;}
    lv_obj_clear_flag(overlay_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(overlay_);
    char text[64];std::snprintf(text,sizeof(text),"Errore OTA: %d",code);
    lv_label_set_text(status_,"AGGIORNAMENTO FALLITO");
    lv_label_set_text(footer_,text);lv_refr_now(nullptr);
  }
 private:
  lv_obj_t *label(int y,const lv_font_t *font,const char *text,uint32_t color) {
    auto *obj=lv_label_create(overlay_);lv_obj_set_width(obj,760);
    lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_label_set_text(obj,text);lv_obj_set_pos(obj,20,y);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);return obj;
  }
  void hide() {
    mode_=Mode::HIDDEN;hmh_status_busy=false;
    // Keep the command lock until the next independent press.
    lv_obj_add_flag(overlay_,LV_OBJ_FLAG_HIDDEN);
  }
  Mode mode_=Mode::HIDDEN;
  const lv_font_t *title_font_=nullptr,*icon_font_=nullptr;
  lv_obj_t *overlay_=nullptr,*icon_=nullptr,*title_=nullptr,*status_=nullptr,*percent_=nullptr,*bar_=nullptr,*footer_=nullptr;
  lv_timer_t *timer_=nullptr;
  uint32_t error_at_=0;
};
static HmhBootOTA hmh_boot_ota;
