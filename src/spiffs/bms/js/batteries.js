/**
 * Battery template utilities for BMS configuration page.
 *
 * Battery data is stored in config.json under "battery_templates" and served
 * via the /bms/config/data endpoint.  This file provides helper functions
 * to build the dropdown and a flat lookup map from that data.
 *
 * To add / modify batteries, edit config.json on the SPIFFS filesystem.
 */

/**
 * Flat lookup map: { id -> { cell_v_min, cell_v_max, current_min, current_max } }.
 * Populated at runtime by initBatteryTemplates().
 */
var BATTERIES = {};

/**
 * Initialise battery data from the templates array received from the server.
 * Builds the flat BATTERIES lookup and populates the <select> dropdown.
 *
 * @param {Array}  groups   - battery_templates array from /bms/config/data JSON
 * @param {string} selectId - ID of the <select> element to populate
 */
function initBatteryTemplates(groups, selectId) {
  if (!Array.isArray(groups)) return;

  // Build flat lookup
  BATTERIES = {};
  groups.forEach(function(group) {
    if (!Array.isArray(group.batteries)) return;
    group.batteries.forEach(function(b) {
      BATTERIES[b.id] = {
        cell_v_min:  b.cell_v_min,
        cell_v_max:  b.cell_v_max,
        current_min: b.current_min,
        current_max: b.current_max
      };
    });
  });

  // Build dropdown options
  var sel = document.getElementById(selectId);
  if (!sel) return;

  groups.forEach(function(group) {
    var optgroup = document.createElement('optgroup');
    optgroup.label = group.label;
    if (!Array.isArray(group.batteries)) return;
    group.batteries.forEach(function(b) {
      var opt = document.createElement('option');
      opt.value = b.id;
      opt.textContent = b.name;
      optgroup.appendChild(opt);
    });
    sel.appendChild(optgroup);
  });
}
