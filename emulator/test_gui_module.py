import unittest


class GuiModuleTests(unittest.TestCase):
    def test_gui_module_imports_without_constructing_a_window(self):
        from .ovd_gui import OVDGui
        from .ovd_vnc import VNCViewer
        self.assertTrue(issubclass(OVDGui, object))
        self.assertTrue(issubclass(VNCViewer, object))


if __name__ == "__main__": unittest.main()
