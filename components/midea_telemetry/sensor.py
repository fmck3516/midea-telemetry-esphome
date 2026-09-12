import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    ENTITY_CATEGORY_DIAGNOSTIC,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_VOLT,
)

from . import CONF_MIDEA_TELEMETRY_ID, MideaTelemetry

DEPENDENCIES = ["midea_telemetry"]


def _temperature_schema(accuracy_decimals):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=accuracy_decimals,
    )


# One entry per decoded field; byte mapping and conversion formulas as
# documented in "Reverse Engineering Midea's ODU Diagnostic Port".
SENSORS = {
    "indoor_ambient_temperature": _temperature_schema(1),
    "indoor_coil_temperature": _temperature_schema(1),
    "outdoor_ambient_temperature": _temperature_schema(1),
    "outdoor_coil_temperature": _temperature_schema(1),
    "discharge_temperature": _temperature_schema(0),
    "operating_mode": sensor.sensor_schema(
        icon="mdi:state-machine",
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "compressor_frequency_indoor_target": sensor.sensor_schema(
        unit_of_measurement=UNIT_HERTZ,
        device_class=DEVICE_CLASS_FREQUENCY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "compressor_frequency_outdoor_target": sensor.sensor_schema(
        unit_of_measurement=UNIT_HERTZ,
        device_class=DEVICE_CLASS_FREQUENCY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "compressor_frequency_actual_int": sensor.sensor_schema(
        unit_of_measurement=UNIT_HERTZ,
        device_class=DEVICE_CLASS_FREQUENCY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "compressor_frequency_actual_float": sensor.sensor_schema(
        unit_of_measurement=UNIT_HERTZ,
        device_class=DEVICE_CLASS_FREQUENCY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=2,
    ),
    "compressor_frequency_outdoor_control": sensor.sensor_schema(
        unit_of_measurement=UNIT_HERTZ,
        device_class=DEVICE_CLASS_FREQUENCY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "outdoor_fan_speed": sensor.sensor_schema(
        icon="mdi:fan",
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "eev_steps": sensor.sensor_schema(
        icon="mdi:valve",
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "indoor_setpoint": _temperature_schema(1),
    "input_voltage": sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "current_draw": sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=2,
    ),
    "dc_bus_voltage": sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
}

# Every response frame byte that carries telemetry, exposed as an opt-in sensor
# so unknown bytes can be watched from Home Assistant without the
# InfluxDB/Grafana stack (issue #47). Bytes 0/1/9 are framing (header, response
# type, checksum). Keys mirror the `midea_raw` InfluxDB field names (`0x00_2`)
# with a `raw_` prefix, so one byte is recognisable across both.
NUM_RESPONSE_TYPES = 7
RAW_BYTE_FIRST = 2
RAW_BYTE_LAST = 8

RAW_BYTE_SENSORS = {
    f"raw_0x{frame:02x}_{byte}": (frame, byte)
    for frame in range(NUM_RESPONSE_TYPES)
    for byte in range(RAW_BYTE_FIRST, RAW_BYTE_LAST + 1)
}


def _raw_byte_schema():
    # No unit and no device class: this is a byte, not a quantity. Diagnostic so
    # the 49 of them stay off the main device card in Home Assistant.
    return sensor.sensor_schema(
        icon="mdi:hexadecimal",
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MIDEA_TELEMETRY_ID): cv.use_id(MideaTelemetry),
        **{cv.Optional(key): schema for key, schema in SENSORS.items()},
        **{cv.Optional(key): _raw_byte_schema() for key in RAW_BYTE_SENSORS},
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_MIDEA_TELEMETRY_ID])
    for key in SENSORS:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
    for key, (frame, byte) in RAW_BYTE_SENSORS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(hub.set_raw_byte_sensor(frame, byte, sens))
