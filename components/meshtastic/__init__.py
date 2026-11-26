import base64
import esphome.codegen as cg
import esphome.config_validation as cv
import re
from esphome.components import sx126x, sx127x
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_DATA,
    CONF_TEXT,
    CONF_TRIGGER_ID,
    CONF_POSITION,
    CONF_ALTITUDE,
    CONF_LATITUDE,
    CONF_LONGITUDE,
)
from esphome import automation
from esphome.automation import Trigger
from esphome.core import ID, CORE, Lambda
from esphome.helpers import IS_MACOS
from esphome.cpp_generator import ExpressionStatement, MockObj

CODEOWNERS = ["@Andrik45719"]
DEPENDENCIES = ["network"]
AUTO_LOAD = ["socket"]
MULTI_CONF = True

# IS_PLATFORM_COMPONENT = True

CONF_MESHTASTIC_ID = "meshtastic_id"

CONF_LORADEVICE = "lora"
CONF_CHANNELS = "channels"
CONF_CHANNEL = "channel"
CONF_PSK = "psk"
CONF_PUBLICKEY = "public_key"
CONF_PRIVATEKEY = "private_key"
CONF_NODES = "nodes"
CONF_NODENUM = "node_number"
CONF_SHORT_NAME = "short_name"
CONF_ON_PACKET = "on_packet"
CONF_UDP = "udp"
CONF_MULTICAST_ADDRESS = "multicast_address"
CONF_ADDRESSES = "addresses"
CONF_BRIDGE_MODE = "bridge_mode"
CONF_TO = "to"
CONF_HOPLIMIT = "hop_limit"
CONF_ROLE = "role"
CONF_SLOT_TIME = "slot_time"
ROLES = {"CLIENT_MUTE": 0, "CLIENT": 1, "ROUTER": 2}
CONF_IGNOREMQTT = "ignore_mqtt"
CONF_OKTOMQTT = "ok_to_mqtt"
CONF_BROADCAST_INTERVAL = "broadcast_interval"
CONF_HW_MODEL = "hw_model"

CONF_EXCLUDE_PKI = "exclude_pki"
CONF_POSITION_PRECISION = "position_precision"
CONF_UPLINK = "uplink"
CONF_DOWNLINK = "downlink"
CONF_MQTT_ROOT_TOPIC = "mqtt_root_topic"
CONF_ON_TEXT_MESSAGE = "on_text_message"
CONF_CLIENT_API = "client_api"
CONF_PORT = "port"
CONF_TCP_PORT = "tcp_port"
CONF_TELEMETRY_INTERVAL = "telemetry_interval"
CONF_REGION = "region"
CONF_MODEM_PRESET = "modem_preset"

# Meshtastic Config.LoRaConfig enums (reported to clients only; radio settings come from sx126x/sx127x)
REGIONS = {"UNSET": 0, "US": 1, "EU_433": 2, "EU_868": 3, "CN": 4, "JP": 5, "ANZ": 6, "KR": 7,
           "TW": 8, "RU": 9, "IN": 10, "NZ_865": 11, "TH": 12, "LORA_24": 13, "UA_433": 14,
           "UA_868": 15, "MY_433": 16, "MY_919": 17, "SG_923": 18}
MODEM_PRESETS = {"LONG_FAST": 0, "LONG_SLOW": 1, "VERY_LONG_SLOW": 2, "MEDIUM_SLOW": 3,
                 "MEDIUM_FAST": 4, "SHORT_SLOW": 5, "SHORT_FAST": 6, "LONG_MODERATE": 7,
                 "SHORT_TURBO": 8}

meshtastic_ns = cg.esphome_ns.namespace("meshtastic")
Meshtastic = meshtastic_ns.class_("Meshtastic", cg.Component)

Channel = meshtastic_ns.class_("Channel")
Node = meshtastic_ns.class_("Node")
TextMessageTrigger = meshtastic_ns.class_(
    "TextMessageTrigger",
    automation.Trigger.template(cg.uint32, cg.std_string, cg.std_string, cg.std_string),
)

CLIENT_API_SCHEMA = cv.Schema(
    {
        # HTTP API (web client); 0 disables
        cv.Optional(CONF_PORT, default=80): cv.int_range(0, 65535),
        # TCP stream API (HA Meshtastic integration, python CLI --host, apps); 0 disables
        cv.Optional(CONF_TCP_PORT, default=4403): cv.int_range(0, 65535),
        cv.Optional(CONF_REGION, default="EU_868"): cv.enum(REGIONS, upper=True),
        cv.Optional(CONF_MODEM_PRESET, default="LONG_FAST"): cv.enum(MODEM_PRESETS, upper=True),
    }
)

SendPacketAction = meshtastic_ns.class_(
    "SendPacketAction", automation.Action, cg.Parented.template(Meshtastic)
)

SendTextMessageAction = meshtastic_ns.class_(
    "SendTextMessageAction", automation.Action, cg.Parented.template(Meshtastic)
)

trigger_args = cg.uint32, cg.uint32, cg.uint32, cg.std_vector.template(cg.uint8)
trigger_args_with_name = [
    (cg.uint32, "from"),
    (cg.uint32, "to"),
    (cg.uint32, "portnum"),
    (cg.std_vector.template(cg.uint8), "data"),
]


def validate_encryption_key(value):
    value = cv.string_strict(value)
    try:
        decoded = base64.b64decode(value, validate=True)
    except ValueError as err:
        raise cv.Invalid("Invalid key format, please check it's using base64") from err

    if len(decoded) not in [0, 1, 16, 32]:
        raise cv.Invalid("Encryption key must be base64 and 0, 1, 16, 32 bytes long")

    # Return original data for roundtrip conversion
    return value


CHANNEL_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Channel),
        cv.Required(CONF_NAME): cv.string,
        cv.Required(CONF_PSK): validate_encryption_key,
        # Bits of lat/lon sent on this channel (32 = exact, 0 = no position on this channel)
        cv.Optional(CONF_POSITION_PRECISION, default=32): cv.int_range(0, 32),
        # MQTT gateway (needs the mqtt: component)
        cv.Optional(CONF_UPLINK, default=False): cv.boolean,
        cv.Optional(CONF_DOWNLINK, default=False): cv.boolean,
    },
)

# def validate_node(config):
#   if config[CONF_NODENUM] = 0 and config[CONF_PRIVATKEY] = ''
#        raise cv.Invalid("The own node must have privat key")
#    return config

NODE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Node),
        cv.Optional(CONF_NODENUM, 0): cv.uint32_t,
        cv.Required(CONF_NAME): cv.string,
        cv.Optional(CONF_SHORT_NAME, ""): cv.string,
        cv.Required(CONF_PUBLICKEY): validate_encryption_key,
        cv.Optional(CONF_PRIVATEKEY, ""): validate_encryption_key,
        cv.Optional(CONF_BROADCAST_INTERVAL, default="3h"): cv.update_interval,
    },
    #    validate_node
    cv.has_at_least_one_key(CONF_NODENUM, CONF_PRIVATEKEY),
)

LAT_LON_REGEX = re.compile(
    r"([+\-])?\s*"
    r"(?:([0-9]+)\s*°)?\s*"
    r"(?:([0-9]+)\s*[′\'])?\s*"
    r'(?:([0-9]+)\s*[″"])?\s*'
    r"([NESW])?"
)


def parse_latlon(value):
    if isinstance(value, str) and value.endswith("°"):
        # strip trailing degree character
        value = value[:-1]
    try:
        return cv.float_(value)
    except cv.Invalid:
        pass

    value = cv.string_strict(value)
    m = LAT_LON_REGEX.match(value)

    if m is None:
        raise cv.Invalid("Invalid format for latitude/longitude")
    sign = m.group(1)
    deg = m.group(2)
    minute = m.group(3)
    second = m.group(4)
    d = m.group(5)

    val = float(deg or 0) + float(minute or 0) / 60 + float(second or 0) / 3600
    if sign == "-":
        val *= -1
    if d and d in "SW":
        val *= -1
    return val


POSITION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LATITUDE): cv.All(
            parse_latlon, cv.float_range(min=-90, max=90)
        ),
        cv.Required(CONF_LONGITUDE): cv.All(
            parse_latlon, cv.float_range(min=-180, max=180)
        ),
        cv.Optional(CONF_ALTITUDE, default=0): cv.int_,
        cv.Optional(CONF_BROADCAST_INTERVAL, default="3h"): cv.update_interval,
    }
)

UDP_SCHEMA = cv.Schema(
    {
        cv.Optional(
            #            CONF_MULTICAST_ADDRESS, default="224.0.0.69"
            CONF_MULTICAST_ADDRESS
        ): cv.ipv4address_multi_broadcast,
        #        cv.Optional(CONF_ADDRESSES, default=["224.0.0.69"]): cv.ensure_list(
        cv.Optional(CONF_ADDRESSES): cv.ensure_list(
            cv.ipv4address,
        ),
        cv.Optional(CONF_BRIDGE_MODE, default=False): cv.boolean,
    },
)


def validate_udp(value):
    if value is None:
        value = {}
    return UDP_SCHEMA(value)


CONFIG_SCHEMA = cv.All(
    cv.ensure_list(
        cv.COMPONENT_SCHEMA.extend(
            {
                cv.Required(CONF_ID): cv.declare_id(Meshtastic),
                cv.Optional(CONF_LORADEVICE): cv.Any(
                    cv.use_id(sx126x), cv.use_id(sx127x)
                ),
                cv.Optional(CONF_HOPLIMIT, 7): cv.int_range(0, 7),
                cv.Optional(CONF_ROLE, default="CLIENT_MUTE"): cv.enum(ROLES, upper=True),
                # Meshtastic slot time for the preset (SHORT_FAST: 9 ms; see device log "Slot time")
                cv.Optional(CONF_SLOT_TIME, default="9ms"): cv.positive_time_period_milliseconds,
                cv.Optional(CONF_HW_MODEL, 59): cv.int_,
                cv.Optional(CONF_IGNOREMQTT, True): cv.boolean,
                cv.Optional(CONF_OKTOMQTT, False): cv.boolean,
                cv.Optional(CONF_UDP): validate_udp,
                cv.Optional(CONF_CHANNELS): cv.All(cv.ensure_list(CHANNEL_SCHEMA)),
                cv.Optional(CONF_NODES): cv.All(cv.ensure_list(NODE_SCHEMA)),
                cv.Optional(CONF_ON_PACKET): automation.validate_automation(
                    {
                        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                            Trigger.template(*trigger_args)
                        )
                    }
                ),
                cv.Optional(CONF_POSITION): cv.All(cv.ensure_list(POSITION_SCHEMA)),
                cv.Optional(CONF_EXCLUDE_PKI): cv.boolean,
                cv.Optional(CONF_MQTT_ROOT_TOPIC, default="msh/EU_868"): cv.publish_topic,
                cv.Optional(CONF_ON_TEXT_MESSAGE): automation.validate_automation(
                    {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TextMessageTrigger)}
                ),
                cv.Optional(CONF_CLIENT_API): CLIENT_API_SCHEMA,
                # Own DeviceMetrics telemetry to the mesh (0 = only to connected clients)
                cv.Optional(
                    CONF_TELEMETRY_INTERVAL, default="30min"
                ): cv.positive_time_period_milliseconds,
            }
        )
    ),
)


async def to_code(configs):
    for config in configs:
        if CONF_EXCLUDE_PKI in config:
            if config[CONF_EXCLUDE_PKI]:
                cg.add_define("MESHTASTIC_EXCLUDE_PKI", 1)

        var = cg.new_Pvariable(config[CONF_ID])
        await cg.register_component(var, config)

        if CONF_LORADEVICE in config:
            loradev = await cg.get_variable(config[CONF_LORADEVICE])
            cg.add(var.set_lora(loradev))
            if params := _lora_params(config[CONF_LORADEVICE]):
                cg.add(var.set_lora_params(*params))
        cg.add(var.set_telemetry_interval(config[CONF_TELEMETRY_INTERVAL].total_milliseconds))

        cg.add(var.set_hop_limit(config[CONF_HOPLIMIT]))
        cg.add(var.set_role(config[CONF_ROLE]))
        cg.add(var.set_slot_time(config[CONF_SLOT_TIME].total_milliseconds))
        cg.add(var.set_ignore_mqtt(config[CONF_IGNOREMQTT]))
        cg.add(var.set_ok_to_mqtt(config[CONF_OKTOMQTT]))
        cg.add(var.set_hw_model(config[CONF_HW_MODEL]))
        cg.add(var.set_mqtt_root_topic(config[CONF_MQTT_ROOT_TOPIC]))
        for conf in config.get(CONF_ON_TEXT_MESSAGE, []):
            trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
            await automation.build_automation(
                trig,
                [
                    (cg.uint32, "from"),
                    (cg.std_string, "sender"),
                    (cg.std_string, "channel"),
                    (cg.std_string, "text"),
                ],
                conf,
            )
        if api := config.get(CONF_CLIENT_API):
            if api[CONF_PORT]:
                cg.add_define("MESHTASTIC_CLIENT_API")
                try:
                    from esphome.components.esp32 import include_builtin_idf_component

                    include_builtin_idf_component("esp_http_server")
                except ImportError:  # older ESPHome builds every IDF component
                    pass
            if api[CONF_TCP_PORT]:
                cg.add_define("MESHTASTIC_TCP_API")
            cg.add(
                var.set_client_api(
                    api[CONF_PORT],
                    api[CONF_TCP_PORT],
                    api[CONF_REGION],
                    api[CONF_MODEM_PRESET],
                )
            )
        if any(
            ch[CONF_UPLINK] or ch[CONF_DOWNLINK] for ch in config.get(CONF_CHANNELS, [])
        ):
            cg.add(var.set_mqtt_gateway(True))
            # ServiceEnvelope uses nanopb POINTER fields; decoding them needs malloc
            cg.add_build_flag("-DPB_ENABLE_MALLOC")
        if on_packet := config.get(CONF_ON_PACKET):
            on_packet = on_packet[0]
            trigger = cg.new_Pvariable(on_packet[CONF_TRIGGER_ID])
            trigger = await automation.build_automation(
                trigger,
                trigger_args_with_name,
                on_packet,
            )
            trigger = Lambda(
                str(
                    ExpressionStatement(
                        trigger.trigger(
                            MockObj("from"),
                            MockObj("to"),
                            MockObj("portnum"),
                            MockObj("data"),
                        )
                    )
                )
            )
            trigger = await cg.process_lambda(trigger, trigger_args_with_name)
            cg.add(var.add_listener(trigger))

        channels = config.get(CONF_CHANNELS, [])
        if channels:
            for channel in channels:
                cg.add(
                    var.add_channel(
                        channel.get(CONF_NAME),
                        list(base64.b64decode(channel.get(CONF_PSK))),
                        channel[CONF_POSITION_PRECISION],
                        channel[CONF_UPLINK],
                        channel[CONF_DOWNLINK],
                    )
                )
        nodes = config.get(CONF_NODES, [])
        if nodes:
            for node in nodes:
                cg.add(
                    var.add_node(
                        node.get(CONF_NODENUM),
                        node.get(CONF_NAME),
                        node.get(CONF_SHORT_NAME),
                        list(base64.b64decode(node.get(CONF_PUBLICKEY))),
                        list(base64.b64decode(node.get(CONF_PRIVATEKEY))),
                    )
                )
                broadcast_interval = node.get(CONF_BROADCAST_INTERVAL)
                if broadcast_interval:
                    cg.add(var.set_node_broadcast_interval(broadcast_interval))

        if positions := config.get(CONF_POSITION, []):
            position = positions[0]
            cg.add(
                var.set_position(
                    position[CONF_LATITUDE],
                    position[CONF_LONGITUDE],
                    position[CONF_ALTITUDE],
                )
            )
            broadcast_interval = position.get(CONF_BROADCAST_INTERVAL)
            if broadcast_interval:
                cg.add(var.set_position_broadcast_interval(broadcast_interval))

        udp = config.get(CONF_UDP, [])
        if udp:
            cg.add(var.set_use_udp(True))
            #            cg.add(var.set_multicast_address(str(udp[CONF_MULTICAST_ADDRESS])))
            addresses = udp.get(CONF_ADDRESSES, [])
            if addresses:
                for address in addresses:
                    cg.add(var.add_address(str(address)))
            if CONF_MULTICAST_ADDRESS in udp:
                cg.add(var.set_multicast_address(str(udp[CONF_MULTICAST_ADDRESS])))
            elif not addresses:
                cg.add(var.set_multicast_address("224.0.0.69"))
            cg.add(var.set_bridge_mode(udp[CONF_BRIDGE_MODE]))
        else:
            cg.add(var.set_use_udp(False))

    cg.add_library("nanopb/nanopb", "0.4.91")
    if CORE.is_host:
        cg.add_library("kochcodes/mbedtls", "3.6.2")


def validate_raw_data(value):
    if isinstance(value, str):
        return value.encode("utf-8")
    if isinstance(value, list):
        return cv.Schema([cv.hex_uint8_t])(value)
    raise cv.Invalid(
        "data must either be a string wrapped in quotes or a list of bytes"
    )


SEND_PACKET_ACTION_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(Meshtastic),
        cv.Required(CONF_DATA): cv.templatable(validate_raw_data),
    },
    key=CONF_DATA,
)


@automation.register_action(
    "meshtastic.send_packet", SendPacketAction, SEND_PACKET_ACTION_SCHEMA
)
async def send_packet_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    data = config[CONF_DATA]
    if isinstance(data, bytes):
        data = list(data)
    if cg.is_template(data):
        templ = await cg.templatable(data, args, cg.std_vector.template(cg.uint8))
        cg.add(var.set_data_template(templ))
    else:
        # Generate static array in flash to avoid RAM copy
        arr_id = ID(f"{action_id}_data", is_declaration=True, type=cg.uint8)
        arr = cg.static_const_array(arr_id, cg.ArrayInitializer(*data))
        cg.add(var.set_data_static(arr, len(data)))
    return var


SEND_TEXT_MESSAGE_ACTION_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(Meshtastic),
        cv.Required(CONF_TEXT): cv.templatable(cv.string_strict),
        cv.Optional(CONF_TO, default=0xFFFFFFFF): cv.templatable(cv.uint32_t),
        cv.Optional(CONF_CHANNEL, default=""): cv.templatable(cv.string_strict),
    },
    key=CONF_TEXT,
)


@automation.register_action(
    "meshtastic.send_text_message",
    SendTextMessageAction,
    SEND_TEXT_MESSAGE_ACTION_SCHEMA,
)
async def send_text_message_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_TEXT], args, cg.std_string)
    cg.add(var.set_text(template_))
    template_ = await cg.templatable(config[CONF_TO], args, cg.uint32)
    cg.add(var.set_to(template_))
    template_ = await cg.templatable(config[CONF_CHANNEL], args, cg.std_string)
    cg.add(var.set_channel(template_))
    return var


def _lora_params(lora_id):
    """SF, bandwidth (Hz), coding rate (1..4) and preamble of the radio, for airtime accounting."""
    for domain in ("sx126x", "sx127x"):
        confs = CORE.config.get(domain, [])
        for conf in confs if isinstance(confs, list) else [confs]:
            if conf.get(CONF_ID) != lora_id or str(conf.get("modulation", "LORA")) != "LORA":
                continue
            try:
                sf = int(conf["spreading_factor"])
                bw = float(str(conf["bandwidth"]).replace("kHz", "").replace("_", ".")) * 1000
                cr = int(str(conf.get("coding_rate", "CR_4_5"))[-1]) - 4
                pre = int(conf.get("preamble_size", 8)) or 8
            except (KeyError, ValueError):
                return None
            return sf, int(bw), cr, pre
    return None


def _final_validate(configs):
    import esphome.final_validate as fv

    full = fv.full_config.get()
    for config in configs:
        if any(
            ch[CONF_UPLINK] or ch[CONF_DOWNLINK] for ch in config.get(CONF_CHANNELS, [])
        ) and "mqtt" not in full:
            raise cv.Invalid("channel uplink/downlink needs the 'mqtt:' component")
    return configs


FINAL_VALIDATE_SCHEMA = _final_validate
