import eslint from '@eslint/js'
import eslintConfigPrettier from 'eslint-config-prettier'
import importX from 'eslint-plugin-import-x'
import jsdoc from 'eslint-plugin-jsdoc'
import tseslint from 'typescript-eslint'

export default tseslint.config(
  {
    ignores: [
      'dist/**',
      '.suitecut/**',
      'test-results/**',
      'playwright-report/**',
      'site/.astro/**',
      'site/dist/**',
      'eslint.config.js',
    ],
  },
  eslint.configs.recommended,
  ...tseslint.configs.recommendedTypeChecked,
  ...tseslint.configs.stylisticTypeChecked,
  importX.flatConfigs.recommended,
  importX.flatConfigs.typescript,
  jsdoc.configs['flat/recommended-typescript'],
  eslintConfigPrettier,
  {
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
    },
    settings: {
      'import-x/resolver': {
        typescript: {
          alwaysTryTypes: true,
          project: './tsconfig.json',
        },
      },
    },
    rules: {
      '@typescript-eslint/consistent-type-imports': [
        'error',
        {
          fixStyle: 'inline-type-imports',
          prefer: 'type-imports',
        },
      ],
      '@typescript-eslint/no-explicit-any': 'error',
      '@typescript-eslint/no-non-null-assertion': 'error',
      '@typescript-eslint/no-restricted-types': [
        'error',
        {
          types: {
            unknown: {
              message: 'Use UntrustedInput at runtime boundaries and validate it before use.',
            },
          },
        },
      ],
      '@typescript-eslint/no-unnecessary-type-assertion': 'error',
      '@typescript-eslint/no-unsafe-argument': 'error',
      '@typescript-eslint/no-unsafe-assignment': 'error',
      '@typescript-eslint/no-unsafe-call': 'error',
      '@typescript-eslint/no-unsafe-member-access': 'error',
      '@typescript-eslint/no-unsafe-return': 'error',
      '@typescript-eslint/require-await': 'error',
      '@typescript-eslint/switch-exhaustiveness-check': 'error',
      'import-x/consistent-type-specifier-style': ['error', 'prefer-inline'],
      'import-x/first': 'error',
      'import-x/newline-after-import': 'error',
      'import-x/no-cycle': ['error', { ignoreExternal: true }],
      'import-x/no-deprecated': 'error',
      'import-x/no-duplicates': 'error',
      'import-x/no-self-import': 'error',
      'import-x/no-useless-path-segments': 'error',
      'import-x/order': [
        'error',
        {
          alphabetize: { order: 'asc', caseInsensitive: true },
          groups: [
            'builtin',
            'external',
            'internal',
            'parent',
            'sibling',
            'index',
            'object',
            'type',
          ],
          named: false,
          'newlines-between': 'always',
        },
      ],
      'jsdoc/check-param-names': 'error',
      'jsdoc/check-tag-names': ['error', { typed: true }],
      'jsdoc/informative-docs': 'error',
      'jsdoc/require-description': 'error',
      'jsdoc/require-description-complete-sentence': 'error',
      'jsdoc/require-jsdoc': 'off',
      'jsdoc/require-param': 'off',
      'jsdoc/require-param-description': 'off',
      'jsdoc/require-returns': 'off',
      'jsdoc/require-returns-description': 'off',
      'jsdoc/reject-any-type': 'error',
      'no-warning-comments': ['error', { terms: ['fixme', 'hack'], location: 'anywhere' }],
    },
  },
  {
    files: ['src/fixtures.ts', 'src/render.ts', 'src/reporter.ts'],
    rules: {
      'jsdoc/require-jsdoc': [
        'error',
        {
          publicOnly: true,
          require: {
            ArrowFunctionExpression: false,
            ClassDeclaration: true,
            ClassExpression: false,
            FunctionDeclaration: true,
            FunctionExpression: false,
            MethodDefinition: true,
          },
        },
      ],
    },
  },
  {
    files: ['tests/**/*.ts'],
    rules: {
      '@typescript-eslint/no-non-null-assertion': 'off',
      '@typescript-eslint/no-unsafe-assignment': 'off',
    },
  },
)
