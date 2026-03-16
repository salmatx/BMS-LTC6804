#!/usr/bin/env python3
"""Parse LTC6804 telemetry CSV data into a human-readable log file."""

import csv

# ESP32 reset reason mapping (from esp_reset_reason_t)
RESET_REASONS = {
    0: "UNKNOWN",
    1: "POWERON",
    2: "EXTERNAL_PIN",
    3: "SOFTWARE",
    4: "PANIC",
    5: "INT_WDT (Interrupt Watchdog)",
    6: "TASK_WDT (Task Watchdog)",
    7: "WDT (Other Watchdog)",
    8: "DEEPSLEEP",
    9: "BROWNOUT",
    10: "SDIO",
}


def parse_soc(raw: int) -> str:
    voltage = raw * 0.0001 * 20
    return f"{voltage:.4f} V"


def parse_itmp(raw: int) -> str:
    temp_c = raw * 0.0001 / 0.0075 - 273
    return f"{temp_c:.2f} °C"


def parse_analog_voltage(raw: int) -> str:
    voltage = raw * 0.0001
    return f"{voltage:.4f} V"


def parse_cell_flags(raw: int) -> str:
    lines = []
    for cell in range(1, 13):
        bit_offset = (cell - 1) * 2
        uv = (raw >> bit_offset) & 1
        ov = (raw >> (bit_offset + 1)) & 1
        flags = []
        if uv:
            flags.append("UV")
        if ov:
            flags.append("OV")
        status = ", ".join(flags) if flags else "OK"
        lines.append(f"    Cell {cell:2d}: {status}")
    return "\n".join(lines)


def parse_diag(raw: int) -> str:
    thsd = (raw >> 0) & 1
    muxfail = (raw >> 1) & 1
    rev = (raw >> 4) & 0x0F
    parts = [
        f"    THSD:    {'YES' if thsd else 'NO'} (Thermal Shutdown)",
        f"    MUXFAIL: {'YES' if muxfail else 'NO'} (MUX Self-Test)",
        f"    REV:     {rev} (Die Revision)",
    ]
    return "\n".join(parts)


def parse_reset_reason(raw: int) -> str:
    return RESET_REASONS.get(raw, f"UNKNOWN ({raw})")


def parse_row(row: dict) -> str:
    time_str = row["_time"]
    soc = int(row["telemetry_ltc_soc"])
    itmp = int(row["telemetry_ltc_itmp"])
    va = int(row["telemetry_ltc_va"])
    vd = int(row["telemetry_ltc_vd"])
    cell_flags = int(row["telemetry_ltc_cell_flags"])
    diag = int(row["telemetry_ltc_diag"])
    reset_reason = int(row["telemetry_reset_reason"])

    block = (
        f"Time: {time_str}\n"
        f"  SOC (Sum of Cells):  {soc} -> {parse_soc(soc)}\n"
        f"  ITMP (Internal Temp): {itmp} -> {parse_itmp(itmp)}\n"
        f"  VA (Analog Supply):  {va} -> {parse_analog_voltage(va)}\n"
        f"  VD (Digital Supply): {vd} -> {parse_analog_voltage(vd)}\n"
        f"  Cell Flags - Undervoltage/Overvoltage (0x{cell_flags:06X}):\n"
        f"{parse_cell_flags(cell_flags)}\n"
        f"  Diagnostics (0x{diag:02X}):\n"
        f"{parse_diag(diag)}\n"
        f"  Reset Reason: {reset_reason} -> {parse_reset_reason(reset_reason)}\n"
    )
    return block


def parse_file(path: str) -> str:
    """Parse a CSV file and return the formatted output as a string."""
    with open(path, newline="", encoding="utf-8") as csvfile:
        reader = csv.DictReader(csvfile)
        blocks = [parse_row(row) for row in reader]
    separator = "-" * 60 + "\n"
    return separator.join(blocks)
