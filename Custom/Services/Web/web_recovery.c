/**
 * @file web_recovery.c
 * @brief Built-in web recovery implementation
 * @details Provides a self-contained HTML page that is served when the normal
 *          web asset firmware is missing/corrupt. The page uploads a web
 *          firmware package to the existing OTA upload endpoint
 *          (firmwareType=web); after a successful upload the assets are
 *          reloaded from flash so the full UI comes back without a reboot.
 */

#include "web_recovery.h"
#include "web_assets.h"
#include "mem_map.h"
#include "debug.h"

/* ==================== Recovery state ==================== */

static aicam_bool_t g_recovery_active = AICAM_FALSE;

void web_recovery_activate(void)
{
    g_recovery_active = AICAM_TRUE;
    LOG_SVC_WARN("[WEB_RECOVERY] Web assets unavailable, entering recovery mode");
}

aicam_bool_t web_recovery_is_active(void)
{
    return g_recovery_active;
}

aicam_result_t web_recovery_reload_assets(void)
{
    /* The web partition is single-slot (active == update == WEB_BASE), so the
     * freshly written asset.bin lives at WEB_BASE right after the OTA. */
    aicam_result_t result = web_asset_adapter_init((const uint8_t*)WEB_BASE);
    if (result == AICAM_OK) {
        if (g_recovery_active) {
            g_recovery_active = AICAM_FALSE;
            LOG_SVC_INFO("[WEB_RECOVERY] Web assets reloaded, exiting recovery mode");
        } else {
            LOG_SVC_INFO("[WEB_RECOVERY] Web assets reloaded after upgrade");
        }
    } else {
        LOG_SVC_WARN("[WEB_RECOVERY] Web assets reload failed: %d", result);
    }
    return result;
}

/* ==================== Embedded recovery page ==================== */

/* A single self-contained HTML page: inline CSS + JS, no external resources.
 * Posts the selected firmware file as a raw octet-stream body to the OTA
 * upload endpoint with firmwareType=web. On success it reloads the page so the
 * freshly loaded normal UI is served. */
static const char RECOVERY_HTML[] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"<meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"<title>Web Recovery</title>\n"
"<style>\n"
"  *{box-sizing:border-box}\n"
"  body{margin:0;font-family:system-ui,-apple-system,\"Segoe UI\",sans-serif;background:#f5f6f8;color:#222;display:flex;min-height:100vh;align-items:center;justify-content:center}\n"
"  .card{background:#fff;border-radius:12px;box-shadow:0 4px 24px rgba(0,0,0,.08);padding:32px;max-width:460px;width:90%}\n"
"  h1{font-size:20px;margin:0 0 8px}\n"
"  .warn{color:#b26a00;background:#fff7e6;border:1px solid #ffd591;border-radius:8px;padding:10px 12px;font-size:13px;margin:16px 0}\n"
"  p{font-size:13px;line-height:1.6;color:#555;margin:8px 0}\n"
"  label{display:block;font-size:13px;margin:16px 0 6px;color:#333}\n"
"  input[type=file]{width:100%;font-size:13px}\n"
"  .row{display:flex;gap:12px;margin-top:20px;align-items:center}\n"
"  button{flex:1;padding:10px 16px;font-size:14px;border:none;border-radius:8px;background:#1677ff;color:#fff;cursor:pointer}\n"
"  button:disabled{background:#9ec3ff;cursor:not-allowed}\n"
"  .progress{height:8px;background:#eee;border-radius:6px;overflow:hidden;margin-top:12px;display:none}\n"
"  .progress>div{height:100%;width:0;background:#1677ff;transition:width .2s}\n"
"  .status{margin-top:12px;font-size:13px;min-height:20px}\n"
"  .ok{color:#237804}.err{color:#cf1322}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div class=\"card\">\n"
"  <h1>Web Recovery Mode</h1>\n"
"  <p>The device could not load a valid web firmware (the partition address may have changed after an upgrade, or the firmware was not flashed).</p>\n"
"  <div class=\"warn\">Select the matching <b>web firmware (.bin)</b> file and upload it. The interface will recover automatically once the upload completes.</div>\n"
"  <label for=\"file\">Web firmware file</label>\n"
"  <input type=\"file\" id=\"file\" accept=\".bin,application/octet-stream\">\n"
"  <div class=\"progress\" id=\"prog\"><div></div></div>\n"
"  <div class=\"row\">\n"
"    <button id=\"btn\" disabled>Upload &amp; Recover</button>\n"
"  </div>\n"
"  <div class=\"status\" id=\"status\"></div>\n"
"</div>\n"
"<script>\n"
"var file=document.getElementById('file'),btn=document.getElementById('btn'),\n"
"    prog=document.getElementById('prog'),bar=prog.firstElementChild,\n"
"    status=document.getElementById('status');\n"
"file.addEventListener('change',function(){btn.disabled=!file.files.length;});\n"
"btn.addEventListener('click',function(){\n"
"  if(!file.files.length)return;\n"
"  var f=file.files[0];\n"
"  btn.disabled=true;file.disabled=true;\n"
"  status.className='status';status.textContent='Preparing upload...';\n"
"  prog.style.display='block';bar.style.width='0%';\n"
"  var xhr=new XMLHttpRequest();\n"
"  xhr.open('POST','/api/v1/system/ota/upload?firmwareType=web',true);\n"
"  xhr.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);bar.style.width=p+'%';status.textContent='Uploading '+p+'%';}};\n"
"  xhr.onload=function(){\n"
"    var msg='';\n"
"    try{var r=JSON.parse(xhr.responseText);msg=r.message||(r.success?'Upload successful':'Upload failed');}catch(e){msg=xhr.responseText||('HTTP '+xhr.status);}\n"
"    if(xhr.status>=200&&xhr.status<300){\n"
"      status.className='status ok';status.textContent='Recovery successful, reloading interface...';\n"
"      bar.style.width='100%';\n"
"      setTimeout(function(){location.reload();},1500);\n"
"    }else{\n"
"      status.className='status err';status.textContent='Recovery failed: '+msg;\n"
"      btn.disabled=false;file.disabled=false;\n"
"    }\n"
"  };\n"
"  xhr.onerror=function(){status.className='status err';status.textContent='Network error, upload failed';btn.disabled=false;file.disabled=false;};\n"
"  xhr.send(f);\n"
"});\n"
"</script>\n"
"</body>\n"
"</html>\n";

const char* web_recovery_get_html(void)
{
    return RECOVERY_HTML;
}

size_t web_recovery_get_html_size(void)
{
    return sizeof(RECOVERY_HTML) - 1;
}
