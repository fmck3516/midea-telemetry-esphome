#include "midea_telemetry.h"

#include "esphome/core/log.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome {
namespace midea_telemetry {

static const char *const TAG = "midea_telemetry";

static const uint32_t HALF_PERIOD_US = 1000;
static const uint32_t RESPONSE_DELAY_MS = 60;    // gap between request and response
static const uint32_t INTER_FRAME_DELAY_MS = 56; // gap between cycles
static const uint32_t NO_RESPONSE_RETRY_MS = 500;
static const uint32_t STALE_TIMEOUT_MS = 60000;

// The request table of the inverter tester. The ODU steps through the seven
// response types on its own, so the responses are keyed by their type byte,
// not by the request that happened to trigger them.
static const uint8_t REQUESTS[4][FRAME_SIZE] = {
    {0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x56},
    {0xAA, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55},
    {0xAA, 0x02, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x55},
    {0xAA, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53},
};

// ── conversions, as documented in ─────────────────────────────────────────────
// "Reverse Engineering Midea's ODU Diagnostic Port"

// Ambient/coil temperatures: NTC divider, Beta model; 0x00/0xFF are fault codes.
static float ntc_temp(uint8_t v) {
  if (v == 0)
    return -66.0f;
  if (v == 255)
    return 255.0f;
  float t = 1.0f / (1.0f / 298.15f + logf(0.81f * (255.0f - v) / v) / 4150.0f) - 273.15f;
  return roundf(t * 2.0f) / 2.0f;  // tester rounds to nearest 0.5
}

// Discharge temperature: Steinhart-Hart model; 0x00 and 0xFE/0xFF are fault codes.
static float discharge_temp(uint8_t v) {
  if (v == 0)
    return -48.0f;
  if (v >= 254)
    return v;
  float l = logf((255.0f - v) / v);
  return roundf(1.0f / (2.873e-3f + 2.491e-4f * l + 9.74e-7f * l * l * l) - 273.15f);
}

static float ac_voltage(uint8_t v) { return truncf(v * 32.0f / 25.0f + 40.0f); }

// Compressor current. The 0x01[2] byte only carries a meaningful reading while
// the compressor runs; when it is off (unit OFF or FAN ONLY) the byte sits at a
// per-unit floor (3 on the 115V MRCOOL, 0 on the 220V Cooper & Hunter) that the
// linear model would misdecode as ~1 A. Gate on the actual compressor frequency
// and report the ~0.2 A standby baseline a Fluke 325 clamp meter measured in
// that state instead (issue #16).
static float current_draw(uint8_t v, uint8_t compressor_freq) {
  if (compressor_freq == 0)
    return 0.2f;
  return truncf((0.117f * v + 0.92f) * 100.0f) / 100.0f;
}

// DC bus (inverter DC-link) voltage.
static float dc_bus_voltage(uint8_t v) { return roundf(v * 59.0f / 32.0f - 1.0f); }

static float uint16(uint8_t lo, uint8_t hi) { return lo | (hi << 8); }

// Indoor set-point in Celsius. Two OEM encodings have been observed, told apart
// by range - a real set-point is ~16-32 C and the two ranges don't overlap:
//   whole-degree C   (e.g. Cooper & Hunter): T = byte        -> byte 16..32
//   half-degree C+50 (e.g. MRCOOL):          T = (byte-50)/2 -> byte 82..114
// Still tentative: confirmed on two units only.
static float indoor_setpoint(uint8_t v) { return v < 50 ? v : (v - 50) / 2.0f; }

#ifdef USE_MIDEA_TELEMETRY_JSON
// Format a frame as an uppercase hex string, e.g. "0x55006D457671401F03B0",
// matching the req=/res= notation used by the Arduino sketches.
static std::string frame_hex(const uint8_t *frame) {
  static const char DIGITS[] = "0123456789ABCDEF";
  char buf[3 + FRAME_SIZE * 2];
  char *p = buf;
  *p++ = '0';
  *p++ = 'x';
  for (size_t i = 0; i < FRAME_SIZE; i++) {
    *p++ = DIGITS[frame[i] >> 4];
    *p++ = DIGITS[frame[i] & 0x0F];
  }
  *p = '\0';
  return std::string(buf);
}
#endif

// ── mapped parameters ─────────────────────────────────────────────────────────
// Every decoded parameter - its response type, byte offset(s) and conversion -
// is described exactly once here, so update() (which publishes to sensors) and
// the /json endpoint (which serves every parameter, whether or not a sensor is
// configured for it) can never drift apart.
struct MappedParam {
  const char *name;
  uint8_t type;      // response type read (also the freshness source)
  uint8_t bytes[2];  // byte offset(s) within that response the value derives from
  uint8_t num_bytes;
  float (*decode)(const uint8_t frames[NUM_RESPONSE_TYPES][FRAME_SIZE]);
};

// clang-format off
static const MappedParam MAPPED_PARAMS[] = {
    {"indoor_ambient_temperature",   0x00, {2},    1, [](const uint8_t f[][FRAME_SIZE]) { return ntc_temp(f[0x00][2]); }},
    {"indoor_coil_temperature",      0x00, {3},    1, [](const uint8_t f[][FRAME_SIZE]) { return ntc_temp(f[0x00][3]); }},
    {"outdoor_ambient_temperature",  0x00, {5},    1, [](const uint8_t f[][FRAME_SIZE]) { return ntc_temp(f[0x00][5]); }},
    {"outdoor_coil_temperature",     0x00, {4},    1, [](const uint8_t f[][FRAME_SIZE]) { return ntc_temp(f[0x00][4]); }},
    {"discharge_temperature",        0x00, {6},    1, [](const uint8_t f[][FRAME_SIZE]) { return discharge_temp(f[0x00][6]); }},
    {"ipm_temperature",              0x01, {4},    1, [](const uint8_t f[][FRAME_SIZE]) { return ntc_temp(f[0x01][4]); }},
    {"operating_mode",               0x02, {8},    1, [](const uint8_t f[][FRAME_SIZE]) { return (float) f[0x02][8]; }},
    {"compressor_frequency_target",  0x02, {2},    1, [](const uint8_t f[][FRAME_SIZE]) { return (float) f[0x02][2]; }},
    {"compressor_frequency_actual",  0x02, {3},    1, [](const uint8_t f[][FRAME_SIZE]) { return (float) f[0x02][3]; }},
    {"outdoor_fan_speed",            0x00, {7, 8}, 2, [](const uint8_t f[][FRAME_SIZE]) { return uint16(f[0x00][7], f[0x00][8]); }},
    {"eev_steps",                    0x01, {5, 6}, 2, [](const uint8_t f[][FRAME_SIZE]) { return uint16(f[0x01][5], f[0x01][6]); }},
    {"indoor_setpoint",              0x01, {7},    1, [](const uint8_t f[][FRAME_SIZE]) { return indoor_setpoint(f[0x01][7]); }},
    {"input_voltage",                0x01, {3},    1, [](const uint8_t f[][FRAME_SIZE]) { return ac_voltage(f[0x01][3]); }},
    {"current_draw",                 0x01, {2},    1, [](const uint8_t f[][FRAME_SIZE]) { return current_draw(f[0x01][2], f[0x02][3]); }},
    {"dc_bus_voltage",               0x03, {6},    1, [](const uint8_t f[][FRAME_SIZE]) { return dc_bus_voltage(f[0x03][6]); }},
};
// clang-format on
static const size_t NUM_MAPPED_PARAMS = sizeof(MAPPED_PARAMS) / sizeof(MAPPED_PARAMS[0]);

// ── bus primitives (mirroring inverter-tester-emulator.ino) ───────────────────

void MideaTelemetry::set_line_(InternalGPIOPin *pin, bool level) {
  if (level) {
    pin->pin_mode(gpio::FLAG_INPUT);  // release - bus pull-up brings it high
  } else {
    pin->digital_write(false);
    pin->pin_mode(gpio::FLAG_OUTPUT);
  }
}

void MideaTelemetry::send_bit_(bool bit) {
  this->set_line_(this->dat_pin_, bit);
  this->set_line_(this->clk_pin_, false);
  delayMicroseconds(HALF_PERIOD_US);
  this->set_line_(this->clk_pin_, true);
  delayMicroseconds(HALF_PERIOD_US);
}

bool MideaTelemetry::read_bit_() {
  this->set_line_(this->dat_pin_, true);  // let the ODU drive it instead of us
  this->set_line_(this->clk_pin_, true);
  delayMicroseconds(HALF_PERIOD_US);
  bool bit = this->dat_pin_->digital_read();
  this->set_line_(this->clk_pin_, false);
  delayMicroseconds(HALF_PERIOD_US);
  return bit;
}

void MideaTelemetry::transfer_frame_(const uint8_t *request, uint8_t *response) {
  // send request, LSB-first
  for (size_t byte_index = 0; byte_index < FRAME_SIZE; byte_index++) {
    for (int bit_index = 0; bit_index < 8; bit_index++) {
      this->send_bit_((request[byte_index] >> bit_index) & 1);
    }
  }
  this->set_line_(this->clk_pin_, false);
  this->set_line_(this->dat_pin_, true);
  vTaskDelay(pdMS_TO_TICKS(RESPONSE_DELAY_MS));

  // read response, LSB-first
  memset(response, 0, FRAME_SIZE);
  for (size_t byte_index = 0; byte_index < FRAME_SIZE; byte_index++) {
    for (int bit_index = 0; bit_index < 8; bit_index++) {
      if (this->read_bit_())
        response[byte_index] |= 1 << bit_index;
    }
  }
  this->set_line_(this->clk_pin_, true);
}

// A frame is valid exactly when all ten bytes sum to zero modulo 256.
static bool checksum_valid(const uint8_t *frame) {
  uint8_t sum = 0;
  for (size_t i = 0; i < FRAME_SIZE; i++)
    sum += frame[i];
  return sum == 0;
}

// All lines stayed high: the ODU did not answer.
static bool all_high(const uint8_t *frame) {
  for (size_t i = 0; i < FRAME_SIZE; i++) {
    if (frame[i] != 0xFF)
      return false;
  }
  return true;
}

// ── bus task ──────────────────────────────────────────────────────────────────

void MideaTelemetry::bus_task_trampoline(void *param) { static_cast<MideaTelemetry *>(param)->bus_task_(); }

void MideaTelemetry::bus_task_() {
  // Wake-up sequence, as performed by the inverter tester on power-up.
  this->set_line_(this->clk_pin_, false);
  this->set_line_(this->dat_pin_, false);
  vTaskDelay(pdMS_TO_TICKS(1000));
  this->set_line_(this->clk_pin_, true);
  this->set_line_(this->dat_pin_, true);
  vTaskDelay(pdMS_TO_TICKS(43));
  for (int i = 0; i < 4; i++)
    this->send_bit_(false);

  uint8_t response[FRAME_SIZE];
  while (true) {
    for (const auto &request : REQUESTS) {
      while (true) {
        this->transfer_frame_(request, response);

        if (all_high(response)) {
          xSemaphoreTake(this->lock_, portMAX_DELAY);
          this->no_response_++;
          xSemaphoreGive(this->lock_);
          vTaskDelay(pdMS_TO_TICKS(NO_RESPONSE_RETRY_MS));
          continue;  // keep repeating this request until the ODU answers
        }

        xSemaphoreTake(this->lock_, portMAX_DELAY);
        if (response[0] == 0x55 && response[1] < NUM_RESPONSE_TYPES && checksum_valid(response)) {
          memcpy(this->frames_[response[1]], response, FRAME_SIZE);
          this->frame_ms_[response[1]] = millis();
          this->frame_valid_[response[1]] = true;
          this->frames_ok_++;
        } else {
          this->checksum_errors_++;
        }
        xSemaphoreGive(this->lock_);

        vTaskDelay(pdMS_TO_TICKS(INTER_FRAME_DELAY_MS));
        break;
      }
    }
  }
}

// ── component ─────────────────────────────────────────────────────────────────

void MideaTelemetry::setup() {
  this->clk_pin_->setup();
  this->dat_pin_->setup();
  // release both lines - the bus pull-ups keep them high
  this->set_line_(this->clk_pin_, true);
  this->set_line_(this->dat_pin_, true);

  this->lock_ = xSemaphoreCreateMutex();
  if (this->lock_ == nullptr ||
      xTaskCreatePinnedToCore(MideaTelemetry::bus_task_trampoline, "midea_telemetry", 4096, this, 1, &this->task_, 0) != pdPASS) {
    this->mark_failed();
    return;
  }

#ifdef USE_MIDEA_TELEMETRY_JSON
  // web_server (required when the endpoint is enabled) owns the server lifecycle
  // and calls init() after WiFi is up - we must not init() it here, as this
  // component sets up much earlier (DATA) and starting the HTTP server before
  // the network is ready panics the boot. add_handler() queues our handler; it
  // is attached when web_server initializes the shared server.
  if (this->json_endpoint_ && web_server_base::global_web_server_base != nullptr)
    web_server_base::global_web_server_base->add_handler(this);
#endif
}

void MideaTelemetry::update() {
  uint8_t frames[NUM_RESPONSE_TYPES][FRAME_SIZE];
  bool fresh[NUM_RESPONSE_TYPES];
  const uint32_t now = millis();

  xSemaphoreTake(this->lock_, portMAX_DELAY);
  memcpy(frames, this->frames_, sizeof(frames));
  for (size_t i = 0; i < NUM_RESPONSE_TYPES; i++)
    fresh[i] = this->frame_valid_[i] && now - this->frame_ms_[i] < STALE_TIMEOUT_MS;
  const uint32_t frames_ok = this->frames_ok_;
  const uint32_t checksum_errors = this->checksum_errors_;
  const uint32_t no_response = this->no_response_;
  xSemaphoreGive(this->lock_);

  ESP_LOGD(TAG, "frames ok=%" PRIu32 " checksum errors=%" PRIu32 " no response=%" PRIu32, frames_ok, checksum_errors,
           no_response);

  // Publishes NAN when the frame carrying the value has gone stale, so the
  // entities become unavailable in Home Assistant instead of freezing.
  auto publish = [](sensor::Sensor *s, bool is_fresh, float value) {
    if (s != nullptr)
      s->publish_state(is_fresh ? value : NAN);
  };

  // Same order as MAPPED_PARAMS; the static_assert guards the pairing.
  sensor::Sensor *const sensors[] = {
      this->indoor_ambient_temperature_sensor_,   this->indoor_coil_temperature_sensor_,
      this->outdoor_ambient_temperature_sensor_,  this->outdoor_coil_temperature_sensor_,
      this->discharge_temperature_sensor_,        this->ipm_temperature_sensor_,
      this->operating_mode_sensor_,               this->compressor_frequency_target_sensor_,
      this->compressor_frequency_actual_sensor_,  this->outdoor_fan_speed_sensor_,
      this->eev_steps_sensor_,                    this->indoor_setpoint_sensor_,
      this->input_voltage_sensor_,                this->current_draw_sensor_,
      this->dc_bus_voltage_sensor_,
  };
  static_assert(sizeof(sensors) / sizeof(sensors[0]) == NUM_MAPPED_PARAMS,
                "sensors[] must line up 1:1 with MAPPED_PARAMS");
  for (size_t i = 0; i < NUM_MAPPED_PARAMS; i++)
    publish(sensors[i], fresh[MAPPED_PARAMS[i].type], MAPPED_PARAMS[i].decode(frames));
}

void MideaTelemetry::dump_config() {
  ESP_LOGCONFIG(TAG, "Midea ODU:");
  LOG_PIN("  CLK Pin: ", this->clk_pin_);
  LOG_PIN("  DAT Pin: ", this->dat_pin_);
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Indoor ambient temperature", this->indoor_ambient_temperature_sensor_);
  LOG_SENSOR("  ", "Indoor coil temperature", this->indoor_coil_temperature_sensor_);
  LOG_SENSOR("  ", "Outdoor ambient temperature", this->outdoor_ambient_temperature_sensor_);
  LOG_SENSOR("  ", "Outdoor coil temperature", this->outdoor_coil_temperature_sensor_);
  LOG_SENSOR("  ", "Compressor discharge temperature", this->discharge_temperature_sensor_);
  LOG_SENSOR("  ", "IPM temperature", this->ipm_temperature_sensor_);
  LOG_SENSOR("  ", "Operating mode", this->operating_mode_sensor_);
  LOG_SENSOR("  ", "Compressor frequency (target)", this->compressor_frequency_target_sensor_);
  LOG_SENSOR("  ", "Compressor frequency (actual)", this->compressor_frequency_actual_sensor_);
  LOG_SENSOR("  ", "Outdoor fan speed", this->outdoor_fan_speed_sensor_);
  LOG_SENSOR("  ", "EEV opening steps", this->eev_steps_sensor_);
  LOG_SENSOR("  ", "Indoor set-point", this->indoor_setpoint_sensor_);
  LOG_SENSOR("  ", "Input voltage", this->input_voltage_sensor_);
  LOG_SENSOR("  ", "Current draw", this->current_draw_sensor_);
  LOG_SENSOR("  ", "DC bus voltage", this->dc_bus_voltage_sensor_);
}

#ifdef USE_MIDEA_TELEMETRY_JSON
bool MideaTelemetry::canHandle(AsyncWebServerRequest *request) const {
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->method() == HTTP_GET && request->url_to(url_buf) == "/json";
}

// Serves every mapped parameter plus the raw response frames as JSON, computed
// straight from the latest frames - independent of which sensors are
// configured. A mapped parameter is null when its response frame is stale or was
// never received (matching how a stale sensor goes unavailable in Home
// Assistant); a raw frame is the last one captured, or null if never received.
void MideaTelemetry::handleRequest(AsyncWebServerRequest *request) {
  uint8_t frames[NUM_RESPONSE_TYPES][FRAME_SIZE];
  bool valid[NUM_RESPONSE_TYPES];
  bool fresh[NUM_RESPONSE_TYPES];
  const uint32_t now = millis();

  xSemaphoreTake(this->lock_, portMAX_DELAY);
  memcpy(frames, this->frames_, sizeof(frames));
  for (size_t i = 0; i < NUM_RESPONSE_TYPES; i++) {
    valid[i] = this->frame_valid_[i];
    fresh[i] = valid[i] && now - this->frame_ms_[i] < STALE_TIMEOUT_MS;
  }
  xSemaphoreGive(this->lock_);

  char buf[48];

  // Decoded values, keyed by parameter name. null when the value's response is
  // stale or was never received (matching how a stale sensor goes unavailable).
  std::string body = "{\"sensors\":{";
  for (size_t i = 0; i < NUM_MAPPED_PARAMS; i++) {
    const MappedParam &p = MAPPED_PARAMS[i];
    if (i != 0)
      body += ',';
    body += '"';
    body += p.name;
    body += "\":";
    const float value = p.decode(frames);
    if (fresh[p.type] && !std::isnan(value)) {
      snprintf(buf, sizeof(buf), "%g", value);
      body += buf;
    } else {
      body += "null";
    }
  }

  // The raw bytes each value derives from, keyed the same way, so unidentified
  // encodings can be eyeballed against the decoded value. Bytes are last-known,
  // null if the response was never received.
  body += "},\"source_bytes\":{";
  for (size_t i = 0; i < NUM_MAPPED_PARAMS; i++) {
    const MappedParam &p = MAPPED_PARAMS[i];
    if (i != 0)
      body += ',';
    body += '"';
    body += p.name;
    body += "\":{";
    for (uint8_t j = 0; j < p.num_bytes; j++) {
      snprintf(buf, sizeof(buf), "%s\"0x%02X[%u]\":", j == 0 ? "" : ",", (unsigned) p.type, (unsigned) p.bytes[j]);
      body += buf;
      if (valid[p.type]) {
        snprintf(buf, sizeof(buf), "%u", (unsigned) frames[p.type][p.bytes[j]]);
        body += buf;
      } else {
        body += "null";
      }
    }
    body += '}';
  }

  // The full latest frame of each response type as hex, null if never received.
  body += "},\"odu_responses\":{";
  for (size_t i = 0; i < NUM_RESPONSE_TYPES; i++) {
    if (i != 0)
      body += ',';
    snprintf(buf, sizeof(buf), "\"0x%02X\":", (unsigned) i);
    body += buf;
    if (valid[i]) {
      body += '"';
      body += frame_hex(frames[i]);
      body += '"';
    } else {
      body += "null";
    }
  }
  body += "}}";

  request->send(200, "application/json", body.c_str());
}
#endif

}  // namespace midea_telemetry
}  // namespace esphome
