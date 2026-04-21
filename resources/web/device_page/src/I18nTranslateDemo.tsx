/**
 * i18next Translation Demo — All Common Scenarios
 *
 * This file demonstrates every translation pattern you'll need in this project.
 * Each section shows the t() call and the corresponding locale JSON keys.
 *
 * ── Key naming convention ──
 * - Use English original text as key (not t1, t2, ...)
 * - keySeparator & namespaceSeparator are disabled, so dots/colons are safe in keys
 */

import { useState } from 'react';
import { useTranslation, Trans } from 'react-i18next';

export function I18nTranslateDemo() {
  const { t } = useTranslation();
  const [count, setCount] = useState(3);

  // ── demo data ──
  const userName = 'Alice';
  const fileName = 'model.3mf';
  const percent = 85;
  const nozzleTemp = 220;
  const bedTemp = 60;

  return (
    <div className="p-6 space-y-8 text-sm max-w-2xl">
      <h1 className="text-xl font-bold">{t('i18next Translation Demo')}</h1>

      {/* ━━ 1. Basic — simple static text ━━
       *  en.json:    { "Calibration": "Calibration" }
       *  zh_CN.json: { "Calibration": "校准" }
       */}
      <section className="space-y-2">
        <h2 className="font-semibold text-blue-600">1. {t('Basic Translation')}</h2>
        <p>{t('Calibration')}</p>
        <p>{t('Filament Manager')}</p>
        <p>{t('Ready')}</p>
      </section>

      {/* ━━ 2. Interpolation — insert dynamic values ━━
       *  en.json:    { "Hello, {{name}}!": "Hello, {{name}}!" }
       *  zh_CN.json: { "Hello, {{name}}!": "你好，{{name}}！" }
       */}
      <section className="space-y-2">
        <h2 className="font-semibold text-blue-600">2. {t('Interpolation')}</h2>
        <p>{t('Hello, {{name}}!', { name: userName })}</p>
        <p>{t('Printing {{file}}...', { file: fileName })}</p>
        <p>{t('Progress: {{percent}}%', { percent })}</p>
        <p>{t('Nozzle {{nozzle}}°C / Bed {{bed}}°C', { nozzle: nozzleTemp, bed: bedTemp })}</p>
      </section>

      {/* ━━ 3. Plurals — different forms based on count ━━
       *  en.json:
       *    "{{count}} item_one":   "{{count}} item"
       *    "{{count}} item_other": "{{count}} items"
       *  zh_CN.json:
       *    "{{count}} item_one":   "{{count}} 个项目"
       *    "{{count}} item_other": "{{count}} 个项目"   (Chinese has no plural form)
       */}
      <section className="space-y-2">
        <h2 className="font-semibold text-blue-600">3. {t('Plurals')}</h2>
        <p>{t('{{count}} item', { count: 0 })}</p>
        <p>{t('{{count}} item', { count: 1 })}</p>
        <p>{t('{{count}} item', { count })}</p>
        <button
          className="px-2 py-1 rounded bg-neutral-200 hover:bg-neutral-300"
          onClick={() => setCount((c) => c + 1)}
        >
          +1
        </button>
      </section>

      {/* ━━ 4. Context — same English, different translation per context ━━
       *  en.json:
       *    "General_settings": "General"
       *    "General_print":    "General"
       *  zh_CN.json:
       *    "General_settings": "常规"
       *    "General_print":    "通用"
       */}
      <section className="space-y-2">
        <h2 className="font-semibold text-blue-600">4. {t('Context')}</h2>
        <p>{t('Settings page')}: {t('General', { context: 'settings' })}</p>
        <p>{t('Print page')}: {t('General', { context: 'print' })}</p>
        <p>{t('File operation')}: {t('Save', { context: 'file' })}</p>
        <p>{t('Preset operation')}: {t('Save', { context: 'preset' })}</p>
      </section>

      {/* ━━ 5. Nesting — reuse other translation keys inside a translation ━━
       *  en.json:
       *    "Welcome": "Welcome"
       *    "Welcome to $t(appName)": "Welcome to $t(appName)"
       *    "appName": "Bambu Studio"
       *  zh_CN.json:
       *    "Welcome to $t(appName)": "欢迎使用 $t(appName)"
       *    "appName": "拓竹切片"
       */}
      <section className="space-y-2">
        <h2 className="font-semibold text-blue-600">5. {t('Nesting')}</h2>
        <p>{t('Welcome to $t(appName)')}</p>
      </section>

      {/* ━━ 6. Trans component — embed JSX (bold, links, etc.) inside translations ━━
       *  en.json:
       *    "Click <1>here</1> to view <3>{{file}}</3>":
       *       "Click <1>here</1> to view <3>{{file}}</3>"
       *  zh_CN.json:
       *    "Click <1>here</1> to view <3>{{file}}</3>":
       *       "点击<1>此处</1>查看<3>{{file}}</3>"
       *
       *  <1> = second child (index 1) = <a> tag
       *  <3> = fourth child (index 3) = <strong> tag
       */}
      <section className="space-y-2">
        <h2 className="font-semibold text-blue-600">6. {t('Trans Component (JSX in translation)')}</h2>
        <p>
          <Trans i18nKey="Click <1>here</1> to view <3>{{file}}</3>" values={{ file: fileName }}>
            Click <a className="text-blue-500 underline" href="#">here</a> to view <strong>{fileName}</strong>
          </Trans>
        </p>
      </section>

      {/* ━━ 7. Default value — provide fallback directly in code ━━
       *  No JSON key needed if you only want English.
       *  Useful for rarely-translated or developer-facing text.
       */}
      <section className="space-y-2">
        <h2 className="font-semibold text-blue-600">7. {t('Default Value')}</h2>
        <p>{t('someMissingKey', { defaultValue: 'This text is used if key not found' })}</p>
        <p>{t('debugInfo', { defaultValue: 'Debug: {{info}}', info: 'v1.2.3' })}</p>
      </section>

      {/* ━━ 8. Multiline / Long text — same key approach, just longer ━━
       *  en.json:
       *    "calibration_help": "To calibrate ... (long text)"
       *  zh_CN.json:
       *    "calibration_help": "校准时请... (长文本)"
       *
       *  For long text where English original as key is impractical,
       *  use a short descriptive key instead.
       */}
      <section className="space-y-2">
        <h2 className="font-semibold text-blue-600">8. {t('Long Text')}</h2>
        <p className="whitespace-pre-line">{t('calibration_help')}</p>
      </section>

      {/* ━━ Summary ━━ */}
      <section className="border-t pt-4 text-neutral-500">
        <h2 className="font-semibold text-neutral-700 mb-2">{t('Cheat Sheet')}</h2>
        <pre className="bg-neutral-50 p-3 rounded text-xs overflow-x-auto">{`
t("Static text")                              // Basic
t("Hello, {{name}}!", { name })               // Interpolation
t("{{count}} item", { count })                // Plurals (_one / _other)
t("General", { context: "settings" })         // Context (_settings)
t("Welcome to $t(appName)")                   // Nesting
<Trans i18nKey="Click <1>here</1>">...</Trans> // JSX in translation
t("key", { defaultValue: "fallback" })        // Default value
t("short_key")                                // Long text (descriptive key)
        `.trim()}</pre>
      </section>
    </div>
  );
}
