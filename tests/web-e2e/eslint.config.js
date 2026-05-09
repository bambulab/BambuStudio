// Flat config for ESLint 9.
// Intentionally lightweight: this suite has no production runtime; the
// goal is to catch obvious TS-friendly mistakes (unused vars, missing
// awaits) without imposing a style framework that would compete with
// the wider repo's tooling.
import tseslint from '@typescript-eslint/eslint-plugin'
import tsparser from '@typescript-eslint/parser'

export default [
  {
    files: ['**/*.ts'],
    languageOptions: {
      parser: tsparser,
      parserOptions: {
        ecmaVersion: 'latest',
        sourceType: 'module',
        project: './tsconfig.json',
      },
    },
    plugins: { '@typescript-eslint': tseslint },
    rules: {
      '@typescript-eslint/no-unused-vars': [
        'warn',
        { argsIgnorePattern: '^_', varsIgnorePattern: '^_' },
      ],
      '@typescript-eslint/no-explicit-any': 'warn',
      'no-empty-pattern': 'off',
    },
  },
  {
    ignores: ['node_modules/', 'reports/', 'test-results/', 'playwright-report/'],
  },
]
