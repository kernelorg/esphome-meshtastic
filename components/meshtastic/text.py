import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text
from esphome.const import CONF_MODE

from . import CONF_MESHTASTIC_ID, Meshtastic, meshtastic_ns

DEPENDENCIES = ["meshtastic"]

MeshtasticText = meshtastic_ns.class_("MeshtasticText", text.Text)

CONFIG_SCHEMA = text.text_schema(MeshtasticText).extend(
    {
        cv.GenerateID(CONF_MESHTASTIC_ID): cv.use_id(Meshtastic),
        cv.Optional(CONF_MODE, default="TEXT"): cv.enum(text.TEXT_MODES, upper=True),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MESHTASTIC_ID])
    # 200 bytes keeps the encrypted packet inside one LoRa frame
    var = await text.new_text(config, min_length=0, max_length=200)
    cg.add(var.set_parent(parent))
    cg.add(var.traits.set_mode(config[CONF_MODE]))
    cg.add_global(cg.RawStatement('#include "esphome/components/meshtastic/entities.h"'))
