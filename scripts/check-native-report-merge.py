#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent decoding and mutation controls for real native report merging.

Run artifacts are retained. The report fixtures disclose their controlled attempt
DTOs; the attachment fixture actually launches and retires its native file writer.
"""
import argparse
import errno
import html.parser
import json
import os
from pathlib import Path
import subprocess
import uuid
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', required=True)
    parser.add_argument('--report-fixture', required=True)
    parser.add_argument('--attachment-fixture', required=True)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    out = args.directory.resolve() / ('run-' + uuid.uuid4().hex)
    out.mkdir(parents=True)
    subprocess.run([args.report_fixture, str(out / 'original')], check=True)
    subprocess.run([args.attachment_fixture, str(out / 'artifact')], check=True)
    original_path = out / 'original' / 'results.json'
    unrun_path = out / 'original' / 'unrun.json'
    attachment_path = out / 'artifact' / 'attachment.json'
    original_bytes = original_path.read_bytes()
    attachment_bytes = attachment_path.read_bytes()
    original = json.loads(original_bytes)
    attachment = json.loads(attachment_bytes)
    artifact = Path(attachment['attachments'][0]['path'])
    artifact_bytes = artifact.read_bytes()
    artifact_stat = artifact.stat()
    assert artifact_bytes == bytes([0, 1, 128, 255])
    saved_inputs = {path: path.read_bytes() for path in (original_path, unrun_path, attachment_path)}
    checked = []

    def call(extra, status=0):
        result = subprocess.run([args.executable, *map(str, extra)], capture_output=True, text=True)
        assert result.returncode == status, (extra, result.returncode, result.stdout, result.stderr)
        return result

    merged, junit, page = (out / name for name in ('merged.json', 'merged.xml', 'merged.html'))
    command = ['--output', merged, '--junit', junit, '--html', page, '--expected-inputs', '2',
               '--input', 'shard-one', original_path, '--input', 'shard-two', attachment_path]
    call(command)
    combined = json.loads(merged.read_bytes())
    assert combined['kind'] == 'suitecut-native-merged-report'
    assert combined['sources'][0]['report'] == original
    assert combined['sources'][1]['report'] == attachment
    assert original_bytes in merged.read_bytes() and attachment_bytes in merged.read_bytes()
    assert combined['summary']['tests'] == 7 and combined['summary']['attempts'] == 8
    assert combined['summary']['runFailed'] and combined['summary']['failedSources'] == 1
    assert combined['sources'][1]['runFailed'] is False
    assert {'workerFailure', 'workerExitUnknown', 'interruptedAttempt', 'globalErrors', 'unexpectedTests'} <= set(combined['sources'][0]['runFailureReasons'])
    tests = combined['tests']
    assert len({(row['sourceId'], row['testId']) for row in tests}) == len(tests)
    retry = next(row for row in tests if row['testId'] == '18446744073709551614')
    assert retry['outcome'] == 'flaky' and retry['attemptIndices'] == [0, 1]
    assert combined['sources'][0]['report']['attempts'][0]['title'].endswith('\ud800 \x00')
    assert combined['sources'][0]['report']['workers'][0]['stdout']['data'] == '00ff3c3e260d0a'
    availability = combined['sources'][1]['artifactAvailability'][0]
    assert availability['availability'] == 'matchesRecordedIdentity'
    assert availability['path'] == str(artifact)
    checked.append('verbatim_sources_unique_local_namespaces_attempts_UTF16_raw_bytes_real_artifact')

    xml = ET.parse(junit).getroot()
    assert xml.tag == 'testsuites' and int(xml.attrib['tests']) == 8
    suites = xml.findall('testsuite')
    assert [suite.attrib['name'] for suite in suites] == ['shard-one', 'shard-two']
    for suite, expected in zip(suites, [original_bytes, attachment_bytes]):
        suite_props = {item.attrib['name']: item.attrib['value'] for item in suite.find('properties')}
        assert suite_props['native.originalReportJSON'].encode() == expected
    assert 'Committed artifact history' in page.read_text()
    assert 'Current attachment view selections remain in each original report' in page.read_text()
    assert sum(len(suite.findall('testcase')) for suite in suites) == 8
    for attr, tag in [('errors', 'error'), ('failures', 'failure'), ('skipped', 'skipped')]:
        assert int(xml.attrib[attr]) == sum(len(case.findall(tag)) for suite in suites for case in suite.findall('testcase'))
    retry_case = next(case for case in suites[0].findall('testcase') if case.attrib['name'].endswith('[18446744073709551614]'))
    props = {item.attrib['name']: item.attrib['value'] for item in retry_case.find('properties')}
    assert json.loads(props['native.attempt0.originalJSON']) == original['attempts'][0]
    assert json.loads(props['native.attempt1.originalJSON']) == original['attempts'][1]
    assert not retry_case.findall('failure')
    assert '00ff3c3e260d0a' in page.read_text()
    class HTMLTags(html.parser.HTMLParser):
        def __init__(self):
            super().__init__()
            self.tags = []
        def handle_starttag(self, tag, attrs):
            self.tags.append(tag)
    document = HTMLTags()
    document.feed(page.read_text())
    assert 'script' not in document.tags and 'iframe' not in document.tags
    assert 'Content-Security-Policy' in page.read_text()
    checked.append('independent_JUnit_XML_final_outcomes_retry_properties_escaped_standalone_HTML')

    hostile = json.loads(attachment_bytes)
    hostile['attempts'][0]['title'] = '</pre><script>authored text only</script>\ufffe'
    hostile_path = out / 'hostile-display.json'
    hostile_path.write_text(json.dumps(hostile, ensure_ascii=True).replace('\\ufffe', '\ufffe'))
    call(['--output', out / 'hostile.json', '--junit', out / 'hostile.xml', '--html', out / 'hostile.html',
          '--input', 'hostile display', hostile_path])
    hostile_xml = ET.parse(out / 'hostile.xml').getroot()
    case_props = {item.attrib['name']: item.attrib['value'] for item in hostile_xml.find('testsuite').find('testcase').find('properties')}
    assert int(case_props['native.titleXmlSanitizedUnits']) == 1
    original_property = bytes.fromhex(case_props['native.attempt0.originalJSON.originalPropertyJSONUTF8Hex']).decode()
    assert json.loads(json.loads(original_property)) == hostile['attempts'][0]
    suite_props = {item.attrib['name']: item.attrib['value'] for item in hostile_xml.find('testsuite').find('properties')}
    original_report = bytes.fromhex(suite_props['native.originalReportJSON.originalPropertyJSONUTF8Hex']).decode()
    assert json.loads(original_report).encode() == hostile_path.read_bytes()
    hostile_doc = HTMLTags()
    hostile_doc.feed((out / 'hostile.html').read_text())
    assert 'script' not in hostile_doc.tags
    assert json.loads((out / 'hostile.json').read_text())['sources'][0]['report'] == hostile
    checked.append('hostile_HTML_escaping_and_XML_invalid_scalar_exact_hex_fallback')

    # Native annotation JSON preserves original UTF16 hex spelling as well as
    # code units; uppercase is a valid native transport spelling.
    annotation_bytes = attachment_bytes.replace(b'"annotations":[]',
        b'"annotations":[{"type":"fail","description":"\\u0041\\u00AF","location":{"file":"\\u0078","line":1,"column":1}}]', 1)
    assert annotation_bytes != attachment_bytes
    annotation_path = out / 'annotation-spelling.json'
    annotation_path.write_bytes(annotation_bytes)
    call(['--output', out / 'annotation-merged.json', '--junit', out / 'annotation.xml',
          '--input', 'annotation spelling', annotation_path])
    assert annotation_bytes in (out / 'annotation-merged.json').read_bytes()
    assert json.loads((out / 'annotation-merged.json').read_text())['sources'][0]['report'] == json.loads(annotation_bytes)
    checked.append('native_modifier_UTF16_hex_case_and_callsite_retained')

    # A version-1 schema extension is opaque to the merger. This controlled
    # empty projection introduces no mutation, publication or execution receipt;
    # the genuine committed artifact remains historical evidence, independently
    # of an empty current selection.
    extended = json.loads(attachment_bytes)
    owner_keys = ['testId', 'generation', 'workerIndex', 'pid', 'callbackToken', 'scopeId', 'scopeGeneration', 'callbackKind']
    projection = {key: extended['attachments'][0][key] for key in owner_keys}
    projection.update(revision='0', slots=[], changes=[], rejections=[], publicationReceipts=[])
    extended['attachmentViews'] = [projection]
    extended['source'] = {'label': 'opaque metadata only', 'nested': [None, '\udfff', {'future': 'preserve'}]}
    extended['shard'] = {'current': 1, 'total': 7}
    extended_path = out / 'controlled-schema-extension.json'
    extended_path.write_text(json.dumps(extended, ensure_ascii=True, indent=2) + '\n')
    call(['--output', out / 'extended.json', '--junit', out / 'extended.xml', '--html', out / 'extended.html',
          '--input', 'future schema', extended_path])
    extended_result = json.loads((out / 'extended.json').read_text())
    assert extended_result['sources'][0]['report'] == extended
    assert extended_result['sources'][0]['report']['attachmentViews'][0]['slots'] == []
    assert extended_result['sources'][0]['report']['attachments'] == attachment['attachments']
    extension_props = {item.attrib['name']: item.attrib['value'] for item in ET.parse(out / 'extended.xml').getroot().find('testsuite').find('properties')}
    assert extension_props['native.originalReportJSON'].encode() == extended_path.read_bytes()
    assert json.loads(extension_props['native.originalReportJSON']) == extended
    extended_html = (out / 'extended.html').read_text()
    assert 'Committed artifact history' in extended_html
    assert 'Current attachment view selections remain in each original report' in extended_html
    assert 'attachmentViews' in extended_html and 'publicationReceipts' in extended_html
    checked.append('opaque_source_shard_attachment_view_extensions_and_committed_history_preserved_all_formats')

    # Same local test ID, generation and worker index across inputs remain separate.
    collision = json.loads(attachment_bytes)
    collision_path = out / 'collision.json'
    collision_path.write_text(json.dumps(collision, ensure_ascii=True))
    call(['--output', out / 'collision-merged.json', '--input', 'a', attachment_path, '--input', 'b', collision_path])
    rows = json.loads((out / 'collision-merged.json').read_text())['tests']
    assert [(row['sourceId'], row['testId']) for row in rows] == [('a', '1'), ('b', '1')]
    checked.append('colliding_local_identity_preserved')

    # Explicitly controlled serial-replay DTO: native policy keeps the earlier
    # expected member result after a later replay fails, rather than taking the
    # last attempt indiscriminately.
    serial = json.loads(attachment_bytes)
    serial['attachments'] = []
    serial['workers'] = []
    for index in (1, 2):
        worker = json.loads(json.dumps(attachment['workers'][0]))
        worker.update(workerIndex=index, pid=100 + index)
        serial['workers'].append(worker)
    serial['attempts'] = []
    for test_id, generation, retry_number, worker_index, expected in [(1, 1, 0, 1, True), (2, 2, 0, 1, False), (1, 3, 1, 2, False)]:
        attempt = json.loads(json.dumps(attachment['attempts'][0]))
        attempt.update(testId=str(test_id), generation=str(generation), retry=retry_number,
                       workerIndex=worker_index, pid=100 + worker_index, expected=expected,
                       status='passed' if expected else 'failed', title='serial ' + str(test_id),
                       error='' if expected else 'controlled mismatch', durationMs=1)
        serial['attempts'].append(attempt)
    serial['unrun'] = [{'testId': '3', 'title': 'serial omitted', 'reason': 'prior failure'}]
    serial['serialGroups'] = [{'id': '8', 'retries': 1, 'status': 'failed', 'testIds': ['1', '2', '3'], 'omissions': [
        {'testId': '3', 'retry': 0, 'causeTestId': '2', 'causeGeneration': '2'},
        {'testId': '2', 'retry': 1, 'causeTestId': '1', 'causeGeneration': '3'},
        {'testId': '3', 'retry': 1, 'causeTestId': '1', 'causeGeneration': '3'}]}]
    serial['summary'] = dict(tests=3, attempts=3, expected=1, unexpected=1, skipped=0, flaky=1,
                             didNotRun=1, globalErrors=0, totalAttemptDurationMs=3)
    serial_path = out / 'controlled-serial.json'
    serial_path.write_text(json.dumps(serial))
    call(['--output', out / 'serial-merged.json', '--junit', out / 'serial.xml', '--input', 'serial', serial_path])
    serial_result = json.loads((out / 'serial-merged.json').read_text())
    assert next(row for row in serial_result['tests'] if row['testId'] == '1')['outcome'] == 'flaky'
    assert serial_result['summary']['expected'] == 1 and serial_result['summary']['unexpected'] == 1
    serial_xml = ET.parse(out / 'serial.xml').getroot()
    assert not serial_xml.find('testsuite').findall('testcase')[0].findall('failure')
    checked.append('native_serial_replay_summary_uses_actual_wave_policy')

    # Ordinary retries classify the final result; serial replays preserve an
    # interruption from any actual wave. Source-level error cases are counted
    # separately in both reports and cannot silently mask either distinction.
    interrupted = json.loads(json.dumps(serial))
    interrupted.pop('serialGroups')
    interrupted['unrun'] = []
    interrupted['attempts'] = [json.loads(json.dumps(serial['attempts'][0])), json.loads(json.dumps(serial['attempts'][2]))]
    interrupted['attempts'][0].update(status='interrupted', expected=False, error='earlier worker interruption')
    interrupted['attempts'][1].update(status='failed', expected=False, error='final controlled failure')
    interrupted['summary'] = dict(tests=1, attempts=2, expected=0, unexpected=1, skipped=0, flaky=0,
                                  didNotRun=0, globalErrors=0, totalAttemptDurationMs=2)
    ordinary_path = out / 'controlled-ordinary-interrupted-retry.json'
    ordinary_path.write_text(json.dumps(interrupted))
    call(['--output', out / 'ordinary-interrupted.json', '--junit', out / 'ordinary-interrupted.xml',
          '--input', 'ordinary', ordinary_path])
    ordinary_xml = ET.parse(out / 'ordinary-interrupted.xml').getroot()
    ordinary_cases = ordinary_xml.find('testsuite').findall('testcase')
    assert ordinary_xml.attrib['failures'] == '1' and ordinary_xml.attrib['errors'] == '1'
    assert ordinary_cases[0].find('failure') is not None and ordinary_cases[0].find('error') is None
    assert ordinary_cases[1].attrib['name'] == 'Native source run failure'
    assert ordinary_cases[1].find('error') is not None
    checked.append('ordinary_retry_final_failure_differs_from_earlier_interruption_and_source_error')
    interrupted['attempts'][1].update(status='passed', expected=True, error='')
    interrupted['serialGroups'] = [{'id': '1', 'retries': 1, 'status': 'interrupted', 'testIds': ['1'], 'omissions': []}]
    serial_interrupt_path = out / 'controlled-serial-interrupted-replay.json'
    serial_interrupt_path.write_text(json.dumps(interrupted))
    call(['--output', out / 'serial-interrupted.json', '--junit', out / 'serial-interrupted.xml',
          '--input', 'serial interruption', serial_interrupt_path])
    serial_interrupt_xml = ET.parse(out / 'serial-interrupted.xml').getroot()
    assert serial_interrupt_xml.attrib['errors'] == '2' and serial_interrupt_xml.attrib['failures'] == '0'
    serial_interrupt_case = serial_interrupt_xml.find('testsuite').findall('testcase')[0]
    assert serial_interrupt_case.find('error') is not None and serial_interrupt_case.find('failure') is None
    checked.append('serial_replay_interruption_remains_error_after_later_pass')

    # Project names may repeat across independent shards. This is declared
    # report metadata, not evidence that these controlled DTOs executed a shard.
    project = json.loads(attachment_bytes)
    project['projects'] = [{'id': '1', 'name': 'same project name', 'metadata': {'nested': ['\ud800', 42]},
                            'status': 'passed', 'default': True, 'selectionRole': 'primary',
                            'dependenciesIgnored': False, 'selectedTestIds': ['1'], 'testIds': ['1'],
                            'dependencies': [], 'workerIndices': [1]}]
    project_paths = []
    for shard in (1, 2):
        project['shard'] = {'current': shard, 'total': 2}
        path = out / ('project-shard-' + str(shard) + '.json')
        path.write_text(json.dumps(project, ensure_ascii=True))
        project_paths.append(path)
    call(['--output', out / 'projects-merged.json', '--junit', out / 'projects.xml', '--input', 'project-shard-1', project_paths[0],
          '--input', 'project-shard-2', project_paths[1], '--expected-inputs', '2'])
    project_result = json.loads((out / 'projects-merged.json').read_text())
    assert [source['report']['shard']['current'] for source in project_result['sources']] == [1, 2]
    assert [source['report']['projects'][0]['name'] for source in project_result['sources']] == ['same project name'] * 2
    assert project_result['sources'][0]['report']['projects'][0]['metadata']['nested'][0] == '\ud800'
    project_suites = ET.parse(out / 'projects.xml').getroot().findall('testsuite')
    for suite, project_input_path in zip(project_suites, project_paths):
        suite_props = {item.attrib['name']: item.attrib['value'] for item in suite.find('properties')}
        assert suite_props['native.originalReportJSON'].encode() == project_input_path.read_bytes()
    checked.append('same_project_names_shard_evidence_and_opaque_metadata_preserved')

    def rejected(name, path=None, payload=None, extra=None):
        target = out / ('rejected-' + name + '.json')
        target.write_bytes(b'preexisting owned output remains')
        source = path or out / ('input-' + name + '.json')
        if payload is not None:
            source.write_bytes(payload if isinstance(payload, bytes) else json.dumps(payload, ensure_ascii=True).encode())
        call(['--output', target, '--input', 'control', source, *(extra or [])], status=2)
        assert target.read_bytes() == b'preexisting owned output remains'
        checked.append('reject_' + name)

    rejected('missing-input', path=out / 'missing.json')
    rejected('count-mismatch', path=original_path, extra=['--expected-inputs', '2'])
    rejected('same-id', path=original_path, extra=['--input', 'control', attachment_path])
    rejected('same-file', path=original_path, extra=['--input', 'different', original_path])
    hardlink = out / 'input-hardlink.json'
    os.link(original_path, hardlink)
    rejected('same-inode', path=original_path, extra=['--input', 'different', hardlink])
    rejected('truncated', payload=original_bytes[:-5])
    rejected('invalid-UTF8', payload=original_bytes.replace(b'first actual error', b'\xfffirst actual error'))
    rejected('duplicate-key', payload=original_bytes.replace(b'"version":1', b'"version":1,"\\u0076ersion":1'))
    incompatible = json.loads(original_bytes)
    incompatible['version'] = 2
    rejected('incompatible-version', payload=incompatible)
    rejected('merged-input', path=merged)
    corrupt = json.loads(original_bytes)
    corrupt['summary']['attempts'] += 1
    rejected('lying-summary', payload=corrupt)
    corrupt = json.loads(original_bytes)
    corrupt['attempts'][1]['retry'] = 0
    rejected('retry-order', payload=corrupt)
    corrupt = json.loads(original_bytes)
    corrupt['attempts'][1]['generation'] = corrupt['attempts'][0]['generation']
    rejected('generation-identity', payload=corrupt)
    corrupt = json.loads(original_bytes)
    corrupt['workers'][0]['pid'] += 1
    rejected('worker-owner', payload=corrupt)
    corrupt = json.loads(original_bytes)
    corrupt['workers'][0]['stdout']['byteLength'] += 1
    rejected('raw-stream-size', payload=corrupt)
    corrupt = json.loads(attachment_bytes)
    corrupt['attachments'][0]['writerExitConfirmed'] = False
    rejected('invented-commit', payload=corrupt)
    corrupt = json.loads(attachment_bytes)
    corrupt['attachments'][0]['generation'] = '999'
    rejected('foreign-artifact-owner', payload=corrupt)
    corrupt = json.loads(attachment_bytes)
    corrupt['attachments'][0]['id'] = '0'
    rejected('zero-artifact-id', payload=corrupt)

    absent = json.loads(attachment_bytes)
    absent['attachments'][0]['path'] = str(out / 'machine-that-is-not-mounted' / 'artifact.bin')
    absent_path = out / 'absent-artifact.json'
    absent_path.write_text(json.dumps(absent, ensure_ascii=True))
    call(['--output', out / 'absent-merged.json', '--input', 'foreign-machine', absent_path])
    absent_merged = json.loads((out / 'absent-merged.json').read_text())
    assert absent_merged['sources'][0]['artifactAvailability'][0]['availability'] == 'unavailable'
    assert absent_merged['sources'][0]['report'] == absent
    assert absent_merged['summary']['runFailed'] is False
    rejected('absent-artifact-strict', path=absent_path, extra=['--artifacts', 'require'])
    mismatched = json.loads(attachment_bytes)
    mismatched['attachments'][0]['inode'] = str(artifact_stat.st_ino + 1)
    mismatch_path = out / 'mismatch-artifact.json'
    mismatch_path.write_text(json.dumps(mismatched, ensure_ascii=True))
    call(['--output', out / 'mismatch-merged.json', '--input', 'replacement', mismatch_path])
    assert json.loads((out / 'mismatch-merged.json').read_text())['sources'][0]['artifactAvailability'][0]['availability'] == 'identityMismatch'
    rejected('replacement-artifact-strict', path=mismatch_path, extra=['--artifacts', 'require'])
    checked.append('foreign_machine_artifact_absence_explicit_and_original_receipts_preserved')

    # Actual EACCES: the current user cannot traverse an owned directory at
    # merge time. Its reference remains metadata in default mode; output and
    # input path canonicalization errors still reject the operation.
    private_dir = out / 'foreign-inaccessible'
    private_dir.mkdir()
    private_artifact = private_dir / 'same-real-artifact.bin'
    os.link(artifact, private_artifact)
    private_input = private_dir / 'input-report.json'
    os.link(original_path, private_input)
    inaccessible = json.loads(attachment_bytes)
    inaccessible['attachments'][0]['path'] = str(private_artifact)
    inaccessible_path = out / 'inaccessible-artifact.json'
    inaccessible_path.write_text(json.dumps(inaccessible, ensure_ascii=True))
    private_dir.chmod(0)
    try:
        try:
            private_artifact.lstat()
        except PermissionError as error:
            assert error.errno == errno.EACCES
        else:
            raise AssertionError('EACCES control requires an ordinary unprivileged native test user')
        call(['--output', out / 'inaccessible-merged.json', '--junit', out / 'inaccessible.xml',
              '--html', out / 'inaccessible.html', '--input', 'private foreign machine', inaccessible_path])
        inaccessible_result = json.loads((out / 'inaccessible-merged.json').read_text())
        assert inaccessible_result['sources'][0]['artifactAvailability'][0]['availability'] == 'unreadable'
        assert inaccessible_result['sources'][0]['report'] == inaccessible
        assert inaccessible_result['summary']['runFailed'] is False
        rejected('inaccessible-artifact-strict', path=inaccessible_path, extra=['--artifacts', 'require'])
        call(['--output', private_dir / 'blocked-output.json', '--input', 'valid', attachment_path], status=2)
        rejected('inaccessible-input-canonicalization', path=private_input)
    finally:
        private_dir.chmod(0o700)
    assert not (private_dir / 'blocked-output.json').exists()
    assert private_artifact.stat().st_ino == artifact_stat.st_ino and private_artifact.read_bytes() == artifact_bytes
    checked.append('actual_EACCES_artifact_preserved_default_and_input_output_errors_stay_strict')

    # Actual ELOOP in a foreign reference ancestor must also remain explicit
    # availability metadata. No native filesystem exception is swallowed for
    # the actual input or requested output path.
    loop = out / 'foreign-symlink-loop'
    loop.symlink_to(loop.name)
    looping = json.loads(attachment_bytes)
    looping['attachments'][0]['path'] = str(loop / 'artifact.bin')
    looping_path = out / 'looping-artifact.json'
    looping_path.write_text(json.dumps(looping, ensure_ascii=True))
    try:
        (loop / 'artifact.bin').lstat()
    except OSError as error:
        assert error.errno == errno.ELOOP
    else:
        raise AssertionError('Symlink-loop control did not produce actual ELOOP')
    call(['--output', out / 'looping-merged.json', '--junit', out / 'looping.xml', '--html', out / 'looping.html',
          '--input', 'looping foreign machine', looping_path])
    looping_result = json.loads((out / 'looping-merged.json').read_text())
    assert looping_result['sources'][0]['artifactAvailability'][0]['availability'] == 'unreadable'
    assert looping_result['sources'][0]['report'] == looping
    assert looping_result['summary']['runFailed'] is False
    rejected('looping-artifact-strict', path=looping_path, extra=['--artifacts', 'require'])
    call(['--output', loop / 'blocked-output.json', '--input', 'valid', attachment_path], status=2)
    rejected('looping-input-canonicalization', path=loop / 'input-report.json')
    checked.append('actual_ELOOP_artifact_preserved_default_and_input_output_errors_stay_strict')

    artifact_alias = out / 'actual-artifact-hardlink.bin'
    os.link(artifact, artifact_alias)
    call(['--output', artifact_alias, '--input', 'protected alias', attachment_path], status=2)
    assert artifact_alias.read_bytes() == artifact_bytes and artifact_alias.stat().st_ino == artifact_stat.st_ino
    checked.append('actual_committed_artifact_hardlink_output_collision_rejected')

    call(['--output', out / 'strict.json', '--input', 'real', attachment_path, '--artifacts', 'require'])
    call(['--output', out / 'failing-status.json', '--input', 'failure', original_path, '--fail-on-run-failure'], status=1)
    call(['--output', out / 'passing-status.json', '--input', 'success', attachment_path, '--fail-on-run-failure'])
    call(['--output', original_path, '--input', 'protected', original_path], status=2)
    call(['--output', hardlink, '--input', 'protected', original_path], status=2)
    call(['--output', artifact, '--input', 'protected', attachment_path], status=2)
    call(['--output', out / 'same-output', '--html', out / 'same-output', '--input', 'a', original_path], status=2)
    checked.append('exit_status_and_input_artifact_output_overlap_protection')

    call(['--output', out / 'unrun-merged.json', '--junit', out / 'unrun.xml', '--input', 'unrun', unrun_path])
    unrun_result = json.loads((out / 'unrun-merged.json').read_text())
    assert unrun_result['summary']['didNotRun'] == 1
    assert 'unrunTests' in unrun_result['sources'][0]['runFailureReasons']
    assert next(row for row in unrun_result['tests'] if row['testId'] == '77')['attemptIndices'] == []
    checked.append('unrun_declarations_remain_unexecuted_and_fail_source')

    # Actual OS write fault: the destination is a directory, with unrelated proof retained.
    blocked = out / 'blocked-directory'
    blocked.mkdir()
    sentinel = blocked / 'foreign-proof.txt'
    sentinel.write_text('do not remove')
    call(['--output', blocked, '--input', 'a', original_path], status=2)
    assert sentinel.read_text() == 'do not remove'
    assert not list(out.glob('*.tmp'))
    assert all(path.read_bytes() == content for path, content in saved_inputs.items())
    assert artifact.read_bytes() == artifact_bytes and artifact.stat().st_ino == artifact_stat.st_ino
    checked.append('actual_directory_commit_fault_owned_temporary_cleanup_no_source_or_artifact_changes')
    receipt = {'version': 1, 'controls': checked, 'outputs': [str(merged), str(junit), str(page)],
               'note': 'Controlled report DTOs; actual attachment child/writer waitpid and actual filesystem checks.'}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS', len(checked), 'native report merge controls; retained proof', out)


if __name__ == '__main__':
    main()
