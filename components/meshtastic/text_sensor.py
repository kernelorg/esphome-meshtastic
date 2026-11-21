import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_TYPE

from . import CONF_MESHTASTIC_ID, Meshtastic

DEPENDENCIES = ["meshtastic"]

TYPES = {
    "last_message": "set_last_message_sensor",
    "last_sender": "set_last_sender_sensor",
    "last_channel": "set_last_channel_sensor",
}

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.GenerateID(CONF_MESHTASTIC_ID): cv.use_id(Meshtastic),
        cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MESHTASTIC_ID])
    sens = await text_sensor.new_text_sensor(config)
    cg.add(getattr(parent, TYPES[config[CONF_TYPE]])(sens))
