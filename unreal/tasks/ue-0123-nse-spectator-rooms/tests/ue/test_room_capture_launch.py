import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from room_network_workers import WorkerGroup


class CaptureLaunch(unittest.TestCase):
    def test_only_named_capture_client_gets_inherited_render_flags(self):
        with tempfile.TemporaryDirectory() as temp, patch("room_network_workers.subprocess.Popen"):
            group = WorkerGroup(Path("/unused/editor"), Path("/unused/project"), Path(temp) / "workers",
                                editor_args=["-d3d11", "-sm5", "-AllowSoftwareRendering"],
                                extra_args=["-NullRHI"], rendering_roles={"client4"})
            try:
                group._launch("client4", "127.0.0.1")
                group._launch("client3", "127.0.0.1")
                group._launch("server", "/Engine/Maps/Entry", server=True)
                target = json.loads((group.directory / "launch-client4.json").read_text())
                peer = json.loads((group.directory / "launch-client3.json").read_text())
                server = json.loads((group.directory / "launch-server.json").read_text())
                self.assertNotIn("-NullRHI", target)
                self.assertIn("-d3d11", target)
                self.assertIn("-AllowSoftwareRendering", target)
                self.assertIn("-NullRHI", peer)
                self.assertNotIn("-d3d11", peer)
                self.assertIn("-server", server)
                self.assertIn("-NullRHI", server)
            finally:
                for log in group.logs:
                    log.close()


if __name__ == "__main__":
    unittest.main()
