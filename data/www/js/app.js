// Funções comuns: menu, API, WebSocket e formatação. Texto vindo do armário só
// entra na página por esc() ou textContent, pois nomes são digitados por pessoas.
const PAGES = [["index.html", "Dashboard"], ["performance.html", "Performance"], ["admin.html", "Admin"], ["sobre.html", "Sobre"], ["changelog.html", "Changelog"]];

function renderNav() {
  const here = location.pathname.split("/").pop() || "index.html";
  const links = PAGES.map(([href, label]) => `<a href="${href}" class="${href === here ? "on" : ""}">${label}</a>`).join("");
  document.querySelector("nav").innerHTML = `<strong>Armário</strong>${links}<span id="link">offline</span>`;
}

const esc = (v) => String(v ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
const $ = (id) => document.getElementById(id);

function toast(message) {
  let box = $("toast");
  if (!box) {
    box = document.createElement("div");
    box.id = "toast";
    document.body.append(box);
  }
  box.textContent = message;
  box.classList.add("show");
  clearTimeout(box.timer);
  box.timer = setTimeout(() => box.classList.remove("show"), 3500);
}

async function api(path, options = {}) {
  const init = { method: options.method || "GET", headers: {} };
  if (options.body !== undefined) {
    init.body = JSON.stringify(options.body);
    init.headers["Content-Type"] = "application/json";
  }
  const res = await fetch(path, init);
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    const err = new Error(data.error || `Erro ${res.status}`);
    err.status = res.status;
    throw err;
  }
  return data;
}

// Reconecta com atraso crescente para não martelar o ESP32 quando ele reinicia.
function connectSocket(onMessage) {
  let delay = 1000;
  const open = () => {
    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onopen = () => {
      delay = 1000;
      $("link").textContent = "ao vivo";
    };
    ws.onmessage = (ev) => {
      try {
        onMessage(JSON.parse(ev.data));
      } catch (err) {
        console.warn("mensagem inválida", err);
      }
    };
    ws.onclose = () => {
      $("link").textContent = "reconectando...";
      setTimeout(open, delay);
      delay = Math.min(delay * 2, 15000);
    };
  };
  open();
}

const pad = (n) => String(n).padStart(2, "0");

function fmtTime(ts, synced, short = false) {
  if (!synced) return `sem hora (+${ts}s)`;
  const d = new Date(ts * 1000);
  const hm = `${pad(d.getHours())}:${pad(d.getMinutes())}`;
  return short ? hm : `${pad(d.getDate())}/${pad(d.getMonth() + 1)} ${hm}`;
}

function fmtDuration(sec) {
  if (!sec) return "—";
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  if (h > 0) return `${h} h ${m} min`;
  return m > 0 ? `${m} min` : `${sec} s`;
}

const fmtKb = (bytes) => `${(bytes / 1024).toFixed(1)} KB`;

function lineChart(id, datasets, opts = {}) {
  return new Chart($(id), {
    type: opts.type || "line",
    data: { labels: [], datasets },
    options: {
      animation: false,
      responsive: true,
      maintainAspectRatio: false,
      elements: { point: { radius: 0 }, line: { borderWidth: 2, tension: 0.2 } },
      interaction: { mode: "index", intersect: false },
      plugins: { legend: { display: datasets.length > 1 } },
      scales: { x: { ticks: { maxTicksLimit: opts.maxTicks || 8, maxRotation: 0 } }, y: { beginAtZero: opts.beginAtZero !== false, max: opts.max, min: opts.min } },
    },
  });
}

const COLORS = ["#1f6f8b", "#c9622b", "#3f8f5f", "#7a5bb5"];

function setChart(chart, labels, ...series) {
  chart.data.labels = labels;
  series.forEach((data, i) => (chart.data.datasets[i].data = data));
  chart.update();
}

renderNav();
