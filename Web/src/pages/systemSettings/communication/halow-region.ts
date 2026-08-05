import { locales, type LocaleType } from '@/locales';
import { getItem } from '@/utils/storage';

/** Built-in labels for extended regdomain codes (au_2020 etc.). */
const HALOW_REGION_FALLBACK: Record<string, Record<LocaleType, string>> = {
    au_2020: {
        en: 'Australia (AU_2020)',
        zh: '澳大利亚 (AU_2020)',
    },
    au_2024: {
        en: 'Australia (AU_2024)',
        zh: '澳大利亚 (AU_2024)',
    },
    au_revmf: {
        en: 'Australia (AU_REVMF)',
        zh: '澳大利亚 (AU_REVMF)',
    },
};

export function normalizeHalowRegionCode(code: string): string {
    return code.trim().toLowerCase();
}

export function getHalowRegionLabel(code: string, locale?: LocaleType): string {
    const normalized = normalizeHalowRegionCode(code);
    if (!normalized) {
        return '';
    }

    const lang = locale ?? ((getItem('locale') || 'en') as LocaleType);
    const i18nKey = `halow_region_${normalized}`;
    const sysMgmt = locales[lang]?.sys?.system_management as Record<string, string> | undefined;

    if (sysMgmt?.[i18nKey]) {
        return sysMgmt[i18nKey];
    }
    if (HALOW_REGION_FALLBACK[normalized]?.[lang]) {
        return HALOW_REGION_FALLBACK[normalized][lang];
    }
    return normalized.toUpperCase();
}
