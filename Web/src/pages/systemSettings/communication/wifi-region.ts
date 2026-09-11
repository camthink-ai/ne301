import { locales, type LocaleType } from '@/locales';
import { getItem } from '@/utils/storage';

export function normalizeWifiRegionCode(code: string): string {
    return code.trim().toLowerCase();
}

/** Label for a legacy WiFi region code (us/eu/jp/world/kr/cn). Falls back to upper-case code. */
export function getWifiRegionLabel(code: string, locale?: LocaleType): string {
    const normalized = normalizeWifiRegionCode(code);
    if (!normalized) {
        return '';
    }

    const lang = locale ?? ((getItem('locale') || 'en') as LocaleType);
    const i18nKey = `wifi_region_${normalized}`;
    const sysMgmt = locales[lang]?.sys?.system_management as Record<string, string> | undefined;

    if (sysMgmt?.[i18nKey]) {
        return sysMgmt[i18nKey];
    }
    return normalized.toUpperCase();
}
