#include <Arduino.h>
#include "ui.h"
#include "../app_data.h"
#include "../serial_util.h"

static const lv_coord_t KB_H = 250;
static const lv_coord_t CLIP_H = 26;  // clipboard strip shown above the keyboard
static const lv_coord_t BODY_H = SCR_H - HEADER_H - FOOTER_H;

static lv_obj_t *s_body, *s_footer, *s_kb, *s_clip_lbl;
static lv_obj_t *s_dd_model, *s_spec_lbl;
static lv_obj_t *s_cb[NUM_CH], *s_ta[NUM_CH], *s_row[NUM_CH];
static char s_clip[SERIAL_MAX_LEN + 1];

// Serial keyboard: digits, upper-case letters, a few separators, plus COPY / PASTE / CLEAR.
static const char *KB_MAP[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
  "A", "S", "D", "F", "G", "H", "J", "K", "L", "-", "\n",
  "Z", "X", "C", "V", "B", "N", "M", "/", "_", ".", "\n",
  "COPY", "PASTE", "CLEAR", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

#define K(w) (lv_btnmatrix_ctrl_t)(w)
#define KS(w) (lv_btnmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BTN_FLAGS | (w))
static const lv_btnmatrix_ctrl_t KB_CTRL[] = {
  K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), KS(6),
  K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4),
  K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4),
  K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4),
  KS(4), KS(4), KS(4), KS(2), KS(2), KS(4)};

static int ta_index(lv_obj_t *ta) {
  for (int i = 0; i < NUM_CH; i++) {
    if (s_ta[i] == ta) return i;
  }
  return -1;
}

static void update_clip_label() {
  lv_label_set_text_fmt(s_clip_lbl, "Clipboard: %s", s_clip[0] ? s_clip : "(empty)");
}

static void show_kb(lv_obj_t *ta) {
  lv_keyboard_set_textarea(s_kb, ta);
  lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_clip_lbl, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(s_body, SCR_H - HEADER_H - KB_H - CLIP_H);
  lv_obj_update_layout(s_body);
  int i = ta_index(ta);
  if (i >= 0) lv_obj_scroll_to_view(s_row[i], LV_ANIM_ON);
}

static void hide_kb() {
  lv_obj_t *ta = lv_keyboard_get_textarea(s_kb);
  if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(s_kb, nullptr);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_clip_lbl, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_height(s_body, BODY_H);
}

static void clear_error(int i) { lv_obj_remove_local_style_prop(s_ta[i], LV_STYLE_BORDER_COLOR, 0); }

static void mark_error(int i) {
  lv_obj_set_style_border_color(s_ta[i], COL_FAIL, 0);
  lv_obj_set_style_border_width(s_ta[i], 3, 0);
}

static void kb_event_cb(lv_event_t *e) {
  lv_obj_t *kb = lv_event_get_target(e);
  uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  if (id == LV_BTNMATRIX_BTN_NONE) return;
  const char *txt = lv_btnmatrix_get_btn_text(kb, id);
  if (!txt) return;
  lv_obj_t *ta = lv_keyboard_get_textarea(kb);

  if (strcmp(txt, "COPY") == 0) {
    if (ta) strlcpy(s_clip, lv_textarea_get_text(ta), sizeof(s_clip));
    update_clip_label();
    return;
  }
  if (strcmp(txt, "PASTE") == 0) {
    if (ta && s_clip[0]) lv_textarea_add_text(ta, s_clip);
    return;
  }
  if (strcmp(txt, "CLEAR") == 0) {
    if (ta) lv_textarea_set_text(ta, "");
    return;
  }
  lv_keyboard_def_event_cb(e);  // normal keys, backspace, cursor, OK
}

static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  int i = ta_index(ta);
  if (i < 0) return;

  if (code == LV_EVENT_FOCUSED) {
    show_kb(ta);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (lv_keyboard_get_textarea(s_kb) == ta) hide_kb();
  } else if (code == LV_EVENT_VALUE_CHANGED) {
    strlcpy(g_app.serial[i], lv_textarea_get_text(ta), sizeof(g_app.serial[i]));
    clear_error(i);
    if (g_app.serial[i][0] && !g_app.chEnabled[i]) {
      g_app.chEnabled[i] = true;
      lv_obj_add_state(s_cb[i], LV_STATE_CHECKED);
    }
  }
}

static void cb_event_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  g_app.chEnabled[i] = lv_obj_has_state(s_cb[i], LV_STATE_CHECKED);
  if (!g_app.chEnabled[i]) clear_error(i);
}

static bool fill_from_previous(int i) {
  const char *prev = lv_textarea_get_text(s_ta[i - 1]);
  if (!prev[0]) return false;
  char next[SERIAL_MAX_LEN + 2];
  serial_increment(prev, next, sizeof(next));
  next[SERIAL_MAX_LEN] = '\0';
  lv_textarea_set_text(s_ta[i], next);
  return true;
}

static void plus_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (!fill_from_previous(i)) ui_toast("Serial", "Enter the CH%d serial first.", i);
}

static void autofill_cb(lv_event_t *) {
  if (!lv_textarea_get_text(s_ta[0])[0]) {
    ui_toast("Auto fill", "Enter the CH1 serial first.");
    return;
  }
  for (int i = 1; i < NUM_CH; i++) {
    if (!lv_textarea_get_text(s_ta[i])[0]) fill_from_previous(i);
  }
}

static void update_spec_label() {
  if (g_app.modelIdx < 0) {
    lv_label_set_text(s_spec_lbl, "Select a model to see its limits.");
    return;
  }
  const ModelSpec &m = g_models[g_app.modelIdx];
  char ir[40];
  if (m.hasInrush()) snprintf(ir, sizeof(ir), "%.2f - %.2f A", m.inrushLsl, m.inrushUsl);
  else strlcpy(ir, "NA", sizeof(ir));
  lv_label_set_text_fmt(s_spec_lbl, "In-rush: %s     Continuous: %.2f - %.2f A     Cycles: %d", ir,
                        m.contLsl, m.contUsl, m.cycles);
}

static void model_cb(lv_event_t *) {
  g_app.modelIdx = (int)lv_dropdown_get_selected(s_dd_model) - 1;
  update_spec_label();
}

static void logout_cb(lv_event_t *) {
  g_app.operatorName[0] = '\0';
  ui_show_login();
}

static void setup_cb(lv_event_t *) { ui_show_setup(ui_show_model); }

static void proceed_cb(lv_event_t *) {
  if (g_app.modelIdx < 0) {
    ui_toast("Check", "Please select a model.");
    return;
  }
  int enabled = 0;
  bool ok = true;
  for (int i = 0; i < NUM_CH; i++) {
    clear_error(i);
    if (!g_app.chEnabled[i]) continue;
    enabled++;
    if (!g_app.serial[i][0]) {
      mark_error(i);
      ok = false;
    }
  }
  if (enabled == 0) {
    ui_toast("Check", "Tick at least one channel.");
    return;
  }
  if (!ok) {
    ui_toast("Check", "Enter a serial number for every ticked channel.");
    return;
  }
  for (int i = 0; i < NUM_CH; i++) {
    for (int j = i + 1; j < NUM_CH; j++) {
      if (g_app.chEnabled[i] && g_app.chEnabled[j] && strcmp(g_app.serial[i], g_app.serial[j]) == 0) {
        mark_error(i);
        mark_error(j);
        ok = false;
      }
    }
  }
  if (!ok) {
    ui_toast("Check", "Duplicate serial numbers (highlighted in red).");
    return;
  }
  ui_show_test();
}

static lv_obj_t *make_row(lv_obj_t *parent, lv_coord_t h) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), h);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 12, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

void ui_show_model() {
  lv_obj_t *scr = ui_screen_create();
  char title[80];
  snprintf(title, sizeof(title), "Operator: %s", g_app.operatorName);
  ui_header(scr, title);

  // Scrollable body: model selector + 5 channel rows
  s_body = lv_obj_create(scr);
  lv_obj_set_size(s_body, SCR_W, BODY_H);
  lv_obj_set_pos(s_body, 0, HEADER_H);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_body, 0, 0);
  lv_obj_set_style_radius(s_body, 0, 0);
  lv_obj_set_style_pad_hor(s_body, 20, 0);
  lv_obj_set_style_pad_ver(s_body, 8, 0);
  lv_obj_set_style_pad_row(s_body, 4, 0);
  lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_body, LV_DIR_VER);

  lv_obj_t *mrow = make_row(s_body, 48);
  lv_obj_t *ml = lv_label_create(mrow);
  lv_label_set_text(ml, "Model");
  lv_obj_set_style_text_font(ml, FONT_M, 0);
  lv_obj_set_width(ml, 110);

  static char opts[(NAME_MAX_LEN + 2) * (MAX_MODELS + 1)];
  strlcpy(opts, "-- Select model --", sizeof(opts));
  for (int i = 0; i < g_modelCount; i++) {
    strlcat(opts, "\n", sizeof(opts));
    strlcat(opts, g_models[i].name, sizeof(opts));
  }
  s_dd_model = lv_dropdown_create(mrow);
  lv_dropdown_set_options(s_dd_model, opts);
  lv_dropdown_set_selected(s_dd_model, g_app.modelIdx + 1);
  lv_obj_set_width(s_dd_model, 560);
  lv_obj_set_style_text_font(s_dd_model, FONT_M, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(s_dd_model), FONT_M, 0);
  lv_obj_add_event_cb(s_dd_model, model_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  s_spec_lbl = lv_label_create(s_body);
  lv_obj_set_style_text_color(s_spec_lbl, COL_MUTED, 0);
  update_spec_label();

  for (int i = 0; i < NUM_CH; i++) {
    lv_obj_t *row = make_row(s_body, 46);
    s_row[i] = row;

    s_cb[i] = lv_checkbox_create(row);
    char name[8];
    snprintf(name, sizeof(name), "CH%d", i + 1);
    lv_checkbox_set_text(s_cb[i], name);
    lv_obj_set_style_text_font(s_cb[i], FONT_M, 0);
    lv_obj_set_width(s_cb[i], 110);
    if (g_app.chEnabled[i]) lv_obj_add_state(s_cb[i], LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_cb[i], cb_event_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);

    s_ta[i] = lv_textarea_create(row);
    lv_textarea_set_one_line(s_ta[i], true);
    lv_textarea_set_max_length(s_ta[i], SERIAL_MAX_LEN);
    lv_textarea_set_placeholder_text(s_ta[i], "Serial number");
    lv_textarea_set_text(s_ta[i], g_app.serial[i]);
    lv_obj_set_width(s_ta[i], 480);
    lv_obj_set_style_text_font(s_ta[i], FONT_M, 0);
    lv_obj_add_event_cb(s_ta[i], ta_event_cb, LV_EVENT_ALL, nullptr);

    if (i > 0) {
      lv_obj_t *p = ui_btn(row, LV_SYMBOL_PLUS, COL_PRIMARY, 80, 42, plus_cb, (void *)(intptr_t)i);
      (void)p;
    }
  }

  s_footer = ui_footer(scr);
  ui_btn(s_footer, LV_SYMBOL_LEFT " LOGOUT", COL_NEUTRAL, 170, BTN_H, logout_cb, nullptr);
  ui_btn(s_footer, LV_SYMBOL_LIST " SETUP", COL_NEUTRAL, 160, BTN_H, setup_cb, nullptr);
  ui_btn(s_footer, LV_SYMBOL_PLUS " AUTO FILL", COL_PRIMARY, 200, BTN_H, autofill_cb, nullptr);
  ui_btn(s_footer, "PROCEED " LV_SYMBOL_RIGHT, COL_OK, 200, BTN_H, proceed_cb, nullptr);

  s_kb = lv_keyboard_create(scr);
  lv_obj_set_size(s_kb, SCR_W, KB_H);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_USER_1, KB_MAP, KB_CTRL);
  lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_USER_1);
  lv_obj_set_style_text_font(s_kb, FONT_M, 0);
  lv_obj_remove_event_cb(s_kb, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

  s_clip_lbl = lv_label_create(scr);
  lv_obj_set_size(s_clip_lbl, SCR_W, CLIP_H);
  lv_obj_set_style_bg_color(s_clip_lbl, COL_LABEL_BG, 0);
  lv_obj_set_style_bg_opa(s_clip_lbl, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(s_clip_lbl, 14, 0);
  lv_obj_set_style_pad_top(s_clip_lbl, 3, 0);
  lv_obj_set_style_text_color(s_clip_lbl, COL_MUTED, 0);
  lv_obj_align(s_clip_lbl, LV_ALIGN_BOTTOM_LEFT, 0, -KB_H);
  update_clip_label();
  lv_obj_add_flag(s_clip_lbl, LV_OBJ_FLAG_HIDDEN);

  ui_load(scr);
}
