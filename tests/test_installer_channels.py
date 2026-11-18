import importlib.util
from pathlib import Path
import unittest

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('make_site', root/'web/make_site.py')
site = importlib.util.module_from_spec(spec)
spec.loader.exec_module(site)

class InstallerChannels(unittest.TestCase):
    def test_only_two_public_references(self):
        entries = [{'version':v} for v in ['V10.7.23','V10.7.53','v0.9.9-par12-527c7e4','V10.7.52']]
        self.assertEqual([e['version'] for e in site.installable_versions(entries,False)],
                         ['V10.7.23','v0.9.9-par12-527c7e4'])

    def test_new_public_release_fails_closed(self):
        with self.assertRaises(SystemExit):
            site.installable_versions([{'version':'V10.7.54'}],False)

    def test_beta_keeps_candidate(self):
        entries=[{'version':'V10.7.54-abcdef0-preview','status':'beta'}]
        self.assertEqual(site.installable_versions(entries,True),entries)

    def test_all_par14_variants_blocked_even_if_mislabeled(self):
        add = {'version':'v0.9.9-par10d-beta2-42947e9-preview','edition':'ADDITIVE + CONTROLS','status':'beta'}
        for suffix in ['beta1-86b85ad-preview','beta2-4be8592-preview','beta4-4d3156d-preview','future-1234567']:
            bad = {'version':'v0.9.9-par14-'+suffix,'edition':'SAMPLE','status':'public'}
            self.assertEqual(site.installable_versions([bad,add], True), [add])

    def test_withdrawn_only_fails_closed(self):
        with self.assertRaises(SystemExit):
            site.installable_versions([{'version':'v0.9.9-par14-beta4','edition':'SAMPLE'}],True)

    def test_beta_still_forbidden_in_public(self):
        with self.assertRaises(SystemExit):
            site.installable_versions([{'version':'v0.9.9-par10d-beta2','edition':'ADDITIVE','status':'beta'}],False)

if __name__ == '__main__':
    unittest.main()
