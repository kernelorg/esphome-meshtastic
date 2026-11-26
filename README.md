# ESPHome Meshtastic Component

![Platform](https://img.shields.io/badge/platform-ESPHome-green)

An ESPHome component that turns an ESP32 with an SX126x/SX127x LoRa radio into a
[Meshtastic](https://meshtastic.org/) node: encrypted channels and direct messages, packet
relaying, position and telemetry, an MQTT gateway, the Meshtastic client APIs (web client,
Android/iOS apps, `meshtastic` CLI, Home Assistant Meshtastic integration) and Home Assistant
entities.

Fork of [Andrik45719/esphome-meshtastic](https://github.com/Andrik45719/esphome-meshtastic).
Changes per release: [CHANGELOG.md](CHANGELOG.md) and
[Releases](https://github.com/kernelorg/esphome-meshtastic/releases).

[![asciicast](https://asciinema.org/a/1041922.svg)](https://asciinema.org/a/1041922)

## Features

- LoRa mesh node on SX1262/SX1268 and SX1276–SX1279 radios
- Channels with pre-shared keys, direct messages with public-key encryption (PKI)
- Packet relaying with `CLIENT` or `ROUTER` role
- Fixed position with per-channel precision, device telemetry
- Public keys of other nodes learned from NodeInfo and kept in flash
- MQTT gateway (uplink/downlink per channel)
- Meshtastic client APIs: HTTP (web client) and TCP port 4403 (apps, CLI, Home Assistant
  integration), with a packet history for clients that were offline
- Home Assistant entities: received text, destination, send; `on_text_message` trigger
- UDP bridge and `packet_transport` platform
- Builds with Ethernet only (no `wifi:` needed)

## Getting Started

### Requirements

- ESP32 with an SX126x or SX127x LoRa radio, ESP-IDF framework
- ESPHome 2026.4 or later
- PSRAM recommended for the client packet history

### Installation

```yaml
external_components:
  - source: github://kernelorg/esphome-meshtastic@v5   # pin a release tag
    components: [meshtastic]
    refresh: 24h
```

Or clone the repository and use it as a local component:

```bash
git clone https://github.com/kernelorg/esphome-meshtastic.git
```

```yaml
external_components:
  - source:
      type: local
      path: esphome-meshtastic/components
    components: [meshtastic]
```

### Examples

- [meshtastic-api.yaml](meshtastic-api.yaml): Ethernet-only Olimex ESP32-PoE2 + Seeed
  Wio-SX1262 with relaying, telemetry, MQTT gateway on a public broker, HTTP and TCP client API,
  mDNS for the apps and Home Assistant entities.
- [meshtastic.yaml](meshtastic.yaml): ESP32-C3 + SX1262 with buttons, `on_packet` handling,
  UDP bridge and position.

### Minimal Configuration

```yaml
esphome:
  name: my-meshtastic-node

esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf

wifi:                        # or ethernet:, a network component is required
  ssid: !secret wifi_ssid
  password: !secret wifi_password

spi:
  clk_pin: GPIO10
  mosi_pin: GPIO7
  miso_pin: GPIO6

sx126x:
  id: lora_radio
  hw_version: sx1262
  modulation: LORA
  cs_pin: GPIO8
  busy_pin: GPIO4
  dio1_pin: GPIO3
  rst_pin: GPIO5
  rf_switch: true
  tcxo_voltage: 1_8V
  frequency: 869525000       # EU_868
  spreading_factor: 11       # LONG_FAST
  bandwidth: 250_0kHz
  coding_rate: CR_4_5
  preamble_size: 16
  sync_value: [0x20, 0xb0]   # Meshtastic sync word
  crc_enable: true
  rx_start: true

meshtastic:
  - id: mesh_node
    lora: lora_radio
    hw_model: 39             # DIY_V1
    hop_limit: 3
    role: CLIENT
    channels:
      - name: LongFast
        psk: "AQ=="          # default key
    nodes:
      - node_number: 0
        name: "My ESPHome Node"
        short_name: "ESPH"
        private_key: !secret node_private_key
        public_key: !secret node_public_key
```

The radio settings must match the mesh: frequency slot, spreading factor, bandwidth and coding
rate of its modem preset, preamble 16, sync word `0x2B` (`sync_value: [0x20, 0xb0]`).

## Configuration Reference

### Component

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `id` | ID | required | Component ID |
| `lora` | ID | — | `sx126x` or `sx127x` component |
| `hw_model` | int | 59 | Meshtastic hardware model reported in NodeInfo (39 = DIY_V1) |
| `hop_limit` | int | 7 | Hop limit of packets sent by this node (0–7) |
| `role` | enum | `CLIENT_MUTE` | `CLIENT_MUTE`: no relaying; `CLIENT`: relay, cancel when another node relays first; `ROUTER`: relay with priority |
| `slot_time` | time | 9ms | Contention slot of the modem preset, used for the relay delay (SHORT_FAST: 9 ms) |
| `ignore_mqtt` | bool | true | Drop packets that came through MQTT |
| `ok_to_mqtt` | bool | false | Allow MQTT gateways to publish this node's packets |
| `exclude_pki` | bool | false | Build without public-key encryption |
| `telemetry_interval` | time | 30min | DeviceMetrics to the mesh; `0s` = to connected clients only |
| `mqtt_root_topic` | string | `msh/EU_868` | MQTT gateway root topic |
| `channels` | list | — | See [Channels](#channels) |
| `nodes` | list | — | See [Nodes](#nodes) |
| `position` | map | — | See [Position](#position) |
| `client_api` | map | — | See [Client API](#client-api) |
| `udp` | map | — | See [UDP Bridge](#udp-bridge) |
| `on_packet` | automation | — | See [Triggers](#triggers) |
| `on_text_message` | automation | — | See [Triggers](#triggers) |

### Channels

```yaml
channels:
  - name: LongFast           # the first channel is the primary channel
    psk: "AQ=="              # 16/32-byte key in base64; 1 byte: "AQ==" default key, "AA==" no encryption
    position_precision: 13   # bits of lat/lon on this channel: 32 exact, 16 ≈ 350 m, 13 ≈ 3 km, 0 = no position
    uplink: false            # MQTT gateway: publish this channel
    downlink: false          # MQTT gateway: transmit this channel's packets received from MQTT
```

The channel name is part of the channel hash: it must be exactly the name the mesh uses.

### Nodes

```yaml
nodes:
  - node_number: 0           # 0 = this node
    name: "Full Name"
    short_name: "SHRT"
    private_key: !secret node_private_key   # 32 bytes, base64 (Curve25519)
    public_key: !secret node_public_key
    broadcast_interval: 3h   # NodeInfo broadcast
  - node_number: 0x12345678  # other node: pins its public key for direct messages
    name: "Peer"
    short_name: "PEER"
    public_key: "<32-byte base64>"
```

The node number of this node is derived from the MAC address and shown as `Node Num` in the log.

Keys of other nodes are also learned from their NodeInfo and stored in flash (16 nodes).
A learned key is never replaced by a different one; keys listed in `nodes:` take precedence.
A direct message from a node without a known key triggers a NodeInfo request to that node.

### Position

```yaml
position:
  latitude: 50.0614
  longitude: 19.9366
  altitude: 220
  broadcast_interval: 1h
```

The position is broadcast on the primary channel and sent with the precision of that channel.
Position requests are answered on the channel they arrive on.

### Packet Relaying

```yaml
meshtastic:
  - id: mesh_node
    role: CLIENT
    slot_time: 9ms
```

With `CLIENT` or `ROUTER` the node rebroadcasts packets that are not from or to itself, have not
been seen before and have hops left. Packets are forwarded encrypted, so no channel key is
needed. The delay before rebroadcasting depends on the SNR: weaker signal relays first.

### Device Telemetry

The node sends DeviceMetrics: battery level 101 (external power), uptime, channel utilization
(share of the last minute the channel was busy) and `air_util_tx` (share of the last hour this
node transmitted). Airtime is calculated from the radio settings. Telemetry goes to the mesh
every `telemetry_interval` and to connected clients every minute. The last DeviceMetrics
received from other nodes are passed to clients in the node list.

### Client API

```yaml
meshtastic:
  - id: mesh_node
    client_api:
      port: 80               # HTTP API, 0 = off
      tcp_port: 4403         # TCP API, 0 = off
      history: 1000          # packets kept for clients, 0 = off
      region: EU_868         # reported to clients
      modem_preset: SHORT_FAST
```

| Option | Default | Description |
|--------|---------|-------------|
| `port` | 80 | HTTP API (`PUT /api/v1/toradio`, `GET /api/v1/fromradio`) for the Meshtastic web client. Runs its own HTTP server; use another port if `web_server` uses 80 |
| `tcp_port` | 4403 | TCP stream API for the Android/iOS apps, `meshtastic --host <ip>` and the Home Assistant Meshtastic integration; up to 3 clients |
| `history` | 1000 | Packets kept for clients (up to 512 bytes each, in PSRAM; 32 without PSRAM), max 4000 |
| `region` | `EU_868` | Region shown to clients |
| `modem_preset` | `LONG_FAST` | Modem preset shown to clients |

Clients get the node, the node list, channels and settings, and exchange packets with the mesh,
including direct messages. Settings are read-only for clients: they come from the YAML, change
requests are refused.

Every packet received from the mesh is kept in the history and delivered to each client once,
after it has connected: a client that reconnects from the same IP gets what it missed, a new
client gets the whole history. The history is kept in RAM until the next reboot.

Give the node a fixed IP address (DHCP reservation), clients connect by address.

**App discovery.** The Meshtastic apps find nodes on the local network by mDNS. Announce the
service with the ESPHome `mdns:` component:

```yaml
mdns:
  services:
    - service: "_meshtastic"
      protocol: "_tcp"
      port: 4403             # = client_api.tcp_port
      txt:
        shortname: ESPH      # short_name of this node
        id: "!xxxxxxxx"      # node id ("Owner ID" in the log)
        pio_env: esphome
```

**Web client.** Browsers do not let `https://client.meshtastic.org` connect to an `http://`
device. Run the web client locally and connect to the node's IP:

```bash
docker run -p 8080:8080 ghcr.io/meshtastic/web
```

### Home Assistant

The node can be used from Home Assistant through its ESPHome entities, through the Meshtastic
integration (HACS) on `client_api.tcp_port`, or both. The integration adds a device per mesh
node with telemetry sensors, message triggers and notify entities.

ESPHome entities:

```yaml
text_sensor:
  - platform: meshtastic
    type: last_message       # last_message | last_sender | last_channel ("direct" for direct messages)
    name: "Mesh last message"

select:
  - platform: meshtastic     # destination: channels, then nodes listed in nodes:
    name: "Mesh destination"

text:
  - platform: meshtastic     # setting a value sends it to the selected destination
    name: "Mesh send"
```

Every received text message as a Home Assistant event (also when the same text repeats):

```yaml
meshtastic:
  - id: mesh_node
    on_text_message:
      - homeassistant.event:
          event: esphome.mesh_text
          data:
            sender: !lambda return sender;
            channel: !lambda return channel;   # "" = direct message
            text: !lambda return text;
```

### MQTT Gateway

Set `uplink`/`downlink` on channels and add the `mqtt:` component. Packets are exchanged
encrypted as Meshtastic `ServiceEnvelope` on `<mqtt_root_topic>/2/e/<channel>/<node id>`.

- Uplink publishes the node's own packets and packets received over LoRa whose sender allows
  MQTT (`ok_to_mqtt` / "OK to MQTT" on that node).
- Downlink transmits packets from other gateways on LoRa; it requires `ignore_mqtt: false`.
- After connecting to the broker the node sends its NodeInfo and position.

On a public broker, publish nothing but Meshtastic packets: disable ESPHome discovery, status
and log messages and the IP announcement, and give every entity `state_topic: null` (they stay
available to Home Assistant over the native API):

```yaml
mqtt:
  broker: loranet.pl
  discovery: false
  discover_ip: false
  log_topic: null
  birth_message: null
  will_message: null
  shutdown_message: null
  reboot_timeout: 0s         # keep running when the broker is unreachable

meshtastic:
  - id: mesh_node
    mqtt_root_topic: msh/PL
    ok_to_mqtt: true
    channels:
      - name: ShortFast
        psk: "AQ=="
        uplink: true

text_sensor:
  - platform: meshtastic
    type: last_message
    name: "Mesh last message"
    state_topic: null
```

Set `use_address` (or a DHCP reservation) so the ESPHome dashboard finds the node without MQTT.

### UDP Bridge

```yaml
meshtastic:
  - id: mesh_node
    udp:
      addresses:
        - 192.168.1.100
      multicast_address: 224.0.0.69
      bridge_mode: true      # forward LoRa <-> UDP
```

### Packet Transport

```yaml
packet_transport:
  - platform: meshtastic
    meshtastic_id: mesh_node
    binary_sensors:
      - my_sensor_id
    hop_limit: 2
    update_interval: 30min
```

### Triggers

`on_packet`: every received packet.

```yaml
on_packet:
  then:
    - lambda: |-
        ESP_LOGD("mesh", "from %08X to %08X port %d, %u bytes", from, to, portnum, data.size());
```

Variables: `from` (uint32), `to` (uint32), `portnum` (int32), `data` (`std::vector<uint8_t>`).

`on_text_message`: every received text message. Variables: `from` (uint32), `sender`
(`"!xxxxxxxx (SHORT)"`), `channel` (channel name, `""` for direct messages), `text`.

### Actions

`meshtastic.send_text_message`:

```yaml
- meshtastic.send_text_message:
    id: mesh_node
    channel: LongFast        # broadcast on a channel
    text: "Hello"

- meshtastic.send_text_message:
    id: mesh_node
    to: 0x12345678           # direct message
    text: "Hello"
```

`meshtastic.send_packet` sends raw data through `packet_transport`:

```yaml
- meshtastic.send_packet:
    id: mesh_node
    data: [0x01, 0x02, 0x03]
```

## Hardware

### Radios

SX1262, SX1268, SX1276, SX1277, SX1278, SX1279.

### Tested Boards

- Olimex ESP32-PoE2 (Ethernet) with Seeed Wio-SX1262
- ESP32-C3-DevKitM-1 with SX1262
- Generic ESP32 with SX1276

### Wiring Example (ESP32-C3 + SX1262)

```
ESP32-C3       SX1262
GPIO10   ----> CLK
GPIO7    ----> MOSI
GPIO6    ----> MISO
GPIO8    ----> CS
GPIO3    ----> DIO1
GPIO4    ----> BUSY
GPIO5    ----> RESET
```

## Troubleshooting

**No packets received.** Check frequency, spreading factor, bandwidth, coding rate, preamble and
sync word against the mesh preset, the antenna and the wiring.

**Packets received but not decoded (`No channel found for hash`).** Channel name and key must
match the mesh exactly; the log shows the hash of every configured channel at boot.

**Direct messages not decrypted (`No public key`).** The sender's key is not known yet. Wait for
its NodeInfo or list it under `nodes:`.

**Apps or the Home Assistant integration cannot connect.** Check `TCP API` lines in the log,
the node's IP address and that client and node are in the same network (mDNS does not cross
subnets).

**Nothing published to MQTT for another node.** That node must allow MQTT ("OK to MQTT"), and the
channel needs `uplink: true`.

## Resources

- [Meshtastic](https://meshtastic.org/)
- [ESPHome](https://esphome.io/)

## Maintainer

**kernelorg** — [@kernelorg](https://github.com/kernelorg)

Original component by **Andrik45719** — [@Andrik45719](https://github.com/Andrik45719)

## Acknowledgments

- [Meshtastic](https://github.com/meshtastic/firmware) for the mesh protocol
- [ESPHome](https://github.com/esphome/esphome) for the framework
- [nanopb](https://github.com/nanopb/nanopb) for Protocol Buffers
