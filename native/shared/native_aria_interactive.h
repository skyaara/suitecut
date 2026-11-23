// SPDX-License-Identifier: MIT
#pragma once
// Generated from the authored formatNativeAriaInteractive in src/native-aria.ts.
// Keep the fixed native and SDK serialization contract identical; regenerate with the
// repository formatter generator after changing that function.
inline constexpr char kSuiteCutNativeAriaInteractive[] =
    R"JS(function formatNativeAriaInteractive(nodes, options = {}) {
    const interactiveRoles = new Set([
        'button',
        'checkbox',
        'combobox',
        'link',
        'listbox',
        'menuitem',
        'menuitemcheckbox',
        'menuitemradio',
        'option',
        'radio',
        'scrollbar',
        'searchbox',
        'slider',
        'spinbutton',
        'switch',
        'tab',
        'textbox',
        'treeitem',
    ]);
    // Containers kept for context when they are named and hold a control.
    const contextRoles = new Set([
        'alertdialog',
        'banner',
        'complementary',
        'contentinfo',
        'dialog',
        'form',
        'grid',
        'iframe',
        'main',
        'menu',
        'menubar',
        'navigation',
        'radiogroup',
        'region',
        'search',
        'tablist',
        'tabpanel',
        'toolbar',
        'tree',
    ]);
    const output = [];
    // A node's text is its own text and its descendants' in document order, with
    // whitespace collapsed. One walk splits the tree into those collapsed pieces
    // and records each node's run of them, so a node's text is a slice of the run
    // instead of a fresh walk of its subtree for every listed control.
    const pieces = [];
    const spans = new Map();
    const piece = (value) => {
        const text = value.replace(/\s+/gu, ' ').trim();
        if (text)
            pieces.push(text);
    };
    const walk = [];
    // A text node's text is its own; its children are indexed as separate roots
    // after the walk, outside every ancestor's run.
    const detached = [];
    const open = (node) => {
        const start = pieces.length;
        if (node.role === 'text') {
            piece(node.text ?? node.name ?? '');
            spans.set(node, { start, end: pieces.length });
            for (const child of node.children ?? [])
                if (typeof child !== 'string' && child !== undefined)
                    detached.push(child);
            return;
        }
        if (node.text)
            piece(node.text);
        walk.push({ node, start, index: 0 });
    };
    const roots = [...nodes];
    for (let next = 0; next < roots.length || detached.length; ++next) {
        if (next >= roots.length)
            roots.push(...detached.splice(0));
        const root = roots[next];
        if (root === undefined)
            continue;
        open(root);
        while (walk.length) {
            const frame = walk.at(-1);
            if (!frame)
                break;
            const children = frame.node.children ?? [];
            if (frame.index < children.length) {
                const child = children[frame.index++];
                if (typeof child === 'string')
                    piece(child);
                else if (child !== undefined)
                    open(child);
                continue;
            }
            walk.pop();
            spans.set(frame.node, { start: frame.start, end: pieces.length });
        }
    }
    const textOf = (node) => {
        const span = spans.get(node);
        return span ? pieces.slice(span.start, span.end).join(' ') : '';
    };
    const kept = (node) => interactiveRoles.has(node.role) ||
        node.editable === true ||
        (node.cursor === 'pointer' && node.ref !== undefined);
    const line = (node, depth) => {
        const name = node.name?.length
            ? node.name
            : node.role === 'heading' || kept(node)
                ? textOf(node)
                : '';
        let text = `${'  '.repeat(depth)}- ${node.role}${name ? ` ${JSON.stringify(name)}` : ''}`;
        for (const key of [
            'checked',
            'disabled',
            'expanded',
            'level',
            'pressed',
            'selected',
        ]) {
            const state = node[key];
            if (state === true)
                text += ` [${key}]`;
            // A collapsed control says so, since an agent may need to expand it first.
            else if (state !== undefined && (state !== false || key === 'expanded'))
                text += ` [${key}=${String(state)}]`;
        }
        if (node.placeholder)
            text += ` [placeholder=${JSON.stringify(node.placeholder)}]`;
        if (options.boxes && node.box)
            text += ` [box=${[node.box.x, node.box.y, node.box.width, node.box.height].join(',')}]`;
        if (node.ref !== undefined)
            text += ` [ref=${node.ref}]`;
        if ((node.role === 'textbox' || node.role === 'searchbox' || node.role === 'combobox') &&
            node.text)
            text += `: ${JSON.stringify(node.text)}`;
        return text;
    };
    const visit = (roots) => {
        const top = [];
        const stack = [];
        const enter = (node, depth, own, control) => stack.push({ node, depth, lines: [], index: 0, own, control });
        const emit = (lines) => {
            const parent = stack.at(-1);
            (parent ? parent.lines : top).push(...lines);
        };
        const depthOf = () => {
            for (let index = stack.length - 1; index >= 0; --index) {
                const frame = stack[index];
                if (frame && frame.depth >= 0)
                    return frame.depth + 1;
            }
            return 0;
        };
        const push = (node) => {
            const inside = stack.at(-1)?.control ?? false;
            // A pointer cursor inside a control is the control's own, not another one.
            const pointerOnly = !interactiveRoles.has(node.role) && node.editable !== true && node.cursor === 'pointer';
            const own = kept(node) && !(inside && pointerOnly);
            const self = own ||
                node.role === 'heading' ||
                (contextRoles.has(node.role) && (!!node.name || node.role === 'dialog'));
            enter(node, self ? depthOf() : -1, own, inside || own);
        };
        for (let index = roots.length - 1; index >= 0; --index) {
            const root = roots[index];
            if (root)
                push(root);
            while (stack.length) {
                const frame = stack.at(-1);
                if (!frame)
                    break;
                const child = frame.node.children?.[frame.index++];
                if (child !== undefined) {
                    if (typeof child !== 'string' && child.role !== 'text')
                        push(child);
                    continue;
                }
                stack.pop();
                const { node, depth, lines, own } = frame;
                if (depth < 0)
                    emit(lines);
                else if (own || node.role === 'heading' || lines.length)
                    emit([line(node, depth), ...lines]);
            }
        }
        return top;
    };
    // Roots are visited last to first above, so restore document order per root.
    for (const root of nodes)
        output.push(...visit([root]));
    return output.join('\n');
})JS";
