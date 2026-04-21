import { createFileRoute } from '@tanstack/react-router'
import { I18nTranslateDemo } from '../I18nTranslateDemo'

export const Route = createFileRoute('/i18n-demo')({
  component: I18nTranslateDemo,
})
