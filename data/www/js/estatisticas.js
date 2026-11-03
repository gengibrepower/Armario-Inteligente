// Estatísticas para o administrador. O firmware entrega os eventos crus e os totais
// gerais; médias por dia, semana e pessoa são calculadas aqui, no navegador.
const WEEK = ["Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"];
const DAY_MS = 86400000;

const startOfDay = (ms) => new Date(ms).setHours(0, 0, 0, 0);
const startOfWeek = (ms) => {
  const d = new Date(startOfDay(ms));
  d.setDate(d.getDate() - ((d.getDay() + 6) % 7));  // semana começa na segunda
  return d.getTime();
};
const dayLabel = (ms) => {
  const d = new Date(ms);
  return `${pad(d.getDate())}/${pad(d.getMonth() + 1)}`;
};
const dec = (v) => v.toLocaleString("pt-BR", { minimumFractionDigits: 1, maximumFractionDigits: 1 });
const kpi = (value, label) => `<div class="kpi"><b>${value}</b><span>${label}</span></div>`;

let charts;
function makeCharts() {
  charts = {
    perDay: lineChart("cPerDay", [{ label: "Retiradas", data: [], backgroundColor: COLORS[0] }, { label: "Notebooks diferentes", data: [], backgroundColor: COLORS[2] }], { type: "bar", maxTicks: 14 }),
    week: lineChart("cWeek", [{ data: [], backgroundColor: COLORS[0] }], { type: "bar" }),
    weekday: lineChart("cWeekday", [{ data: [], backgroundColor: COLORS[1] }], { type: "bar" }),
    hours: lineChart("cHours", [{ data: [], backgroundColor: COLORS[2] }], { type: "bar" }),
    hour: lineChart("cHour", [{ data: [], backgroundColor: COLORS[0] }], { type: "bar" }),
    top: lineChart("cTop", [{ data: [], backgroundColor: COLORS[3] }], { type: "bar" }),
    d7: lineChart("c7d", [{ data: [], borderColor: COLORS[2] }], { max: 2, maxTicks: 7 }),
  };
  charts.top.options.indexAxis = "y";
}

function fromEvents(events) {
  // Só eventos com hora real entram nas contas por dia; sem NTP a data é desconhecida.
  const loans = events.filter((e) => e.type === "retirada" && e.synced).map((e) => ({ ...e, ms: e.ts * 1000 }));
  const today = startOfDay(Date.now());
  const first = loans.length ? startOfDay(Math.min(...loans.map((l) => l.ms))) : today;
  const days = Math.round((today - first) / DAY_MS) + 1;

  const perDay = new Map();
  for (let t = Math.min(first, today - 13 * DAY_MS); t <= today; t = startOfDay(t + DAY_MS + 3600000)) perDay.set(t, { count: 0, slots: new Set() });
  loans.forEach((l) => {
    const d = perDay.get(startOfDay(l.ms));
    d.count++;
    d.slots.add(l.slot);
  });
  const all = [...perDay].filter(([t]) => t >= first);
  const avgLoans = all.reduce((s, [, d]) => s + d.count, 0) / days;
  const avgNotebooks = all.reduce((s, [, d]) => s + d.slots.size, 0) / days;
  const last14 = [...perDay].slice(-14);
  setChart(charts.perDay, last14.map(([t]) => `${WEEK[new Date(t).getDay()]} ${dayLabel(t)}`), last14.map(([, d]) => d.count), last14.map(([, d]) => d.slots.size));

  const perWeek = new Map();
  for (let t = startOfWeek(first); t <= today; t = startOfWeek(t + 8 * DAY_MS)) perWeek.set(t, 0);
  loans.forEach((l) => perWeek.set(startOfWeek(l.ms), perWeek.get(startOfWeek(l.ms)) + 1));
  setChart(charts.week, [...perWeek.keys()].map((t) => `sem. ${dayLabel(t)}`), [...perWeek.values()]);

  const users = new Map();
  const get = (name) => users.get(name) || users.set(name, { count: 0, total: 0, timed: 0 }).get(name);
  events.forEach((e) => {
    if (!e.user) return;
    if (e.type === "retirada") get(e.user).count++;
    if (e.type === "devolucao" && e.dur) {
      const u = get(e.user);
      u.total += e.dur;
      u.timed++;
    }
  });
  const ranked = [...users].sort((a, b) => b[1].count - a[1].count || b[1].total - a[1].total);
  const top = ranked.slice(0, 8);
  setChart(charts.top, top.map(([name]) => name), top.map(([, u]) => u.count));
  $("perUser").innerHTML = ranked.map(([name, u]) => `<tr><td>${esc(name)}</td><td class="num">${u.count}</td>
    <td class="num">${fmtDuration(u.total)}</td><td class="num">${fmtDuration(u.timed ? Math.round(u.total / u.timed) : 0)}</td></tr>`).join("")
    || `<tr><td class="muted" colspan="4">Sem retiradas registradas</td></tr>`;

  $("period").textContent = loans.length
    ? `Médias e ranking calculados sobre ${days} dia(s), desde ${dayLabel(first)} (últimos ${events.length} eventos guardados no armário).`
    : "Ainda não há retiradas com data registrada.";
  return { avgLoans, avgNotebooks, users: users.size };
}

async function load() {
  const [u, events, hours] = await Promise.all([api("/api/usage"), api("/api/history/events?limit=500"), api("/api/history/hours")]);
  const e = fromEvents(events);
  $("kpis").innerHTML = kpi(dec(e.avgNotebooks), "Notebooks usados por dia (média)") + kpi(dec(e.avgLoans), "Retiradas por dia (média)")
    + kpi(fmtDuration(u.avgLoanSec), "Tempo médio de empréstimo") + kpi(`${u.occupancyPct}%`, "Taxa de ocupação")
    + kpi(e.users, "Pessoas que usaram") + kpi(u.withdrawals, "Retiradas no total") + kpi(u.denied, "Acessos negados") + kpi(u.violations, "Violações");
  setChart(charts.weekday, WEEK, u.perWeekday);
  setChart(charts.hour, u.perHour.map((_, h) => `${pad(h)}h`), u.perHour);

  // Cada hora guarda a média de notebooks em uso × 50; somar por dia dá notebook-horas.
  setChart(charts.d7, hours.map((h) => fmtTime(h[0], h[1] & 1)), hours.map((h) => h[2] / 50));
  const perDay = new Map();
  hours.filter((h) => h[1] & 1).forEach((h) => {
    const t = startOfDay(h[0] * 1000);
    perDay.set(t, (perDay.get(t) || 0) + h[2] / 50);
  });
  setChart(charts.hours, [...perDay.keys()].map(dayLabel), [...perDay.values()].map((v) => +v.toFixed(1)));
}

async function start() {
  const s = await api("/api/session");
  const ok = s.authenticated && !s.mustChange;
  $("needLogin").classList.toggle("hide", ok);
  $("app").classList.toggle("hide", !ok);
  if (!ok) return;
  makeCharts();
  await load();
  setInterval(() => load().catch(() => {}), 300000);
}

start().catch((e) => toast(e.message));
