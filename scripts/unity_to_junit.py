#!/usr/bin/env python3
"""
Convert Unity test-runner output to JUnit XML.

Usage (pipe):
    ./build/test_host.elf | python3 scripts/unity_to_junit.py > results.xml

Usage (file redirect):
    python3 scripts/unity_to_junit.py < unity-output.txt > results.xml

Exit code:
    0 — at least one result parsed and XML written
    1 — no test results found in input (likely a build problem)
"""

import re
import sys
import xml.etree.ElementTree as ET

# Unity result line format:
#   FILE:LINE:TEST_NAME:(PASS|FAIL|IGNORE)[: message]
# FILE may be a bare name or a relative path; no colons on Linux.
_RESULT_RE = re.compile(
    r'^([^:]+):(\d+):([^:]+):(PASS|FAIL|IGNORE)(?::(.*))?$'
)


def _parse(text):
    tests = []
    for line in text.splitlines():
        m = _RESULT_RE.match(line.strip())
        if m:
            tests.append({
                'file':    m.group(1),
                'line':    m.group(2),
                'name':    m.group(3),
                'result':  m.group(4),
                'message': (m.group(5) or '').strip(),
            })
    return tests


def _to_junit(tests, suite_name='HostTests'):
    n_fail = sum(1 for t in tests if t['result'] == 'FAIL')
    n_skip = sum(1 for t in tests if t['result'] == 'IGNORE')

    suite = ET.Element('testsuite', {
        'name':     suite_name,
        'tests':    str(len(tests)),
        'failures': str(n_fail),
        'skipped':  str(n_skip),
        'errors':   '0',
    })

    for t in tests:
        # classname: convert path separators to dots so reporters group by module
        classname = t['file'].replace('/', '.').replace('\\', '.')
        case = ET.SubElement(suite, 'testcase', {
            'name':      t['name'],
            'classname': classname,
        })
        if t['result'] == 'FAIL':
            fail = ET.SubElement(case, 'failure', {'message': t['message']})
            fail.text = '{}:{}: {}'.format(t['file'], t['line'], t['message'])
        elif t['result'] == 'IGNORE':
            skip = ET.SubElement(case, 'skipped')
            if t['message']:
                skip.text = t['message']

    root = ET.Element('testsuites')
    root.append(suite)
    return ET.ElementTree(root)


def main():
    text = sys.stdin.read()
    tests = _parse(text)

    if not tests:
        print('unity_to_junit.py: no test results found in input', file=sys.stderr)
        return 1

    tree = _to_junit(tests)
    if hasattr(ET, 'indent'):   # Python >= 3.9
        ET.indent(tree, space='  ')
    tree.write(sys.stdout, encoding='unicode', xml_declaration=True)
    print()  # trailing newline
    return 0


if __name__ == '__main__':
    sys.exit(main())
