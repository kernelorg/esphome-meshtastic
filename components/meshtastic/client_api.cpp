// Text message handling, Home Assistant helpers and the Meshtastic client API (HTTP transport)
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "meshtastic.h"

#include <pb_decode.h>
#include <pb_encode.h>
#include "config.pb.h"
#include "module_config.pb.h"
#include "channel.pb.h"

#ifdef MESHTASTIC_CLIENT_API
#include <esp_http_server.h>
#endif

namespace esphome {
namespace meshtastic {

static const char *const TAG = "meshtastic.component";

static constexpr size_t CLIENT_PACKET_QUEUE_MAX = 32;
static constexpr size_t CLIENT_TORADIO_QUEUE_MAX = 8;
static constexpr uint32_t CLIENT_IDLE_MS = 5 * 60 * 1000;  // stop queueing when no client polled
static constexpr uint32_t NONCE_ONLY_CONFIG = 69420;
static constexpr uint32_t NONCE_ONLY_NODES = 69421;
static constexpr size_t MAX_NODES_IN_RAM = 64;

// ---------------------------------------------------------------------------
// Text messages and node bookkeeping
// ---------------------------------------------------------------------------
std::string Meshtastic::node_label_(NodeNum num) {
  char id[12];
  snprintf(id, sizeof(id), "!%08" PRIx32, num);
  Node *n = get_node_by_num(num);
  if (n != nullptr && !n->name_short_.empty())
    return std::string(id) + " (" + n->name_short_ + ")";
  return id;
}

void Meshtastic::update_node_seen_(const meshtastic_MeshPacket &mp) {
  if (mp.from == this->node_num_)
    return;
  Node *n = get_node_by_num(mp.from);
  if (n == nullptr) {
    // keep RAM bounded: drop the least recently heard learned node
    if (this->nodes_.size() >= MAX_NODES_IN_RAM) {
      auto oldest = this->nodes_.end();
      for (auto it = this->nodes_.begin(); it != this->nodes_.end(); ++it)
        if ((*it)->learned_ && (oldest == this->nodes_.end() || (*it)->last_heard_ < (*oldest)->last_heard_))
          oldest = it;
      if (oldest == this->nodes_.end())
        return;
      this->nodes_.erase(oldest);
    }
    auto nn = std::make_unique<Node>();
    nn->node_num_ = mp.from;
    nn->learned_ = true;
    n = nn.get();
    this->nodes_.push_back(std::move(nn));
  }
  n->last_heard_ = (uint32_t) ::time(nullptr);
  if (mp.transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA)
    n->snr_ = mp.rx_snr;
  if (mp.hop_start != 0 && mp.hop_limit <= mp.hop_start)
    n->hops_away_ = mp.hop_start - mp.hop_limit;
}

void Meshtastic::on_text_received_(const meshtastic_MeshPacket &mp, const std::string &channel_name,
                                   const std::string &text) {
  bool direct = channel_name.empty();
  std::string sender = this->node_label_(mp.from);
  if (direct)
    ESP_LOGI(TAG, "Text from %s direct (PKI): %s", sender.c_str(), text.c_str());
  else if (mp.to == this->node_num_)
    ESP_LOGI(TAG, "Text from %s direct on channel '%s': %s", sender.c_str(), channel_name.c_str(), text.c_str());
  else
    ESP_LOGI(TAG, "Text from %s on channel '%s': %s", sender.c_str(), channel_name.c_str(), text.c_str());

  // channel is "" for direct messages
  std::string channel = (mp.to == this->node_num_) ? std::string() : channel_name;
#ifdef USE_TEXT_SENSOR
  if (this->last_sender_sensor_ != nullptr)
    this->last_sender_sensor_->publish_state(sender);
  if (this->last_channel_sensor_ != nullptr)
    this->last_channel_sensor_->publish_state(channel.empty() ? std::string("direct") : channel);
  if (this->last_message_sensor_ != nullptr)
    this->last_message_sensor_->publish_state(text);  // last, so automations see sender/channel already set
#endif
  this->text_listeners_.call(mp.from, sender, channel, text);
}

bool Meshtastic::send_text_to_destination(const std::string &destination, const std::string &text) {
  if (text.empty())
    return false;
  if (!destination.empty() && destination[0] == '!') {
    NodeNum to = strtoul(destination.c_str() + 1, nullptr, 16);
    return this->send_text_message(to, "", text);
  }
  // "Name (!xxxxxxxx)" style options from the destination select
  auto pos = destination.rfind("(!");
  if (pos != std::string::npos) {
    NodeNum to = strtoul(destination.c_str() + pos + 2, nullptr, 16);
    return this->send_text_message(to, "", text);
  }
  return this->send_text_message(NODENUM_BROADCAST, destination, text);
}

int Meshtastic::channel_index_by_hash_(uint8_t hash) {
  for (size_t i = 0; i < this->channels_.size(); i++)
    if (this->channels_[i].get_hash() == hash)
      return (int) i;
  return -1;
}

// ---------------------------------------------------------------------------
// Client API queues (shared with the HTTP task, guarded by client_mutex_)
// ---------------------------------------------------------------------------
void Meshtastic::client_push_(std::deque<std::vector<uint8_t>> &q, meshtastic_FromRadio &fr, size_t max) {
  fr.id = ++this->client_fromradio_id_;
  std::vector<uint8_t> buf(meshtastic_FromRadio_size);
  size_t len = pb_encode_to_bytes(buf.data(), buf.size(), &meshtastic_FromRadio_msg, &fr);
  if (len == 0) {
    ESP_LOGW(TAG, "FromRadio encode failed (variant %u)", (unsigned) fr.which_payload_variant);
    return;
  }
  buf.resize(len);
  LockGuard lock(this->client_mutex_);
  if (max && q.size() >= max)
    q.pop_front();
  q.push_back(std::move(buf));
}

void Meshtastic::client_push_packet_(const meshtastic_MeshPacket &mp, const meshtastic_Data &decoded) {
  if (this->client_api_port_ == 0 || millis() - this->client_last_seen_ > CLIENT_IDLE_MS)
    return;
  auto fr = std::make_unique<meshtastic_FromRadio>();
  memset(fr.get(), 0, sizeof(meshtastic_FromRadio));
  fr->which_payload_variant = meshtastic_FromRadio_packet_tag;
  meshtastic_MeshPacket &p = fr->packet;
  p = mp;
  p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  p.decoded = decoded;
  // Web client 2.7.x updates the message state by the routing packet's own id instead of
  // request_id; give ACK/NAK packets the id of the message they answer so it matches.
  if (decoded.portnum == meshtastic_PortNum_ROUTING_APP && decoded.request_id != 0)
    p.id = decoded.request_id;
  bool pki = (mp.channel == 0 && mp.to == this->node_num_ && channel_index_by_hash_(0) < 0);
  int idx = pki ? 0 : channel_index_by_hash_(mp.channel);
  p.channel = idx < 0 ? 0 : idx;  // clients expect the channel index, not the hash
  p.pki_encrypted = pki;
  if (pki) {
    Node *n = get_node_by_num(mp.from);
    if (n != nullptr && n->public_key_.size() == 32) {
      memcpy(p.public_key.bytes, n->public_key_.data(), 32);
      p.public_key.size = 32;
    }
  }
  this->client_push_(this->client_packet_q_, *fr, CLIENT_PACKET_QUEUE_MAX);
}

void Meshtastic::client_push_routing_ack_(PacketId id) {
  if (this->client_pending_acks_.erase(id) == 0)
    return;
  auto fr = std::make_unique<meshtastic_FromRadio>();
  memset(fr.get(), 0, sizeof(meshtastic_FromRadio));
  fr->which_payload_variant = meshtastic_FromRadio_packet_tag;
  meshtastic_MeshPacket &p = fr->packet;
  p.from = this->node_num_;
  p.to = this->node_num_;
  p.id = id;  // see client_push_packet_(): the web client matches the ACK by packet id
  p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  p.decoded.portnum = meshtastic_PortNum_ROUTING_APP;
  p.decoded.request_id = id;
  meshtastic_Routing r = meshtastic_Routing_init_zero;
  r.which_variant = meshtastic_Routing_error_reason_tag;
  r.error_reason = meshtastic_Routing_Error_NONE;
  p.decoded.payload.size = pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_Routing_msg, &r);
  this->client_push_(this->client_packet_q_, *fr, CLIENT_PACKET_QUEUE_MAX);
}

void Meshtastic::client_api_put_(const uint8_t *data, size_t len) {
  LockGuard lock(this->client_mutex_);
  if (this->client_toradio_q_.size() >= CLIENT_TORADIO_QUEUE_MAX)
    this->client_toradio_q_.pop_front();
  this->client_toradio_q_.emplace_back(data, data + len);
}

bool Meshtastic::client_api_get_(std::vector<uint8_t> &out) {
  LockGuard lock(this->client_mutex_);
  this->client_last_seen_ = millis();
  auto &q = !this->client_config_q_.empty() ? this->client_config_q_ : this->client_packet_q_;
  if (q.empty())
    return false;
  out = std::move(q.front());
  q.pop_front();
  return true;
}

// ---------------------------------------------------------------------------
// ToRadio handling (main loop)
// ---------------------------------------------------------------------------
void Meshtastic::client_api_loop_() {
  if (this->client_api_port_ == 0)
    return;
  for (;;) {
    std::vector<uint8_t> buf;
    {
      LockGuard lock(this->client_mutex_);
      if (this->client_toradio_q_.empty())
        return;
      buf = std::move(this->client_toradio_q_.front());
      this->client_toradio_q_.pop_front();
    }
    this->client_handle_toradio_(buf);
  }
}

void Meshtastic::client_handle_toradio_(const std::vector<uint8_t> &buf) {
  auto tr = std::make_unique<meshtastic_ToRadio>();
  memset(tr.get(), 0, sizeof(meshtastic_ToRadio));
  if (!pb_decode_from_bytes(buf.data(), buf.size(), &meshtastic_ToRadio_msg, tr.get())) {
    ESP_LOGW(TAG, "Bad ToRadio (%u bytes)", (unsigned) buf.size());
    return;
  }
  switch (tr->which_payload_variant) {
    case meshtastic_ToRadio_want_config_id_tag:
      ESP_LOGI(TAG, "Client wants config, nonce=%" PRIu32, tr->want_config_id);
      this->client_send_config_(tr->want_config_id);
      break;
    case meshtastic_ToRadio_disconnect_tag: {
      LockGuard lock(this->client_mutex_);
      this->client_config_q_.clear();
      this->client_packet_q_.clear();
      break;
    }
    case meshtastic_ToRadio_heartbeat_tag:
      break;
    case meshtastic_ToRadio_packet_tag: {
      meshtastic_MeshPacket &p = tr->packet;
      if (p.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        break;
      if (p.decoded.portnum == meshtastic_PortNum_ADMIN_APP && (p.to == this->node_num_ || p.to == 0)) {
        ESP_LOGW(TAG, "Admin messages from the client are not supported, configure the node in YAML");
        break;
      }
      size_t idx = p.channel < this->channels_.size() ? p.channel : 0;
      if (this->channels_.empty())
        break;
      meshtastic_MeshPacket mp;
      memset(&mp, 0, sizeof(mp));
      mp.from = this->node_num_;
      mp.to = p.to ? p.to : NODENUM_BROADCAST;
      mp.id = p.id ? p.id : generatePacketId();
      mp.hop_limit = (p.hop_limit && p.hop_limit <= this->hop_limit_) ? p.hop_limit : this->hop_limit_;
      mp.want_ack = p.want_ack;
      mp.priority = p.priority ? p.priority : meshtastic_MeshPacket_Priority_RELIABLE;
      mp.decoded = p.decoded;
      mp.decoded.has_bitfield = false;
      mp.channel = this->channels_[idx].get_hash();
      if (mp.to != NODENUM_BROADCAST && idx == 0) {
        Node *dest = get_node_by_num(mp.to);
        if (dest != nullptr && dest->public_key_.size() == 32)
          mp.channel = 0;  // direct message with PKI, like Meshtastic
      }
      ESP_LOGI(TAG, "Client send id=%08" PRIx32 " to=%08" PRIx32 " ch=%u port=%d", mp.id, mp.to, (unsigned) idx,
               mp.decoded.portnum);
      if (mp.want_ack || mp.to == NODENUM_BROADCAST)
        this->client_pending_acks_.insert(mp.id);
      if (this->client_pending_acks_.size() > 32)
        this->client_pending_acks_.erase(this->client_pending_acks_.begin());
      this->transmit_packet_encrypt(mp, mp.decoded.want_response);
      break;
    }
    default:
      ESP_LOGD(TAG, "Unhandled ToRadio variant %u", (unsigned) tr->which_payload_variant);
  }
}

void Meshtastic::client_send_config_(uint32_t nonce) {
  {
    LockGuard lock(this->client_mutex_);
    this->client_config_q_.clear();
    this->client_last_seen_ = millis();
  }
  auto fr = std::make_unique<meshtastic_FromRadio>();
  auto reset = [&fr](pb_size_t tag) {
    memset(fr.get(), 0, sizeof(meshtastic_FromRadio));
    fr->which_payload_variant = tag;
  };
  auto &q = this->client_config_q_;

  // MyNodeInfo
  reset(meshtastic_FromRadio_my_info_tag);
  fr->my_info.my_node_num = this->node_num_;
  fr->my_info.min_app_version = 30200;
  strncpy(fr->my_info.pio_env, "esphome", sizeof(fr->my_info.pio_env) - 1);
  fr->my_info.nodedb_count = this->nodes_.size();
  this->client_push_(q, *fr, 0);

  if (nonce != NONCE_ONLY_NODES) {
    // DeviceMetadata
    reset(meshtastic_FromRadio_metadata_tag);
    auto &md = fr->metadata;
    strncpy(md.firmware_version, "2.7.0.esphome", sizeof(md.firmware_version) - 1);
    md.device_state_version = 24;
    md.hasEthernet = true;
#ifdef USE_WIFI
    md.hasWifi = true;
#endif
    md.role = this->owner.role;
    md.hw_model = this->owner.hw_model;
    md.hasPKC = true;
    this->client_push_(q, *fr, 0);
  }

  // NodeInfo: own node first
  if (nonce != NONCE_ONLY_CONFIG) {
    reset(meshtastic_FromRadio_node_info_tag);
    fr->node_info.num = this->node_num_;
    fr->node_info.has_user = true;
    fr->node_info.user = this->owner;
    if (this->owner_position.has_latitude_i) {
      fr->node_info.has_position = true;
      fr->node_info.position = this->owner_position;
    }
    fr->node_info.last_heard = (uint32_t) ::time(nullptr);
    this->client_push_(q, *fr, 0);
    for (auto &n : this->nodes_) {
      if (n->node_num_ == 0 || n->node_num_ == this->node_num_)
        continue;
      reset(meshtastic_FromRadio_node_info_tag);
      auto &ni = fr->node_info;
      ni.num = n->node_num_;
      ni.has_user = true;
      snprintf(ni.user.id, sizeof(ni.user.id), "!%08" PRIx32, n->node_num_);
      strncpy(ni.user.long_name, n->name_.c_str(), sizeof(ni.user.long_name) - 1);
      strncpy(ni.user.short_name, n->name_short_.c_str(), sizeof(ni.user.short_name) - 1);
      ni.user.hw_model = n->hw_model_;
      ni.user.role = n->role_;
      if (n->public_key_.size() == 32) {
        memcpy(ni.user.public_key.bytes, n->public_key_.data(), 32);
        ni.user.public_key.size = 32;
      }
      ni.snr = n->snr_;
      ni.last_heard = n->last_heard_;
      ni.has_hops_away = n->last_heard_ != 0;
      ni.hops_away = n->hops_away_;
      this->client_push_(q, *fr, 0);
    }
  }

  if (nonce != NONCE_ONLY_NODES) {
    // Channels (always 8)
    for (int i = 0; i < 8; i++) {
      reset(meshtastic_FromRadio_channel_tag);
      auto &ch = fr->channel;
      ch.index = i;
      // settings is always present, also for disabled slots (clients read its fields unconditionally)
      ch.has_settings = true;
      ch.settings.has_module_settings = true;
      if (i < (int) this->channels_.size()) {
        const Channel &c = this->channels_[i];
        ch.role = i == 0 ? meshtastic_Channel_Role_PRIMARY : meshtastic_Channel_Role_SECONDARY;
        ch.has_settings = true;
        strncpy(ch.settings.name, c.get_name().c_str(), sizeof(ch.settings.name) - 1);
        const psk_t &psk = c.get_psk_config();
        ch.settings.psk.size = std::min(psk.size(), sizeof(ch.settings.psk.bytes));
        memcpy(ch.settings.psk.bytes, psk.data(), ch.settings.psk.size);
        ch.settings.uplink_enabled = c.get_uplink();
        ch.settings.downlink_enabled = c.get_downlink();
        ch.settings.has_module_settings = true;
        ch.settings.module_settings.position_precision = c.get_position_precision();
      } else {
        ch.role = meshtastic_Channel_Role_DISABLED;
      }
      this->client_push_(q, *fr, 0);
    }

    // Config: values that reflect the YAML; everything else stays default
    const pb_size_t config_tags[] = {meshtastic_Config_device_tag,  meshtastic_Config_position_tag,
                                     meshtastic_Config_power_tag,   meshtastic_Config_network_tag,
                                     meshtastic_Config_display_tag, meshtastic_Config_lora_tag,
                                     meshtastic_Config_bluetooth_tag, meshtastic_Config_security_tag};
    for (pb_size_t tag : config_tags) {
      reset(meshtastic_FromRadio_config_tag);
      auto &cfg = fr->config;
      cfg.which_payload_variant = tag;
      switch (tag) {
        case meshtastic_Config_device_tag:
          cfg.payload_variant.device.role = this->owner.role;
          cfg.payload_variant.device.node_info_broadcast_secs = this->node_broadcast_interval_ / 1000;
          break;
        case meshtastic_Config_position_tag:
          cfg.payload_variant.position.fixed_position = this->owner_position.has_latitude_i;
          cfg.payload_variant.position.position_broadcast_secs = this->position_broadcast_interval_ / 1000;
          break;
        case meshtastic_Config_network_tag:
          cfg.payload_variant.network.eth_enabled = true;
          break;
        case meshtastic_Config_lora_tag:
          cfg.payload_variant.lora.use_preset = true;
          cfg.payload_variant.lora.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset) this->client_preset_;
          cfg.payload_variant.lora.region = (meshtastic_Config_LoRaConfig_RegionCode) this->client_region_;
          cfg.payload_variant.lora.hop_limit = this->hop_limit_;
          cfg.payload_variant.lora.tx_enabled = true;
          cfg.payload_variant.lora.ignore_mqtt = this->ignore_mqtt_;
          cfg.payload_variant.lora.config_ok_to_mqtt = this->ok_to_mqtt_;
          break;
        case meshtastic_Config_security_tag:
          memcpy(cfg.payload_variant.security.public_key.bytes, this->owner.public_key.bytes, this->owner.public_key.size);
          cfg.payload_variant.security.public_key.size = this->owner.public_key.size;
          break;
        default:
          break;
      }
      this->client_push_(q, *fr, 0);
    }

    // Module config: all defaults except MQTT
    const pb_size_t module_tags[] = {
        meshtastic_ModuleConfig_mqtt_tag,          meshtastic_ModuleConfig_serial_tag,
        meshtastic_ModuleConfig_external_notification_tag, meshtastic_ModuleConfig_store_forward_tag,
        meshtastic_ModuleConfig_range_test_tag,    meshtastic_ModuleConfig_telemetry_tag,
        meshtastic_ModuleConfig_canned_message_tag, meshtastic_ModuleConfig_audio_tag,
        meshtastic_ModuleConfig_remote_hardware_tag, meshtastic_ModuleConfig_neighbor_info_tag,
        meshtastic_ModuleConfig_ambient_lighting_tag, meshtastic_ModuleConfig_detection_sensor_tag,
        meshtastic_ModuleConfig_paxcounter_tag};
    for (pb_size_t tag : module_tags) {
      reset(meshtastic_FromRadio_moduleConfig_tag);
      fr->moduleConfig.which_payload_variant = tag;
      if (tag == meshtastic_ModuleConfig_mqtt_tag) {
        fr->moduleConfig.payload_variant.mqtt.enabled = this->mqtt_gateway_;
        strncpy(fr->moduleConfig.payload_variant.mqtt.root, this->mqtt_root_topic_.c_str(), sizeof(fr->moduleConfig.payload_variant.mqtt.root) - 1);
        fr->moduleConfig.payload_variant.mqtt.encryption_enabled = true;
      }
      this->client_push_(q, *fr, 0);
    }
  }

  if (nonce != NONCE_ONLY_NODES) {
    // Module configs newer than our generated protobufs (statusmessage 14, traffic_management 15,
    // tak 16): send them empty (= defaults), hand-encoded, so clients stop waiting.
    for (uint32_t field : {14u, 15u, 16u}) {
      uint8_t inner[4];
      size_t n = 0;
      uint32_t key = (field << 3) | 2;  // length-delimited
      while (key >= 0x80) {
        inner[n++] = (key & 0x7F) | 0x80;
        key >>= 7;
      }
      inner[n++] = key;
      inner[n++] = 0;  // empty message
      std::vector<uint8_t> buf;
      uint32_t id = ++this->client_fromradio_id_;
      buf.push_back(0x08);  // FromRadio.id
      while (id >= 0x80) {
        buf.push_back((id & 0x7F) | 0x80);
        id >>= 7;
      }
      buf.push_back(id);
      buf.push_back(0x4A);  // FromRadio.moduleConfig (field 9, length-delimited)
      buf.push_back(n);
      buf.insert(buf.end(), inner, inner + n);
      LockGuard lock(this->client_mutex_);
      q.push_back(std::move(buf));
    }
  }

  reset(meshtastic_FromRadio_config_complete_id_tag);
  fr->config_complete_id = nonce;
  this->client_push_(q, *fr, 0);
  ESP_LOGI(TAG, "Config for client queued (%u messages)", (unsigned) q.size());
}

// ---------------------------------------------------------------------------
// HTTP server (own esp_http_server instance: the ESPHome web server has no PUT)
// ---------------------------------------------------------------------------
#ifdef MESHTASTIC_CLIENT_API
static void set_cors_headers(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, PUT, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
}

static esp_err_t handle_options(httpd_req_t *req) {
  set_cors_headers(req);
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t handle_toradio(httpd_req_t *req) {
  auto *self = static_cast<Meshtastic *>(req->user_ctx);
  set_cors_headers(req);
  size_t total = req->content_len;
  if (total == 0 || total > meshtastic_ToRadio_size + 16) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, nullptr, 0);
  }
  std::vector<uint8_t> buf(total);
  size_t got = 0;
  while (got < total) {
    int r = httpd_req_recv(req, reinterpret_cast<char *>(buf.data()) + got, total - got);
    if (r <= 0) {
      if (r == HTTPD_SOCK_ERR_TIMEOUT)
        continue;
      return ESP_FAIL;
    }
    got += r;
  }
  self->client_api_put_(buf.data(), buf.size());
  httpd_resp_set_type(req, "application/x-protobuf");
  return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t handle_fromradio(httpd_req_t *req) {
  auto *self = static_cast<Meshtastic *>(req->user_ctx);
  set_cors_headers(req);
  httpd_resp_set_type(req, "application/x-protobuf");
  std::vector<uint8_t> out;
  if (!self->client_api_get_(out))
    return httpd_resp_send(req, nullptr, 0);
  return httpd_resp_send(req, reinterpret_cast<const char *>(out.data()), out.size());
}
#endif

void Meshtastic::client_api_setup_() {
  if (this->client_api_port_ == 0)
    return;
#ifdef MESHTASTIC_CLIENT_API
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = this->client_api_port_;
  cfg.ctrl_port = 32768 + 17;  // must differ from the ESPHome web server's control port
  cfg.max_uri_handlers = 6;
  cfg.lru_purge_enable = true;
  cfg.stack_size = 6144;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Client API: cannot start HTTP server on port %u", this->client_api_port_);
    return;
  }
  const httpd_uri_t uris[] = {
      {.uri = "/api/v1/toradio", .method = HTTP_PUT, .handler = handle_toradio, .user_ctx = this},
      {.uri = "/api/v1/fromradio", .method = HTTP_GET, .handler = handle_fromradio, .user_ctx = this},
      {.uri = "/api/v1/toradio", .method = HTTP_OPTIONS, .handler = handle_options, .user_ctx = this},
      {.uri = "/api/v1/fromradio", .method = HTTP_OPTIONS, .handler = handle_options, .user_ctx = this},
  };
  for (const auto &u : uris)
    httpd_register_uri_handler(server, &u);
  this->client_httpd_ = server;
  ESP_LOGI(TAG, "Meshtastic client API on port %u (/api/v1/toradio, /api/v1/fromradio)", this->client_api_port_);
#endif
}

}  // namespace meshtastic
}  // namespace esphome
