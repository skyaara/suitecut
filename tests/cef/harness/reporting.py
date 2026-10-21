# SPDX-License-Identifier: MIT
import html
import json
import xml.etree.ElementTree as ET


def atomic_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=True) + '\n')
    temporary.replace(path)


def write_reports(output, report):
    atomic_json(output / 'report.json', report)
    attempts = report['attempts']
    junit = ET.Element('testsuite', name='SuiteCut CEF regressions', tests=str(len(attempts)),
                       failures=str(sum(not r['passed'] for r in attempts)), time=str(report['durationSeconds']))
    for row in attempts:
        case = ET.SubElement(junit, 'testcase', classname=row['kind'],
                             name=f'{row["id"]} [seed={row["seed"]}, repeat={row["repeat"]}]',
                             time=str(row.get('durationSeconds', 0)))
        if not row['passed']:
            ET.SubElement(case, 'failure', message='; '.join(row['errors'])[:2000]).text = json.dumps(row, indent=2)
        ET.SubElement(case, 'system-out').text = row['directory'] + '/stdout.log'
    for error in report.get('globalErrors', []):
        # Global input drift must also make JUnit red, even if individual tests passed.
        case = ET.SubElement(junit, 'testcase', classname='supervisor', name='suite-integrity')
        ET.SubElement(case, 'failure', message=error).text = error
    junit.set('tests', str(len(junit)))
    junit.set('failures', str(len(junit.findall('./testcase/failure'))))
    ET.ElementTree(junit).write(output / 'junit.xml', encoding='utf-8', xml_declaration=True)
    rows = ''.join('<tr><td>' + html.escape(r['id']) + '</td><td>' + str(r['seed']) + '</td><td>' +
                   ('PASS' if r['passed'] else 'FAIL') + '</td><td>' + str(r.get('durationSeconds', 0)) +
                   '</td><td><a href="' + r['directory'] + '/attempt.json">Evidence</a> · <a href="' +
                   r['directory'] + '/stdout.log">stdout</a> · <a href="' + r['directory'] +
                   '/stderr.log">stderr</a><br>' + html.escape('; '.join(r['errors'])) +
                   ('<details><summary>Failure details</summary><pre>'+html.escape(json.dumps(r.get('failureEvidence',[]),indent=2))+
                    html.escape(r.get('stderrTail',''))+'</pre></details>' if not r['passed'] else '') + '</td></tr>' for r in attempts)
    (output / 'report.html').write_text('<!doctype html><meta charset="utf-8"><title>CEF regressions</title>'
        '<style>body{font:15px/1.5 system-ui;margin:32px}table{border-collapse:collapse;width:100%}td,th{border:1px solid #ddd;padding:8px;text-align:left}pre{white-space:pre-wrap}</style>'
        f'<h1>CEF regressions: {"PASS" if report["passed"] else "FAIL"}</h1>'
        '<p>Real CEF, controlled local applications, isolated attempts. No retries replace a failed attempt. '
        'This is finite regression coverage, not proof of every possible interleaving or website.</p>'
        '<p><a href="report.json">JSON report</a> · <a href="junit.xml">JUnit</a> · <a href="inputs-before.json">Pinned inputs</a></p>'
        '<table><tr><th>Scenario</th><th>Seed</th><th>Result</th><th>Seconds</th><th>Evidence</th></tr>' + rows + '</table>'
        '<pre>' + html.escape(json.dumps(report.get('globalErrors', []), indent=2)) + '</pre>')
