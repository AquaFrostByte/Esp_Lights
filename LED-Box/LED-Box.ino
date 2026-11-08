// Diy-Led - LED box firmware (MQTT + Home Assistant edition)
//
// The polarity flipping is done entirely by the LEDC hardware peripheral:
// two PWM channels with the same frequency, the second one shifted by half a
// period. IN1 and IN2 therefore alternate on their own, with no CPU time and
// no dependency on Wi-Fi / MQTT timing. Brightness = duty cycle of each half.
//
// Home Assistant: the box announces itself via MQTT discovery as a light with
// brightness and the effects "static" / "fade". No YAML needed.
//
// Needs: arduino-esp32 core 3.x, library "PubSubClient" (Nick O'Leary).
// Copy config.example.h to config.h and fill it in.

#include <WiFi.h>
#include <PubSubClient.h>
#include <driver/ledc.h>
#include <strings.h>
#include "config.h"

// ---------- pins (L293D) ----------
constexpr uint8_t PIN_IN1 = 27;   // L293D 1A
constexpr uint8_t PIN_IN2 = 26;   // L293D 2A

// ---------- PWM ----------
constexpr uint32_t PWM_FREQ = 2000;                 // polarity flips 4000x/s
constexpr ledc_timer_bit_t PWM_BITS = LEDC_TIMER_10_BIT;
constexpr uint32_t PWM_PERIOD = 1UL << 10;          // 1024 counts
constexpr uint32_t PWM_HALF = PWM_PERIOD / 2;       // 512 counts per polarity

// ---------- MQTT topics (all retained) ----------
#define T_POWER  MQTT_PREFIX "/power"       // ON / OFF
#define T_EFFECT MQTT_PREFIX "/effect"      // static / fade
#define T_BRIGHT MQTT_PREFIX "/brightness"  // 0..255
#define T_STATUS MQTT_PREFIX "/status"      // online / offline (last will)

#define T_DISCOVERY HA_DISCOVERY_PREFIX "/light/" MQTT_CLIENT_ID "/config"

// Home Assistant MQTT discovery payload (MQTT light, default schema).
// Command and state share a topic; everything is retained, so HA, the web page
// and the box always agree.
const char HA_CONFIG[] =
  R"({"name":"LED Box","unique_id":")" MQTT_CLIENT_ID R"(",)"
  R"("command_topic":")" T_POWER R"(","state_topic":")" T_POWER R"(",)"
  R"("payload_on":"ON","payload_off":"OFF",)"
  R"("brightness_command_topic":")" T_BRIGHT R"(","brightness_state_topic":")" T_BRIGHT R"(",)"
  R"("brightness_scale":255,)"
  R"("effect_command_topic":")" T_EFFECT R"(","effect_state_topic":")" T_EFFECT R"(",)"
  R"("effect_list":["static","fade"],)"
  R"("availability_topic":")" T_STATUS R"(",)"
  R"("payload_available":"online","payload_not_available":"offline",)"
  R"("qos":1,"retain":true,)"
  R"("device":{"identifiers":[")" MQTT_CLIENT_ID R"("],"name":"LED Box",)"
  R"("manufacturer":"DIY","model":"ESP32 + L293D"}})";

enum Effect : uint8_t { EFFECT_STATIC = 0, EFFECT_FADE = 1 };

volatile bool power = true;
volatile uint8_t effect = EFFECT_STATIC;
volatile uint8_t brightness = 125;
volatile bool dirty = true;   // set when anything changed

WiFiClient wifi;
PubSubClient mqtt(wifi);

// ---------- hardware PWM ----------
void pwmBegin() {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = PWM_BITS;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = PWM_FREQ;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer);

  const int pins[2] = {PIN_IN1, PIN_IN2};
  for (int i = 0; i < 2; i++) {
    ledc_channel_config_t ch = {};
    ch.gpio_num = pins[i];
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = (ledc_channel_t)i;
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = 0;
    ch.hpoint = i == 0 ? 0 : PWM_HALF;   // IN2 starts half a period later
    ledc_channel_config(&ch);
  }
}

// level 0..255 -> duty 0..PWM_HALF. At 255 each half is on for exactly half the
// period, so the two halves never overlap (no shoot-through) and together they
// cover the whole period = "everything lit".
void setHalf(uint8_t channel, uint8_t level) {
  uint32_t duty = (uint32_t)level * PWM_HALF / 255;
  uint32_t hpoint = channel == 0 ? 0 : PWM_HALF;
  ledc_set_duty_with_hpoint(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel, duty, hpoint);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel);
}

// ---------- effects ----------
void applyStatic() {
  if (!power) {
    setHalf(0, 0);
    setHalf(1, 0);
  } else if (effect == EFFECT_STATIC) {
    setHalf(0, brightness);
    setHalf(1, brightness);
  }
  // EFFECT_FADE is driven by runFade()
}

// One half fades in/out, then the other half. Non-blocking, ~10 s per cycle.
void runFade() {
  static uint32_t last = 0;
  static uint16_t step = 0;
  if (millis() - last < 10) return;
  last = millis();

  uint8_t seg = step >> 8;          // 0..3
  uint8_t i = step & 0xFF;          // 0..255
  uint8_t level = (seg & 1) ? 255 - i : i;
  level = (uint16_t)level * brightness / 255;

  if (seg < 2) { setHalf(1, level); setHalf(0, 0); }
  else         { setHalf(0, level); setHalf(1, 0); }

  step = (step + 1) & 0x3FF;
}

// ---------- MQTT ----------
void onMessage(char* topic, byte* payload, unsigned int len) {
  char buf[16];
  len = min(len, (unsigned int)(sizeof(buf) - 1));
  memcpy(buf, payload, len);
  buf[len] = 0;

  if (strcmp(topic, T_POWER) == 0) {
    if (strcasecmp(buf, "ON") == 0) power = true;
    else if (strcasecmp(buf, "OFF") == 0) power = false;
    else return;
  } else if (strcmp(topic, T_EFFECT) == 0) {
    if (strcasecmp(buf, "static") == 0) effect = EFFECT_STATIC;
    else if (strcasecmp(buf, "fade") == 0) effect = EFFECT_FADE;
    else return;
  } else if (strcmp(topic, T_BRIGHT) == 0) {
    int v = atoi(buf);
    if (v < 0 || v > 255) return;
    brightness = v;
  } else {
    return;
  }
  dirty = true;
}

void mqttReconnect() {
  static uint32_t lastTry = 0;
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastTry < 5000) return;
  lastTry = millis();

  bool ok = mqtt.connect(
      MQTT_CLIENT_ID,
      strlen(MQTT_USER) ? MQTT_USER : nullptr,
      strlen(MQTT_USER) ? MQTT_PASS : nullptr,
      T_STATUS, 1, true, "offline");   // last will
  if (ok) {
    mqtt.publish(T_DISCOVERY, HA_CONFIG, true);   // Home Assistant picks this up
    mqtt.publish(T_STATUS, "online", true);
    mqtt.subscribe(T_POWER, 1);
    mqtt.subscribe(T_EFFECT, 1);
    mqtt.subscribe(T_BRIGHT, 1);
    Serial.println("MQTT connected");
  } else {
    Serial.printf("MQTT failed, rc=%d\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  pwmBegin();           // lights run from here on, independent of the network

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  mqtt.setBufferSize(1024);   // discovery payload is bigger than the 256 byte default
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
}

void loop() {
  mqttReconnect();
  mqtt.loop();

  if (dirty) {
    dirty = false;
    applyStatic();
  }
  if (power && effect == EFFECT_FADE) runFade();
}
