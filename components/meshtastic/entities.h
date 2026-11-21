#pragma once
// Home Assistant entities: send text (text + select) and on_text_message trigger
#include "esphome/core/automation.h"
#include "meshtastic.h"
#ifdef USE_TEXT
#include "esphome/components/text/text.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif

namespace esphome {
namespace meshtastic {

class TextMessageTrigger : public Trigger<uint32_t, std::string, std::string, std::string> {
 public:
  explicit TextMessageTrigger(Meshtastic *parent) {
    parent->add_text_listener([this](uint32_t from, std::string sender, std::string channel, std::string text) {
      this->trigger(from, sender, channel, text);
    });
  }
};

#ifdef USE_TEXT
class MeshtasticText : public text::Text, public Parented<Meshtastic> {
 protected:
  void control(const std::string &value) override {
    if (this->parent_->send_text_from_ha(value))
      this->publish_state(value);
  }
};
#endif

#ifdef USE_SELECT
class MeshtasticSelect : public select::Select, public Component, public Parented<Meshtastic> {
 public:
  void setup() override {
    // first option (primary channel) is the default destination
    this->publish_state((size_t) 0);
    this->parent_->set_ha_destination(std::string(this->option_at(0)));
  }

 protected:
  void control(size_t index) override {
    this->publish_state(index);
    this->parent_->set_ha_destination(std::string(this->option_at(index)));
  }
};
#endif

}  // namespace meshtastic
}  // namespace esphome
