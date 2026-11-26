// Own device telemetry (DeviceMetrics) and airtime accounting, after Meshtastic AirTime/DeviceTelemetry
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "meshtastic.h"

#include <cmath>
#include <pb_encode.h>

namespace esphome {
namespace meshtastic {

static const char *const TAG = "meshtastic.component";

static constexpr uint32_t CU_PERIOD_MS = 10 * 1000;
static constexpr uint32_t TX_PERIOD_MS = 60 * 1000;

// LoRa time on air (Semtech AN1200.13): explicit header, CRC on
uint32_t Meshtastic::airtime_ms_(size_t packet_len) {
  if (this->lora_sf_ == 0 || this->lora_bw_hz_ == 0)
    return 0;
  double tsym = std::pow(2.0, this->lora_sf_) / this->lora_bw_hz_ * 1000.0;  // ms
  int de = tsym > 16.0 ? 1 : 0;                                              // low data rate optimisation
  double tpre = (this->lora_preamble_ + 4.25) * tsym;
  double num = 8.0 * packet_len - 4.0 * this->lora_sf_ + 28 + 16;
  double den = 4.0 * (this->lora_sf_ - 2 * de);
  double nsym = 8 + std::max(std::ceil(num / den) * (this->lora_cr_ + 4), 0.0);
  return (uint32_t) std::lround(tpre + nsym * tsym);
}

void Meshtastic::airtime_rotate_() {
  uint32_t now = millis();
  uint32_t cu = now / CU_PERIOD_MS, tx = now / TX_PERIOD_MS;
  while (this->cu_period_ < cu) {
    this->cu_period_++;
    this->cu_ms_[this->cu_period_ % CU_PERIODS] = 0;
    if (cu - this->cu_period_ > CU_PERIODS)
      this->cu_period_ = cu - CU_PERIODS;
  }
  while (this->tx_period_ < tx) {
    this->tx_period_++;
    this->tx_ms_[this->tx_period_ % TX_PERIODS] = 0;
    if (tx - this->tx_period_ > TX_PERIODS)
      this->tx_period_ = tx - TX_PERIODS;
  }
}

void Meshtastic::account_airtime_(uint32_t ms, bool tx) {
  this->airtime_rotate_();
  this->cu_ms_[this->cu_period_ % CU_PERIODS] += ms;  // channel busy: our TX and everything we hear
  if (tx)
    this->tx_ms_[this->tx_period_ % TX_PERIODS] += ms;
}

// Percent of the last minute the channel was busy (RX + own TX)
float Meshtastic::channel_utilization_() {
  this->airtime_rotate_();
  uint32_t sum = 0;
  for (uint32_t v : this->cu_ms_)
    sum += v;
  uint32_t window = std::min<uint32_t>(millis(), CU_PERIODS * CU_PERIOD_MS);
  return window ? 100.0f * sum / window : 0.0f;
}

// Percent of the last hour we transmitted
float Meshtastic::air_util_tx_() {
  this->airtime_rotate_();
  uint32_t sum = 0;
  for (uint32_t v : this->tx_ms_)
    sum += v;
  uint32_t window = std::min<uint32_t>(millis(), TX_PERIODS * TX_PERIOD_MS);
  return window ? 100.0f * sum / window : 0.0f;
}

meshtastic_DeviceMetrics Meshtastic::own_device_metrics_() {
  meshtastic_DeviceMetrics dm = meshtastic_DeviceMetrics_init_zero;
  dm.has_battery_level = true;
  dm.battery_level = 101;  // Meshtastic convention: >100 = external power (no battery)
  dm.has_uptime_seconds = true;
  dm.uptime_seconds = millis() / 1000;
  if (this->lora_sf_ != 0) {
    dm.has_channel_utilization = true;
    dm.channel_utilization = this->channel_utilization_();
    dm.has_air_util_tx = true;
    dm.air_util_tx = this->air_util_tx_();
  }
  return dm;
}

void Meshtastic::send_telemetry_(bool mesh) {
  if (!mesh && !this->client_any_active_())
    return;
  meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;
  t.time = (uint32_t) ::time(nullptr);
  t.which_variant = meshtastic_Telemetry_device_metrics_tag;
  t.variant.device_metrics = this->own_device_metrics_();

  meshtastic_MeshPacket mp;
  memset(&mp, 0, sizeof(mp));
  if (this->channels_.empty())
    return;
  mp.from = this->node_num_;
  mp.to = NODENUM_BROADCAST;
  mp.id = generatePacketId();
  mp.channel = this->channels_.front().get_hash();
  mp.priority = meshtastic_MeshPacket_Priority_BACKGROUND;
  mp.hop_limit = this->hop_limit_;
  mp.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
  mp.decoded.payload.size =
      pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes), &meshtastic_Telemetry_msg, &t);
  if (mesh) {
    ESP_LOGI(TAG, "Send telemetry: uptime=%" PRIu32 " ch_util=%.2f air_tx=%.2f", t.variant.device_metrics.uptime_seconds,
             t.variant.device_metrics.channel_utilization, t.variant.device_metrics.air_util_tx);
    meshtastic_Data decoded = mp.decoded;  // transmit_packet_encrypt() replaces the payload variant
    this->transmit_packet_encrypt(mp);
    mp.decoded = decoded;
  }
  // our own telemetry is shown to clients as a packet from us (Meshtastic sends it to the phone too)
  mp.rx_time = (uint32_t) ::time(nullptr);
  mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  this->client_push_packet_(mp, mp.decoded, false);  // live only, not kept in the history
}

// Answer a DeviceMetrics request ("request telemetry" in the apps); a PKI request (channel 0)
// is answered PKI-encrypted, otherwise on the channel it came on.
void Meshtastic::send_telemetry_response_(NodeNum to, uint8_t channel, PacketId request_id) {
  meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;
  t.time = (uint32_t) ::time(nullptr);
  t.which_variant = meshtastic_Telemetry_device_metrics_tag;
  t.variant.device_metrics = this->own_device_metrics_();
  meshtastic_MeshPacket mp;
  memset(&mp, 0, sizeof(mp));
  mp.from = this->node_num_;
  mp.to = to;
  mp.id = generatePacketId();
  mp.channel = channel;
  mp.priority = meshtastic_MeshPacket_Priority_RELIABLE;
  mp.hop_limit = this->hop_limit_;
  mp.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
  mp.decoded.request_id = request_id;
  mp.decoded.payload.size =
      pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes), &meshtastic_Telemetry_msg, &t);
  ESP_LOGI(TAG, "Telemetry request from 0x%08" PRIx32 ", answering", to);
  this->transmit_packet_encrypt(mp);
}

void Meshtastic::store_telemetry_(NodeNum from, const meshtastic_Telemetry &t) {
  if (t.which_variant != meshtastic_Telemetry_device_metrics_tag || from == this->node_num_)
    return;
  Node *n = get_node_by_num(from);
  if (n == nullptr)
    return;
  n->device_metrics_ = t.variant.device_metrics;
  n->has_device_metrics_ = true;
}

}  // namespace meshtastic
}  // namespace esphome
