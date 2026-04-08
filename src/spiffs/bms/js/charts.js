/// Reference to pack voltage chart instance
let packChart = null;
/// Object mapping cell index to its Chart instance
const cellCharts = {};
/// Reference to current chart instance
let iChart = null;
/// Runtime configuration (fetched once at startup)
let bmsCfg = { num_cells: 5, current_enable: true };

/// Client-side history buffer (circular array, max 60 seconds of data)
const HISTORY_MAX_SEC = 60;
const HISTORY_POLL_MS = 1000;
const HISTORY_CAPACITY = Math.ceil((HISTORY_MAX_SEC * 1000) / HISTORY_POLL_MS);
let history = [];

/// Helper – create a Chart.js dataset descriptor.
function ds(label, data, color) {
  const o = { label, data, borderWidth: 1.5, pointRadius: 0 };
  if (color) { o.borderColor = color; o.backgroundColor = color; }
  return o;
}

/// Predefined colours for avg / min / max lines.
const AVG_COLOR = "rgba(54,162,235,1)";
const MIN_COLOR = "rgba(255,159,64,1)";
const MAX_COLOR = "rgba(75,192,192,1)";

// ── Pack-level voltage ──────────────────────────────────────────────

/// Build data object for the pack voltage chart.
function buildPackVoltage(hist) {
  return {
    labels: hist.map(s => s.timestamp),
    datasets: [
      ds("avg", hist.map(s => s.pack_v_avg), AVG_COLOR),
    ]
  };
}

// ── Per-cell voltage ────────────────────────────────────────────────

/// Build data object for one cell chart.
function buildCellVoltage(hist, idx) {
  return {
    labels: hist.map(s => s.timestamp),
    datasets: [
      ds("avg", hist.map(s => s.cell_v_avg[idx]), AVG_COLOR),
    ]
  };
}

// ── Current ─────────────────────────────────────────────────────────

/// Build data object for the current chart.
function buildCurrent(hist) {
  return {
    labels: hist.map(s => s.timestamp),
    datasets: [
      ds("avg", hist.map(s => s.pack_i_avg), AVG_COLOR),
    ]
  };
}

// ── Checkbox helpers ────────────────────────────────────────────────

/// Toggle visibility of every cell chart.
function toggleAllCells(checked) {
  const nc = bmsCfg.num_cells;
  for (let i = 0; i < nc; i++) {
    const cb = document.getElementById("cbCell" + i);
    if (cb) cb.checked = checked;
    const wrap = document.getElementById("cellWrap" + i);
    if (wrap) wrap.style.display = checked ? "" : "none";
  }
}

/// Toggle visibility of a single cell chart.
function toggleCell(idx, checked) {
  const wrap = document.getElementById("cellWrap" + idx);
  if (wrap) wrap.style.display = checked ? "" : "none";
  // Sync "Select all" checkbox
  const all = document.getElementById("cbSelectAll");
  if (!all) return;
  const nc = bmsCfg.num_cells;
  let allChecked = true;
  for (let i = 0; i < nc; i++) {
    const cb = document.getElementById("cbCell" + i);
    if (cb && !cb.checked) { allChecked = false; break; }
  }
  all.checked = allChecked;
}

// ── DOM scaffolding ─────────────────────────────────────────────────

/// Create per-cell checkboxes and chart containers.
function buildCellUI() {
  const nc = bmsCfg.num_cells;
  const cbList = document.getElementById("cellCbList");
  const container = document.getElementById("cellChartsContainer");
  if (!cbList || !container) return;

  cbList.innerHTML = "";
  container.innerHTML = "";

  for (let i = 0; i < nc; i++) {
    // Checkbox
    const lbl = document.createElement("label");
    lbl.style.cssText = "cursor:pointer;white-space:nowrap;";
    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.id = "cbCell" + i;
    cb.checked = true;
    cb.style.cssText = "width:auto;margin-right:4px;";
    cb.onchange = function () { toggleCell(i, this.checked); };
    lbl.appendChild(cb);
    lbl.appendChild(document.createTextNode("Cell " + (i + 1)));
    cbList.appendChild(lbl);

    // Chart wrapper
    const wrap = document.createElement("div");
    wrap.id = "cellWrap" + i;
    wrap.className = "chart-container";
    wrap.style.marginBottom = "0";

    const title = document.createElement("h3");
    title.className = "chart-title";
    title.textContent = "\uD83D\uDD0B Cell " + (i + 1);
    wrap.appendChild(title);

    const canvas = document.createElement("canvas");
    canvas.id = "cellChart" + i;
    wrap.appendChild(canvas);

    container.appendChild(wrap);
  }
}

// ── Common chart options ────────────────────────────────────────────

const CHART_OPTS = {
  animation: false,
  responsive: true,
  plugins: {
    legend: { labels: { boxWidth: 12, padding: 8 } }
  },
  scales: {
    x: { display: false }
  }
};

// ── Refresh loop ────────────────────────────────────────────────────

/// Fetch the latest single sample and append it to the client-side history buffer.
/// Then update all charts from the local history.
async function refresh() {
  let sample;
  try {
    const r = await fetch("/bms/stats/data");
    sample = await r.json();
  } catch (e) {
    console.error("Stats fetch failed", e);
    return;
  }

  // Append valid sample to client-side history buffer
  if (sample !== null && typeof sample === "object") {
    history.push(sample);
    // Trim history to keep only the last HISTORY_CAPACITY entries
    if (history.length > HISTORY_CAPACITY) {
      history = history.slice(history.length - HISTORY_CAPACITY);
    }
  }

  // Data-points indicator
  const dpEl = document.getElementById("dataPoints");
  if (dpEl) dpEl.textContent = history.length;

  // ── Pack voltage chart ──
  const pv = buildPackVoltage(history);
  if (!packChart) {
    const ctx = document.getElementById("pack-vchart");
    if (ctx) {
      packChart = new Chart(ctx, { type: "line", data: pv, options: CHART_OPTS });
    }
  } else {
    packChart.data = pv;
    packChart.update("none");
  }

  // ── Per-cell charts ──
  const nc = bmsCfg.num_cells;
  for (let i = 0; i < nc; i++) {
    const cv = buildCellVoltage(history, i);
    if (!cellCharts[i]) {
      const ctx = document.getElementById("cellChart" + i);
      if (ctx) {
        cellCharts[i] = new Chart(ctx, { type: "line", data: cv, options: CHART_OPTS });
      }
    } else {
      cellCharts[i].data = cv;
      cellCharts[i].update("none");
    }
  }

  // ── Current chart ──
  const iContainer = document.getElementById("ichart-container");
  if (bmsCfg.current_enable) {
    if (iContainer) iContainer.style.display = "";
    const ci = buildCurrent(history);
    if (!iChart) {
      const ctx = document.getElementById("ichart");
      if (ctx) {
        iChart = new Chart(ctx, { type: "line", data: ci, options: CHART_OPTS });
      }
    } else {
      iChart.data = ci;
      iChart.update("none");
    }
  } else {
    if (iContainer) iContainer.style.display = "none";
  }
}

// ── Initialisation ──────────────────────────────────────────────────

/// Load runtime config, build UI, start refresh loop.
async function init() {
  try {
    const r = await fetch("/bms/config/data");
    const cfg = await r.json();
    bmsCfg.num_cells = cfg.battery.num_cells || 5;
    bmsCfg.current_enable = cfg.battery.current_enable !== false;
  } catch (e) {
    console.error("Failed to load config, using defaults", e);
  }

  buildCellUI();
  refresh();
  setInterval(refresh, HISTORY_POLL_MS);
}

init();
