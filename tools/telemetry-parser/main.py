"""Entry point for the LTC6804 Telemetry Parser GUI."""

from tkinterdnd2 import TkinterDnD

from gui import TelemetryParserGUI


def main():
    root = TkinterDnD.Tk()
    TelemetryParserGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
