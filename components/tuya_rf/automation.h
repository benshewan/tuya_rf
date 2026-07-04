#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "tuya_rf.h"

namespace esphome {
namespace tuya_rf {

template<typename... Ts> class TurnOffReceiverAction : public Action<Ts...> {
 public:
  TurnOffReceiverAction(TuyaRfComponent *tuya_rf) : tuya_rf_(tuya_rf) {}

  void play(Ts... x) override { this->tuya_rf_->turn_off_receiver(); this->play_next_(x...);}

 protected:
  TuyaRfComponent *tuya_rf_;
};

template<typename... Ts> class TurnOnReceiverAction : public Action<Ts...> {
 public:
  TurnOnReceiverAction(TuyaRfComponent *tuya_rf) : tuya_rf_(tuya_rf) {}

  void play(Ts... x) override { this->tuya_rf_->turn_on_receiver(); this->play_next_(x...);}

 protected:
  TuyaRfComponent *tuya_rf_;
};

template<typename... Ts> class SetFrequencyAction : public Action<Ts...> {
 public:
  SetFrequencyAction(TuyaRfComponent *tuya_rf) : tuya_rf_(tuya_rf) {}

  void play(Ts... x) override {
    this->tuya_rf_->set_frequency(this->frequency_.value(x...));
    this->play_next_(x...);
  }

  template<typename T> void set_frequency(T frequency) { this->frequency_ = frequency; }

 protected:
  TuyaRfComponent *tuya_rf_;
  TemplatableValue<uint32_t, Ts...> frequency_;
};

template<typename... Ts> class ReplayLastCaptureAction : public Action<Ts...> {
 public:
  ReplayLastCaptureAction(TuyaRfComponent *tuya_rf) : tuya_rf_(tuya_rf) {}

  void play(Ts... x) override {
    this->tuya_rf_->replay_last_capture(this->repeat_.value(x...), this->wait_.value(x...));
    this->play_next_(x...);
  }

  template<typename T> void set_repeat(T repeat) { this->repeat_ = repeat; }
  template<typename T> void set_wait(T wait) { this->wait_ = wait; }

 protected:
  TuyaRfComponent *tuya_rf_;
  TemplatableValue<uint32_t, Ts...> repeat_{1};
  TemplatableValue<uint32_t, Ts...> wait_{0};
};


}  // namespace tuya_rf
}  // namespace esphome
