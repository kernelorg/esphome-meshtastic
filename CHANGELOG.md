# Changelog

## Unreleased

- Meshtastic TCP API: `client_api.tcp_port` (default 4403, 0 = off) serves the stream protocol
  for the Home Assistant Meshtastic integration, `meshtastic --host` and apps; up to 3 clients.
- Every client (each TCP connection, the HTTP client) has its own FromRadio queue; `client_api.port`
  0 disables the HTTP API.
- Example `meshtastic-api.yaml`: Ethernet-only node with the client API and Home Assistant entities.

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
