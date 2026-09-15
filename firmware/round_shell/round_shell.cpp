// SPDX-FileCopyrightText: 2026 Round ESP32 contributors
// SPDX-License-Identifier: Apache-2.0
#include "round_shell.hpp"
#include "XiaozhiApp.hpp"
#include "audio_player.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "round_clock.hpp"
#include "system_status.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

LV_FONT_DECLARE(round_font_18);
LV_FONT_DECLARE(round_font_24);
namespace {
using Phone = esp_brookesia::systems::phone::Phone;
using App = esp_brookesia::systems::base::App;
using Context = esp_brookesia::systems::base::Context;
using AI = esp_brookesia::apps::XiaozhiApp;
constexpr const char *TAG = "Round";
constexpr uint32_t BG = 0x080f10, ACCENT = 0xa6ead6, MUTED = 0x6f9584;
Phone *phone;
lv_obj_t *root, *chrome, *chrome_header, *clock_label, *date_label, *headline,
    *subtitle, *orb_button, *orbit, *island, *island_icon, *island_label,
    *wifi_detail, *battery_detail, *detail_hint, *reply, *orb_image_obj;
int chrome_mode = -1;
bool orb_small = false;
lv_obj_t *bars[3];
lv_draw_buf_t *orb_background = nullptr;
lv_image_dsc_t orb_background_image{};
QueueHandle_t commands;
std::atomic<int> raw_pwr{-1}, raw_boot{-1};
bool home = true, sleeping = false, embedded_ai = false, pending_ai = false,
     status_open = false;
int app_page = 0, idle_index = 0, last_ai_state = -1;
uint32_t last_copy = 0, last_clock = 0, transient_until = 0, last_status = 0;
char last_text[512] = {}, transient[100] = {};
std::vector<App *> installed;
std::vector<lv_obj_t *> covered_children;
struct Command {
  int kind;
  int value;
};
enum { HOME = 1, APPS, VOICE, KEY1, KEY2, OPEN_APP, STATUS };
constexpr const char *INVITATIONS[] = {"有什么想聊的？", "今天，想从哪里开始？",
                                       "让灵感，落在这一刻。",
                                       "慢一点，也没关系。"};
constexpr const char *HINTS[] = {"轻触说话 · 或按下侧键 2",
                                 "把一个小想法，讲给我听",
                                 "我在这里，随时可以聊", "按下侧键 2，说说看"};

void queue(int kind, int value = 0) {
  Command c{kind, value};
  if (xQueueSend(commands, &c, 0) != pdTRUE)
    ESP_LOGW(TAG, "Input queue full");
}
void hidden(lv_obj_t *obj, bool hide) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hide)
    return;
  if (hide)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
void clean(lv_obj_t *obj) {
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color,
              int radius = 0) {
  auto *o = lv_obj_create(parent);
  clean(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}
lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                const lv_font_t *font, uint32_t color = 0xd7e5dc) {
  auto *o = lv_label_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_width(o, w);
  lv_label_set_text(o, text);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
  return o;
}
void set_text(lv_obj_t *o, const char *s) {
  if (o && strcmp(lv_label_get_text(o), s))
    lv_label_set_text(o, s);
}
void on_button(lv_event_t *e) {
  auto *c = static_cast<Command *>(lv_event_get_user_data(e));
  queue(c->kind, c->value);
}
void free_command(lv_event_t *e) {
  delete static_cast<Command *>(lv_event_get_user_data(e));
}
void action(lv_obj_t *o, int kind, int value = 0) {
  auto *c = new Command{kind, value};
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, on_button, LV_EVENT_CLICKED, c);
  lv_obj_add_event_cb(o, free_command, LV_EVENT_DELETE, c);
}
lv_obj_t *button(const char *text, int x, int y, int w, int h, int kind,
                 int value = 0) {
  auto *o = box(root, x, y, w, h, 0x162b24, 18);
  action(o, kind, value);
  auto *l = label(o, text, 0, 0, w, &round_font_18);
  lv_obj_center(l);
  return o;
}
void notify(const char *s) {
  strlcpy(transient, s, sizeof(transient));
  transient_until = lv_tick_get() + 4500;
}
App *find_app(const char *needle) {
  for (auto *a : installed)
    if (strstr(a->getName(), needle))
      return a;
  return nullptr;
}
// Screen roots have no parent: changing their HIDDEN flag is not a supported
// layout operation in this LVGL version. Cover only live child objects and
// restore them before the app manager can destroy or replace a screen.
void uncover_scene() {
  for (auto *child : covered_children) {
    if (lv_obj_is_valid(child))
      hidden(child, false);
  }
  covered_children.clear();
}
void cover_scene() {
  uncover_scene();
  auto *screen = lv_screen_active();
  for (uint32_t i = 0; i < lv_obj_get_child_count(screen); i++) {
    auto *child = lv_obj_get_child(screen, i);
    if (!lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
      covered_children.push_back(child);
      hidden(child, true);
    }
  }
}
void stop_active() {
  uncover_scene();
  auto *a = phone->getManager().getActiveApp();
  if (a) {
    Context::AppEventData e{a->getId(), Context::AppEventType::STOP, nullptr};
    phone->sendAppEvent(&e);
  }
  embedded_ai = false;
  pending_ai = false;
}
void launch(App *a, bool embedded = false) {
  if (!a) {
    notify("应用暂不可用");
    return;
  }
  stop_active();
  AI::requestInstance()->round_set_embedded(embedded);
  if (!embedded)
    hidden(root, true);
  Context::AppEventData e{a->getId(), Context::AppEventType::START, nullptr};
  if (!phone->sendAppEvent(&e)) {
    hidden(root, false);
    notify("应用启动失败");
    return;
  }
  if (embedded)
    cover_scene();
  embedded_ai = embedded;
  pending_ai = embedded;
  last_ai_state = -1;
  last_text[0] = 0;
  ESP_LOGI(TAG, "APP %s embedded=%d", a->getName(), embedded);
}
void wake() {
  if (sleeping) {
    sleeping = false;
    bsp_display_backlight_on();
  }
}
void make_home(bool stop = true) {
  if (stop) {
    stop_active();
  }
  wake();
  lv_obj_clean(root);
  if (orb_background) {
    lv_draw_buf_destroy(orb_background);
    orb_background = nullptr;
  }
  hidden(root, false);
  home = true;
  cover_scene(); // Do not repaint the covered Brookesia launcher.
  status_open = false;
  auto *edge = lv_arc_create(root);
  clean(edge);
  lv_obj_set_size(edge, 440, 440);
  lv_obj_set_pos(edge, 13, 13);
  lv_arc_set_rotation(edge, 265);
  lv_arc_set_bg_angles(edge, 0, 359);
  lv_arc_set_angles(edge, 0, 26);
  lv_obj_set_style_arc_width(edge, 1, LV_PART_MAIN);
  lv_obj_set_style_arc_color(edge, lv_color_hex(0x1b3027), LV_PART_MAIN);
  lv_obj_set_style_arc_width(edge, 2, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(edge, lv_color_hex(ACCENT), LV_PART_INDICATOR);
  lv_obj_remove_style(edge, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(edge, LV_OBJ_FLAG_CLICKABLE);
  clock_label = label(root, "--:--", 70, 91, 326, &lv_font_montserrat_48);
  date_label = label(root, "等待校时", 53, 153, 360, &round_font_18, MUTED);
  orb_button = box(root, 165, 184, 136, 136, BG, 0);
  action(orb_button, VOICE);
  auto *base_circle = box(orb_button, 0, 0, 136, 136, 0x0d211e, 68);
  lv_obj_remove_flag(base_circle, LV_OBJ_FLAG_CLICKABLE);
  orb_small = false;
  for (int i = 0; i < 5; i++) {
    auto *c = box(orb_button, 8 + i * 8, 8 + i * 8, 120 - i * 16, 120 - i * 16,
                  0x163c33 + i * 0x020604, 70);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
  }
  // Flatten static circular layers once; subsequent arc frames draw one image.
  // Keep the fallback objects if the optional PSRAM allocation fails.
  orb_background = lv_snapshot_take(orb_button, LV_COLOR_FORMAT_RGB565);
  if (orb_background) {
    lv_obj_clean(orb_button);
    lv_draw_buf_to_image(orb_background, &orb_background_image);
    orb_image_obj = lv_image_create(orb_button);
    lv_image_set_src(orb_image_obj, &orb_background_image);
    lv_obj_set_style_clip_corner(orb_button, false, 0);
    lv_obj_remove_flag(orb_image_obj, LV_OBJ_FLAG_CLICKABLE);
  }
  orbit = lv_arc_create(orb_button);
  clean(orbit);
  lv_obj_set_size(orbit, 122, 122);
  lv_obj_center(orbit);
  lv_arc_set_bg_angles(orbit, 0, 359);
  lv_arc_set_angles(orbit, 0, 245);
  lv_obj_set_style_arc_width(orbit, 1, LV_PART_MAIN);
  lv_obj_set_style_arc_color(orbit, lv_color_hex(0x2f5e4f), LV_PART_MAIN);
  lv_obj_set_style_arc_width(orbit, 2, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(orbit, lv_color_hex(0x70ad96), LV_PART_INDICATOR);
  lv_obj_remove_style(orbit, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(orbit, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < 3; i++) {
    bars[i] =
        box(orb_button, 50 + i * 13, 48, 5, 24 + (i == 1 ? 12 : 0), ACCENT, 3);
    lv_obj_remove_flag(bars[i], LV_OBJ_FLAG_CLICKABLE);
  }
  headline = label(root, INVITATIONS[idle_index], 53, 327, 360, &round_font_24);
  subtitle =
      label(root, HINTS[idle_index], 53, 365, 360, &round_font_18, MUTED);
  reply = label(root, "", 66, 289, 334, &round_font_24);
  lv_obj_set_height(reply, 112);
  lv_label_set_long_mode(reply, LV_LABEL_LONG_WRAP);
  hidden(reply, true);
  button("应用", 158, 404, 72, 35, APPS);
  button("设置", 237, 404, 72, 35, OPEN_APP, -1);
  last_copy = lv_tick_get();
  last_clock = 0;
  last_status = 0;
  last_ai_state = -1;
  last_text[0] = 0;
  ESP_LOGI(TAG, "HOME ready");
}
const char *app_title(App *a) {
  const char *n = a->getName();
  if (strstr(n, "Settings"))
    return "设置";
  if (strstr(n, "Music"))
    return "音乐";
  if (strstr(n, "Gravity"))
    return "重力球";
  if (strstr(n, "Spec"))
    return "声音频谱";
  if (strstr(n, "AIChats") || strstr(n, "Xiaozhi"))
    return "AI 配置";
  if (strstr(n, "Video"))
    return "视频";
  if (strstr(n, "Gallery"))
    return "相册";
  if (strstr(n, "Recorder"))
    return "录音";
  if (strstr(n, "Calculator"))
    return "计算器";
  return n;
}
void make_apps() {
  stop_active();
  wake();
  home = false;
  cover_scene();
  lv_obj_clean(root);
  hidden(root, false);
  label(root, "随身工具", 83, 97, 300, &round_font_24);
  const int begin = app_page * 4;
  for (int j = 0; j < 4 && begin + j < (int)installed.size(); j++) {
    App *a = installed[begin + j];
    auto *b = box(root, 109 + (j % 2) * 133, 135 + (j / 2) * 112, 115, 100, BG);
    action(b, OPEN_APP, begin + j);
    const uint32_t colors[] = {0x193a2c, 0x332c20, 0x203340, 0x362638};
    auto *tile = box(b, 26, 0, 63, 63, colors[j], 21);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    const char *name = a->getName();
    const char *symbol = strstr(name, "Settings")   ? LV_SYMBOL_SETTINGS
                         : strstr(name, "Music")    ? LV_SYMBOL_AUDIO
                         : strstr(name, "Gravity")  ? LV_SYMBOL_GPS
                         : strstr(name, "Spec")     ? LV_SYMBOL_VOLUME_MAX
                         : strstr(name, "Video")    ? LV_SYMBOL_VIDEO
                         : strstr(name, "Gallery")  ? LV_SYMBOL_IMAGE
                         : strstr(name, "Recorder") ? LV_SYMBOL_CALL
                                                    : LV_SYMBOL_BARS;
    auto *glyph =
        label(tile, symbol, 0, 16, 63, &lv_font_montserrat_28, ACCENT);
    lv_obj_remove_flag(glyph, LV_OBJ_FLAG_CLICKABLE);
    label(b, app_title(a), 0, 72, 115, &round_font_18);
  }
  button("上一页", 94, 376, 91, 38, APPS, -1);
  button("返回", 190, 376, 86, 38, HOME);
  button("下一页", 281, 376, 91, 38, APPS, 1);
}
void voice() {
  if (!home || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
    make_home();
  }
  wake();
  if (embedded_ai) {
    AI::requestInstance()->round_press();
    return;
  }
  brookesia::system_status::Snapshot s{};
  brookesia::system_status::get_snapshot(s);
  if (!s.wifi_connected) {
    notify("请先在设置中连接 Wi-Fi");
    return;
  }
  launch(AI::requestInstance(), true);
}
void handle(Command c) {
  last_status = 0;
  if (c.kind == HOME) {
    make_home();
    return;
  }
  if (c.kind == APPS) {
    app_page = (app_page + c.value + ((installed.size() + 3) / 4)) %
               ((installed.size() + 3) / 4);
    make_apps();
    return;
  }
  if (c.kind == VOICE) {
    voice();
    return;
  }
  if (c.kind == KEY1) {
    if (sleeping) {
      wake();
      return;
    }
    if (!home || embedded_ai || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
      make_home();
      return;
    }
    sleeping = true;
    bsp_display_backlight_off();
    return;
  }
  if (c.kind == KEY2) {
    auto *a = phone->getManager().getActiveApp();
    if (a && strstr(a->getName(), "Music")) {
      auto s = audio_player_get_state();
      if (s == AUDIO_PLAYER_STATE_PLAYING)
        audio_player_pause();
      else if (s == AUDIO_PLAYER_STATE_PAUSE)
        audio_player_resume();
      return;
    }
    voice();
    return;
  }
  if (c.kind == OPEN_APP) {
    App *a = c.value < 0                       ? find_app("Settings")
             : c.value < (int)installed.size() ? installed[c.value]
                                               : nullptr;
    launch(a, false);
    return;
  }
  if (c.kind == STATUS) {
    status_open = !status_open;
    transient_until = lv_tick_get() + 6000;
    return;
  }
}
void create_chrome() {
  chrome = lv_obj_create(lv_layer_top());
  clean(chrome);
  lv_obj_set_size(chrome, 466, 466);
  lv_obj_set_pos(chrome, 0, 0);
  lv_obj_remove_flag(chrome, LV_OBJ_FLAG_CLICKABLE);
  chrome_header = box(chrome, 0, 0, 466, 80, BG);
  lv_obj_remove_flag(chrome_header, LV_OBJ_FLAG_CLICKABLE);
  island = box(chrome, 215, 39, 36, 36, 0x14251e, 18);
  action(island, STATUS);
  lv_obj_set_style_border_width(island, 1, 0);
  lv_obj_set_style_border_color(island, lv_color_hex(0x36584b), 0);
  island_icon =
      label(island, LV_SYMBOL_WIFI, 0, 10, 36, &lv_font_montserrat_16, MUTED);
  island_label = label(island, "", 39, 11, 207, &round_font_18);
  lv_label_set_long_mode(island_label, LV_LABEL_LONG_DOT);
  lv_obj_set_height(island_label, 24);
  wifi_detail = label(island, "", 15, 62, 234, &round_font_18);
  battery_detail = label(island, "", 15, 100, 234, &round_font_18);
  detail_hint = label(island, "轻触收起", 15, 144, 234, &round_font_18, MUTED);
  hidden(island_label, true);
  hidden(wifi_detail, true);
  hidden(battery_detail, true);
  hidden(detail_hint, true);
}
void update_chrome(uint32_t now) {
  hidden(chrome, sleeping);
  hidden(chrome_header, !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN));
  if (sleeping || now - last_status < 250)
    return;
  last_status = now;
  auto *old = phone->getDisplay().getStatusBar();
  if (old && old->checkVisible())
    old->setVisualMode(
        esp_brookesia::systems::phone::StatusBar::VisualMode::HIDE);
  if (status_open && (int32_t)(transient_until - now) <= 0)
    status_open = false;
  AI::RoundSnapshot ai{};
  bool ai_active =
      embedded_ai && AI::requestInstance()->round_snapshot(ai) && ai.state != 4;
  int mode = status_open                                           ? 2
             : (ai_active || (int32_t)(transient_until - now) > 0) ? 1
                                                                   : 0;
  brookesia::system_status::Snapshot status{};
  brookesia::system_status::get_snapshot(status);
  if (mode != chrome_mode) {
    chrome_mode = mode;
    lv_anim_delete(island, nullptr);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, island);
    lv_anim_set_duration(&a, 240);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_values(&a, lv_obj_get_width(island),
                       mode == 2   ? 264
                       : mode == 1 ? 244
                                   : 36);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
      auto *obj = static_cast<lv_obj_t *>(o);
      lv_obj_set_width(obj, v);
      lv_obj_set_x(obj, (466 - v) / 2);
    });
    lv_anim_start(&a);
    lv_anim_set_values(&a, lv_obj_get_height(island),
                       mode == 2   ? 184
                       : mode == 1 ? 44
                                   : 36);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
      lv_obj_set_height(static_cast<lv_obj_t *>(o), v);
    });
    lv_anim_start(&a);
    hidden(island_label, mode == 0);
    hidden(wifi_detail, mode != 2);
    hidden(battery_detail, mode != 2);
    hidden(detail_hint, mode != 2);
    lv_obj_set_x(island_icon, mode ? 10 : 0);
  }
  const lv_color_t color = lv_color_hex(status.wifi_connected ? ACCENT : MUTED);
  if (!lv_color_eq(lv_obj_get_style_text_color(island_icon, LV_PART_MAIN),
                   color))
    lv_obj_set_style_text_color(island_icon, color, 0);
  set_text(island_icon, ai_active ? LV_SYMBOL_AUDIO : LV_SYMBOL_WIFI);
  if (mode == 1)
    set_text(island_label, ai_active ? ai.status : transient);
  if (mode == 2) {
    set_text(island_label, "连接与电量");
    set_text(wifi_detail,
             status.wifi_connected ? "Wi-Fi  已连接" : "Wi-Fi  未连接");
    char battery[64];
    if (status.battery_present && status.battery_valid)
      snprintf(battery, sizeof(battery), "%s  %d%%",
               status.charging ? "正在充电" : "剩余电量",
               status.battery_percent);
    else
      snprintf(battery, sizeof(battery), "USB 供电 / 电量未知");
    set_text(battery_detail, battery);
  }
}
void tick(lv_timer_t *) {
  Command c;
  while (xQueueReceive(commands, &c, 0) == pdTRUE) {
    handle(c);
    printf("ROUND_UI %d\n", c.kind);
  }
  if (!embedded_ai && lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN) &&
      !phone->getManager().getActiveApp())
    make_home(false);
  const uint32_t now = lv_tick_get();
  update_chrome(now);
  if (!home || sleeping || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN))
    return;
  if (now - last_clock >= 1000 || last_clock == 0) {
    time_t t = time(nullptr);
    tm tm{};
    localtime_r(&t, &tm);
    char clock[32];
    if (tm.tm_year < 124)
      snprintf(clock, sizeof(clock), "--:--");
    else
      strftime(clock, sizeof(clock), "%H:%M", &tm);
    set_text(clock_label, clock);
    char date[80];
    const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    if (tm.tm_year >= 124)
      snprintf(date, sizeof(date), "%d月%d日 · 星期%s", tm.tm_mon + 1,
               tm.tm_mday, weekdays[tm.tm_wday]);
    else
      snprintf(date, sizeof(date), "等待校时");
    set_text(date_label, date);
    last_clock = now;
  }
  AI::RoundSnapshot ai{};
  bool has_ai = embedded_ai && AI::requestInstance()->round_snapshot(ai);
  const bool active = has_ai && ai.state != 4;
  if (pending_ai && has_ai && ai.state == 4) {
    AI::requestInstance()->round_press();
    pending_ai = false;
  }
  if (has_ai) {
    if (ai.state != last_ai_state) {
      set_text(headline, ai.status);
      last_ai_state = ai.state;
      ESP_LOGI(TAG, "AI state=%d", ai.state);
    }
    const char *content = ai.activation[0] ? ai.activation : ai.text;
    bool has_text = content[0] != 0;
    if (strcmp(last_text, content)) {
      strlcpy(last_text, content, sizeof(last_text));
      set_text(reply, content);
    }
    const lv_font_t *font = AI::requestInstance()->round_font();
    if (font && lv_obj_get_style_text_font(reply, LV_PART_MAIN) != font)
      lv_obj_set_style_text_font(reply, font, 0);
    hidden(reply, !has_text);
    hidden(headline, has_text);
    hidden(subtitle, has_text);
    if (has_text != orb_small) {
      orb_small = has_text;
      const int size = has_text ? 104 : 136;
      lv_obj_set_size(orb_button, size, size);
      lv_obj_set_pos(orb_button, (466 - size) / 2, has_text ? 156 : 184);
      if (orb_image_obj)
        lv_image_set_scale(orb_image_obj, has_text ? 196 : 256);
      lv_obj_set_size(orbit, has_text ? 94 : 122, has_text ? 94 : 122);
      lv_obj_center(orbit);
      for (int i = 0; i < 3; i++)
        lv_obj_set_x(bars[i], size / 2 - 18 + i * 13);
    }
    if (!has_text)
      set_text(subtitle, "再次轻触控制 · 按键 1 退出");
  } else if (now - last_copy >= 14000) {
    idle_index = (idle_index + 1) % 4;
    set_text(headline, INVITATIONS[idle_index]);
    set_text(subtitle, HINTS[idle_index]);
    last_copy = now;
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, headline);
    lv_anim_set_values(&fade, 0, 255);
    lv_anim_set_duration(&fade, 400);
    lv_anim_set_exec_cb(&fade, [](void *o, int32_t value) {
      lv_obj_set_style_text_color(
          static_cast<lv_obj_t *>(o),
          lv_color_mix(lv_color_hex(0xd7e5dc), lv_color_hex(MUTED), value), 0);
    });
    lv_anim_start(&fade);
  }
  const int angle = (now / (active ? 12 : 20)) % 360;
  lv_arc_set_rotation(orbit, angle);
  for (int i = 0; i < 3; i++) {
    int h = active   ? 14 + (int)(22 * (.5 + .5 * std::sin(now / 190.0 + i)))
            : i == 1 ? 34
                     : 22;
    if (lv_obj_get_height(bars[i]) != h)
      lv_obj_set_height(bars[i], h);
    const int y = ((orb_small ? 104 : 136) - h) / 2;
    if (lv_obj_get_y(bars[i]) != y)
      lv_obj_set_y(bars[i], y);
  }
}

void key_task(void *) {
  esp_io_expander_handle_t expander = nullptr;
  bool pwr_ok =
      bsp_io_expander_try_init(&expander) == ESP_OK &&
      esp_io_expander_set_dir(expander, 1u << 4, IO_EXPANDER_INPUT) == ESP_OK;
  gpio_config_t cfg{};
  cfg.pin_bit_mask = 1ULL << 0;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&cfg);
  int stable[2] = {0, 0}, candidate[2] = {0, 0}, count[2] = {0, 0};
  uint32_t pressed_at[2] = {0, 0};
  bool armed[2] = {false, false};
  for (;;) {
    uint32_t level = 0;
    int pwr = -1;
    if (pwr_ok &&
        esp_io_expander_get_level(expander, 1u << 4, &level) == ESP_OK)
      pwr = !!(level & (1u << 4));
    int boot = gpio_get_level(GPIO_NUM_0);
    raw_pwr = pwr;
    raw_boot = boot;
    int vals[2] = {pwr, boot < 0 ? -1 : !boot};
    for (int i = 0; i < 2; i++) {
      if (vals[i] < 0)
        continue;
      if (vals[i] != candidate[i]) {
        candidate[i] = vals[i];
        count[i] = 1;
      } else if (count[i] < 3)
        count[i]++;
      if (count[i] == 3) {
        if (!vals[i] && !armed[i])
          armed[i] = true;
        if (vals[i] != stable[i]) {
          stable[i] = vals[i];
          if (vals[i])
            pressed_at[i] = xTaskGetTickCount();
          else if (armed[i] &&
                   xTaskGetTickCount() - pressed_at[i] < pdMS_TO_TICKS(650))
            queue(i == 0 ? KEY1 : KEY2);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
void snapshot() {
  lv_draw_buf_t *frame = nullptr;
  if (esp_lv_adapter_lock(1000) == ESP_OK) {
    if (!lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
      frame = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_RGB565);
    } else {
      auto *active = lv_screen_active();
      lv_area_t area{};
      lv_obj_get_coords(active, &area);
      auto *app_frame = lv_snapshot_take(active, LV_COLOR_FORMAT_RGB565);
      auto *chrome_frame = lv_snapshot_take(chrome, LV_COLOR_FORMAT_ARGB8888);
      if (app_frame && chrome_frame) {
        auto *surface = box(nullptr, 0, 0, 466, 466, BG);
        lv_image_dsc_t app_image{}, chrome_image{};
        lv_draw_buf_to_image(app_frame, &app_image);
        lv_draw_buf_to_image(chrome_frame, &chrome_image);
        auto *app = lv_image_create(surface);
        lv_image_set_src(app, &app_image);
        lv_obj_set_pos(app, area.x1, area.y1);
        auto *overlay = lv_image_create(surface);
        lv_image_set_src(overlay, &chrome_image);
        lv_obj_set_pos(overlay, 0, 0);
        frame = lv_snapshot_take(surface, LV_COLOR_FORMAT_RGB565);
        lv_obj_delete(surface);
      }
      if (app_frame)
        lv_draw_buf_destroy(app_frame);
      if (chrome_frame)
        lv_draw_buf_destroy(chrome_frame);
    }
    esp_lv_adapter_unlock();
  }
  if (!frame) {
    puts("ROUND_FRAME_ERROR no-buffer");
    return;
  }
  uint32_t checksum = 2166136261u;
  for (uint32_t i = 0; i < frame->data_size; ++i)
    checksum = (checksum ^ frame->data[i]) * 16777619u;
  printf("ROUND_FRAME_BEGIN %u %u %u %lu %08lx\n", frame->header.w,
         frame->header.h, frame->header.stride, (unsigned long)frame->data_size,
         (unsigned long)checksum);
  constexpr char HEX[] = "0123456789abcdef";
  char line[1050];
  for (uint32_t i = 0; i < frame->data_size; i += 256) {
    uint32_t n = std::min<uint32_t>(256, frame->data_size - i);
    int p = snprintf(line, sizeof(line), "ROUND_DATA %lu ", (unsigned long)i);
    for (uint32_t j = 0; j < n; j++) {
      uint8_t b = frame->data[i + j];
      line[p++] = HEX[b >> 4];
      line[p++] = HEX[b & 15];
    }
    line[p] = 0;
    puts(line);
    vTaskDelay(1);
  }
  puts("ROUND_FRAME_END");
  if (esp_lv_adapter_lock(1000) == ESP_OK) {
    lv_draw_buf_destroy(frame);
    esp_lv_adapter_unlock();
  }
}
void serial_task(void *) {
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t cfg =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 4096;
    cfg.rx_buffer_size = 1024;
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
      ESP_LOGE(TAG, "USB diagnostic console unavailable");
      vTaskDelete(nullptr);
      return;
    }
  }
  usb_serial_jtag_vfs_use_driver();
  char command[128] = {};
  size_t len = 0;
  for (;;) {
    char ch;
    if (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100)) != 1)
      continue;
    if (ch == '\n' || ch == '\r') {
      command[len] = 0;
      if (!strcmp(command, "round snap"))
        snapshot();
      else if (!strcmp(command, "round home"))
        queue(HOME);
      else if (!strcmp(command, "round apps"))
        queue(APPS);
      else if (!strcmp(command, "round key1"))
        queue(KEY1);
      else if (!strcmp(command, "round key2"))
        queue(KEY2);
      else if (!strcmp(command, "round settings"))
        queue(OPEN_APP, -1);
      else if (!strncmp(command, "round time ", 11)) {
        long long epoch;
        char zone[96], extra;
        bool ok =
            sscanf(command + 11, "%lld %95s %c", &epoch, zone, &extra) == 2 &&
            round_clock_sync(time_t(epoch), zone);
        printf("ROUND_TIME ok=%d rtc=%d epoch=%lld source=%s\n", ok,
               round_clock_rtc_valid(), (long long)time(nullptr),
               round_clock_source());
      } else if (!strcmp(command, "round status")) {
        printf("ROUND_STATUS version=0.1.0-alpha.2 chip=esp32s3 pwr=%d boot=%d "
               "internal_free=%u psram_free=%u epoch=%lld clock=%s rtc=%d\n",
               raw_pwr.load(), raw_boot.load(),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               (long long)time(nullptr), round_clock_source(),
               round_clock_rtc_valid());
      }
      len = 0;
    } else if (len < sizeof(command) - 1)
      command[len++] = ch;
    else
      len = 0;
  }
}
} // namespace
void round_shell_start(Phone *p) {
  phone = p;
  commands = xQueueCreate(12, sizeof(Command));
  if (!commands) {
    ESP_LOGE(TAG, "Cannot allocate input queue");
    return;
  }
  for (int i = 0; i < 64; i++) {
    auto *a = p->getManager().getInstalledApp(i);
    if (a)
      installed.push_back(a);
  }
  // Keep daily tools ahead of upstream diagnostic/demo applications.
  auto rank = [](App *a) {
    const char *keys[] = {"AIChats",  "Xiaozhi",   "Settings", "Music",
                          "Gravity",  "Spec",      "Video",    "Gallery",
                          "Recorder", "Calculator"};
    for (int i = 0; i < 10; ++i)
      if (strstr(a->getName(), keys[i]))
        return i;
    return 20;
  };
  std::stable_sort(installed.begin(), installed.end(),
                   [&](App *a, App *b) { return rank(a) < rank(b); });
  round_clock_start();
  if (esp_lv_adapter_lock(-1) != ESP_OK)
    return;
  root = box(lv_layer_top(), 0, 0, 466, 466, BG, 0);
  create_chrome();
  lv_obj_set_style_clip_corner(root, false,
                               0); // The physical AMOLED masks its corners.
  make_home(false);
  lv_timer_create(tick, 20, nullptr);
  esp_lv_adapter_unlock();
  xTaskCreate(key_task, "round_keys", 4096, nullptr, 4, nullptr);
  xTaskCreate(serial_task, "round_usb", 6144, nullptr, 3, nullptr);
  ESP_LOGI(TAG, "READY version=0.1.0-alpha.2 apps=%u ui=466x466",
           (unsigned)installed.size());
}
