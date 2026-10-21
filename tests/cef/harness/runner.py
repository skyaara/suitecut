# SPDX-License-Identifier: MIT
"""Isolated real-CEF qualification. See docs/cef-regressions.md."""
from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import signal
import subprocess
import sys
import time

from .paths import ROOT, SUITE
from .contracts import require
from .catalogue import catalogue, selected
from .evidence import pin_inputs, sha, validate_reports
from .reporting import atomic_json, write_reports
from .supervisor import STOP, supervised


def run_case(case, seed, attempt, args, output):
    identity = f'{case["id"]}--seed-{seed}--repeat-{attempt}'
    directory = output / 'cases' / identity
    directory.mkdir(parents=True)
    if STOP.is_set():
        row = dict(id=case['id'], kind=case['kind'], seed=seed, repeat=attempt,
                   directory=str(directory.relative_to(output)), passed=False,
                   errors=['Interrupted before launch; scenario was not run'])
        atomic_json(directory / 'attempt.json', row)
        (directory / 'stdout.log').touch()
        (directory / 'stderr.log').touch()
        return row
    work = directory / 'work'
    work.mkdir()
    artifacts = directory / 'artifacts'
    artifacts.mkdir()
    # Legacy proofs have local output roots relative to cwd. Only immutable source
    # directories are linked; .suitecut outputs never point at the main checkout.
    for name in ('dist', 'native', 'assets', 'scripts'):
        (work / name).symlink_to(ROOT / name, target_is_directory=True)
    env = dict(os.environ, SUITECUT_NATIVE_EXECUTABLE=str(args.engine),
               SUITECUT_NATIVE_SDK_DIRECTORY=str(args.sdk))
    for dependency in ('ffmpeg','ffprobe'):
        if getattr(args,dependency,None):
            env['SUITECUT_'+dependency.upper()]=str(getattr(args,dependency))
    env.update(case.get('env', {}))
    if case['kind'] == 'journey':
        command = [args.node, str(SUITE / 'harness/worker.mjs'), '--case', case['id'], '--seed', str(seed),
                   '--engine', str(args.engine), '--sdk', str(args.sdk), '--output', str(directory)]
        for dependency in case.get('requires', []):
            command += ['--'+dependency, str(getattr(args,dependency))]
    elif case['kind'] == 'proof':
        script = ROOT / case['script']
        for variable in re.findall(r'process\.env\.(SUITECUT_[A-Z_]+)', script.read_text()):
            if variable.endswith(('_DIRECTORY', '_OUTPUT')) and 'SDK' not in variable:
                env[variable] = str(artifacts)
            elif variable.endswith('_REPORT'):
                env[variable] = str(artifacts / 'report.json')
        command = [args.node, str(script)] + case.get('args', [])
    else:
        if not args.client_build:
            raise ValueError('Full suite C++ cases require --client-build; use --profile journeys without a C++ build')
        binary = args.client_build / case['binary']
        if case['id'] == 'cpp.decoded-media':
            command=[sys.executable,str(SUITE/'harness/native_media.py'),'--client',str(binary),'--engine',str(args.engine),
                     '--ffmpeg',str(args.ffmpeg),'--ffprobe',str(args.ffprobe),'--output',str(artifacts)]
        elif case['id'] == 'cpp.facade-lifecycle':
            command = [str(binary), '--browser', str(args.engine), '--fixture', str(ROOT / 'native/client/tools/fixture.html'),
                       '--iterations', '30', '--cycles', '50', '--output', str(artifacts / 'results.json')]
        elif case['id'] == 'cpp.features-network':
            command = [str(binary), '--backend', 'cef', '--browser', str(args.engine), '--output', str(artifacts)]
        else:
            command = [str(binary), str(args.engine), str(artifacts)]
    atomic_json(directory / 'invocation.json', {'case': case, 'seed': seed, 'repeat': attempt, 'command': command})
    row = dict(id=case['id'], kind=case['kind'], seed=seed, repeat=attempt,
               directory=str(directory.relative_to(output)), passed=False, errors=[])
    try:
        row.update(supervised(command, work, directory, env, args.timeout or case['timeout']))
        if row['passed']:
            row['evidencePredicatesVerified'] = validate_reports(directory, case, seed)
    except Exception as error:
        row['errors'].append(f'Evidence or invocation failure: {error}')
        row['passed'] = False
    if not row['passed']:
        evidence_files = [directory / 'result.json']
        evidence_files += list(artifacts.rglob('*.json'))[:30]
        evidence_files += list((work / '.suitecut').rglob('failure.json'))[:10]
        evidence_files += list((work / '.suitecut').rglob('report.json'))[:30]
        details=[]
        for path in evidence_files:
            if not path.is_file() or path.stat().st_size > 2*1024*1024:
                continue
            try:
                value=json.loads(path.read_text())
                if isinstance(value,dict):
                    for key in ('error','errors','cleanupErrors','failures','diagnosticFailure'):
                        if value.get(key):
                            details.append({'file':str(path.relative_to(directory)),key:value[key]})
            except (ValueError,OSError):
                pass
        row['failureEvidence']=details
        stderr=directory/'stderr.log'
        if stderr.exists():
            with stderr.open('rb') as stream:
                stream.seek(max(0,stderr.stat().st_size-8192))
                row['stderrTail']=stream.read().decode(errors='replace')
    if row['errors']:
        row['passed'] = False
    atomic_json(directory / 'attempt.json', row)
    return row


def main(argv=None):
    STOP.clear()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--sdk', type=Path, default=ROOT / 'dist')
    parser.add_argument('--client-build', type=Path)
    parser.add_argument('--node', default=shutil.which('node') or 'node')
    parser.add_argument('--ffmpeg', type=Path, default=shutil.which('ffmpeg'))
    parser.add_argument('--ffprobe', type=Path, default=shutil.which('ffprobe'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--profile', choices=['smoke', 'journeys', 'races', 'proofs', 'full'], default='full')
    parser.add_argument('--case', action='append', default=[], help='Case ID/glob; repeat option to select multiple')
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--repeat', type=int, default=1, help='Every attempt counts; repetition is not retry')
    parser.add_argument('--workers', type=int, choices=range(1,5), default=1)
    parser.add_argument('--timeout', type=int, help='Override per-attempt hard deadline in seconds')
    parser.add_argument('--shard', default='1/1', help='Stable 1-based index/count, e.g. 2/4')
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args(argv)
    if os.name != 'posix':
        parser.error('This supervisor requires POSIX sessions (macOS/Linux)')
    if not (0 < args.seed < 2**31 and 0 < args.repeat <= 100 and (args.timeout is None or args.timeout > 0)):
        parser.error('Use a positive seed, repeat 1–100 and a positive timeout')
    try:
        index, total = map(int, args.shard.split('/'))
        require(1 <= index <= total <= 256, 'Invalid shard bounds')
    except ValueError:
        parser.error('--shard must be index/count with 1 <= index <= count <= 256')
    try:
        cases = selected(catalogue(args.node), args.profile, args.case)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    if not cases:
        parser.error('Selection matched zero scenarios')
    if args.list:
        for case in cases:
            print(f'{case["id"]:48} {case["kind"]:8} {case["timeout"]:4}s {",".join(case["tags"])}')
        return 0
    if not args.engine or not args.engine.is_file():
        parser.error('--engine must name the actual compiled CEF executable')
    args.engine = args.engine.resolve(); args.sdk = args.sdk.resolve()
    if not (args.sdk / 'native.js').is_file():
        parser.error('Built SDK missing; run pnpm build or pass --sdk')
    for dependency in {d for c in cases for d in c.get('requires', [])}:
        binary=getattr(args,dependency)
        if not binary or not Path(binary).is_file():
            parser.error(f'Selected scenario requires --{dependency} or {dependency} on PATH')
        setattr(args,dependency,Path(binary).resolve())
    if any(c['kind'] == 'proof' for c in cases) and args.sdk != ROOT / 'dist':
        parser.error('Existing proofs import repository dist; full/proofs profiles require --sdk to be repository dist')
    if any(c['kind'] == 'cpp' for c in cases):
        if not args.client_build:
            parser.error('C++ scenarios require --client-build (or select --profile journeys)')
        args.client_build = args.client_build.resolve()
        for case in cases:
            if case['kind'] == 'cpp' and not (args.client_build / case['binary']).is_file():
                parser.error('Missing compiled C++ test: ' + case['binary'])
    output = (args.output or ROOT / '.suitecut/cef-regressions' / (dt.datetime.now().strftime('%Y%m%d-%H%M%S') + f'-{os.getpid()}')).resolve()
    output.mkdir(parents=True, exist_ok=False)
    inputs = pin_inputs(args, cases)
    atomic_json(output / 'inputs-before.json', inputs)
    jobs = [(c, args.seed + repeat, repeat + 1) for c in cases for repeat in range(args.repeat)]
    planned = len(jobs)
    jobs = [job for job in jobs if int(hashlib.sha256(f'{job[0]["id"]}:{job[1]}:{job[2]}'.encode()).hexdigest(),16) % total == index-1]
    if not jobs:
        parser.error('Shard matched zero attempts; no green empty runs')
    atomic_json(output / 'plan.json', [dict(id=c['id'], seed=s, repeat=r) for c,s,r in jobs])
    random.Random(args.seed).shuffle(jobs)
    report = dict(schemaVersion=2, passed=False, started=dt.datetime.now(dt.timezone.utc).isoformat(),
                  engine=str(args.engine), sdk=str(args.sdk), profile=args.profile, workers=args.workers,
                  seed=args.seed, repeat=args.repeat, shard=args.shard, totalUnshardedAttempts=planned,
                  plannedAttempts=len(jobs), selectedCases=[{k:v for k,v in c.items() if k != 'run'} for c in cases],
                  host={'platform':sys.platform,'cpuCount':os.cpu_count(),'loadAverageAtStart':os.getloadavg()},
                  attempts=[], globalErrors=[], evidenceBoundary='Finite local real-browser regressions; no exhaustive interleaving, public bot-detector, max-FPS or portability claim.')
    try:
        report['gitHead'] = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
        report['gitStatus'] = subprocess.check_output(['git','status','--short'],cwd=ROOT,text=True)
    except subprocess.SubprocessError:
        pass
    started = time.monotonic()
    print(f'Running {len(jobs)} attempts with {args.workers} bounded worker(s): {output}',flush=True)
    def interrupted(_signum, _frame):
        STOP.set()
    old_handlers = {sig:signal.signal(sig, interrupted) for sig in (signal.SIGINT,signal.SIGTERM)}
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            futures = {pool.submit(run_case,c,s,r,args,output):(c,s,r) for c,s,r in jobs}
            for future in concurrent.futures.as_completed(futures):
                c,s,r = futures[future]
                try:
                    row=future.result()
                except Exception as error:
                    row=dict(id=c['id'],kind=c['kind'],seed=s,repeat=r,passed=False,errors=[str(error)],directory='cases/'+f'{c["id"]}--seed-{s}--repeat-{r}')
                report['attempts'].append(row)
                print(f'{"PASS" if row["passed"] else "FAIL"} {row["id"]} seed={s} repeat={r} '+ '; '.join(row['errors']),flush=True)
                report['durationSeconds']=round(time.monotonic()-started,3)
                write_reports(output,report)
    finally:
        for sig,handler in old_handlers.items():
            signal.signal(sig,handler)
        if STOP.is_set():
            report['globalErrors'].append('Suite interrupted')
        try:
            after=pin_inputs(args, cases)
        except Exception as error:
            after={}
            report['globalErrors'].append('Final input inventory could not be verified: '+str(error))
        atomic_json(output / 'inputs-after.json',after)
        if after != inputs:
            report['globalErrors'].append('Pinned browser, SDK, test source or C++ binary changed during run')
        report['inputsUnchanged']=after==inputs
        report['attempts'].sort(key=lambda r:(r['id'],r['seed'],r['repeat']))
        report['durationSeconds']=round(time.monotonic()-started,3)
        report['finished']=dt.datetime.now(dt.timezone.utc).isoformat()
        report['host']['loadAverageAtFinish']=os.getloadavg()
        expected={(c['id'],s,r) for c,s,r in jobs}
        actual=[(r['id'],r['seed'],r['repeat']) for r in report['attempts']]
        if len(actual) != len(expected) or set(actual) != expected:
            report['globalErrors'].append('Attempt identities do not exactly match the pinned execution plan')
        report['passed']=bool(actual) and all(r['passed'] is True for r in report['attempts']) and not report['globalErrors']
        write_reports(output,report)
    print(f'{"PASS" if report["passed"] else "FAIL"}: {sum(r["passed"] for r in report["attempts"])}/{len(jobs)} attempts; {output / "report.html"}',flush=True)
    return 0 if report['passed'] else 1
