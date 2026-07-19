import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_CURRENT,
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
    "compressor_frequency_target": sensor.sensor_schema(
        unit_of_measurement=UNIT_HERTZ,
        device_class=DEVICE_CLASS_FREQUENCY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "compressor_frequency_actual": sensor.sensor_schema(
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
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MIDEA_TELEMETRY_ID): cv.use_id(MideaTelemetry),
        **{cv.Optional(key): schema for key, schema in SENSORS.items()},
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_MIDEA_TELEMETRY_ID])
    for key in SENSORS:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
