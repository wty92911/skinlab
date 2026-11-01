"""Normal user entry: request UAC once, then start the compact controller."""
from skinlab.launcher import is_admin,elevate_launcher


def run():
    if not is_admin():
        elevate_launcher()
        return
    from skinlab.compact_gui import CompactApp
    CompactApp().mainloop()
