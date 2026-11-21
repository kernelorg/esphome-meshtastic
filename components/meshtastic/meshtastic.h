#pragma once

#include "esphome/core/defines.h"
#ifdef USE_NETWORK
#include "esphome/components/network/ip_address.h"
#if defined(USE_SOCKET_IMPL_BSD_SOCKETS) || defined(USE_SOCKET_IMPL_LWIP_SOCKETS)
#include "esphome/components/socket/socket.h"
#endif
#endif
#include "esphome/core/component.h"
#include "esphome/core/automation.h"

#if __has_include("esphome/components/sx126x/sx126x.h")
#include "esphome/components/sx126x/sx126x.h"
#define USE_SX126X 1
#endif

#if __has_include("esphome/components/sx127x/sx127x.h")
#include "esphome/components/sx127x/sx127x.h"
#define USE_SX127X 1
#endif

#include "mesh.pb.h"
#include "mqtt.pb.h"
#include "esphome/core/preferences.h"
#include "esphome/core/helpers.h"
#include <deque>
#include <set>
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include <unordered_set>
#include <unordered_map>


#define NODENUM_BROADCAST UINT32_MAX
//#define UDP_MULTICAST_IP "224.0.0.69"
#define UDP_MULTICAST_DEFAUL_PORT 4403 // Default port for UDP multicast is same as TCP api server


namespace esphome {
namespace meshtastic {

typedef uint32_t NodeNum;
typedef uint32_t PacketId;

typedef struct {
    NodeNum to, from;
    PacketId id;
    uint8_t flags;
    uint8_t channel;
    uint8_t next_hop;
    uint8_t relay_node;
} PacketHeader;

#define MAX_LORA_PAYLOAD_LEN 255

typedef struct {
    PacketHeader header;
    uint8_t payload[MAX_LORA_PAYLOAD_LEN + 1 - sizeof(PacketHeader)] __attribute__((__aligned__));
} RadioBuffer;

using psk_t = std::vector<uint8_t>;

// Rebroadcast behaviour, mirrors Meshtastic device roles
enum RelayRole : uint8_t {
  RELAY_ROLE_CLIENT_MUTE = 0,  // never rebroadcast
  RELAY_ROLE_CLIENT = 1,       // rebroadcast, cancel if another node relays first
  RELAY_ROLE_ROUTER = 2,       // rebroadcast with priority, never cancel
};

struct PendingRelay {
  uint32_t due;
  NodeNum from;
  PacketId id;
  std::vector<uint8_t> raw;
};

//    uint8_t default_secret[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

struct Node {
    std::string name_;
    std::string name_short_;
    NodeNum node_num_;
    std::vector<uint8_t> private_key_;
    std::vector<uint8_t> public_key_;
    bool learned_ = false;  // public key learned from NodeInfo (not from YAML)
    // NodeDB data for the client API
    uint32_t last_heard_ = 0;
    float snr_ = 0;
    uint8_t hops_away_ = 0;
    meshtastic_HardwareModel hw_model_ = meshtastic_HardwareModel_UNSET;
    meshtastic_Config_DeviceConfig_Role role_ = meshtastic_Config_DeviceConfig_Role_CLIENT;
};

// Learned public keys, persisted in flash
#define MAX_LEARNED_NODES 16
struct LearnedNodeRecord {
  uint32_t num;
  uint8_t key[32];
};
struct LearnedNodesStore {
  uint32_t next;  // ring index for replacement
  LearnedNodeRecord nodes[MAX_LEARNED_NODES];
};

class Channel {
 private:
  std::string name_;
  psk_t psk_;
  psk_t psk_config_;  // as configured (1-byte shorthand kept), reported to clients
  uint8_t hash_;
  uint8_t position_precision_ = 32;  // bits kept in lat/lon; 0 = never send position on this channel
  bool uplink_ = false;              // MQTT gateway: publish packets of this channel
  bool downlink_ = false;            // MQTT gateway: transmit packets of this channel received from MQTT
 public:
  void set_position_precision(uint8_t p) { this->position_precision_ = p; };
  uint8_t get_position_precision() const { return this->position_precision_; };
  void set_uplink(bool v) { this->uplink_ = v; };
  bool get_uplink() const { return this->uplink_; };
  void set_downlink(bool v) { this->downlink_ = v; };
  bool get_downlink() const { return this->downlink_; };
  void set_name(const std::string &name) { this->name_ = name; };
  std::string get_name() const { return this->name_; };
  void set_psk(const psk_t &psk);
  psk_t get_psk() const { return this->psk_; };
  const psk_t &get_psk_config() const { return this->psk_config_; };
  void generate_hash();
  void set_hash(uint8_t hash) { this->hash_ = hash; };
  uint8_t get_hash() const { return this->hash_; };
};

#define MAX_NUM_NODES 100
#define FLOOD_EXPIRE_TIME (10 * 60 * 1000L)

struct PacketRecord {
    NodeNum sender;
    PacketId id;
    uint32_t rxTimeMsec;
    bool operator==(const PacketRecord &p) const { return sender == p.sender && id == p.id; }
};

class PacketRecordHashFunction
{
  public:
    size_t operator()(const PacketRecord &p) const { return (std::hash<NodeNum>()(p.sender)) ^ (std::hash<PacketId>()(p.id)); }
};

class PacketHistory
{
  private:
    std::unordered_set<PacketRecord, PacketRecordHashFunction> recentPackets;
    void clearExpiredRecentPackets();
  public:
    PacketHistory(){ recentPackets.reserve(MAX_NUM_NODES); }
    bool wasSeenRecently(const meshtastic_MeshPacket &p, bool withUpdate = true);
};


class Meshtastic : public Component
#ifdef USE_SX126X
  , sx126x::SX126xListener
#endif
#ifdef USE_SX127X
  , sx127x::SX127xListener
#endif
{
 private:
#ifdef USE_SX126X
  sx126x::SX126x* sx126x_ = nullptr;
#endif
#ifdef USE_SX127X
  sx127x::SX127x* sx127x_ = nullptr;
#endif
  NodeNum node_num_;
  meshtastic_User owner;
  Node *owner_node_ = nullptr;
  meshtastic_Position owner_position = meshtastic_Position_init_default;

  uint32_t node_broadcast_interval_ = (3*60*60*1000);
  uint32_t position_broadcast_interval_ = (3*60*60*1000);

  std::vector<Channel> channels_;
  std::vector<std::unique_ptr<Node>> nodes_;

#if !defined(MESHTASTIC_EXCLUDE_PKI)
  bool init_shared_key(uint8_t* shared_key, uint8_t* pubKey);
  bool encrypt_curve25519(uint32_t toNode, uint32_t fromNode, uint8_t* remotePublic, uint64_t packetNum, size_t numBytes, const uint8_t* bytes, uint8_t* bytesOut);
  bool decrypt_curve25519(uint32_t fromNode, uint8_t* remotePublic, uint64_t packetNum, size_t numBytes, const uint8_t* bytes, uint8_t* bytesOut);
#endif
  int rx_count_lora = 0;
  int rx_count_udp = 0;
  int rx_bad_udp = 0;
  int rx_bad_count = 0;
 protected:
  CallbackManager<void(uint32_t from, uint32_t to, int32_t port, std::vector<uint8_t> &data)> packet_listeners_{};

  bool transmit_radio_packet(meshtastic_MeshPacket *mp);
  bool transmit_udp_packet(meshtastic_MeshPacket *mp, int ntries = 0);
  void on_udp_packet(const std::vector<uint8_t> &packet, sockaddr_in *addr);
  void handle_received_packet(meshtastic_MeshPacket &mp);
  Channel* get_channel_by_hash(uint8_t hash);
  Channel* get_channel_by_name(const std::string &name);
  PacketId generatePacketId();
  Node* get_node_by_num(const NodeNum num);

  uint8_t hop_limit_ = 7;
  uint8_t relay_role_ = RELAY_ROLE_CLIENT_MUTE;
  uint32_t slot_time_ms_ = 9;
  uint32_t relay_tx_count_ = 0;
  uint32_t relay_cancel_count_ = 0;
  std::vector<PendingRelay> pending_relays_;
  void process_rx_(const std::vector<uint8_t> &packet, meshtastic_MeshPacket &mp, float snr);
  // Learned node keys (TOFU, like Meshtastic NodeDB)
  ESPPreferenceObject learned_pref_;
  LearnedNodesStore learned_store_{};
  std::unordered_map<NodeNum, uint32_t> nodeinfo_requested_;
  void load_learned_nodes_();
  void learn_node_(NodeNum num, const meshtastic_User &user);
  void request_node_info_(NodeNum num);
  // MQTT gateway
  std::string mqtt_root_topic_ = "msh/EU_868";
  bool mqtt_gateway_ = false;
  void mqtt_setup_();
  void mqtt_uplink_(meshtastic_MeshPacket &mp, const Channel &ch);
  void on_mqtt_message_(const std::string &topic, const std::string &payload);
  // Received text messages: log, HA entities, on_text_message
  CallbackManager<void(uint32_t, std::string, std::string, std::string)> text_listeners_{};
  void on_text_received_(const meshtastic_MeshPacket &mp, const std::string &channel_name, const std::string &text);
  std::string node_label_(NodeNum num);
  void update_node_seen_(const meshtastic_MeshPacket &mp);
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *last_message_sensor_{nullptr};
  text_sensor::TextSensor *last_sender_sensor_{nullptr};
  text_sensor::TextSensor *last_channel_sensor_{nullptr};
#endif
  std::string ha_destination_;
  // Meshtastic client API over HTTP (/api/v1/toradio, /api/v1/fromradio)
  uint16_t client_api_port_ = 0;
  uint8_t client_region_ = 3;   // EU_868
  uint8_t client_preset_ = 0;   // LONG_FAST
  void *client_httpd_ = nullptr;
  Mutex client_mutex_;
  // One session per connected client: its own config answer and packet queue
  struct ClientSession {
    std::deque<std::vector<uint8_t>> config_q;  // FromRadio, answer to want_config
    std::deque<std::vector<uint8_t>> packet_q;  // FromRadio packets
    uint32_t last_seen = 0;
  };
  ClientSession http_session_;                         // HTTP API (guarded by client_mutex_)
  std::deque<std::vector<uint8_t>> client_toradio_q_;  // ToRadio from HTTP, handled in loop()
  // TCP API (Meshtastic stream protocol, port 4403), served from loop()
  struct TcpClient {
    int fd = -1;
    ClientSession session;
    std::vector<uint8_t> rx;
    std::vector<uint8_t> tx;
  };
  uint16_t tcp_api_port_ = 0;
  int tcp_listen_fd_ = -1;
  std::vector<std::unique_ptr<TcpClient>> tcp_clients_;
  void tcp_api_setup_();
  void tcp_api_loop_();
  void tcp_client_service_(TcpClient &c);
  std::set<PacketId> client_pending_acks_;
  uint32_t client_fromradio_id_ = 0;
  void client_api_setup_();
  void client_api_loop_();
  void client_handle_toradio_(const std::vector<uint8_t> &buf, ClientSession &session);
  void client_send_config_(uint32_t nonce, ClientSession &session);
  bool client_encode_(meshtastic_FromRadio &fr, std::vector<uint8_t> &out);
  void client_queue_(std::deque<std::vector<uint8_t>> &q, std::vector<uint8_t> &&buf, size_t max);
  void client_push_(std::deque<std::vector<uint8_t>> &q, meshtastic_FromRadio &fr, size_t max);
  void client_broadcast_(meshtastic_FromRadio &fr);
  bool client_any_active_();
  void client_push_packet_(const meshtastic_MeshPacket &mp, const meshtastic_Data &decoded);
  void client_push_routing_ack_(PacketId id);
  int channel_index_by_hash_(uint8_t hash);
 public:
  // called from the HTTP server task
  void client_api_put_(const uint8_t *data, size_t len);
  bool client_api_get_(std::vector<uint8_t> &out);
 private:
  void schedule_relay_(const std::vector<uint8_t> &packet, const meshtastic_MeshPacket &mp, float snr);
  void cancel_relay_(NodeNum from, PacketId id);
  void process_relays_();
  bool transmit_raw_(const std::vector<uint8_t> &packet);
  bool ignore_mqtt_ = true;
  bool ok_to_mqtt_ = false;
#if defined(USE_SOCKET_IMPL_BSD_SOCKETS) || defined(USE_SOCKET_IMPL_LWIP_SOCKETS)
  std::unique_ptr<socket::Socket> broadcast_socket_ = nullptr;
  std::unique_ptr<socket::Socket> listen_socket_ = nullptr;
  std::vector<struct sockaddr> sockaddrs_{};
  std::vector<std::string> addresses_{};
  uint16_t listen_port_ = UDP_MULTICAST_DEFAUL_PORT;
  uint16_t broadcast_port_ = UDP_MULTICAST_DEFAUL_PORT;

  bool use_udp_ = false;
  bool bridge_mode_ = false;
#endif
  optional<network::IPAddress> multicast_address_{};
  PacketHistory packet_history_;

  uint8_t get_hop_limit4response(uint8_t hop_start, uint8_t hop_limit);
  void send_ack(NodeNum to, PacketId id_from, uint8_t channel, meshtastic_Routing_Error err);
  void handle_traceroute(meshtastic_MeshPacket *p, PacketId req_id, meshtastic_RouteDiscovery *route_discovery_msg);

 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; };

#ifdef USE_SX126X
  void set_lora(sx126x::SX126x* sx);
#endif
#ifdef USE_SX127X
  void set_lora(sx127x::SX127x* sx);
#endif
  void add_channel(const std::string &name, const std::vector<uint8_t> &psk, uint8_t position_precision = 32,
                   bool uplink = false, bool downlink = false) {
    this->channels_.push_back(Channel());
    Channel &c = this->channels_.back();
    c.set_name(name);
    c.set_psk(psk);
    c.generate_hash();
    c.set_position_precision(position_precision);
    c.set_uplink(uplink);
    c.set_downlink(downlink);
  };
  void set_mqtt_root_topic(const std::string &t) { this->mqtt_root_topic_ = t; };
  void set_client_api(uint16_t port, uint16_t tcp_port, uint8_t region, uint8_t preset) {
    this->client_api_port_ = port;
    this->tcp_api_port_ = tcp_port;
    this->client_region_ = region;
    this->client_preset_ = preset;
  };
  void add_text_listener(std::function<void(uint32_t, std::string, std::string, std::string)> &&f) {
    this->text_listeners_.add(std::move(f));
  };
#ifdef USE_TEXT_SENSOR
  void set_last_message_sensor(text_sensor::TextSensor *s) { this->last_message_sensor_ = s; };
  void set_last_sender_sensor(text_sensor::TextSensor *s) { this->last_sender_sensor_ = s; };
  void set_last_channel_sensor(text_sensor::TextSensor *s) { this->last_channel_sensor_ = s; };
#endif
  // HA send entities: destination is a channel name or "!xxxxxxxx" (direct message)
  void set_ha_destination(const std::string &d) { this->ha_destination_ = d; };
  bool send_text_to_destination(const std::string &destination, const std::string &text);
  bool send_text_from_ha(const std::string &text) { return this->send_text_to_destination(this->ha_destination_, text); };
  void set_mqtt_gateway(bool v) { this->mqtt_gateway_ = v; };
  void add_node(NodeNum node_num, const std::string &name, const std::string &short_name, const std::vector<uint8_t> &public_key, const std::vector<uint8_t> &private_key) {
      auto node = std::make_unique<Node>();
      node->name_ = name;
      node->name_short_ = short_name;
      node->node_num_ = node_num;
      node->private_key_ = private_key;
      node->public_key_ = public_key;
      if(node_num == 0) {
          std::strcpy(owner.long_name, name.c_str());
          std::strcpy(owner.short_name, short_name.c_str());
          if(public_key.size() == 32 && private_key.size() == 32) {
            std::memcpy(owner.public_key.bytes, public_key.data(), public_key.size());
            owner.public_key.size = public_key.size();
          }
          this->owner_node_ = node.get();
      }
      this->nodes_.push_back(std::move(node));
  };
  void set_hw_model(int hw_model) {
      owner.hw_model = static_cast<meshtastic_HardwareModel>(hw_model);
  };
  void set_hop_limit(uint8_t hop_limit) { this->hop_limit_ = hop_limit; };
  void set_ignore_mqtt(bool ignore_mqtt) { this->ignore_mqtt_ = ignore_mqtt; };
  void set_role(uint8_t role) { this->relay_role_ = role; };
  void set_slot_time(uint32_t ms) { this->slot_time_ms_ = ms; };
  void set_ok_to_mqtt(bool ok_to_mqtt) { this->ok_to_mqtt_ = ok_to_mqtt; };
  void set_node_broadcast_interval(uint32_t broadcast_interval) { this->node_broadcast_interval_ = broadcast_interval; };
  void set_position_broadcast_interval(uint32_t broadcast_interval) { this->position_broadcast_interval_ = broadcast_interval; };
  void set_position(float latitude, float longitude, float altitude) {
      this->owner_position.has_latitude_i = true;
      this->owner_position.latitude_i = static_cast<int32_t>(latitude * 1e7);
      this->owner_position.has_longitude_i = true;
      this->owner_position.longitude_i = static_cast<int32_t>(longitude * 1e7);
      this->owner_position.has_altitude = true;
      this->owner_position.altitude = altitude;
      this->owner_position.location_source = meshtastic_Position_LocSource_LOC_MANUAL;
      this->owner_position.altitude_source = meshtastic_Position_AltSource_ALT_MANUAL;
  };

  void set_use_udp(bool use_udp) { this->use_udp_ = use_udp; };
  void set_multicast_address(const char *multicast_addr) { this->multicast_address_ = network::IPAddress(multicast_addr); }
  void add_address(const char *addr) { this->addresses_.emplace_back(addr); }
  void set_bridge_mode(bool bridge_mode) { this->bridge_mode_ = bridge_mode; }
  void add_listener(std::function<void(uint32_t from, uint32_t to, int32_t port, std::vector<uint8_t> &data)> &&listener) { this->packet_listeners_.add(std::move(listener)); }
  //packet transport
  size_t get_max_packet_size() {return MAX_LORA_PAYLOAD_LEN + 1 - sizeof(PacketHeader); };
  bool transmit_packet(const std::vector<uint8_t> &packet, uint8_t hop_limit);

  bool transmit_packet_encrypt(meshtastic_MeshPacket &mp, bool want_response = false);
  bool send_text_message(NodeNum to, const std::string &channel_name, const std::string &msg);
  void send_our_node_info(NodeNum to, uint8_t channel, meshtastic_MeshPacket_Priority priority);
  void send_our_node_position(NodeNum to, uint8_t channel, meshtastic_MeshPacket_Priority priority);

  //lora callbacks
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr);

  //protobuf helpers
  static bool pb_decode_from_bytes(const uint8_t* srcbuf, size_t srcbufsize, const pb_msgdesc_t* fields, void* dest_struct);
  static size_t pb_encode_to_bytes(uint8_t* destbuf, size_t destbufsize, const pb_msgdesc_t* fields, const void* src_struct);
};


}  // namespace meshtastic
}  // namespace esphome
