import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import LanguageDetector from 'i18next-browser-languagedetector';

import en from './locales/en.json';
import zh from './locales/zh.json';
import zhTW from './locales/zh-TW.json';
import ja from './locales/ja.json';
import ko from './locales/ko.json';
import fr from './locales/fr.json';
import de from './locales/de.json';
import es from './locales/es.json';
import ru from './locales/ru.json';

i18n
    .use(LanguageDetector)
    .use(initReactI18next)
    .init({
        resources: {
            en: { translation: en },
            zh: { translation: zh },
            'zh-TW': { translation: zhTW },
            ja: { translation: ja },
            ko: { translation: ko },
            fr: { translation: fr },
            de: { translation: de },
            es: { translation: es },
            ru: { translation: ru },
        },
        supportedLngs: ['en', 'zh', 'zh-TW', 'ja', 'ko', 'fr', 'de', 'es', 'ru'],
        fallbackLng: 'en',
        nonExplicitSupportedLngs: true,
        debug: true,
        interpolation: {
            escapeValue: false,
        },
        detection: {
            order: ['localStorage', 'navigator'],
            caches: ['localStorage'],
        },
    });

export default i18n;
