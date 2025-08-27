# === File: main.py ===
import tkinter as tk
from map_manager_main import MapManagerApp
from asset_manager_main import AssetOrganizerApp

try:
    import screeninfo
except ImportError:
    raise ImportError("You need to install 'screeninfo': pip install screeninfo")


def launch_on_monitor(app_class, screen, title=""):
    win = app_class()
    if isinstance(win, tk.Toplevel):
        win.title(title or app_class.__name__)
        win.geometry(f"+{screen.x}+{screen.y}")
    return win


if __name__ == "__main__":
    root = tk.Tk()
    root.withdraw()

    monitors = screeninfo.get_monitors()
    if len(monitors) < 2:
        # Single monitor setup — launch both on same screen
        primary = monitors[0]
        launch_on_monitor(MapManagerApp, primary, "Map Manager")
        launch_on_monitor(AssetOrganizerApp, primary, "Asset Organizer")
    else:
        # Dual monitor setup — MapManager on primary, AssetOrganizer on secondary
        primary = monitors[0]
        secondary = monitors[1]
        launch_on_monitor(MapManagerApp, primary, "Map Manager")
        launch_on_monitor(AssetOrganizerApp, secondary, "Asset Organizer")

    root.mainloop()
