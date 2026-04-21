// Centralized locales path — update here + vite.config.ts + tsconfig.app.json when moving
const LOCALES_DIR = 'locales';

export default {
  input: ['src/**/*.{js,jsx,ts,tsx}'],
  output: `${LOCALES_DIR}/$LOCALE.json`,
  locales: [
    'en', 'zh_CN', 'ja_JP', 'it_IT', 'fr_FR', 'de_DE',
    'hu_HU', 'es_ES', 'sv_SE', 'cs_CZ', 'nl_NL', 'uk_UA',
    'ru_RU', 'tr_TR', 'pt_BR', 'ko_KR', 'pl_PL',
  ],
  defaultNamespace: 'translation',
  // Disable key/namespace separators so English sentences with dots/colons
  // are treated as flat keys, not nested paths
  keySeparator: false,
  namespaceSeparator: false,
  // Use the key itself as default value (key = English original text)
  useKeysAsDefaultValue: true,
  // Keep existing translations, only add new keys
  createOldCatalogs: false,
  sort: true,
};
