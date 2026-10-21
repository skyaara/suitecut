// SPDX-License-Identifier: MIT
// Standalone architecture tests use Node's test runner without a TS project.
import js from '@eslint/js'

export default [
  js.configs.recommended,
  { files: ['**/*.mjs'], languageOptions: { ecmaVersion: 2024, sourceType: 'module' } },
]
