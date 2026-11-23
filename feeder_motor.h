#pragma once
// Feed controller for the PETKIT Element Solo (D4).
//
// Pure logic, no ESPHome or GPIO dependency: the YAML ticks it every 10 ms with the time, the
// motor current and two sensor levels, forwards sensor edges, and applies the Drive it returns.
// Behaviour and constants follow the stock firmware (docs/motion.md); the few choices made for
// ESPHome are marked "ESPHome choice".
#include <cstdint>

namespace feeder {

// H-bridge commands (GPIO19 enable, GPIO17, GPIO18), stock primitives:
//   FORWARD 1/0/1, REVERSE 1/1/0, BRAKE 1/1/1, SLEEP 0/0/0.
enum class Drive : uint8_t { SLEEP, FORWARD, REVERSE, BRAKE };

enum class Result : uint8_t {
  NONE,         // nothing new
  OK,           // feed completed
  GRATE_ERROR,  // stock GRAT_ERR: timeout while the motor was not straining
  BLOCKED,      // stock BLOCK_FOOD: timeout under load, or too many un-jam attempts
  CANCELLED,    // cancelled (button click, Home Assistant, or button released in hold mode)
};

struct Config {
  uint32_t loop_timeout_ms = 10000;          // run timeout = (loops + 1) * 10 s + 5 s
  uint32_t timeout_margin_ms = 5000;
  uint32_t current_blank_ms = 500;           // over-current ignored for the first 500 ms
  float overcurrent_mv = 799.0f;             // current > 799 ...
  uint32_t overcurrent_hold_ms = 150;        // ... for more than 150 ms
  float jam_judge_mv = 700.0f;               // on timeout: feed max < 700 -> GRATE_ERROR
  uint32_t pause_ms = 500;                   // vTaskDelay(50) at the stock 100 Hz tick
  uint32_t double_reverse_window_ms = 3000;  // 2 loops if the previous attempt was < 3 s ago
  uint8_t max_attempts = 5;                  // then BLOCKED
  uint32_t grate_relatch_ms = 300;           // grate re-armed 300 ms after a counted loop
  uint32_t idle_grate_stable_ms = 2000;      // idle grate state needs 2 s of stable level
  uint32_t cancel_pause_ms = 100;            // vTaskDelay(10) before checking the outlet
  uint8_t hold_chunk_loops = 5;              // button feed: 5 loops per run, repeated while held
  uint16_t outlet_min_uncovered_ticks = 3;   // last loop uncovered < 3 ticks -> outlet blocked
  uint32_t brake_ms = 200;                   // ESPHome choice: brake time before driver sleep
};

class MotorController {
 public:
  MotorController() = default;
  explicit MotorController(const Config &cfg) : cfg_(cfg) {}

  bool busy() const { return phase_ != Phase::IDLE; }
  bool needs_current() const { return phase_ == Phase::RUNNING; }
  bool hold_mode() const { return hold_; }
  uint8_t target() const { return target_; }
  uint8_t loops_done() const { return done_; }
  uint8_t valid_loops() const { return valid_; }  // loops with food seen at the outlet
  uint8_t attempts() const { return attempts_; }
  float max_current_mv() const { return feed_max_mv_; }
  bool outlet_blocked() const { return outlet_blocked_; }  // stock BLOCK_DOOR judge
  bool grate_closed() const { return closed_; }

  // Timed / app feed of `scoops` loops. One extra loop when the outlet is open at start.
  bool start(uint8_t scoops, uint32_t now) {
    if (busy() || scoops == 0) return false;
    reset_feed_();
    hold_ = false;
    target_ = uint8_t(scoops + (closed_ ? 0 : 1));
    begin_segment_(true, target_, now);
    return true;
  }

  // Button feed: runs while the button is held, 5 loops at a time.
  bool start_hold(uint32_t now) {
    if (busy()) return false;
    reset_feed_();
    hold_ = true;
    held_ = true;
    target_ = cfg_.hold_chunk_loops;
    begin_segment_(true, cfg_.hold_chunk_loops, now);
    return true;
  }

  // Button released: ends a hold feed (stock: release seen -> cancel state).
  void release(uint32_t now) {
    held_ = false;
    if (hold_ && busy()) cancel(now);
  }

  // Stock cancel: stop, wait 100 ms, then one more loop if the outlet is not closed.
  void cancel(uint32_t now) {
    if (!busy() || cancelling_ || phase_ == Phase::STOPPING) return;
    cancelling_ = true;
    phase_ = Phase::CANCEL_WAIT;
    phase_start_ = now;
  }

  // GPIO27 falling edge (grate closes the outlet). Counts once per closure.
  void on_grate_edge(uint32_t now) {
    if (phase_ != Phase::RUNNING) return;
    if (closed_) return;
    closed_ = true;
    last_loop_at_ = now;
    seg_done_++;
    if (forward_) {
      done_++;
      if (loop_pulses_ > 0) valid_++;
      last_uncovered_ = loop_uncovered_;
      loop_pulses_ = 0;
      loop_uncovered_ = 0;
    }
  }

  // GPIO14 rising edge (something crosses the outlet beam).
  void on_outlet_rise() {
    if (phase_ == Phase::RUNNING && forward_ && loop_pulses_ < 0xffff) loop_pulses_++;
  }

  // Call every 10 ms. grate_low: GPIO27 low. outlet_covered: GPIO14 high.
  Drive tick(uint32_t now, float current_mv, bool grate_low, bool outlet_covered) {
    if (phase_ != Phase::RUNNING) track_idle_grate_(now, grate_low);
    switch (phase_) {
      case Phase::IDLE:
        return Drive::SLEEP;
      case Phase::PAUSE:
        if (now - phase_start_ >= cfg_.pause_ms) begin_segment_(next_forward_, next_loops_, now);
        return phase_ == Phase::RUNNING ? run_drive_() : Drive::BRAKE;
      case Phase::CANCEL_WAIT:
        if (now - phase_start_ >= cfg_.cancel_pause_ms) {
          if (closed_) {
            finish_(cancel_result_(), now);
          } else {
            begin_segment_(true, 1, now);  // close the outlet first
            return run_drive_();
          }
        }
        return Drive::BRAKE;
      case Phase::STOPPING:
        if (now - phase_start_ >= cfg_.brake_ms) {
          phase_ = Phase::IDLE;
          return Drive::SLEEP;
        }
        return Drive::BRAKE;
      case Phase::RUNNING:
        return tick_running_(now, current_mv, grate_low, outlet_covered);
    }
    return Drive::SLEEP;
  }

  // Returns the result of the last feed once, then NONE.
  Result take_result() {
    Result r = pending_;
    pending_ = Result::NONE;
    return r;
  }

 private:
  enum class Phase : uint8_t { IDLE, RUNNING, PAUSE, CANCEL_WAIT, STOPPING };

  Drive run_drive_() const { return forward_ ? Drive::FORWARD : Drive::REVERSE; }

  void reset_feed_() {
    done_ = 0;
    valid_ = 0;
    attempts_ = 0;
    has_attempted_ = false;
    feed_max_mv_ = 0.0f;
    pending_ = Result::NONE;
    cancelling_ = false;
    outlet_blocked_ = false;
    loop_pulses_ = 0;
    loop_uncovered_ = 0;
    last_uncovered_ = 0;
  }

  void track_idle_grate_(uint32_t now, bool grate_low) {
    if (grate_low != idle_level_) {
      idle_level_ = grate_low;
      idle_since_ = now;
    } else if (now - idle_since_ >= cfg_.idle_grate_stable_ms) {
      closed_ = grate_low;
    }
  }

  void begin_segment_(bool forward, uint8_t loops, uint32_t now) {
    forward_ = forward;
    seg_loops_ = loops;
    seg_done_ = 0;
    seg_start_ = now;
    seg_timeout_ = (uint32_t(loops) + 1) * cfg_.loop_timeout_ms + cfg_.timeout_margin_ms;
    over_ = false;
    phase_ = Phase::RUNNING;
  }

  void pause_then_(bool forward, uint8_t loops, uint32_t now) {
    next_forward_ = forward;
    next_loops_ = loops;
    phase_ = Phase::PAUSE;
    phase_start_ = now;
  }

  // A released button ends a hold feed normally; any other cancel is reported as such.
  Result cancel_result_() const { return hold_ ? Result::OK : Result::CANCELLED; }

  // Stock end-of-feed judge: loops with no food are not counted, and an outlet that stayed
  // covered through the last loop raises BLOCK_DOOR.
  void judge_outlet_() {
    uint16_t uncovered = done_ > 0 ? last_uncovered_ : loop_uncovered_;
    outlet_blocked_ = uncovered < cfg_.outlet_min_uncovered_ticks;
    if (outlet_blocked_) valid_ = done_;
  }

  void finish_(Result r, uint32_t now) {
    judge_outlet_();
    pending_ = r;
    phase_ = Phase::STOPPING;
    phase_start_ = now;
  }

  // A run ended because its loops were all counted.
  void after_run_(uint32_t now) {
    if (cancelling_) {
      finish_(cancel_result_(), now);
    } else if (hold_) {
      if (held_) {
        target_ = uint8_t(target_ + cfg_.hold_chunk_loops);
        begin_segment_(true, cfg_.hold_chunk_loops, now);  // stock: run again while held
      } else {
        finish_(Result::OK, now);
      }
    } else if (done_ < target_) {
      begin_segment_(true, uint8_t(target_ - done_), now);  // stock "feed replenish"
    } else {
      finish_(Result::OK, now);
    }
  }

  void on_overcurrent_(uint32_t now) {
    if (attempts_ >= cfg_.max_attempts) {
      finish_(Result::BLOCKED, now);
      return;
    }
    attempts_++;
    bool recent = has_attempted_ && (now - last_attempt_at_) <= cfg_.double_reverse_window_ms;
    has_attempted_ = true;
    last_attempt_at_ = now;
    pause_then_(!forward_, recent ? 2 : 1, now);
  }

  Drive tick_running_(uint32_t now, float current_mv, bool grate_low, bool outlet_covered) {
    const uint32_t elapsed = now - seg_start_;
    if (current_mv > feed_max_mv_) feed_max_mv_ = current_mv;
    if (forward_ && !outlet_covered && loop_uncovered_ < 0xffff) loop_uncovered_++;

    // Stock: the closed latch follows GPIO27 only 300 ms after the last counted loop.
    if (now - last_loop_at_ >= cfg_.grate_relatch_ms) closed_ = grate_low;

    if (seg_done_ >= seg_loops_) {
      after_run_(now);
      return phase_ == Phase::RUNNING ? run_drive_() : Drive::BRAKE;
    }

    if (elapsed >= cfg_.current_blank_ms && current_mv > cfg_.overcurrent_mv) {
      if (!over_) {
        over_ = true;
        over_since_ = now;
      } else if (now - over_since_ > cfg_.overcurrent_hold_ms) {
        on_overcurrent_(now);
        return Drive::BRAKE;
      }
    } else {
      over_ = false;
    }

    if (elapsed >= seg_timeout_) {
      finish_(feed_max_mv_ < cfg_.jam_judge_mv ? Result::GRATE_ERROR : Result::BLOCKED, now);
      return Drive::BRAKE;
    }
    return run_drive_();
  }

  Config cfg_{};
  Phase phase_ = Phase::IDLE;
  bool forward_ = true;
  bool hold_ = false;
  bool held_ = false;
  bool cancelling_ = false;
  uint8_t target_ = 0;
  uint8_t done_ = 0;
  uint8_t valid_ = 0;
  uint8_t seg_loops_ = 0;
  uint8_t seg_done_ = 0;
  uint32_t seg_start_ = 0;
  uint32_t seg_timeout_ = 0;
  float feed_max_mv_ = 0.0f;
  bool over_ = false;
  uint32_t over_since_ = 0;
  uint32_t phase_start_ = 0;
  bool next_forward_ = true;
  uint8_t next_loops_ = 0;
  bool has_attempted_ = false;
  uint32_t last_attempt_at_ = 0;
  uint8_t attempts_ = 0;
  bool closed_ = true;
  bool idle_level_ = true;
  uint32_t idle_since_ = 0;
  uint32_t last_loop_at_ = 0;
  uint16_t loop_pulses_ = 0;
  uint16_t loop_uncovered_ = 0;
  uint16_t last_uncovered_ = 0;
  bool outlet_blocked_ = false;
  Result pending_ = Result::NONE;
};

// The single controller instance used by the YAML lambdas. ESPHome emits globals before
// `includes`, so the instance lives here rather than in a `globals:` entry.
inline MotorController &motor() {
  static MotorController instance;
  return instance;
}

}  // namespace feeder
