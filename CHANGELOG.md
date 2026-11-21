# Changelog

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
