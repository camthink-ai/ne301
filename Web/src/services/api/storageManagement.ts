import request from "../request";

const storageManagement = {
    getStorage: () => request.get('/api/v1/device/storage'),
    /** Format the internal flash LittleFS volume (destructive — erases all
     *  flash files: logs, captures. NVS config is separate and unaffected). */
    formatFlash: () => request.post('/api/v1/device/storage/format'),
}

export default storageManagement