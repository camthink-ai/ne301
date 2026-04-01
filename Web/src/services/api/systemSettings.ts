import request from '../request';

interface SetWifiReq {
    interface: string;
    ssid: string;
    password: string;
    ap_sleep_time?: number;
    bssid: string;
}
interface SetWifiConfigReq {
    interface: string;
    ssid: string;
    password: string;
    ap_sleep_time: number;
}
interface DeleteWifiReq {
    ssid: string;
    bssid: string;
}

// authentication: 0=None, 1=PAP, 2=CHAP
export interface SetCellularReq {
    apn?: string;
    username?: string;
    password?: string;
    pin_code?: string;
    authentication?: number;
    enable_roaming?: boolean;
    operator?: number;
    save?: boolean;
}

interface atCmdCellularReq {
    command?: string;
    timeout_ms?: number;
}

interface SwitchNetworkTypeReq {
    type: string;
    timeout_ms?: number;

}

interface SetPoeConfigReq {
    ip_mode?: "static" | "dhcp";
    ip_address?: string;
    netmask?: string;
    gateway?: string;
    dns_primary?: string;
    dns_secondary?: string;
    hostname?: string;
    dhcp_timeout_ms?: number;
    dhcp_retry_count?: number;
    auto_reconnect?: boolean;
    validate_gateway?: boolean;
    detect_ip_conflict?: boolean;
}

interface ValidatePoeConfigReq {
    ip_address?: string;
    netmask?: string;
    gateway?: string;
    dns_primary?: string;
    dns_secondary?: string;
    hostname?: string;
    check_gateway?: boolean;
    check_conflict?: boolean;
}

const systemSettings = {
    restartDevice: ({ delaySeconds }: { delaySeconds: number }) => request.post('/api/v1/system/restart', { delay_seconds: delaySeconds }),
    getNetworkStatusReq: () => request.get('/api/v1/system/network/status'),
    // common
    getNetworkTypesReq: () => request.get('/api/v1/system/network/comm/types'),
    switchNetworkTypeReq: (data: SwitchNetworkTypeReq) => request.post('/api/v1/system/network/comm/switch', data),
    prefetchNetworkConfigReq: () => request.post('/api/v1/system/network/comm/prefetch'),
    prioritizeNetworkReq: (data: { interface: string }) => request.post('/api/v1/system/network/comm/prioritize', data),

    // wifi
    getNetworkSTAReq: () => request.get('/api/v1/system/network/wifi/sta'),
    getAPConfigReq: () => request.get('/api/v1/system/network/wifi/ap'),
    scanWifi: () => request.post('/api/v1/system/network/wifi/scan'),
    setWifi: (data: SetWifiReq) => request.post('/api/v1/system/network/wifi', data),
    setWifiConfig: (data: SetWifiConfigReq) => request.post('/api/v1/system/network/wifi/config', data),
    deleteWifi: (data: DeleteWifiReq) => request.post('/api/v1/system/network/wifi/delete', data),
    disconnectWifi: (data: { interface: string }) => request.post('/api/v1/system/network/wifi/disconnect', data),

    // cellular
    getCellularStatusReq: () => request.get('/api/v1/system/network/cellular/status'),
    setCellularReq: (data: SetCellularReq) => request.post('/api/v1/system/network/cellular/settings', data),
    getCellularInfoReq: () => request.get('/api/v1/system/network/cellular/info'),
    connectCellularReq: () => request.post('/api/v1/system/network/cellular/connect'),
    disconnectCellularReq: () => request.post('/api/v1/system/network/cellular/disconnect'),
    deleteCellularReq: () => request.post('/api/v1/system/network/cellular/delete'),
    saveCellularReq: (data: SetCellularReq) => request.post('/api/v1/system/network/cellular/settings', data),
    refreshCellularReq: () => request.post('/api/v1/system/network/cellular/refresh'),
    atCmdCellularReq: (data: atCmdCellularReq) => request.post('/api/v1/system/network/cellular/at', data),

    // poe
    getPoeInfoReq: () => request.get('/api/v1/system/network/poe/info'),
    getPoeStatusReq: () => request.get('/api/v1/system/network/poe/status'),
    getPoeConfigReq: () => request.get('/api/v1/system/network/poe/config'),
    setPoeConfigReq: (data: SetPoeConfigReq) => request.post('/api/v1/system/network/poe/config', data),
    validatePoeConfigReq: (data: ValidatePoeConfigReq) => request.post('/api/v1/system/network/poe/validate', data),
    applyPoeConfigReq: () => request.post('/api/v1/system/network/poe/apply'),
    savePoeConfigReq: () => request.post('/api/v1/system/network/poe/save'),
    connectPoeReq: () => request.post('/api/v1/system/network/poe/connect'),
    disconnectPoeReq: () => request.post('/api/v1/system/network/poe/disconnect'),
}

export default systemSettings;