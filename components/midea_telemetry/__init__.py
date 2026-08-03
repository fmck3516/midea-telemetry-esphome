import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import pins
from esphome.const import CONF_ID, PLATFORM_ESP32

CODEOWNERS = ["@fmck3516"]
MULTI_CONF = True

CONF_MIDEA_TELEMETRY_ID = "midea_telemetry_id"
CONF_CLK_PIN = "clk_pin"
CONF_DAT_PIN = "dat_pin"
CONF_EXPOSE_JSON_ENDPOINT = "expose_json_endpoint"

midea_telemetry_ns = cg.esphome_ns.namespace("midea_telemetry")
MideaTelemetry = midea_telemetry_ns.class_("MideaTelemetry", cg.PollingComponent)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MideaTelemetry),
            cv.Required(CONF_CLK_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_DAT_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_EXPOSE_JSON_ENDPOINT, default=False): cv.boolean,
        }
    ).extend(cv.polling_component_schema("10s")),
    cv.only_on([PLATFORM_ESP32]),
)


def _final_validate(config):
    # The JSON endpoint is served on the shared web server, so that component has
    # to be present. Requiring it (rather than auto-loading it) keeps a bare HTTP
    # server out of firmware for users who don't opt in.
    if config[CONF_EXPOSE_JSON_ENDPOINT] and "web_server" not in fv.full_config.get():
        raise cv.Invalid(
            "midea_telemetry: 'expose_json_endpoint' needs the 'web_server' "
            "component to be configured to serve the endpoint on."
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    clk = await cg.gpio_pin_expression(config[CONF_CLK_PIN])
    cg.add(var.set_clk_pin(clk))
    dat = await cg.gpio_pin_expression(config[CONF_DAT_PIN])
    cg.add(var.set_dat_pin(dat))

    if config[CONF_EXPOSE_JSON_ENDPOINT]:
        cg.add_define("USE_MIDEA_TELEMETRY_JSON")
        cg.add(var.enable_json_endpoint())
