// SPDX-License-Identifier: MIT
#pragma once
// Generated from the authored formatNativeAriaTree in src/native-aria.ts.
// Keep the fixed native and SDK serialization contract identical; regenerate with the
// repository formatter generator after changing that function.
inline constexpr char kSuiteCutNativeAriaFormatter[] = R"JS(function formatNativeAriaTree(nodes, options = {}) {
    const output = [];
    let visited = 0, bytes = 0;
    const scalar = (value, property = false) => /:(?:\s|$)|[\n\r[\]{}]|^[,'"#&*!>|%@`-]/u.test(value) ||
        /^(?:true|false|yes|no|y|n|on|off|null|~|[-+]?\d+(?:\.\d+)?(?:e[-+]?\d+)?|\.inf|\.nan)$/iu.test(value) ||
        (!property && value.startsWith('/')) ||
        !value.trim()
        ? JSON.stringify(value)
        : value;
    const append = (line) => {
        bytes += line.length * 2 + 2;
        if (bytes > 65536)
            throw new Error('Native ARIA snapshot exceeds 64 KiB');
        output.push(line);
    };
    const stack = nodes.map((node) => ({ node, depth: 0 })).reverse();
    while (stack.length) {
        const item = stack.pop();
        if (!item)
            break;
        if (++visited > 4096 || item.depth > 64)
            throw new Error('Native ARIA snapshot traversal exceeds bounds');
        const { node, depth } = item;
        const indent = '  '.repeat(depth);
        if (node.role === 'text') {
            append(`${indent}- text: ${scalar(node.text ?? node.name ?? '')}`);
            continue;
        }
        // Keep full names available for matching; bound repeated YAML name previews.
        const namePreview = node.name && node.name.length <= 1000 ? node.name : undefined;
        let line = `${indent}- ${node.role}${namePreview ? ` ${JSON.stringify(namePreview)}` : ''}`;
        for (const key of [
            'checked',
            'disabled',
            'expanded',
            'invalid',
            'level',
            'pressed',
            'selected',
        ]) {
            if (key === 'level' && node.role !== 'heading')
                continue;
            const rawState = node.states?.[key];
            const state = key === 'invalid' && rawState === true && node.invalidToken ? node.invalidToken : rawState;
            if (state === true)
                line += ` [${key}]`;
            else if (state !== undefined && state !== false)
                line += ` [${key}=${String(state)}]`;
        }
        if (options.boxes) {
            if (node.box?.length !== 4 || node.box.some((value) => !Number.isFinite(value)))
                throw new Error('Native accessibility snapshot box is unavailable');
            line += ` [box=${node.box.join(',')}]`;
        }
        if (options.mode === 'ai' && node.ref !== undefined) {
            if (!/^r[A-Za-z0-9]{1,64}_[1-9][0-9]*_(?:0|[1-9][0-9]*)$/u.test(node.ref) ||
                node.ref.length > 192)
                throw new Error('Invalid native ARIA reference token');
            line += ` [ref=${node.ref}]`;
        }
        const properties = Object.entries(node.properties ?? {});
        let children = node.children ?? [];
        let text = node.text && node.text !== node.name ? node.text : undefined;
        if (children.length === 1 && children[0]?.role === 'text' && !node.text) {
            const value = children[0].text ?? children[0].name ?? '';
            if (value !== node.name)
                text = value;
            children = [];
        }
        if (text !== undefined) {
            if (children.length || properties.length)
                children = [{ role: 'text', text }, ...children];
            else
                line += `: ${scalar(text)}`;
        }
        if (depth >= (options.depth ?? 64))
            children = [];
        if (children.length || properties.length)
            line += ':';
        append(line);
        for (const [key, value] of properties)
            append(`${indent}  - /${key}: ${scalar(value, true)}`);
        for (let index = children.length - 1; index >= 0; --index) {
            const child = children[index];
            if (child)
                stack.push({ node: child, depth: depth + 1 });
        }
    }
    return output.join('\n');
})JS";
