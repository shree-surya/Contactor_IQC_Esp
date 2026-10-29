#include "app_data.h"

ModelSpec g_models[MAX_MODELS];
int g_modelCount = 0;
char g_operators[MAX_OPERATORS][NAME_MAX_LEN + 1];
char g_operatorPass[MAX_OPERATORS][PASS_MAX_LEN + 1];
int g_operatorCount = 0;
AppState g_app;

static void add_model(const char *name, float irL, float irU, float cL, float cU) {
  if (g_modelCount >= MAX_MODELS) return;
  ModelSpec &m = g_models[g_modelCount++];
  strlcpy(m.name, name, sizeof(m.name));
  m.inrushLsl = irL;
  m.inrushUsl = irU;
  m.contLsl = cL;
  m.contUsl = cU;
  app_data_default_timings(m);
}

void app_data_default_timings(ModelSpec &m) {
  m.inrushWinMs = 1000;
  m.settleMs = 2000;
  m.avgMs = 3000;
  m.releaseMs = 500;
  m.cycles = 5;
  m.cycleGapMs = 5000;
  m.staggerMs = 1500;
}

void app_data_set_models(const ModelSpec *models, int count) {
  if (count <= 0) return;
  if (count > MAX_MODELS) count = MAX_MODELS;
  memcpy(g_models, models, sizeof(ModelSpec) * count);
  g_modelCount = count;
  g_app.modelIdx = -1;
}

void app_data_set_operators(const char names[][NAME_MAX_LEN + 1], const char pass[][PASS_MAX_LEN + 1], int count) {
  if (count <= 0) return;
  if (count > MAX_OPERATORS) count = MAX_OPERATORS;
  for (int i = 0; i < count; i++) {
    strlcpy(g_operators[i], names[i], NAME_MAX_LEN + 1);
    strlcpy(g_operatorPass[i], pass[i], PASS_MAX_LEN + 1);
  }
  g_operatorCount = count;
}

bool app_data_check_password(int op, const char *password) {
  if (op < 0 || op >= g_operatorCount) return false;
  return strcmp(g_operatorPass[op], password) == 0;
}

static void add_operator(const char *name) {
  if (g_operatorCount >= MAX_OPERATORS) return;
  g_operatorPass[g_operatorCount][0] = '\0';  // built-in placeholders have no password
  strlcpy(g_operators[g_operatorCount++], name, NAME_MAX_LEN + 1);
}

void app_data_init() {
  // Defaults copied from "Contactor IQC Setup.xlsx"
  g_modelCount = 0;
  add_model("TG Charge (BSBC7-600B)", 1.2f, 3.3f, 0.20f, 0.35f);
  add_model("TG Discharge (BSBC7-200)", NAN, NAN, 0.50f, 0.70f);
  add_model("PL Charge (BSBC7-350)", 1.2f, 1.7f, 0.16f, 0.25f);
  add_model("PL Discharge (BSBC7-100)", NAN, NAN, 0.40f, 0.55f);

  // Placeholder names until the "Operators" sheet is connected
  g_operatorCount = 0;
  add_operator("Operator 1");
  add_operator("Operator 2");
  add_operator("Operator 3");

  memset(&g_app, 0, sizeof(g_app));
  g_app.modelIdx = -1;
}
