const STATE_TEXT = { IDLE: "Repouso", AUTHORIZED: "Destrancado", DOOR_OPEN: "Porta em uso", CLOSING: "Trancando", ALARM: "Alarme", ENROLL: "Cadastro de cartão" };
const WEEK = ["Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"];

const charts = {
  hour: lineChart("cHour", [{ data: [], backgroundColor: COLORS[0] }], { type: "bar" }),
  day: lineChart("cDay", [{ data: [], backgroundColor: COLORS[1] }], { type: "bar" }),
  d1: lineChart("c24", [{ data: [], borderColor: COLORS[0], stepped: true }], { max: 2 }),
  d7: lineChart("c7d", [{ data: [], borderColor: COLORS[2] }], { max: 2, maxTicks: 5 }),
};

function renderState(s) {
  const alarm = s.state === "ALARM" ? ` (${s.alarm.replace("_", " ").toLowerCase()})` : "";
  $("status").innerHTML = `
    <dt>Sistema</dt><dd><span class="tag ${s.state === "ALARM" ? "bad" : "ok"}">${STATE_TEXT[s.state] || s.state}${esc(alarm)}</span></dd>
    <dt>Porta</dt><dd>${s.doorOpen ? "Aberta" : "Fechada"}</dd>
    <dt>Tranca</dt><dd>${s.locked ? "Trancada" : "Destrancada"}</dd>
    <dt>Relógio</dt><dd>${s.timeSynced ? fmtTime(s.now, true) : "Sem hora sincronizada"}</dd>`;
  $("slots").innerHTML = s.slots.map((slot) => {
    const name = slot.asset ? `${slot.id} · ${esc(slot.asset)}` : slot.id;
    const stateTag = slot.maintenance ? `<span class="tag warn">Manutenção</span>`
      : slot.occupied ? `<span class="tag ok">Disponível</span>`
      : `<span class="tag ${slot.overdue ? "bad" : "warn"}">${slot.overdue ? "Emprestado (vencido)" : "Emprestado"}</span>`;
    return `<tr><td>${name}</td><td>${stateTag}</td><td>${esc(slot.loan?.user ?? "—")}</td><td>${slot.loan ? fmtTime(slot.loan.since, slot.loan.synced) : "—"}</td></tr>`;
  }).join("");
}

async function loadUsage() {
  const u = await api("/api/usage");
  const kpi = (value, label) => `<div class="kpi"><b>${value}</b><span>${label}</span></div>`;
  $("kpis").innerHTML = kpi(`${u.occupancyPct}%`, "Taxa de ocupação") + kpi(fmtDuration(u.avgLoanSec), "Tempo médio de empréstimo")
    + kpi(u.withdrawals, "Retiradas") + kpi(u.returns, "Devoluções") + kpi(u.denied, "Acessos negados") + kpi(u.violations, "Violações");
  setChart(charts.hour, u.perHour.map((_, h) => `${pad(h)}h`), u.perHour);
  setChart(charts.day, WEEK, u.perWeekday);
  $("ranking").innerHTML = u.ranking.map((r) => `<tr><td>${esc(r.name)}</td><td class="num">${r.count}</td></tr>`).join("") || `<tr><td class="muted" colspan="2">Sem retiradas registradas</td></tr>`;
}

async function loadEvents() {
  const list = await api("/api/events?limit=12");
  $("events").innerHTML = list.map((e) => `<tr><td>${fmtTime(e.ts, e.synced)}</td><td>${esc(e.type)}</td><td>${e.slot || "—"}</td><td>${esc(e.user || "—")}</td></tr>`).join("") || `<tr><td class="muted" colspan="4">Sem eventos</td></tr>`;
}

async function loadHistory() {
  const minutes = await api("/api/history/minutes?step=5");
  setChart(charts.d1, minutes.map((m) => fmtTime(m[0], m[1] & 1, true)), minutes.map((m) => (200 - m[3] - m[4]) / 100));
  const hours = await api("/api/history/hours");
  setChart(charts.d7, hours.map((h) => fmtTime(h[0], h[1] & 1)), hours.map((h) => h[2] / 50));
}

function refresh() {
  api("/api/state").then(renderState).catch((e) => toast(e.message));
  Promise.all([loadUsage(), loadEvents(), loadHistory()]).catch((e) => toast(e.message));
}

connectSocket((msg) => {
  if (msg.t !== "state") return;
  renderState(msg);
  // Mudou a porta ou um slot: um evento pode ter sido gravado.
  loadEvents().catch(() => {});
  loadUsage().catch(() => {});
});
refresh();
setInterval(refresh, 300000);
