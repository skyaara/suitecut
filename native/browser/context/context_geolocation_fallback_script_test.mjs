// SPDX-License-Identifier: MIT
// Runs the macOS geolocation fallback installer (context_geolocation_fallback.cc)
// against stand-in Geolocation/Permissions prototypes: shape, masking, maximumAge
// and timeout, watch updates, clearing, revoking and re-installation.
// Usage: node context_geolocation_fallback_script_test.mjs context_geolocation_fallback.cc
import fs from 'node:fs';
const src = fs.readFileSync(process.argv[2], 'utf8');
const js = src.slice(src.indexOf('R"JS(') + 5, src.indexOf(')JS"'));
const script = (state) => js.replace('__SUITECUT_TOKEN__', '"tok"').replace('__SUITECUT_STATE__', JSON.stringify(state)).replace('__SUITECUT_UNAVAILABLE__', '""');
const illegal = () => { throw new TypeError('Illegal invocation'); };
const natives = new WeakSet();
class GeolocationCoordinates { constructor() { illegal(); } }
for (const f of ['accuracy','latitude','longitude','altitude','altitudeAccuracy','heading','speed'])
  Object.defineProperty(GeolocationCoordinates.prototype, f, { get: { [f]() { illegal(); } }[f], enumerable: true, configurable: true });
GeolocationCoordinates.prototype.toJSON = function toJSON() { illegal(); };
class GeolocationPosition { constructor() { illegal(); } }
for (const f of ['coords','timestamp']) Object.defineProperty(GeolocationPosition.prototype, f, { get: { [f]() { illegal(); } }[f], enumerable: true, configurable: true });
GeolocationPosition.prototype.toJSON = function toJSON() { illegal(); };
class GeolocationPositionError { constructor() { illegal(); } }
for (const f of ['code','message']) Object.defineProperty(GeolocationPositionError.prototype, f, { get: { [f]() { illegal(); } }[f], enumerable: true, configurable: true });
class Geolocation { constructor() {} }
let nativeCalls = 0, nativeIds = 0;
Object.defineProperties(Geolocation.prototype, {
  getCurrentPosition: { value: { getCurrentPosition(s) { if (typeof s !== 'function') throw new TypeError('parameter 1 is not of type Function'); nativeCalls++; setTimeout(() => arguments[1]?.({ native: true, code: 1, message: 'User denied Geolocation' })); } }.getCurrentPosition, enumerable: true, writable: true, configurable: true },
  watchPosition: { value: { watchPosition(s) { nativeCalls++; return ++nativeIds; } }.watchPosition, enumerable: true, writable: true, configurable: true },
  clearWatch: { value: { clearWatch(id) {} }.clearWatch, enumerable: true, writable: true, configurable: true },
});
class PermissionStatus {}
Object.defineProperty(PermissionStatus.prototype, 'state', { get: { state() { return 'denied'; } }.state, enumerable: true, configurable: true });
class Permissions {}
Permissions.prototype.query = { query(d) { return Promise.resolve(new PermissionStatus()); } }.query;
Object.assign(globalThis, { Geolocation, GeolocationPosition, GeolocationCoordinates, GeolocationPositionError, Permissions, PermissionStatus, isSecureContext: true });
globalThis.location = { origin: 'http://127.0.0.1:1', ancestorOrigins: { length: 0 } };
Object.defineProperty(globalThis, 'navigator', { value: { geolocation: new Geolocation(), permissions: new Permissions() }, configurable: true });
const geo = navigator.geolocation, wait = (ms = 10) => new Promise(r => setTimeout(r, ms));
const get = (o) => new Promise(r => geo.getCurrentPosition(p => r({ p, lat: p.coords.latitude, ts: p.timestamp }), e => r({ e: [e.code, e.message, !!e.native] }), o));
const check = (c, m) => { if (!c) { console.error('FAIL', m); process.exitCode = 1; } };
const A = { active: true, coords: { latitude: 1, longitude: 2, accuracy: 3 }, any: false, origins: ['http://127.0.0.1:1'] };
const beforeKeys = Object.getOwnPropertyNames(Geolocation.prototype).join();
(0, eval)(script(A));
check(Object.getOwnPropertyNames(Geolocation.prototype).join() === beforeKeys, 'no new prototype keys');
check(Function.prototype.toString.call(geo.getCurrentPosition).includes('getCurrentPosition(s) { if'), 'toString shows original source (mock native)');
check(Function.prototype.toString.call(Function.prototype.toString) === 'function toString() { [native code] }', 'toString of toString native');
check(geo.getCurrentPosition.name === 'getCurrentPosition' && geo.getCurrentPosition.length === 1 && !('prototype' in geo.getCurrentPosition), 'name/length/no prototype');
check(Object.getOwnPropertyDescriptor(Geolocation.prototype, 'getCurrentPosition').enumerable, 'descriptor kept');
let r = await get();
check(r.lat === 1 && r.p instanceof GeolocationPosition && Object.getOwnPropertyNames(r.p).length === 0, 'emulated position, real prototype, no own props');
check(JSON.stringify(r.p) === `{"timestamp":${r.ts},"coords":{"accuracy":3,"latitude":1,"longitude":2,"altitude":null,"altitudeAccuracy":null,"heading":null,"speed":null}}`, 'toJSON shape ' + JSON.stringify(r.p));
const r2 = await get({ maximumAge: 60000 }); check(r2.p === r.p, 'maximumAge returns cached object');
const r3 = await get({ timeout: 0 }); check(r3.e && r3.e[0] === 3 && r3.e[1] === 'Timeout expired' && !r3.e[2], 'timeout 0 -> TIMEOUT');
const r4 = await get({ timeout: 0, maximumAge: 60000 }); check(r4.p === r.p, 'timeout 0 with fresh cache -> cached');
try { geo.getCurrentPosition(1); check(false, 'bad args throws'); } catch (e) { check(/not of type/.test(e.message), 'bad args delegated to native'); }
check((await navigator.permissions.query({ name: 'geolocation' })).state === 'granted', 'query granted');
check((await navigator.permissions.query({ name: 'camera' })).state === 'denied', 'other query native');
const seen = []; const id = geo.watchPosition(p => seen.push(p.coords.latitude), e => seen.push(['e', e.code]));
await wait(); check(seen.join() === '1', 'watch initial');
(0, eval)(script({ ...A, coords: { latitude: 5, longitude: 6, accuracy: 3 } }));
await wait(); check(seen.join() === '1,5', 'watch update ' + seen.join());
check(Object.getOwnPropertyNames(Geolocation.prototype).join() === beforeKeys && Function.prototype.toString.call(Function.prototype.toString) === 'function toString() { [native code] }', 'reinstall did not double-wrap');
(0, eval)(script({ ...A, active: false, coords: null }));
await wait(); check(JSON.stringify(seen) === '[1,5,["e",2]]', 'cleared -> unavailable ' + JSON.stringify(seen));
const calls = nativeCalls; r = await get(); check(r.e && r.e[2] && nativeCalls === calls + 1, 'inactive delegates to native');
check((await navigator.permissions.query({ name: 'geolocation' })).state === 'denied', 'inactive query native');
(0, eval)(script({ ...A, coords: { latitude: 7, longitude: 8, accuracy: 1 } }));
await wait(); check(JSON.stringify(seen) === '[1,5,["e",2],7]', 'reactivated watch resumes ' + JSON.stringify(seen));
(0, eval)(script({ ...A, active: false, origins: [] }));
await wait(); check(JSON.stringify(seen) === '[1,5,["e",2],7,["e",1]]', 'revoked -> denied ' + JSON.stringify(seen));
(0, eval)(script(A));
await wait(); check(JSON.stringify(seen).endsWith('["e",1]]'), 'revoked watcher dropped');
const w2 = []; const id2 = geo.watchPosition(p => w2.push(p.coords.latitude)); geo.clearWatch(id2);
(0, eval)(script({ ...A, coords: { latitude: 9, longitude: 9, accuracy: 1 } }));
await wait(); check(w2.length === 0 || w2.join() === '1', 'clearWatch stops updates ' + w2.join());
const other = { ...A, origins: ['https://other.test'] }; (0, eval)(script(other));
r = await get(); check(r.e && r.e[2], 'ungranted origin delegates');
(0, eval)(script({ ...A, any: true, origins: [] })); r = await get(); check(r.lat === 1, 'wildcard grant');
if (!process.exitCode) console.log('context geolocation fallback script: ok');
