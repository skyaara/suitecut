// SPDX-License-Identifier: MIT
// Standalone Node/browser boundary tests are plain JavaScript. Production SDK
// modules retain the repository's full TypeScript lint configuration.
import js from '@eslint/js'

export default [
  js.configs.recommended,
  {
    files: ['**/*.mjs'],
    languageOptions: { ecmaVersion: 2024, sourceType: 'module' },
    rules: { 'no-unused-vars': ['error', { argsIgnorePattern: '^_', caughtErrors: 'all' }] },
  },
]
