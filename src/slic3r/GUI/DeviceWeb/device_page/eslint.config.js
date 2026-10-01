import js from '@eslint/js'
import globals from 'globals'
import compat from 'eslint-plugin-compat'
import reactHooks from 'eslint-plugin-react-hooks'
import reactRefresh from 'eslint-plugin-react-refresh'
import tseslint from 'typescript-eslint'

export default tseslint.config(
  { ignores: ['dist'] },
  {
    extends: [js.configs.recommended, ...tseslint.configs.recommended],
    files: ['**/*.{ts,tsx}'],
    languageOptions: {
      ecmaVersion: 2020,
      globals: globals.browser,
    },
    plugins: {
      compat,
      'react-hooks': reactHooks,
      'react-refresh': reactRefresh,
    },
    settings: {
      lintAllEsApis: false,
    },
    rules: {
      ...reactHooks.configs.recommended.rules,
      'react-refresh/only-export-components': ['warn', { allowConstantExport: true }],
      'compat/compat': 'error',
    },
  },
)
