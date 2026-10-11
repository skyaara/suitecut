import { browserTest as test } from '../config/browserTest';
test('native browser session capability', async ({ suitecutEngine }) => {
  for (const method of ['Target.attachToBrowserTarget', 'Target.getTargets']) {
    try {console.log(method, await suitecutEngine.source.command('devtools', {method, params:{}}));} catch(e) {console.log(method, String(e));}
  }
});
