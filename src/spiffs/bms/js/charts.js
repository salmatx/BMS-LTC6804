/// Reference to chart instances for voltage
let vChart = null;
/// Reference to chart instances for current
let iChart = null;

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

  for (let c = 0; c < 5; c++) {
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
  const i = buildCurrent(history);

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
}

refresh();
setInterval(refresh, 1000);
