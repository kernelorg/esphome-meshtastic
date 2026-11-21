# ESPHome Meshtastic Component

![Platform](https://img.shields.io/badge/platform-ESPHome-green)

A powerful ESPHome component that enables integration of [Meshtastic](https://meshtastic.org/)-compatible LoRa devices with ESPHome, allowing you to create mesh networking IoT devices with text messaging, packet routing, and mesh coordination capabilities.


> Fork of [Andrik45719/esphome-meshtastic](https://github.com/Andrik45719/esphome-meshtastic)
> with Ethernet-only builds, packet relaying, per-channel position precision, learned node keys
> and an MQTT gateway. Changes per release: [CHANGELOG.md](CHANGELOG.md),
> [Releases](https://github.com/kernelorg/esphome-meshtastic/releases).

[![asciicast](https://asciinema.org/a/1041922.svg)](https://asciinema.org/a/1041922)

## What It Does

This component transforms your ESP32 microcontroller into a full-featured Meshtastic mesh node. It provides:

- **LoRa Mesh Networking**: Direct integration with SX126x and SX127x LoRa radios for long-range mesh communication
- **Packet Routing**: Automatic routing and relay of packets through multiple nodes to extend network range
- **Text Messaging**: Send and receive text messages over the mesh network to specific nodes or channels
- **UDP Bridge Mode**: Optional bridge between LoRa mesh and UDP network for seamless integration
- **Position Broadcasting**: Share GPS location data across the mesh network
- **Multi-Channel Support**: Configure multiple channels with different Pre-Shared Keys (PSKs) for channel-based communication
- **Public Key Infrastructure (PKI)**: Support for encrypted communications with public/private key pairs
- **Packet Transport Protocol**: Integration with ESPHome's packet_transport component for advanced IoT applications

## Key Features

✨ **Meshtastic Protocol Support**
- Compatible with official Meshtastic clients and nodes
- Support for all Meshtastic port numbers and message types
- Automatic packet encryption, decryption, and authentication
- Most Meshtastic features easily implementable through simple YAML configuration

🌐 **Multiple Radio Options**
- Support for SX126x radios (SX1262, SX1268)
- Support for SX127x radios
- Configurable LoRa parameters (frequency, spreading factor, bandwidth, etc.)

📡 **Flexible Connectivity**
- Native LoRa mesh networking
- UDP multicast and unicast support for local network integration
- Bridging between LoRa mesh and UDP networks

🔐 **Security Features**
- Channel encryption with Pre-Shared Keys (PSK) for secure group communication
- Private message encryption using Public Key Infrastructure (PKI)
- Per-node public/private key management

🎛️ **ESPHome Integration**
- Full automation and trigger support
- On-packet received triggers with lambda expression support
- Send packet and text message actions
- Complete configuration via YAML

## Getting Started

### Requirements

- ESP32 microcontroller with LoRa radio (SX126x or SX127x)
- ESPHome 2026.4 or later
- Meshtastic mesh network (or create a new one)

### Installation

#### Option 1: External Component (Recommended)

Add to your ESPHome configuration:

```yaml
external_components:
  - source: github://kernelorg/esphome-meshtastic@v3   # pin a release tag
    components: [meshtastic]
    refresh: 24h
```

#### Option 2: Local Component

Clone this repository into your ESPHome `components` directory:

```bash
git clone https://github.com/kernelorg/esphome-meshtastic.git \
  ~/.esphome/components/esphome-meshtastic
```

### Basic Configuration Example

```yaml
esphome:
  name: my-meshtastic-node

esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf

# Configure SPI for LoRa radio
spi:
  clk_pin: GPIO10
  mosi_pin: GPIO7
  miso_pin: GPIO6

# Configure LoRa radio (SX1262 example)
sx126x:
  id: lora_radio
  cs_pin: GPIO8
  busy_pin: GPIO4
  dio1_pin: GPIO3
  rst_pin: GPIO5
  frequency: 433125000
  spreading_factor: 11
  bandwidth: 250_0kHz
  coding_rate: CR_4_5

# Configure Meshtastic component
meshtastic:
  - id: mesh_node
    lora: lora_radio
    hw_model: 39  # DIY_V1
    hop_limit: 3
    
    # Configure channels
    channels:
      - name: LongFast
        psk: "XOLPZHTWzHgykJxZ3pnj10mMJdr8glgblsNGLUIvd1w="
      - name: Admin
        psk: "cmF4UzVWbnZWQ0xxZlFyZXBSb2xhaHRNSkI1bFhabzU="
    
    # Configure this node
    nodes:
      - node_number: 0
        name: "My ESPHome Node"
        short_name: "ESPH"
        private_key: !secret node_private_key
        public_key: !secret node_public_key
    
    # Handle received packets
    on_packet:
      then:
        - lambda: |-
            ESP_LOGD("mesh", "Packet from %08X to %08X, port %d", from, to, portnum);

# Send text messages via button press
button:
  - platform: template
    name: "Send Mesh Message"
    on_press:
      then:
        - meshtastic.send_text_message:
            id: mesh_node
            channel: LongFast
            text: "Hello from ESPHome!"
```

### Advanced Configuration

#### UDP Bridge Mode

Connect your mesh to a local network:

```yaml
meshtastic:
  - id: mesh_node
    lora: lora_radio
    
    udp:
      addresses:
        - 192.168.1.100  # UDP server address
      multicast_address: 224.0.0.69
      bridge_mode: true  # Forward LoRa <-> UDP packets
```

#### Position Broadcasting

Share GPS coordinates with the mesh:

```yaml
meshtastic:
  - id: mesh_node
    lora: lora_radio
    
    position:
      latitude: 50.45
      longitude: 30.52
      altitude: 179
      broadcast_interval: 3h
```

#### Packet Transport Integration

Use with ESPHome's packet_transport for sensor data relay:

```yaml
packet_transport:
  - platform: meshtastic
    meshtastic_id: mesh_node
    binary_sensors:
      - my_sensor_id
    hop_limit: 2
    update_interval: 30min
```

#### Direct Node Messaging

Send messages directly to specific nodes:

```yaml
button:
  - platform: template
    name: "Send Direct Message"
    on_press:
      then:
        - meshtastic.send_text_message:
            id: mesh_node
            to: 848894052  # Node ID
            text: "Direct message"
```

### More Usage Examples

For additional configuration examples and a complete working setup, see [`meshtastic.yaml`](meshtastic.yaml) in this repository. It provides a real-world example with:
- SX1262 LoRa radio configuration
- Multiple channel setup
- Node configuration with encryption keys
- Button actions for sending messages
- Packet event handling with lambda expressions
- UDP bridge setup
- Position broadcasting configuration

## Configuration Reference

### Component Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `id` | string | required | Component ID |
| `lora` | string | required | SX126x or SX127x radio component ID |
| `hw_model` | integer | 39 | Hardware model number (39 = DIY_V1) |
| `hop_limit` | integer | 7 | Default hop limit for packets (0-7) |
| `ignore_mqtt` | boolean | true | Ignore MQTT-forwarded packets |
| `ok_to_mqtt` | boolean | false | Allow forwarding to MQTT |
| `exclude_pki` | boolean | false | Disable private key infrastructure |
| `role` | enum | CLIENT_MUTE | `CLIENT_MUTE` (no relaying), `CLIENT` (relay, cancel if another node relays first), `ROUTER` (relay with priority) |
| `slot_time` | time | 9ms | Contention slot of the LoRa preset (SHORT_FAST: 9 ms), used for the relay delay |
| `mqtt_root_topic` | string | msh/EU_868 | MQTT gateway root; topics are `<root>/2/e/<channel>/<gateway id>` |
| `client_api` | map | — | Meshtastic client HTTP API, see below |
| `on_text_message` | automation | — | Text received; variables `from` (uint32), `sender`, `channel` (`""` = direct), `text` |
| `broadcast_interval` | time | 3h | Node info broadcast interval |

### Channel Configuration

```yaml
channels:
  - name: "Channel Name"
    psk: "Base64EncodedPSK"  # 16/32-byte key, or 1 byte: "AQ==" default key, "AA==" no encryption
    position_precision: 13   # bits of lat/lon sent on this channel: 32 exact, 13 ≈ 3 km, 0 = no position
    uplink: false            # MQTT gateway: publish this channel's packets
    downlink: false          # MQTT gateway: transmit packets received from MQTT on LoRa
```

Position is broadcast on the first channel; requests are answered on the channel they arrived on.

### Packet Relaying

With `role: CLIENT` or `ROUTER` the node rebroadcasts LoRa packets like a Meshtastic device:
packets not from/to this node, not seen before, with `hop_limit > 0` and `next_hop` unset or
ours. The payload is forwarded still encrypted (no channel key needed), `hop_limit` is
decremented and `relay_node` set. The delay before rebroadcast is SNR-weighted (weaker signal
relays first); `CLIENT` cancels a pending relay when another node relays the packet first.

```yaml
meshtastic:
  - id: mesh_node
    role: CLIENT
    slot_time: 9ms   # SHORT_FAST
```

### MQTT Gateway

With `uplink`/`downlink` on a channel and the `mqtt:` component configured, the node is a
Meshtastic MQTT gateway. Packets are exchanged still encrypted as `ServiceEnvelope` protobufs.
Uplink publishes packets heard on LoRa whose sender set `ok_to_mqtt`, and our own packets.
Downlink transmits packets of that channel received from other gateways; it requires
`ignore_mqtt: false`.

```yaml
mqtt:
  broker: mqtt.example.org
  username: !secret mqtt_user
  password: !secret mqtt_password
  discovery: false
  log_topic: null
  reboot_timeout: 0s       # do not reboot the node when the broker is unreachable

meshtastic:
  - id: mesh_node
    mqtt_root_topic: msh/EU_868
    ignore_mqtt: false
    channels:
      - name: LongFast
        psk: "AQ=="
        uplink: true
        downlink: true
```

### Meshtastic Client API

Implements the HTTP transport of the Meshtastic clients (`PUT /api/v1/toradio`,
`GET /api/v1/fromradio`), so the Meshtastic web client can connect to the node, show nodes and
channels, and send/receive messages. It runs its own HTTP server (the ESPHome `web_server` has
no `PUT`); choose another port if `web_server` uses 80.

```yaml
meshtastic:
  - id: mesh_node
    client_api:
      port: 80
      region: EU_868          # reported to the client only
      modem_preset: SHORT_FAST
```

Supported: config download (`want_config`, including nonces 69420/69421), node list, channels,
text and other packets in both directions, PKI direct messages, ACK when a neighbour relays our
broadcast. Not supported: changing settings from the client (admin messages are ignored, the
YAML is the configuration). There is no authentication, as on Meshtastic devices.

The hosted client at `https://client.meshtastic.org` cannot reach a plain `http://` device
(mixed content). Run the web client locally (`docker run -p 8080:8080 ghcr.io/meshtastic/web`)
or allow insecure content for that site in the browser.

### Home Assistant Entities

```yaml
meshtastic:
  - id: mesh_node
    on_text_message:
      - homeassistant.event:
          event: esphome.mesh_text
          data:
            sender: !lambda return sender;
            channel: !lambda return channel;
            text: !lambda return text;

text_sensor:
  - platform: meshtastic
    type: last_message   # last_message | last_sender | last_channel ("direct" for DMs)
    name: "Mesh last message"

select:
  - platform: meshtastic   # destination: channels, then nodes from `nodes:`
    name: "Mesh destination"

text:
  - platform: meshtastic   # setting the value sends it to the selected destination
    name: "Mesh send"
```

`on_text_message` fires for every message, also when the same text repeats; the text sensors
only change state when the value changes.

### Learned Node Keys

Public keys from received NodeInfo packets are stored in flash (up to 16 nodes, trust on first
use: a known key is never replaced). Direct (PKI) messages then work without listing the node in
`nodes:`. On a direct message from an unknown node, our NodeInfo is sent to it with
`want_response` to obtain its key. Keys in `nodes:` always take precedence.

### Node Configuration

```yaml
nodes:
  - node_number: 0  # 0 = this node
    name: "Full Name"
    short_name: "SHORT"
    private_key: !secret my_private_key
    public_key: !secret my_public_key
```

### on_packet Trigger

Triggered when a packet is received:

```yaml
on_packet:
  then:
    - lambda: |-
        uint32_t from_addr = from;
        uint32_t to_addr = to;
        uint32_t port = portnum;
        const std::vector<uint8_t>& payload = data;
```

Available variables:
- `from`: Source node ID (uint32)
- `to`: Destination node ID (uint32)
- `portnum`: Port number (uint32)
- `data`: Payload data (vector<uint8>)

### Actions

#### `meshtastic.send_text_message`

Send a text message to a channel or node:

```yaml
- meshtastic.send_text_message:
    id: mesh_node
    channel: "LongFast"  # Send to channel
    text: "Hello"

- meshtastic.send_text_message:
    id: mesh_node
    to: 848894052  # Send to specific node
    text: "Direct message"
```

#### `meshtastic.send_packet`

Send raw binary data:

```yaml
- meshtastic.send_packet:
    id: mesh_node
    data: [0x01, 0x02, 0x03]
    hop_limit: 3
```

## Hardware Compatibility

### Supported LoRa Radios

- **SX1262** - LoRaWAN module (US, EU, JP frequencies)
- **SX1268** - LoRaWAN module
- **SX1276** - Legacy LoRa module
- **SX1277** - Legacy LoRa module
- **SX1278** - Legacy LoRa module
- **SX1279** - Legacy LoRa module

### Tested Boards

- ESP32-C3-DevKitM-1 with SX1262
- Generic ESP32 with SX1276

### Pinout Example (ESP32-C3 + SX1262)

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

### Component Not Showing in ESPHome

- Ensure you've added the external_components section correctly
- Check the ESPHome logs for compilation errors
- Verify your SX126x/SX127x component is properly configured

### Packets Not Being Received

- Verify LoRa radio parameters match your mesh (frequency, SF, BW, etc.)
- Check that PSKs match the channel configuration in your mesh
- Ensure proper antenna connection and LoRa radio wiring
- Check node_number and channel hash configuration

### Cannot Send Messages

- Verify the destination node exists in your mesh
- Check hop_limit is not set to 0
- Ensure the channel PSK matches the target network
- Check ESP32 heap memory availability

### Encryption/Decryption Errors

- Verify public/private keys are correctly configured
- Ensure PKI is not disabled (`exclude_pki: false`)
- Check that key format is correct (32-byte keys in base64)

## Contributing

Contributions are welcome! To contribute:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/your-feature`)
3. Commit your changes (`git commit -am 'Add feature'`)
4. Push to the branch (`git push origin feature/your-feature`)
5. Open a Pull Request

## Support & Resources

- **Meshtastic Official Site**: https://meshtastic.org/
- **ESPHome Documentation**: https://esphome.io/
- **GitHub Issues**: Report bugs or request features on [GitHub](https://github.com/kernelorg/esphome-meshtastic/issues)
- **Meshtastic Discord**: Join the community at https://discord.gg/g9Hgv9KX (Meshtastic community)

## Maintainer

**kernelorg** - [@kernelorg](https://github.com/kernelorg) (this fork)

Original component by **Andrik45719** - [@Andrik45719](https://github.com/Andrik45719)

## Acknowledgments

- [Meshtastic Project](https://github.com/meshtastic/firmware) for the mesh networking protocol
- [ESPHome Project](https://github.com/esphome/esphome) for the framework
- [NanoPB](https://github.com/nanopb/nanopb) for Protocol Buffer support
