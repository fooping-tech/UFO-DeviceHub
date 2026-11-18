// Menu on the M5Dial (hardware independent, host-testable).
//
// Outside the settings mode, turning the dial opens the menu and moves through
// the items; a short press runs the shown item. The menu opens on CLOSE, so a
// knock on the dial plus a press does nothing. It closes by itself after
// timeout_ms without input.
//
//   CLOSE         leave the menu
//   DIAL RESET    restart this dial (main.cpp)
//   POWER OFF     switch this dial off (main.cpp)
//   RELEASE       run the release sequence now (DialController::localRelease)
//   GAME RESTART  ask the game to restart (DIAL_REQUEST event)
//   GAME END      ask the game to end (DIAL_REQUEST event)
//
// After a press the result ("SENT", "NO HUB", ...) stays on the screen for a
// moment (showResult) and then the menu closes.
#pragma once

#include <stdint.h>

namespace ufo {

enum class MenuItem : uint8_t { Close, Reset, PowerOff, Release, GameRestart, GameEnd, Count };

inline const char* menuItemLabel(MenuItem i) {
  switch (i) {
    case MenuItem::Close: return "CLOSE";
    case MenuItem::Reset: return "DIAL RESET";
    case MenuItem::PowerOff: return "POWER OFF";
    case MenuItem::Release: return "RELEASE";
    case MenuItem::GameRestart: return "GAME RESTART";
    case MenuItem::GameEnd: return "GAME END";
    default: return "?";
  }
}

class DialMenu {
 public:
  explicit DialMenu(uint32_t timeout_ms = 10000) : timeout_ms_(timeout_ms) {}

  bool open() const { return open_; }
  MenuItem item() const { return item_; }
  // Result of the last press while it is shown (nullptr: none).
  const char* result() const { return result_; }
  bool resultOk() const { return result_ok_; }

  // The dial moved by `steps` detents (+ clockwise). Opens the menu on CLOSE first.
  void turn(int steps, uint32_t now) {
    if (steps == 0 || result_) return;  // a result is on the screen: wait for it to go
    if (!open_) {
      open_ = true;
      item_ = MenuItem::Close;
    }
    const int n = static_cast<int>(MenuItem::Count);
    int i = (static_cast<int>(item_) + steps) % n;
    if (i < 0) i += n;
    item_ = static_cast<MenuItem>(i);
    last_input_ = now;
  }

  // Short press. Returns the item to run; Count when there is nothing to run
  // (menu closed, a result is shown, or CLOSE - which closes the menu here).
  MenuItem press(uint32_t now) {
    if (!open_ || result_) return MenuItem::Count;
    last_input_ = now;
    if (item_ == MenuItem::Close) {
      close();
      return MenuItem::Count;
    }
    return item_;
  }

  // Shows the outcome of the action for show_ms, then the menu closes.
  void showResult(const char* text, bool ok, uint32_t now, uint32_t show_ms = 1500) {
    if (!open_) return;
    result_ = text;
    result_ok_ = ok;
    close_at_ = now + show_ms;
  }

  void close() {
    open_ = false;
    result_ = nullptr;
    item_ = MenuItem::Close;
  }

  // Call every loop: closes after the result time or the input timeout.
  void tick(uint32_t now) {
    if (!open_) return;
    if (result_) {
      if (static_cast<int32_t>(now - close_at_) >= 0) close();
    } else if (timeout_ms_ > 0 && now - last_input_ >= timeout_ms_) {
      close();
    }
  }

 private:
  uint32_t timeout_ms_;
  bool open_ = false;
  MenuItem item_ = MenuItem::Close;
  const char* result_ = nullptr;
  bool result_ok_ = false;
  uint32_t last_input_ = 0;
  uint32_t close_at_ = 0;
};

}  // namespace ufo
