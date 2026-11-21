# Changelog

## Unreleased

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
