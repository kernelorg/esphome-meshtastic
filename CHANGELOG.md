# Changelog

## v5

- Packet history for clients: every packet received from the mesh is kept in PSRAM
  (`client_api.history`, default 1000 packets of up to 512 bytes, max 4000; 32 in internal RAM
  without PSRAM) and delivered once to each client, identified by IP: a client that reconnects
  gets what it missed, a new client gets the whole history, after its config (like the
  Meshtastic to-phone queue). Lost on reboot.
- Actions are registered with `synchronous=True` on ESPHome 2026.11+ (no more warnings);
  older ESPHome versions keep working.
- Admin messages to the node from clients: `get_channel`, `get_owner`, `get_config`,
  `get_module_config`, `get_device_metadata` and `get_device_connection_status` are answered
  (with a session passkey), `set_time_only` is accepted, requests with `want_ack` get a routing
  ACK; changes (`set_*`, reboot, reset, ...) are refused with `NOT_AUTHORIZED` instead of being
  dropped silently. The Android app kept reconnecting while waiting for these answers.
- Heartbeats from clients are answered with `queueStatus`, and every packet a client sends is
  confirmed with `queueStatus` (`mesh_packet_id`), as Meshtastic does; heartbeat nonce 1 triggers
  a NodeInfo broadcast. Without the reply the Android app dropped the connection every ~20 s.
- Fix: packets a client addresses to the node itself are no longer transmitted over LoRa. NodeInfo,
  position and telemetry requests are answered locally, other requests get `NO_RESPONSE` (as
  Meshtastic does without the module), `want_ack` gets an ACK.
- `want_config` answer in Meshtastic PhoneAPI order (my_info, own NodeInfo, metadata, channels,
  config, module config, other NodeInfos); nonce 69420 now includes the own NodeInfo and 69421
  sends only NodeInfos (no my_info), as the two-stage handshake of the Android app expects.
- Log: client heartbeats and why a TCP client was disconnected.
- TCP API clients silent for 15 minutes are closed, so vanished clients do not hold the 3 slots.
- Fix: the component declares its own sockets (TCP API, HTTP API, UDP bridge) to ESPHome, which
  sizes the lwIP socket limit from them. Before, with `mqtt:` and both client APIs the limit (10)
  was exhausted and lwIP reset new connections on port 4403 (apps kept reconnecting). A failed
  accept is now logged.
- Example `meshtastic-api.yaml`: mDNS service `_meshtastic._tcp` so the Meshtastic apps find the
  node on the LAN.
- README rewritten as usage documentation with a configuration reference; install source tag v5.

## v4

- Meshtastic TCP API: `client_api.tcp_port` (default 4403, 0 = off) serves the stream protocol
  for the Home Assistant Meshtastic integration, `meshtastic --host` and apps; up to 3 clients.
- Every client (each TCP connection, the HTTP client) has its own FromRadio queue; `client_api.port`
  0 disables the HTTP API.
- Device telemetry: own DeviceMetrics (battery 101 = external power, uptime, channel_utilization,
  air_util_tx from LoRa airtime) to the mesh every `telemetry_interval` (default 30 min), to
  clients every minute and in the node list; last DeviceMetrics of other nodes are kept and sent
  to clients in the node list.
- MQTT gateway announces NodeInfo and position once the broker is connected, so the node appears
  on MQTT maps without waiting for the next broadcast.
- Own position sent to clients carries time and precision_bits.
- Example `meshtastic-api.yaml`: Ethernet-only node with HTTP + TCP client API, telemetry,
  MQTT gateway on a public broker (no discovery/status/IP, `state_topic: null` on all entities)
  and Home Assistant entities.
- README: install source tag v4, client APIs, MQTT gateway on public brokers.

## v3

- Meshtastic client API: `client_api` (port, region, modem_preset) serves
  `PUT /api/v1/toradio` / `GET /api/v1/fromradio` on its own HTTP server; config download,
  node list, channels, packets both ways, PKI direct messages, implicit ACK. Admin messages
  are ignored.
- Home Assistant: `text_sensor` (`last_message`, `last_sender`, `last_channel`), `select`
  (destination) and `text` (send) platforms; `on_text_message` trigger with `from`, `sender`,
  `channel`, `text`.
- Received text is logged with sender and channel or "direct".
- Node list keeps names, hardware model, role, SNR, hops and last heard of nodes (64 in RAM);
  names are learned also from NodeInfo without a public key.
- PKI send checks that the destination key is known.
- Routing (ACK) packets are no longer acknowledged.
- Client API compatibility with the Meshtastic web client 2.7.x: channel settings are sent for
  empty slots too, ACKs carry the id of the acknowledged message (the client matches by packet
  id), and the `statusmessage`, `traffic_management` and `tak` module configs are sent (defaults).
- README points to this fork (install source, clone URL, issues, maintainer).

## v2

- Channel option `position_precision` (0..32, default 32): position is rounded like Meshtastic
  (cell centre, `precision_bits` set); `0` sends no position on that channel. Position is
  broadcast on the first channel, requests are answered on the channel they arrived on.
- Learned node keys: public keys from NodeInfo are stored in flash (16 nodes, trust on first
  use, keys in `nodes:` take precedence). A direct message from an unknown node triggers a
  NodeInfo request with `want_response`.
- MQTT gateway on top of the ESPHome `mqtt:` component: channel options `uplink`/`downlink`,
  component option `mqtt_root_topic` (default `msh/EU_868`); topics
  `<root>/2/e/<channel>/<gateway id>`, encrypted `ServiceEnvelope` payloads. Uplink honours
  `ok_to_mqtt` and skips `via_mqtt` packets; downlink drops our own envelopes and duplicates
  and is transmitted on LoRa through the relay path (needs `ignore_mqtt: false`).
- Validation error when `uplink`/`downlink` is set without `mqtt:`.

## v1

- Build without the `wifi:` component (Ethernet-only boards): `esp_wifi.h` and
  `esp_wifi_set_ps()` are guarded with `USE_WIFI`.
- Build on ESPHome 2026.x: include `esphome/core/alloc_helpers.h` for `format_hex_pretty()`.
- Packet relaying: new options `role` (`CLIENT_MUTE` default, `CLIENT`, `ROUTER`) and
  `slot_time` (default `9ms`). NodeInfo advertises the configured role.
- All received packets are recorded in the packet history, not only decryptable ones.
- Fix: 1-byte channel PSKs (`AQ==`, `sQ==`, ...) are expanded to the Meshtastic default key.
  Before, AES failed, the channel hash was wrong (ShortFast `0x73` instead of `0x70`) and
  packets were transmitted unencrypted.
- Transmit and receive abort when AES fails; nothing is sent in plaintext.
