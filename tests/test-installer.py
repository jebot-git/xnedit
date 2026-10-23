import importlib.util,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('installer',Path(__file__).resolve().parents[1]/'scripts/install-system.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class Defaults(unittest.TestCase):
 def test_preserves_other_roles_and_sections(self):
  before='# personal\n[Default Applications]\ntext/html=browser.desktop;\ntext/plain=org.kde.kate.desktop;\nimage/png=viewer.desktop;\n[Added Associations]\ntext/plain=other.desktop;\n'
  after=module.merge_mimes(before,['text/plain','application/json']);self.assertIn('text/html=browser.desktop;',after);self.assertIn('image/png=viewer.desktop;',after);self.assertIn('[Added Associations]\ntext/plain=other.desktop;',after);self.assertIn('text/plain=org.domed.XNEdit.desktop;',after);self.assertIn('application/json=org.domed.XNEdit.desktop;',after);self.assertEqual(module.merge_mimes(after,['text/plain','application/json']),after)
 def test_new_file(self):self.assertIn('[Default Applications]\ntext/plain=org.domed.XNEdit.desktop;',module.merge_mimes('',['text/plain']))
if __name__=='__main__':unittest.main()
