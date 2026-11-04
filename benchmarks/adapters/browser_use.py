# SPDX-License-Identifier: MIT
# One cold browser-use session: python -I benchmarks/adapters/browser_use.py <config.json>
# Drives browser-use's BrowserSession directly, without an LLM: the snapshot is
# the text browser-use gives its model (dom_state.llm_representation()), and
# actions are the same events its tools dispatch.
import asyncio
import json
import os
import re
import sys
import time
from importlib.metadata import version

os.environ.setdefault('ANONYMIZED_TELEMETRY', 'false')
os.environ.setdefault('BROWSER_USE_CLOUD_SYNC', 'false')

from browser_use.browser.events import ClickElementEvent, SendKeysEvent, TypeTextEvent
from browser_use.browser.profile import BrowserProfile
from browser_use.browser.session import BrowserSession

# RETRY_MS in common.mjs: how long a step waits before its retry.
RETRY_MS = 500
# ACTION_MS in common.mjs: how long one action may take before the step retries.
ACTION_MS = 5000

config = json.load(open(sys.argv[1]))
chrome = os.environ.get('CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')


def now():
    return time.perf_counter() * 1000


def median(values):
    return sorted(values)[len(values) // 2]


async def thrice(call):
    times, last = [], None
    for _ in range(3):
        start = now()
        last = await call()
        times.append(now() - start)
    return {'ms': median(times), 'all': times}, last


async def main():
    result = {'tool': 'browser-use', 'version': version('browser-use'), 'sites': [], 'tasks': []}
    start = now()
    # Extensions are off so every tool loads pages as they are; browser-use
    # enables a cookie-banner blocker by default.
    profile = BrowserProfile(
        headless=True,
        executable_path=chrome,
        enable_default_extensions=False,
        user_data_dir=None,
        keep_alive=False,
        viewport={'width': 1280, 'height': 720},
        device_scale_factor=1,
    )
    session = BrowserSession(browser_profile=profile)
    state = {}

    async def snapshot():
        summary = await session.get_browser_state_summary(include_screenshot=False)
        state['map'] = summary.dom_state.selector_map
        return summary.dom_state.llm_representation()

    def candidates(_text):
        found = []
        for index, node in state['map'].items():
            role = (node.ax_node.role if node.ax_node else None) or node.tag_name
            name = (node.ax_node.name if node.ax_node else None) or node.get_all_children_text()
            found.append({'role': role, 'name': (name or '').strip(), 'ref': index})
        return found

    async def wait_for_url(pattern):
        # waitForURL in common.mjs: the page URL must match within 20 s.
        expected = re.compile(pattern)
        deadline = now() + 20000
        current = ''
        while now() < deadline:
            current = await session.get_current_page_url()
            if expected.search(current):
                return
            await asyncio.sleep(0.1)
        raise RuntimeError(f'URL did not reach {pattern}; last {current}')

    async def dispatch(event):
        async def run():
            sent = session.event_bus.dispatch(event)
            await sent
            await sent.event_result(raise_if_any=True, raise_if_none=False)

        # The same action timeout every adapter uses (ACTION_MS in common.mjs).
        await asyncio.wait_for(run(), ACTION_MS / 1000)

    try:
        await session.start()
        result['launchMs'] = now() - start
        for url in config['sites']:
            site = {'url': url, 'host': url.split('/')[2]}
            try:
                start = now()
                await session.navigate_to(url)
                site['loadMs'] = now() - start
                await asyncio.sleep(1.5)
                timing, text = await thrice(snapshot)
                site['snapshot'] = {**timing, 'chars': len(text), 'refs': len(state['map'])}
                open(f"{config['out']}-{site['host']}.snapshot.txt", 'w').write(text)
                timing, png = await thrice(lambda: session.take_screenshot(full_page=False))
                site['screenshot'] = {**timing, 'bytes': len(png)}
            except Exception as error:
                site['error'] = str(error)[:500]
            result['sites'].append(site)
        for task in config['tasks']:
            entry = {'name': task['name'], 'ok': False, 'snapshots': 0, 'chars': 0, 'snapshotMs': 0, 'retries': 0, 'retryErrors': []}
            start = now()
            try:
                await session.navigate_to(task['url'])
                for step in task['steps']:
                    if 'press' in step:
                        await dispatch(SendKeysEvent(keys=step['press']))
                        if step.get('until'):
                            await wait_for_url(step['until'])
                        continue
                    target = step.get('click') or step.get('fill')
                    role = re.compile(f"^(?:{target['role']})$")
                    name = re.compile(target['name'], re.I)
                    # One retry on a fresh snapshot, as in play() in common.mjs.
                    for attempt in range(2):
                        read = now()
                        text = await snapshot()
                        entry['snapshotMs'] += now() - read
                        entry['snapshots'] += 1
                        entry['chars'] += len(text)
                        found = next(
                            (c for c in candidates(text) if role.search(c['role']) and name.search(c['name'])),
                            None,
                        )
                        try:
                            if not found:
                                raise RuntimeError(f"No {target['role']} named /{target['name']}/ in the snapshot")
                            node = state['map'][found['ref']]
                            if 'click' in step:
                                await dispatch(ClickElementEvent(node=node))
                            else:
                                await dispatch(TypeTextEvent(node=node, text=step['text']))
                            if step.get('until'):
                                await wait_for_url(step['until'])
                            break
                        except Exception as error:
                            if attempt:
                                raise
                            entry['retries'] += 1
                            entry['retryErrors'].append(str(error).split('\n')[0])
                            await asyncio.sleep(RETRY_MS / 1000)
                await wait_for_url(task['done'])
                entry['ok'] = True
            except Exception as error:
                entry['error'] = str(error)[:500]
            entry['ms'] = now() - start
            result['tasks'].append(entry)
    finally:
        start = now()
        await session.kill()
        result['closeMs'] = now() - start
    print(json.dumps(result))


asyncio.run(main())
