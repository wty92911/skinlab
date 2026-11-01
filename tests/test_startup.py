import unittest
from unittest.mock import patch,MagicMock
from skinlab.startup import run


class StartupTests(unittest.TestCase):
    def test_normal_launch_requests_uac_before_creating_app(self):
        with patch('skinlab.startup.is_admin',return_value=False),patch('skinlab.startup.elevate_launcher') as elevate,patch('skinlab.compact_gui.CompactApp') as app:
            run();elevate.assert_called_once();app.assert_not_called()

    def test_admin_launch_opens_compact_app_without_second_elevation(self):
        with patch('skinlab.startup.is_admin',return_value=True),patch('skinlab.startup.elevate_launcher') as elevate,patch('skinlab.compact_gui.CompactApp') as app:
            run();elevate.assert_not_called();app.return_value.mainloop.assert_called_once()


if __name__=='__main__':unittest.main()
