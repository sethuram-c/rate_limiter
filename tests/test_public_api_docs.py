"""Phase 31: fail when public docs drift away from installed API contracts."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parent.parent
REF = ROOT / 'docs/public_api_reference.md'
GUIDE = ROOT / 'docs/developer_integration_guide.md'
HEADERS = [ROOT / 'include' / name for name in (
    'ratelimiter.h', 'rate_version.h', 'rate_config.h', 'rate_service.h',
    'request_gateway.h', 'request_policy.h')]

class PublicApiDocumentationTests(unittest.TestCase):
    def test_reference_files_exist_and_name_real_symbols(self):
        self.assertTrue(REF.is_file())
        reference = REF.read_text(encoding='utf-8')
        declarations = '\n'.join(p.read_text(encoding='utf-8') for p in HEADERS)
        names = (
            'rl_version_string', 'rate_config_plan',
            'rate_service_init_with_config', 'rate_service_init_monotonic',
            'rate_service_check', 'rate_service_check_detailed',
            'rate_service_get_stats', 'rate_service_destroy',
            'request_gateway_handle', 'request_gateway_handle_n',
            'request_policy_add_rule', 'request_policy_handle',
        )
        for name in names:
            with self.subTest(symbol=name):
                self.assertIn(name + '(', declarations)
                self.assertIn(name + '(', reference)

    def test_guide_links_exist_and_public_api_example_is_real(self):
        self.assertTrue(GUIDE.is_file())
        text = GUIDE.read_text(encoding='utf-8')
        for link in re.findall(r'\]\(([^)]+\.md|\.\./examples/[^)]+\.c)\)', text):
            with self.subTest(link=link):
                self.assertTrue((GUIDE.parent / link).is_file(), link)
        self.assertTrue((ROOT / 'examples/public_api_walkthrough.c').is_file())
        self.assertIn('make docs-test', text)
        self.assertIn('ratelimiter.h', text)

    def test_caveats_are_explicit(self):
        ref = REF.read_text(encoding='utf-8').lower()
        guide = GUIDE.read_text(encoding='utf-8').lower()
        for term in ('not a stable binary abi', 'thread', 'not transactional',
                     'not a network server', 'proxy'):
            with self.subTest(term=term):
                self.assertIn(term, ref)
        for term in ('not thread-safe', 'destdir', 'source', 'unreleased'):
            with self.subTest(term=term):
                self.assertIn(term, guide)

    def test_documented_version_and_examples(self):
        version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        example = (ROOT / 'examples/public_api_walkthrough.c').read_text(encoding='utf-8')
        guide = GUIDE.read_text(encoding='utf-8')
        reference = REF.read_text(encoding='utf-8')
        self.assertIn(f'version={version}', guide)
        self.assertIn(version, reference)
        self.assertIn('request_gateway_handle_n(', example)
        self.assertIn('rate_service_init_with_config(', example)
        self.assertIn('rate_service_destroy(', example)

if __name__ == '__main__':
    unittest.main()
