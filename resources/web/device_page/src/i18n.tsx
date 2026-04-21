// src/i18n.ts
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';

// Import all locale resources statically (bundled into JS, no runtime fetch needed)
import en from '@locales/en.json';
import zh_CN from '@locales/zh_CN.json';
import ja_JP from '@locales/ja_JP.json';
import it_IT from '@locales/it_IT.json';
import fr_FR from '@locales/fr_FR.json';
import de_DE from '@locales/de_DE.json';
import hu_HU from '@locales/hu_HU.json';
import es_ES from '@locales/es_ES.json';
import sv_SE from '@locales/sv_SE.json';
import cs_CZ from '@locales/cs_CZ.json';
import nl_NL from '@locales/nl_NL.json';
import uk_UA from '@locales/uk_UA.json';
import ru_RU from '@locales/ru_RU.json';
import tr_TR from '@locales/tr_TR.json';
import pt_BR from '@locales/pt_BR.json';
import ko_KR from '@locales/ko_KR.json';
import pl_PL from '@locales/pl_PL.json';

// Detect language: URL ?lang= param > localStorage > fallback 'en'
// Consistent with other webview pages (text.js TranslatePage pattern)
function detectLanguage(): string {
  const params = new URLSearchParams(window.location.search);
  const urlLang = params.get('lang');
  if (urlLang) {
    localStorage.setItem('BambuWebLang', urlLang);
    return urlLang;
  }
  return localStorage.getItem('BambuWebLang') || 'en';
}

i18n
  .use(initReactI18next)
  .init({
    lng: detectLanguage(),
    fallbackLng: 'en',
    debug: false,
    interpolation: {
      escapeValue: false,
    },
    resources: {
      en: { translation: en },
      zh_CN: { translation: zh_CN },
      ja_JP: { translation: ja_JP },
      it_IT: { translation: it_IT },
      fr_FR: { translation: fr_FR },
      de_DE: { translation: de_DE },
      hu_HU: { translation: hu_HU },
      es_ES: { translation: es_ES },
      sv_SE: { translation: sv_SE },
      cs_CZ: { translation: cs_CZ },
      nl_NL: { translation: nl_NL },
      uk_UA: { translation: uk_UA },
      ru_RU: { translation: ru_RU },
      tr_TR: { translation: tr_TR },
      pt_BR: { translation: pt_BR },
      ko_KR: { translation: ko_KR },
      pl_PL: { translation: pl_PL },
    },
    // When a key has no translation, return the key itself (English original text)
    parseMissingKeyHandler: (key) => key,
  });

export default i18n;
