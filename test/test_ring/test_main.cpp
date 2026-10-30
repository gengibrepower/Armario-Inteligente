#include <unity.h>

#include "config.h"
#include "drivers/buzzer_led.h"
#include "storage/ring_math.h"

void setUp() {}
void tearDown() {}

void test_position_comes_from_sequence() {
  RingGeometry g{3, 10};
  TEST_ASSERT_EQUAL_UINT16(0, g.segmentOf(0));
  TEST_ASSERT_EQUAL_UINT16(9, g.offsetOf(9));
  TEST_ASSERT_EQUAL_UINT16(1, g.segmentOf(10));
  TEST_ASSERT_EQUAL_UINT16(2, g.segmentOf(29));
  TEST_ASSERT_EQUAL_UINT16(9, g.offsetOf(29));
  TEST_ASSERT_EQUAL_UINT16(0, g.segmentOf(30));  // Deu a volta.
  TEST_ASSERT_EQUAL_UINT16(5, g.offsetOf(35));
}

void test_retained_before_first_wrap() {
  RingGeometry g{3, 10};
  TEST_ASSERT_EQUAL_UINT32(0, g.retained(0));
  TEST_ASSERT_EQUAL_UINT32(7, g.retained(7));
  TEST_ASSERT_EQUAL_UINT32(30, g.retained(30));
  TEST_ASSERT_EQUAL_UINT32(0, g.oldest(30));
}

void test_wrap_drops_a_whole_segment() {
  RingGeometry g{3, 10};
  TEST_ASSERT_EQUAL_UINT32(21, g.retained(31));
  TEST_ASSERT_EQUAL_UINT32(10, g.oldest(31));
  TEST_ASSERT_EQUAL_UINT32(30, g.retained(40));
  TEST_ASSERT_EQUAL_UINT32(10, g.oldest(40));
  TEST_ASSERT_EQUAL_UINT32(21, g.retained(41));
  TEST_ASSERT_EQUAL_UINT32(20, g.oldest(41));
}

void test_project_rings_meet_requirements() {
  RingGeometry logs{RING_LOG_SEGMENTS, RING_LOG_PER_SEGMENT};
  RingGeometry minutes{RING_MINUTE_SEGMENTS, RING_MINUTE_PER_SEGMENT};
  RingGeometry hours{RING_HOUR_SEGMENTS, RING_HOUR_PER_SEGMENT};
  // Varre uma volta inteira depois de cheio: o requisito vale em todo instante.
  for (uint32_t head = logs.capacity(); head < logs.capacity() * 3; head++) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1000, logs.retained(head));
  }
  for (uint32_t head = minutes.capacity(); head < minutes.capacity() * 3; head++) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1440, minutes.retained(head));
  }
  for (uint32_t head = hours.capacity(); head < hours.capacity() * 3; head++) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(168, hours.retained(head));
  }
}

void test_sequence_counter_rollover_keeps_slots_valid() {
  RingGeometry g{RING_LOG_SEGMENTS, RING_LOG_PER_SEGMENT};
  const uint32_t head = 4000000000UL;
  TEST_ASSERT_TRUE(g.segmentOf(head) < g.segments);
  TEST_ASSERT_TRUE(g.oldest(head) < head);
}

void test_blinker_finishes_after_count() {
  Blinker b;
  b.start(100, 50, 50, 2);
  TEST_ASSERT_TRUE(b.update(100));
  TEST_ASSERT_FALSE(b.update(160));
  TEST_ASSERT_TRUE(b.update(210));
  TEST_ASSERT_FALSE(b.update(300));
  TEST_ASSERT_FALSE(b.update(310));
}

void test_blinker_loops_until_stopped() {
  Blinker b;
  b.start(0, 10, 10, 0);
  TEST_ASSERT_TRUE(b.update(100000));
  b.stop();
  TEST_ASSERT_FALSE(b.update(100000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_position_comes_from_sequence);
  RUN_TEST(test_retained_before_first_wrap);
  RUN_TEST(test_wrap_drops_a_whole_segment);
  RUN_TEST(test_project_rings_meet_requirements);
  RUN_TEST(test_sequence_counter_rollover_keeps_slots_valid);
  RUN_TEST(test_blinker_finishes_after_count);
  RUN_TEST(test_blinker_loops_until_stopped);
  return UNITY_END();
}
