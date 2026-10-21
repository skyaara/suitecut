// SPDX-License-Identifier: MIT
#pragma once
// Generated from the authored formatNativeAriaJSON in src/native-aria.ts.
// Keep the fixed native and SDK serialization contract identical; regenerate with the
// repository formatter generator after changing that function.
inline constexpr char kSuiteCutNativeAriaJSON[] = R"JS(function formatNativeAriaJSON(nodes, options = {}) {
    const result = [];
    const pending = [{ values: nodes, output: result, depth: 0 }];
    let visited = 0;
    while (pending.length) {
        const item = pending.pop();
        if (!item)
            break;
        if (item.depth > 64)
            throw new Error('Native ARIA JSON depth limit exceeded');
        for (const actual of item.values) {
            if (++visited > 4096)
                throw new Error('Native ARIA JSON node limit exceeded');
            if (actual.role === 'text') {
                const text = actual.text ?? actual.name ?? '';
                if (item.depth)
                    item.output.push(text);
                else
                    item.output.push({ role: 'text', text });
                continue;
            }
            const node = { role: actual.role };
            if (actual.name)
                node.name = actual.name;
            for (const key of [
                'checked',
                'disabled',
                'expanded',
                'active',
                'invalid',
                'level',
                'pressed',
                'selected',
            ]) {
                if (key === 'level' && actual.role !== 'heading')
                    continue;
                const state = key === 'invalid' && actual.states?.invalid === true && actual.invalidToken
                    ? actual.invalidToken
                    : actual.states?.[key];
                if (state === undefined)
                    continue;
                const valid = key === 'level'
                    ? typeof state === 'number' && Number.isInteger(state) && state >= 1
                    : key === 'checked' || key === 'pressed'
                        ? typeof state === 'boolean' || state === 'mixed'
                        : key === 'invalid'
                            ? typeof state === 'boolean' || state === 'grammar' || state === 'spelling'
                            : typeof state === 'boolean';
                if (!valid)
                    throw new Error('Invalid native ARIA JSON state value');
                Object.defineProperty(node, key, {
                    value: state,
                    enumerable: true,
                    configurable: true,
                    writable: true,
                });
            }
            for (const key of ['url', 'placeholder'])
                if (actual.properties?.[key] !== undefined)
                    node[key] = actual.properties[key];
            if (options.boxes) {
                if (actual.box?.length !== 4 ||
                    actual.box.some((value) => !Number.isFinite(value)) ||
                    actual.box[2] < 0 ||
                    actual.box[3] < 0)
                    throw new Error('Native ARIA JSON actual box is unavailable');
                node.box = {
                    x: actual.box[0],
                    y: actual.box[1],
                    width: actual.box[2],
                    height: actual.box[3],
                };
            }
            if (options.mode === 'ai' && actual.ref !== undefined) {
                if (!/^r[A-Za-z0-9]{1,64}_[1-9][0-9]*_(?:0|[1-9][0-9]*)$/u.test(actual.ref) ||
                    actual.ref.length > 192)
                    throw new Error('Invalid native ARIA JSON reference token');
                node.ref = actual.ref;
            }
            if (options.mode === 'ai' && actual.cursor === 'pointer')
                node.cursor = actual.cursor;
            if (options.mode === 'ai') {
                if (actual.inputType !== undefined)
                    node.inputType = actual.inputType;
                for (const key of ['readOnly', 'editable'])
                    if (actual[key] !== undefined)
                        node[key] = actual[key];
            }
            let children = actual.children ?? [];
            let text = actual.text && actual.text !== actual.name ? actual.text : undefined;
            if (options.mode === 'ai' && actual.inputType === 'password') {
                children = [];
                text = undefined;
            }
            if (children.length === 1 && children[0]?.role === 'text' && !actual.text) {
                const childText = children[0].text ?? children[0].name ?? '';
                if (childText !== actual.name)
                    text = childText;
                children = [];
            }
            if (text !== undefined)
                node.text = text;
            item.output.push(node);
            if (children.length && (options.depth === undefined || item.depth < options.depth)) {
                node.children = [];
                pending.push({ values: children, output: node.children, depth: item.depth + 1 });
            }
        }
    }
    if (new TextEncoder().encode(JSON.stringify(result)).byteLength > 65_536)
        throw new Error('Native ARIA JSON exceeds 64 KiB');
    return result;
})JS";
