#include "state_machine.h"

namespace {
// Comparação por diferença: continua correta quando millis() dá a volta (49 dias).
bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
}  // namespace

void StateMachine::note(FsmNote n, ActionList& out) {
  FsmAction a{ActionType::Note};
  a.note = n;
  a.userId = user_;
  out.push(a);
}

void StateMachine::engage(ActionList& out) {
  locked_ = true;
  out.push({ActionType::EngageLock});
}

void StateMachine::begin(uint32_t nowMs, bool doorOpen, const bool occupied[SLOT_COUNT], bool wokenByDoor, ActionList& out) {
  now_ = nowMs;
  doorOpen_ = doorOpen;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    occupied_[i] = occupied[i];
    initial_[i] = occupied[i];
  }
  engage(out);

  if (!doorOpen) {
    state_ = LockerState::Idle;
    return;
  }
  if (wokenByDoor) {
    // Dormindo, a tranca estava fechada: acordar pela porta só pode ser arrombamento.
    enterAlarm(AlarmReason::Intrusion, out);
    return;
  }
  // Reset no meio de uma sessão: exige apenas o fechamento normal, sem alarme falso.
  state_ = LockerState::DoorOpen;
  user_ = USER_NONE;
  deadline_ = now_ + cfg_.maxOpenMs;
  note(FsmNote::BootWithDoorOpen, out);
  out.push({ActionType::LightOn});
}

void StateMachine::handle(const FsmEvent& ev, ActionList& out) {
  now_ = ev.nowMs;
  switch (ev.type) {
    case FsmInput::Tick:
      onTick(out);
      break;
    case FsmInput::CardGranted:
      onCardGranted(ev, out);
      break;
    case FsmInput::CardDenied:
      onCardDenied(out);
      break;
    case FsmInput::DoorOpened:
      doorOpen_ = true;
      onDoorOpened(out);
      break;
    case FsmInput::DoorClosed:
      doorOpen_ = false;
      onDoorClosed(out);
      break;
    case FsmInput::SlotOccupied:
      onSlot(ev.slot, true, out);
      break;
    case FsmInput::SlotFreed:
      onSlot(ev.slot, false, out);
      break;
    case FsmInput::AdminUnlock:
      onAdminUnlock(ev, out);
      break;
    case FsmInput::AdminLock:
      onAdminLock(out);
      break;
    case FsmInput::SilenceAlarm:
      onSilence(out);
      break;
    case FsmInput::EnrollStart:
      if (state_ == LockerState::Idle) {
        state_ = LockerState::Enroll;
        deadline_ = now_ + cfg_.enrollMs;
      } else {
        note(FsmNote::Refused, out);
      }
      break;
    case FsmInput::EnrollCancel:
      if (state_ == LockerState::Enroll) state_ = LockerState::Idle;
      break;
    case FsmInput::EnrollCaptured:
      if (state_ == LockerState::Enroll) {
        state_ = LockerState::Idle;
        out.push({ActionType::FeedbackEnrollDone});
      }
      break;
  }
}

void StateMachine::onTick(ActionList& out) {
  if (!reached(now_, deadline_)) return;
  switch (state_) {
    case LockerState::Authorized:
      engage(out);
      state_ = LockerState::Idle;
      note(FsmNote::UnlockTimedOut, out);
      user_ = USER_NONE;
      break;
    case LockerState::DoorOpen:
      enterAlarm(AlarmReason::DoorTimeout, out);
      break;
    case LockerState::Closing:
      finishSession(out);
      break;
    case LockerState::Enroll:
      state_ = LockerState::Idle;
      note(FsmNote::EnrollTimedOut, out);
      break;
    default:
      break;
  }
}

void StateMachine::onDoorOpened(ActionList& out) {
  switch (state_) {
    case LockerState::Authorized:
      startSession(out);
      break;
    case LockerState::Closing:
      // Reabriu antes de trancar: é a mesma sessão, a fotografia inicial continua valendo.
      state_ = LockerState::DoorOpen;
      deadline_ = now_ + cfg_.maxOpenMs;
      break;
    case LockerState::Idle:
    case LockerState::Enroll:
      enterAlarm(AlarmReason::Intrusion, out);
      break;
    default:
      break;
  }
}

void StateMachine::onDoorClosed(ActionList& out) {
  if (state_ == LockerState::DoorOpen) {
    state_ = LockerState::Closing;
    deadline_ = now_ + cfg_.relockMs;
    return;
  }
  if (state_ != LockerState::Alarm) return;

  if (alarm_ == AlarmReason::DoorTimeout) {
    // A causa sumiu com o fechamento: não exige ação do administrador.
    if (!silenced_) out.push({ActionType::AlarmOff});
    alarm_ = AlarmReason::None;
    silenced_ = false;
    state_ = LockerState::Closing;
    deadline_ = now_ + cfg_.relockMs;
  } else if (silenced_) {
    leaveAlarmToIdle(out);
  }
}

bool StateMachine::sessionActive() const {
  return state_ == LockerState::DoorOpen || state_ == LockerState::Closing ||
         (state_ == LockerState::Alarm && alarm_ == AlarmReason::DoorTimeout);
}

void StateMachine::onSlot(uint8_t slot, bool occupied, ActionList& out) {
  if (slot >= SLOT_COUNT || occupied_[slot] == occupied) return;
  occupied_[slot] = occupied;

  if (cfg_.maintenance[slot]) {
    FsmAction a{ActionType::Note};
    a.slot = slot;
    a.note = FsmNote::SlotInMaintenance;
    out.push(a);
    return;
  }
  // Closing conta como sessão: o microswitch pode assentar logo depois de o reed
  // fechar, e isso não é violação enquanto a tranca ainda está aberta.
  if (sessionActive()) return;

  FsmAction a{ActionType::SlotViolation};
  a.slot = slot;
  a.occupied = occupied;
  out.push(a);
  if (state_ != LockerState::Alarm) enterAlarm(AlarmReason::Violation, out);
}

void StateMachine::onCardGranted(const FsmEvent& ev, ActionList& out) {
  switch (state_) {
    case LockerState::Idle:
    case LockerState::Authorized:
      out.push({ActionType::FeedbackGranted});
      startAuthorized(ev.userId, out);
      break;
    case LockerState::Closing:
      // Fila de usuários: fecha a sessão anterior antes de abrir a do próximo.
      commitSession(out);
      out.push({ActionType::FeedbackGranted});
      startAuthorized(ev.userId, out);
      break;
    case LockerState::DoorOpen:
      if (user_ == USER_NONE) user_ = ev.userId;  // Sessão herdada de um reset.
      out.push({ActionType::FeedbackGranted});
      break;
    case LockerState::Alarm:
      if (ev.admin) {
        onSilence(out);
      } else {
        out.push({ActionType::FeedbackDenied});
      }
      break;
    case LockerState::Enroll:
      break;
  }
}

void StateMachine::onCardDenied(ActionList& out) {
  if (state_ != LockerState::Enroll) out.push({ActionType::FeedbackDenied});
}

void StateMachine::onSilence(ActionList& out) {
  if (state_ != LockerState::Alarm) return;
  if (!silenced_) {
    silenced_ = true;
    out.push({ActionType::AlarmOff});
  }
  if (!doorOpen_ && alarm_ != AlarmReason::DoorTimeout) leaveAlarmToIdle(out);
}

void StateMachine::onAdminUnlock(const FsmEvent& ev, ActionList& out) {
  if (state_ == LockerState::Idle || state_ == LockerState::Authorized) {
    startAuthorized(ev.userId, out);
  } else {
    note(FsmNote::Refused, out);
  }
}

void StateMachine::onAdminLock(ActionList& out) {
  if (state_ == LockerState::Authorized) {
    engage(out);
    state_ = LockerState::Idle;
    user_ = USER_NONE;
  } else if (state_ == LockerState::Closing) {
    finishSession(out);
  } else {
    note(FsmNote::Refused, out);
  }
}

void StateMachine::startAuthorized(uint16_t user, ActionList& out) {
  user_ = user;
  state_ = LockerState::Authorized;
  deadline_ = now_ + cfg_.unlockMs;
  locked_ = false;
  out.push({ActionType::ReleaseLock});
}

void StateMachine::startSession(ActionList& out) {
  state_ = LockerState::DoorOpen;
  deadline_ = now_ + cfg_.maxOpenMs;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) initial_[i] = occupied_[i];
  note(FsmNote::SessionStarted, out);
  out.push({ActionType::LightOn});
}

void StateMachine::commitSession(ActionList& out) {
  // Grava o saldo da sessão, não cada transição: tirar e repor o mesmo notebook
  // na mesma abertura não é empréstimo.
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    if (occupied_[i] == initial_[i] || cfg_.maintenance[i]) continue;
    FsmAction a{occupied_[i] ? ActionType::CommitReturn : ActionType::CommitWithdraw};
    a.slot = i;
    a.userId = user_;
    a.occupied = occupied_[i];
    out.push(a);
    initial_[i] = occupied_[i];
  }
}

void StateMachine::finishSession(ActionList& out) {
  commitSession(out);
  engage(out);
  out.push({ActionType::LightOff});
  state_ = LockerState::Idle;
  user_ = USER_NONE;
}

void StateMachine::enterAlarm(AlarmReason reason, ActionList& out) {
  state_ = LockerState::Alarm;
  alarm_ = reason;
  silenced_ = false;
  out.push({ActionType::AlarmOn});
  if (reason == AlarmReason::DoorTimeout) note(FsmNote::DoorLeftOpen, out);
  if (reason == AlarmReason::Intrusion) note(FsmNote::Intrusion, out);
}

void StateMachine::leaveAlarmToIdle(ActionList& out) {
  engage(out);
  out.push({ActionType::LightOff});
  state_ = LockerState::Idle;
  alarm_ = AlarmReason::None;
  silenced_ = false;
  user_ = USER_NONE;
}
