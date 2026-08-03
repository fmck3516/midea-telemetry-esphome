#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#ifdef USE_MIDEA_TELEMETRY_JSON
#include "esphome/components/web_server_base/web_server_base.h"
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace esphome {
namespace midea_telemetry {

static const size_t FRAME_SIZE = 10;
static const size_t NUM_RESPONSE_TYPES = 7;

// Drives the two-wire diagnostic bus on the outdoor inverter board the same
// way Midea's handheld inverter tester does: 80-bit frames, LSB-first, with
// the tester (us) driving the clock in both directions. The bit-banging runs
// in a dedicated FreeRTOS task - a full request/response cycle keeps the bus
// busy for ~380 ms, far too long for the main loop. update() only publishes
// the most recent frames.
class MideaTelemetry : public PollingComponent
#ifdef USE_MIDEA_TELEMETRY_JSON
    ,
                       public AsyncWebHandler
#endif
{
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_clk_pin(InternalGPIOPin *pin) { this->clk_pin_ = pin; }
  void set_dat_pin(InternalGPIOPin *pin) { this->dat_pin_ = pin; }

  void set_indoor_ambient_temperature_sensor(sensor::Sensor *s) { this->indoor_ambient_temperature_sensor_ = s; }
  void set_indoor_coil_temperature_sensor(sensor::Sensor *s) { this->indoor_coil_temperature_sensor_ = s; }
  void set_outdoor_ambient_temperature_sensor(sensor::Sensor *s) { this->outdoor_ambient_temperature_sensor_ = s; }
  void set_outdoor_coil_temperature_sensor(sensor::Sensor *s) { this->outdoor_coil_temperature_sensor_ = s; }
  void set_discharge_temperature_sensor(sensor::Sensor *s) { this->discharge_temperature_sensor_ = s; }
  void set_ipm_temperature_sensor(sensor::Sensor *s) { this->ipm_temperature_sensor_ = s; }
  void set_operating_mode_sensor(sensor::Sensor *s) { this->operating_mode_sensor_ = s; }
  void set_compressor_frequency_target_sensor(sensor::Sensor *s) { this->compressor_frequency_target_sensor_ = s; }
  void set_compressor_frequency_actual_sensor(sensor::Sensor *s) { this->compressor_frequency_actual_sensor_ = s; }
  void set_outdoor_fan_speed_sensor(sensor::Sensor *s) { this->outdoor_fan_speed_sensor_ = s; }
  void set_eev_steps_sensor(sensor::Sensor *s) { this->eev_steps_sensor_ = s; }
  void set_indoor_setpoint_sensor(sensor::Sensor *s) { this->indoor_setpoint_sensor_ = s; }
  void set_input_voltage_sensor(sensor::Sensor *s) { this->input_voltage_sensor_ = s; }
  void set_current_draw_sensor(sensor::Sensor *s) { this->current_draw_sensor_ = s; }
  void set_dc_bus_voltage_sensor(sensor::Sensor *s) { this->dc_bus_voltage_sensor_ = s; }

  // Raw frames as hex text (e.g. "0x55006D457671401F03B0") for the web server
  // and API. Responses are keyed by response type (0x00-0x06).
  void set_response_text_sensor(uint8_t type, text_sensor::TextSensor *s) { this->response_text_[type] = s; }

#ifdef USE_MIDEA_TELEMETRY_JSON
  // Serves all mapped parameters and the raw frames as JSON at /json on the
  // ESPHome web server (see handleRequest), so everything can be read for
  // reverse engineering without publishing it as (HA-streamed) sensors.
  void enable_json_endpoint() { this->json_endpoint_ = true; }
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
#endif

 protected:
  static void bus_task_trampoline(void *param);
  void bus_task_();
  void set_line_(InternalGPIOPin *pin, bool level);
  void send_bit_(bool bit);
  bool read_bit_();
  void transfer_frame_(const uint8_t *request, uint8_t *response);

  InternalGPIOPin *clk_pin_{nullptr};
  InternalGPIOPin *dat_pin_{nullptr};

  sensor::Sensor *indoor_ambient_temperature_sensor_{nullptr};
  sensor::Sensor *indoor_coil_temperature_sensor_{nullptr};
  sensor::Sensor *outdoor_ambient_temperature_sensor_{nullptr};
  sensor::Sensor *outdoor_coil_temperature_sensor_{nullptr};
  sensor::Sensor *discharge_temperature_sensor_{nullptr};
  sensor::Sensor *ipm_temperature_sensor_{nullptr};
  sensor::Sensor *operating_mode_sensor_{nullptr};
  sensor::Sensor *compressor_frequency_target_sensor_{nullptr};
  sensor::Sensor *compressor_frequency_actual_sensor_{nullptr};
  sensor::Sensor *outdoor_fan_speed_sensor_{nullptr};
  sensor::Sensor *eev_steps_sensor_{nullptr};
  sensor::Sensor *indoor_setpoint_sensor_{nullptr};
  sensor::Sensor *input_voltage_sensor_{nullptr};
  sensor::Sensor *current_draw_sensor_{nullptr};
  sensor::Sensor *dc_bus_voltage_sensor_{nullptr};

  text_sensor::TextSensor *response_text_[NUM_RESPONSE_TYPES]{};

#ifdef USE_MIDEA_TELEMETRY_JSON
  bool json_endpoint_{false};
#endif

  TaskHandle_t task_{nullptr};

  // Shared between the bus task and the main loop; guarded by lock_.
  SemaphoreHandle_t lock_{nullptr};
  uint8_t frames_[NUM_RESPONSE_TYPES][FRAME_SIZE]{};
  uint32_t frame_ms_[NUM_RESPONSE_TYPES]{};
  bool frame_valid_[NUM_RESPONSE_TYPES]{};
  uint32_t frames_ok_{0};
  uint32_t checksum_errors_{0};
  uint32_t no_response_{0};
};

}  // namespace midea_telemetry
}  // namespace esphome
