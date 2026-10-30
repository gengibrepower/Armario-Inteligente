// Cenários da seção 3 do prompt, sem hardware: entra evento, sai ação.
#include <unity.h>

#include "core/state_machine.h"

namespace {
constexpr uint16_t ALICE = 1;
constexpr uint16_t BOB = 2;

StateMachine fsm;
ActionList out;
uint32_t now;

void start(bool door = false, bool slot1 = true, bool slot2 = true, bool wokenByDoor = false) {
  fsm = StateMachine();
  FsmConfig cfg;
  cfg.unlockMs = 15000;
  cfg.relockMs = 3000;
  cfg.maxOpenMs = 120000;
  fsm.setConfig(cfg);
  out.clear();
  now = 1000;
  const bool occupied[SLOT_COUNT] = {slot1, slot2};
  fsm.begin(now, door, occupied, wokenByDoor, out);
}

void send(FsmInput type, uint8_t slot = 0, uint16_t user = USER_NONE, bool admin = false) {
  out.clear();
  FsmEvent ev{type, now};
  ev.slot = slot;
  ev.userId = user;
  ev.admin = admin;
  fsm.handle(ev, out);
}

void wait(uint32_t ms) {
  now += ms;
  send(FsmInput::Tick);
}

bool has(ActionType type) {
  for (uint8_t i = 0; i < out.count; i++) {
    if (out.items[i].type == type) return true;
  }
  return false;
}

const FsmAction* find(ActionType type) {
  for (uint8_t i = 0; i < out.count; i++) {
    if (out.items[i].type == type) return &out.items[i];
  }
  return nullptr;
}

bool hasNote(FsmNote note) {
  for (uint8_t i = 0; i < out.count; i++) {
    if (out.items[i].type == ActionType::Note && out.items[i].note == note) return true;
  }
  return false;
}

void openSession(uint16_t user = ALICE) {
  send(FsmInput::CardGranted, 0, user);
  send(FsmInput::DoorOpened);
}

void assertState(LockerState expected) { TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(fsm.state())); }
}  // namespace

void setUp() { start(); }
void tearDown() {}

void test_boots_idle_and_locked() {
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(fsm.locked());
  TEST_ASSERT_TRUE(has(ActionType::EngageLock));
}

void test_denied_card_keeps_lock_closed() {
  send(FsmInput::CardDenied);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(has(ActionType::FeedbackDenied));
  TEST_ASSERT_FALSE(has(ActionType::ReleaseLock));
}

void test_granted_card_unlocks() {
  send(FsmInput::CardGranted, 0, ALICE);
  assertState(LockerState::Authorized);
  TEST_ASSERT_TRUE(has(ActionType::ReleaseLock));
  TEST_ASSERT_TRUE(has(ActionType::FeedbackGranted));
  TEST_ASSERT_FALSE(fsm.locked());
}

void test_relocks_if_door_never_opens() {
  send(FsmInput::CardGranted, 0, ALICE);
  wait(14999);
  assertState(LockerState::Authorized);
  wait(1);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(has(ActionType::EngageLock));
  TEST_ASSERT_TRUE(hasNote(FsmNote::UnlockTimedOut));
}

void test_withdraw_is_committed_only_after_relock() {
  openSession();
  assertState(LockerState::DoorOpen);
  TEST_ASSERT_TRUE(has(ActionType::LightOn));
  send(FsmInput::SlotFreed, 0);
  TEST_ASSERT_FALSE(has(ActionType::CommitWithdraw));
  send(FsmInput::DoorClosed);
  assertState(LockerState::Closing);
  TEST_ASSERT_FALSE(has(ActionType::EngageLock));
  wait(3000);
  assertState(LockerState::Idle);
  const FsmAction* commit = find(ActionType::CommitWithdraw);
  TEST_ASSERT_NOT_NULL(commit);
  TEST_ASSERT_EQUAL_UINT8(0, commit->slot);
  TEST_ASSERT_EQUAL_UINT16(ALICE, commit->userId);
  TEST_ASSERT_TRUE(has(ActionType::EngageLock));
  TEST_ASSERT_TRUE(has(ActionType::LightOff));
}

void test_return_is_committed() {
  start(false, false, true);
  openSession();
  send(FsmInput::SlotOccupied, 0);
  send(FsmInput::DoorClosed);
  wait(3000);
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitReturn));
}

void test_taking_and_replacing_is_not_a_loan() {
  openSession();
  send(FsmInput::SlotFreed, 0);
  send(FsmInput::SlotOccupied, 0);
  send(FsmInput::DoorClosed);
  wait(3000);
  TEST_ASSERT_FALSE(has(ActionType::CommitWithdraw));
  TEST_ASSERT_FALSE(has(ActionType::CommitReturn));
}

void test_swap_between_slots_records_both() {
  start(false, true, false);
  openSession();
  send(FsmInput::SlotFreed, 0);
  send(FsmInput::SlotOccupied, 1);
  send(FsmInput::DoorClosed);
  wait(3000);
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitWithdraw));
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitReturn));
}

void test_door_reopened_while_closing_keeps_session() {
  openSession();
  send(FsmInput::SlotFreed, 0);
  send(FsmInput::DoorClosed);
  wait(1000);
  send(FsmInput::DoorOpened);
  assertState(LockerState::DoorOpen);
  send(FsmInput::DoorClosed);
  wait(3000);
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitWithdraw));
}

void test_slot_settling_while_closing_is_not_violation() {
  openSession();
  send(FsmInput::DoorClosed);
  send(FsmInput::SlotFreed, 0);
  assertState(LockerState::Closing);
  TEST_ASSERT_FALSE(has(ActionType::SlotViolation));
}

void test_next_user_card_while_closing_keeps_credit_with_first_user() {
  openSession(ALICE);
  send(FsmInput::SlotFreed, 0);
  send(FsmInput::DoorClosed);
  send(FsmInput::CardGranted, 0, BOB);
  const FsmAction* commit = find(ActionType::CommitWithdraw);
  TEST_ASSERT_NOT_NULL(commit);
  TEST_ASSERT_EQUAL_UINT16(ALICE, commit->userId);
  assertState(LockerState::Authorized);
  TEST_ASSERT_EQUAL_UINT16(BOB, fsm.sessionUser());
}

void test_door_left_open_alarm_clears_on_close() {
  openSession();
  wait(120000);
  assertState(LockerState::Alarm);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AlarmReason::DoorTimeout), static_cast<int>(fsm.alarmReason()));
  TEST_ASSERT_TRUE(has(ActionType::AlarmOn));
  TEST_ASSERT_TRUE(hasNote(FsmNote::DoorLeftOpen));
  send(FsmInput::SlotFreed, 0);
  TEST_ASSERT_FALSE(has(ActionType::SlotViolation));
  send(FsmInput::DoorClosed);
  TEST_ASSERT_TRUE(has(ActionType::AlarmOff));
  assertState(LockerState::Closing);
  wait(3000);
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitWithdraw));
}

void test_door_opened_without_card_is_intrusion() {
  send(FsmInput::DoorOpened);
  assertState(LockerState::Alarm);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AlarmReason::Intrusion), static_cast<int>(fsm.alarmReason()));
  TEST_ASSERT_TRUE(hasNote(FsmNote::Intrusion));
}

void test_slot_changed_with_door_closed_is_violation() {
  send(FsmInput::SlotFreed, 1);
  assertState(LockerState::Alarm);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AlarmReason::Violation), static_cast<int>(fsm.alarmReason()));
  const FsmAction* v = find(ActionType::SlotViolation);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT8(1, v->slot);
}

void test_intrusion_needs_silence_and_closed_door() {
  send(FsmInput::DoorOpened);
  send(FsmInput::DoorClosed);
  assertState(LockerState::Alarm);  // Fechar não apaga arrombamento.
  send(FsmInput::SilenceAlarm);
  TEST_ASSERT_TRUE(has(ActionType::AlarmOff));
  assertState(LockerState::Idle);
}

void test_silenced_with_door_open_waits_for_close() {
  send(FsmInput::DoorOpened);
  send(FsmInput::SilenceAlarm);
  assertState(LockerState::Alarm);
  TEST_ASSERT_TRUE(fsm.alarmSilenced());
  send(FsmInput::DoorClosed);
  assertState(LockerState::Idle);
}

void test_common_card_does_not_silence_alarm() {
  send(FsmInput::DoorOpened);
  send(FsmInput::CardGranted, 0, ALICE, false);
  assertState(LockerState::Alarm);
  TEST_ASSERT_FALSE(fsm.alarmSilenced());
  TEST_ASSERT_TRUE(has(ActionType::FeedbackDenied));
}

void test_admin_card_silences_alarm() {
  send(FsmInput::DoorOpened);
  send(FsmInput::CardGranted, 0, BOB, true);
  TEST_ASSERT_TRUE(fsm.alarmSilenced());
}

void test_enroll_captures_and_times_out() {
  send(FsmInput::EnrollStart);
  assertState(LockerState::Enroll);
  send(FsmInput::EnrollCaptured);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(has(ActionType::FeedbackEnrollDone));

  send(FsmInput::EnrollStart);
  wait(ENROLL_TIMEOUT_MS);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(hasNote(FsmNote::EnrollTimedOut));
}

void test_enroll_refused_outside_idle() {
  send(FsmInput::CardGranted, 0, ALICE);
  send(FsmInput::EnrollStart);
  assertState(LockerState::Authorized);
  TEST_ASSERT_TRUE(hasNote(FsmNote::Refused));
}

void test_maintenance_slot_ignores_changes() {
  FsmConfig cfg;
  cfg.maintenance[0] = true;
  fsm.setConfig(cfg);
  send(FsmInput::SlotFreed, 0);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(hasNote(FsmNote::SlotInMaintenance));
}

void test_admin_unlock_and_lock() {
  send(FsmInput::AdminUnlock, 0, USER_WEB_ADMIN);
  assertState(LockerState::Authorized);
  send(FsmInput::AdminLock);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(fsm.locked());
}

void test_admin_lock_refused_with_door_open() {
  openSession();
  send(FsmInput::AdminLock);
  assertState(LockerState::DoorOpen);
  TEST_ASSERT_TRUE(hasNote(FsmNote::Refused));
}

void test_boot_with_door_open_does_not_alarm() {
  start(true);
  assertState(LockerState::DoorOpen);
  TEST_ASSERT_TRUE(hasNote(FsmNote::BootWithDoorOpen));
}

void test_wake_by_door_is_intrusion() {
  start(true, true, true, true);
  assertState(LockerState::Alarm);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AlarmReason::Intrusion), static_cast<int>(fsm.alarmReason()));
}

void test_timer_survives_millis_rollover() {
  start();
  now = 0xFFFFF000;
  send(FsmInput::CardGranted, 0, ALICE);
  wait(1000);
  assertState(LockerState::Authorized);
  wait(14000);
  assertState(LockerState::Idle);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boots_idle_and_locked);
  RUN_TEST(test_denied_card_keeps_lock_closed);
  RUN_TEST(test_granted_card_unlocks);
  RUN_TEST(test_relocks_if_door_never_opens);
  RUN_TEST(test_withdraw_is_committed_only_after_relock);
  RUN_TEST(test_return_is_committed);
  RUN_TEST(test_taking_and_replacing_is_not_a_loan);
  RUN_TEST(test_swap_between_slots_records_both);
  RUN_TEST(test_door_reopened_while_closing_keeps_session);
  RUN_TEST(test_slot_settling_while_closing_is_not_violation);
  RUN_TEST(test_next_user_card_while_closing_keeps_credit_with_first_user);
  RUN_TEST(test_door_left_open_alarm_clears_on_close);
  RUN_TEST(test_door_opened_without_card_is_intrusion);
  RUN_TEST(test_slot_changed_with_door_closed_is_violation);
  RUN_TEST(test_intrusion_needs_silence_and_closed_door);
  RUN_TEST(test_silenced_with_door_open_waits_for_close);
  RUN_TEST(test_common_card_does_not_silence_alarm);
  RUN_TEST(test_admin_card_silences_alarm);
  RUN_TEST(test_enroll_captures_and_times_out);
  RUN_TEST(test_enroll_refused_outside_idle);
  RUN_TEST(test_maintenance_slot_ignores_changes);
  RUN_TEST(test_admin_unlock_and_lock);
  RUN_TEST(test_admin_lock_refused_with_door_open);
  RUN_TEST(test_boot_with_door_open_does_not_alarm);
  RUN_TEST(test_wake_by_door_is_intrusion);
  RUN_TEST(test_timer_survives_millis_rollover);
  return UNITY_END();
}
