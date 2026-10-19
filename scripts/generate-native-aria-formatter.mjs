// SPDX-License-Identifier: MIT
import { readFile, writeFile } from 'node:fs/promises'
import process from 'node:process'

import * as ts from 'typescript'

const [input, output, name = 'formatNativeAriaTree'] = process.argv.slice(2)
if (!['formatNativeAriaTree', 'formatNativeAriaJSON'].includes(name))
  throw new Error('Unsupported native formatter')
if (!input || !output) throw new Error('Usage: generate-formatter INPUT.ts OUTPUT.h')
const source = await readFile(input, 'utf8')
const parsed = ts.createSourceFile(input, source, ts.ScriptTarget.Latest, true)
const declaration = parsed.statements.find(
  (node) => ts.isFunctionDeclaration(node) && node.name?.text === name,
)
if (!declaration || !ts.isFunctionDeclaration(declaration))
  throw new Error('Missing canonical authored formatter')
const compiled = ts
  .transpileModule(declaration.getText(parsed), {
    compilerOptions: { target: ts.ScriptTarget.ES2023, module: ts.ModuleKind.ESNext },
  })
  .outputText.replace(/^export /u, '')
  .trim()
if (compiled.includes(')JS"')) throw new Error('Formatter conflicts with C++ raw literal delimiter')
await writeFile(
  output,
  `// SPDX-License-Identifier: MIT\n#pragma once\n// Generated from the authored ${name} in src/native-aria.ts.\n// Keep the fixed native and SDK serialization contract identical; regenerate with the\n// repository formatter generator after changing that function.\ninline constexpr char ${name === 'formatNativeAriaTree' ? 'kSuiteCutNativeAriaFormatter' : 'kSuiteCutNativeAriaJSON'}[] = R"JS(${compiled})JS";\n`,
)
