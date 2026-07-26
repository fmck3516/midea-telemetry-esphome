import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import CONF_MIDEA_TELEMETRY_ID, MideaTelemetry

DEPENDENCIES = ["midea_telemetry"]

# Raw diagnostic frames exposed as hex text (e.g. "0x55006D457671401F03B0") so
# they show up in the web server and Home Assistant. There is one response entry
# per response type (0x00-0x06), each tracking the latest frame of that type,
# and one entry per fixed tester request (0x00-0x03).
RESPONSE_KEYS = [f"response_{i}" for i in range(7)]
REQUEST_KEYS = [f"request_{i}" for i in range(4)]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MIDEA_TELEMETRY_ID): cv.use_id(MideaTelemetry),
        **{
            cv.Optional(key): text_sensor.text_sensor_schema(icon="mdi:arrow-down-box")
            for key in RESPONSE_KEYS
        },
        **{
            cv.Optional(key): text_sensor.text_sensor_schema(icon="mdi:arrow-up-box")
            for key in REQUEST_KEYS
        },
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_MIDEA_TELEMETRY_ID])
    for i, key in enumerate(RESPONSE_KEYS):
        if key in config:
            ts = await text_sensor.new_text_sensor(config[key])
            cg.add(hub.set_response_text_sensor(i, ts))
    for i, key in enumerate(REQUEST_KEYS):
        if key in config:
            ts = await text_sensor.new_text_sensor(config[key])
            cg.add(hub.set_request_text_sensor(i, ts))
