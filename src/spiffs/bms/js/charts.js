/// Reference to chart instances for voltage
let vChart = null;
/// Reference to chart instances for current
let iChart = null;
/// Runtime configuration (fetched once at startup)
let bmsCfg = { num_cells: 5, current_enable: true };

/// Helper function to create a dataset object for Chart.js.
///
/// \param label Dataset label
/// \param data Array of data points
/// \return Dataset object
function ds(label, data) {
  return { label, data, borderWidth: 1, pointRadius: 0 };
}

/// This function builds the voltage chart data from the history.
///
/// \param history Array of statistics history objects
/// \return Chart.js data object
function buildVoltage(history) {
  const labels = history.map(s => s.timestamp);

  const datasets = [];
  datasets.push(ds("pack_v_avg", history.map(s => s.pack_v_avg)));
  datasets.push(ds("pack_v_min", history.map(s => s.pack_v_min)));
  datasets.push(ds("pack_v_max", history.map(s => s.pack_v_max)));

  const nc = bmsCfg.num_cells;
  for (let c = 0; c < nc; c++) {
    datasets.push(ds(`cell${c+1}_v_avg`, history.map(s => s.cell_v_avg[c])));
    datasets.push(ds(`cell${c+1}_v_min`, history.map(s => s.cell_v_min[c])));
    datasets.push(ds(`cell${c+1}_v_max`, history.map(s => s.cell_v_max[c])));
  }

  return { labels, datasets };
}

/// This function builds the current chart data from the history.
///
/// \param history Array of statistics history objects
/// \return Chart.js data object
function buildCurrent(history) {
  const labels = history.map(s => s.timestamp);
  return {
    labels,
    datasets: [
      ds("pack_i_avg", history.map(s => s.pack_i_avg)),
      ds("pack_i_min", history.map(s => s.pack_i_min)),
      ds("pack_i_max", history.map(s => s.pack_i_max)),
    ]
  };
}

/// This function refreshes the charts by fetching new data and updating the Chart.js instances.
/// It is called periodically to keep the charts up to date.
///
/// \param None
/// \return None
async function refresh() {
  const r = await fetch("/bms/stats/data");
  const history = await r.json();

  const v = buildVoltage(history);

  if (!vChart) {
    vChart = new Chart(document.getElementById("vchart"), {
      type: "line",
      data: v,
      options: { animation: false, responsive: true }
    });
  } else {
    vChart.data = v;
    vChart.update("none");
  }

  // Current chart: only create/update if current measurement is enabled
  const iContainer = document.getElementById("ichart-container");
  if (bmsCfg.current_enable) {
    if (iContainer) iContainer.style.display = '';
    const i = buildCurrent(history);
    if (!iChart) {
      iChart = new Chart(document.getElementById("ichart"), {
        type: "line",
        data: i,
        options: { animation: false, responsive: true }
      });
    } else {
      iChart.data = i;
      iChart.update("none");
    }
  } else {
    if (iContainer) iContainer.style.display = 'none';
  }
}

/// This function loads runtime configuration and starts the chart refresh loop.
async function init() {
  try {
    const r = await fetch("/bms/config/data");
    const cfg = await r.json();
    bmsCfg.num_cells = cfg.battery.num_cells || 5;
    bmsCfg.current_enable = cfg.battery.current_enable !== false;
  } catch (e) {
    console.error("Failed to load config, using defaults", e);
  }
  refresh();
  setInterval(refresh, 1000);
}

init();
