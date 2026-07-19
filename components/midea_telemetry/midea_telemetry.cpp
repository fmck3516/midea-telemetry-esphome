#include "midea_telemetry.h"

#include "esphome/core/log.h"

#include <cinttypes>
#include <cmath>
#include <cstring>

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

static float current_draw(uint8_t v) { return truncf((0.117f * v + 0.92f) * 100.0f) / 100.0f; }

static float uint16(uint8_t lo, uint8_t hi) { return lo | (hi << 8); }

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
  }
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
  const uint8_t *t0 = frames[0x00], *t1 = frames[0x01], *t2 = frames[0x02];

  publish(this->indoor_ambient_temperature_sensor_, fresh[0x00], ntc_temp(t0[2]));
  publish(this->indoor_coil_temperature_sensor_, fresh[0x00], ntc_temp(t0[3]));
  publish(this->outdoor_ambient_temperature_sensor_, fresh[0x00], ntc_temp(t0[5]));
  publish(this->outdoor_coil_temperature_sensor_, fresh[0x00], ntc_temp(t0[4]));
  publish(this->discharge_temperature_sensor_, fresh[0x00], discharge_temp(t0[6]));
  publish(this->operating_mode_sensor_, fresh[0x02], t2[8]);
  publish(this->compressor_frequency_target_sensor_, fresh[0x02], t2[2]);
  publish(this->compressor_frequency_actual_sensor_, fresh[0x02], t2[3]);
  publish(this->outdoor_fan_speed_sensor_, fresh[0x00], uint16(t0[7], t0[8]));
  publish(this->eev_steps_sensor_, fresh[0x01], uint16(t1[5], t1[6]));
  // half-degree C steps, offset by 50 (tentative mapping)
  publish(this->indoor_setpoint_sensor_, fresh[0x01], (t1[7] - 50) / 2.0f);
  publish(this->input_voltage_sensor_, fresh[0x01], ac_voltage(t1[3]));
  publish(this->current_draw_sensor_, fresh[0x01], current_draw(t1[2]));
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
  LOG_SENSOR("  ", "Operating mode", this->operating_mode_sensor_);
  LOG_SENSOR("  ", "Compressor frequency (target)", this->compressor_frequency_target_sensor_);
  LOG_SENSOR("  ", "Compressor frequency (actual)", this->compressor_frequency_actual_sensor_);
  LOG_SENSOR("  ", "Outdoor fan speed", this->outdoor_fan_speed_sensor_);
  LOG_SENSOR("  ", "EEV opening steps", this->eev_steps_sensor_);
  LOG_SENSOR("  ", "Indoor set-point", this->indoor_setpoint_sensor_);
  LOG_SENSOR("  ", "Input voltage", this->input_voltage_sensor_);
  LOG_SENSOR("  ", "Current draw", this->current_draw_sensor_);
}

}  // namespace midea_telemetry
}  // namespace esphome
