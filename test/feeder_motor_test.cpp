// Host test for feeder_motor.h:  c++ -std=c++17 -Wall -Wextra -I../components/feeder_motor feeder_motor_test.cpp && ./a.out
#include "feeder_motor.h"

#include <cstdio>
#include <cstdlib>

using feeder::Drive;
using feeder::MotorController;
using feeder::Result;

static int failures = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                \
    }                                                            \
  } while (0)

// Simulated sensors around the controller, ticked every 10 ms.
struct Bench {
  MotorController m;
  uint32_t t = 0;
  bool grate_low = true;        // outlet closed by the grate
  bool outlet_covered = false;  // outlet IR beam free
  float mv = 300.0f;
  Drive last = Drive::SLEEP;

  Drive run_for(uint32_t ms) {
    for (uint32_t end = t + ms; t < end; t += 10) last = m.tick(t, mv, grate_low, outlet_covered);
    return last;
  }
  // One grate turn: the grate opens for 400 ms, food may cross the beam, then it closes.
  void turn(bool with_food = true) {
    grate_low = false;
    run_for(200);
    if (with_food) {
      outlet_covered = true;
      m.on_outlet_rise();
      run_for(50);
      outlet_covered = false;
    }
    run_for(150);
    grate_low = true;
    m.on_grate_edge(t);
    run_for(10);
  }
  Result finish() {
    Result r = Result::NONE;
    for (int i = 0; i < 20000 && r == Result::NONE; i++) {
      run_for(10);
      r = m.take_result();
    }
    return r;
  }
};

static void idle_settles_closed(Bench &b) { b.run_for(2100); }

static void normal_feed() {
  Bench b;
  idle_settles_closed(b);
  CHECK(b.m.start(2, b.t));
  CHECK(b.m.target() == 2);
  CHECK(!b.m.start(1, b.t));  // busy
  CHECK(b.run_for(100) == Drive::FORWARD);
  b.turn();
  CHECK(b.last == Drive::FORWARD);
  b.turn();
  CHECK(b.finish() == Result::OK);
  CHECK(b.m.loops_done() == 2);
  CHECK(b.m.valid_loops() == 2);
  CHECK(!b.m.outlet_blocked());
  CHECK(b.run_for(300) == Drive::SLEEP);
  CHECK(!b.m.busy());
}

static void open_outlet_adds_a_loop() {
  Bench b;
  b.grate_low = false;
  b.run_for(2100);
  CHECK(b.m.start(1, b.t));
  CHECK(b.m.target() == 2);
}

static void bounce_counts_once() {
  Bench b;
  idle_settles_closed(b);
  b.m.start(2, b.t);
  b.turn();
  b.m.on_grate_edge(b.t);  // bounce right after a counted loop
  CHECK(b.m.loops_done() == 1);
}

static void loop_without_food_is_not_valid() {
  Bench b;
  idle_settles_closed(b);
  b.m.start(2, b.t);
  b.turn(true);
  b.turn(false);
  CHECK(b.finish() == Result::OK);
  CHECK(b.m.valid_loops() == 1);
}

static void timeout_grate_error() {
  Bench b;
  idle_settles_closed(b);
  b.m.start(2, b.t);
  uint32_t t0 = b.t;
  CHECK(b.run_for(34900) == Drive::FORWARD);  // (2 + 1) * 10 s + 5 s = 35 s
  Result r = b.finish();
  CHECK(r == Result::GRATE_ERROR);
  CHECK(b.t - t0 >= 35000 && b.t - t0 < 35200);
}

static void timeout_blocked() {
  Bench b;
  idle_settles_closed(b);
  b.mv = 750;  // >= 700 but never > 799
  b.m.start(1, b.t);
  CHECK(b.finish() == Result::BLOCKED);
  CHECK(b.m.attempts() == 0);
}

static void overcurrent_reverses_then_resumes() {
  Bench b;
  idle_settles_closed(b);
  b.mv = 900;
  b.m.start(1, b.t);
  CHECK(b.run_for(400) == Drive::FORWARD);  // blanking: first 500 ms ignored
  CHECK(b.run_for(250) == Drive::FORWARD);  // over since 500 ms, not yet > 150 ms
  CHECK(b.run_for(50) == Drive::BRAKE);
  CHECK(b.m.attempts() == 1);
  b.mv = 300;
  CHECK(b.run_for(520) == Drive::REVERSE);  // after the 500 ms pause
  b.turn(false);                            // one reverse loop: not a scoop
  CHECK(b.m.loops_done() == 0);
  CHECK(b.last == Drive::FORWARD);  // straight back to the scoops still due
  b.turn();
  CHECK(b.finish() == Result::OK);
  CHECK(b.m.loops_done() == 1);
}

static void persistent_jam_blocks() {
  Bench b;
  idle_settles_closed(b);
  b.mv = 900;
  b.m.start(1, b.t);
  CHECK(b.finish() == Result::BLOCKED);
  CHECK(b.m.attempts() == 5);
}

static void reverse_timeout_is_a_fault() {
  Bench b;
  idle_settles_closed(b);
  b.mv = 900;
  b.m.start(1, b.t);
  b.run_for(700);  // over-current -> pause -> reverse
  b.mv = 300;
  b.run_for(520);
  CHECK(b.last == Drive::REVERSE);
  CHECK(b.finish() == Result::BLOCKED);  // stock: judged in any direction, on the feed-wide max (900)
}

static void cancel_with_outlet_closed() {
  Bench b;
  idle_settles_closed(b);
  b.m.start(3, b.t);
  b.turn();
  b.run_for(400);  // grate re-latched closed
  b.m.cancel(b.t);
  CHECK(b.run_for(50) == Drive::BRAKE);
  CHECK(b.finish() == Result::CANCELLED);
  CHECK(b.m.loops_done() == 1);
}

static void cancel_with_outlet_open_closes_it() {
  Bench b;
  idle_settles_closed(b);
  b.m.start(3, b.t);
  b.grate_low = false;
  b.run_for(400);  // latch follows the open grate
  b.m.cancel(b.t);
  CHECK(b.run_for(120) == Drive::FORWARD);  // one more loop to close the outlet
  b.grate_low = true;
  b.m.on_grate_edge(b.t);
  CHECK(b.finish() == Result::CANCELLED);
}

static void hold_feed_runs_while_held() {
  Bench b;
  idle_settles_closed(b);
  CHECK(b.m.start_hold(b.t));
  for (int i = 0; i < 5; i++) b.turn();
  CHECK(b.last == Drive::FORWARD);  // still held: next 5-loop run
  CHECK(b.m.target() == 10);
  b.turn();
  b.run_for(400);
  b.m.release(b.t);
  CHECK(b.finish() == Result::OK);
  CHECK(b.m.loops_done() == 6);
}

static void covered_outlet_is_blocked() {
  Bench b;
  idle_settles_closed(b);
  b.m.start(1, b.t);
  b.outlet_covered = true;  // beam blocked the whole time
  b.grate_low = false;
  b.run_for(400);
  b.grate_low = true;
  b.m.on_grate_edge(b.t);
  CHECK(b.finish() == Result::OK);
  CHECK(b.m.outlet_blocked());
  CHECK(b.m.valid_loops() == 1);  // stock: all loops count when the outlet is blocked
}

int main() {
  normal_feed();
  open_outlet_adds_a_loop();
  bounce_counts_once();
  loop_without_food_is_not_valid();
  timeout_grate_error();
  timeout_blocked();
  overcurrent_reverses_then_resumes();
  persistent_jam_blocks();
  reverse_timeout_is_a_fault();
  cancel_with_outlet_closed();
  cancel_with_outlet_open_closes_it();
  hold_feed_runs_while_held();
  covered_outlet_is_blocked();
  if (failures == 0) std::printf("all tests passed\n");
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
