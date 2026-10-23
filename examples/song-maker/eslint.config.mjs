import js from '@eslint/js'

export default [
  { ignores: ['node_modules/**', 'output/**'] },
  js.configs.recommended,
  { languageOptions: { ecmaVersion: 2024, sourceType: 'module' } },
]
