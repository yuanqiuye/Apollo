#pragma once

#include <memory>

#include "src/platform/common.h"

namespace platf {
  class steam_controller_winuhid_t {
  public:
    steam_controller_winuhid_t();
    ~steam_controller_winuhid_t();

    int init();
    bool is_available() const;
    bool owns_gamepad(int nr) const;

    int alloc_gamepad(const gamepad_id_t &id, const gamepad_arrival_t &metadata, feedback_queue_t feedback_queue);
    void free_gamepad(int nr);
    void update(int nr, const gamepad_state_t &gamepad_state);
    void touch(const gamepad_touch_t &touch);
    void motion(const gamepad_motion_t &motion);
    void battery(const gamepad_battery_t &battery);
    void raw_hid(const gamepad_raw_hid_report_t &report);

  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl;
  };
}  // namespace platf
