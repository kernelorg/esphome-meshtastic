import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.core import CORE

from . import (
    CONF_CHANNELS,
    CONF_MESHTASTIC_ID,
    CONF_NODENUM,
    CONF_NODES,
    Meshtastic,
    meshtastic_ns,
)

DEPENDENCIES = ["meshtastic"]

MeshtasticSelect = meshtastic_ns.class_("MeshtasticSelect", select.Select, cg.Component)

CONFIG_SCHEMA = (
    select.select_schema(MeshtasticSelect)
    .extend({cv.GenerateID(CONF_MESHTASTIC_ID): cv.use_id(Meshtastic)})
    .extend(cv.COMPONENT_SCHEMA)
)


def _destinations(parent_id):
    """Channel names, then nodes from `nodes:` as 'Name (!xxxxxxxx)'."""
    confs = []
    for item in CORE.config.get("meshtastic", []):  # MULTI_CONF of ensure_list -> nested lists
        confs.extend(item if isinstance(item, list) else [item])
    for conf in confs:
        if conf["id"] != parent_id:
            continue
        opts = [ch["name"] for ch in conf.get(CONF_CHANNELS, [])]
        for node in conf.get(CONF_NODES, []):
            if node.get(CONF_NODENUM, 0):
                opts.append(f"{node['name']} (!{node[CONF_NODENUM]:08x})")
        return opts
    return []


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MESHTASTIC_ID])
    options = _destinations(config[CONF_MESHTASTIC_ID]) or ["-"]
    var = await select.new_select(config, options=options)
    await cg.register_component(var, config)
    cg.add(var.set_parent(parent))
    cg.add_global(cg.RawStatement('#include "esphome/components/meshtastic/entities.h"'))
