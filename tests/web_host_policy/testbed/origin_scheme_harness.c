#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "web_host_policy.h"

#define HOST_IP "127.0.0.1"
#define ADMIN_PORT 8080
#define APP_PORT 8081

static volatile int g_keep_running = 1;
static volatile int g_side_effects;
static volatile int g_media_denied;
static volatile int g_revision = 3;

static const char *ADMIN_PAGE =
"<!DOCTYPE html><html><body><h1>idle</h1><script>\n"
"const APP='http://" HOST_IP ":8081';\n"
"function servable(p,appId,adminOrigin){"
"if(p.state!=='active')return 'state';"
"if(!p.app_site_origin||!p.ui_base_path)return 'origin-missing';"
"if(p.app_id!==appId)return 'appid';"
"if(p.host_ui_abi!==1)return 'abi';"
"let o;try{o=new URL(p.app_site_origin).origin}catch(e){return 'url'};"
"if(o!==p.app_site_origin)return 'origin-form';"
"if(adminOrigin===o)return 'same-origin';"
"return 'PASS';}\n"
"const bridge={video:null,save:null,rev:null,appReport:null};\n"
"window.addEventListener('message',function(e){"
"if(e.origin!==APP)return;"
"const m=e.data;if(!m||!m.op)return;"
"if(m.op==='video-grant'){e.source.postMessage({op:'video-grant',"
"url:APP+'/appsite/g1/media.mjpeg?grant=TOK123'},APP);}"
"else if(m.op==='save-config'){"
"fetch('/api/v1/app/config/save',{method:'POST',"
"headers:{'Content-Type':'application/json'},body:JSON.stringify(m.config)})"
".then(function(r){return r.json()}).then(function(j){"
"e.source.postMessage({op:'save-result',revision:j.revision},APP);});}"
"else if(m.op==='report'){bridge.appReport=m.data;}\n"
"});\n"
"window.__runScheme=async function(){\n"
"  const r=await fetch('/api/v1/app/sidecar/ui');const p=await r.json();\n"
"  const v=servable(p,'line-crossing',window.location.origin);\n"
"  let iframeLoaded=false;\n"
"  if(v==='PASS'){const f=document.createElement('iframe');"
"  f.sandbox='allow-scripts allow-same-origin';f.src=p.app_site_origin+p.ui_base_path+'index.html';"
"  f.onload=()=>{iframeLoaded=true;};document.body.appendChild(f);"
"  await new Promise(res=>setTimeout(res,5000));f.remove();}\n"
"  const out=document.createElement('pre');out.id='scheme-result';\n"
"  out.textContent=JSON.stringify({verdict:v,iframeLoaded,"
"adminOrigin:window.location.origin,appOrigin:p.app_site_origin,appReport:bridge.appReport});\n"
"  document.body.appendChild(out);document.querySelector('h1').textContent='SCHEME-DONE';\n"
"};\n"
"</script></body></html>";

static const char *APP_PAGE =
"<!DOCTYPE html><html><body><h1>app-site</h1><script>\n"
"const ADMIN='http://" HOST_IP ":8080';\n"
"const results=[];function rec(n,ok){results.push({n,ok});}\n"
"function send(op,extra){window.parent.postMessage(Object.assign({op:op},extra||{}),ADMIN);}\n"
"async function adminRead(){try{await fetch(ADMIN+'/api/v1/count');rec('admin-read',false);}"
"catch(e){rec('admin-read',true);}}\n"
"async function adminWrite(){try{const r=await fetch(ADMIN+'/api/v1/echo',{method:'POST',mode:'no-cors',body:'x'});rec('admin-write',r.type==='opaque');}"
"catch(e){rec('admin-write',true);}}\n"
"function videoGrant(){return new Promise(res=>{"
"function h(e){if(e.origin!==ADMIN)return;"
"if(e.data.op==='video-grant'){window.removeEventListener('message',h);"
"const img=new Image();img.onload=()=>{rec('video-grant',true);res();};"
"img.onerror=()=>{rec('video-grant',false);res();};img.src=e.data.url;}}"
"window.addEventListener('message',h);send('video-grant');});}\n"
"function draftSave(){return new Promise(res=>{"
"function h(e){if(e.origin!==ADMIN)return;"
"if(e.data.op==='save-result'){window.removeEventListener('message',h);"
"rec('save-revision',e.data.revision===4);res();}}"
"window.addEventListener('message',h);"
"send('save-config',{config:{threshold:5}});});}\n"
"window.__appProbe=async function(){await adminRead();await adminWrite();"
"await videoGrant();await draftSave();"
"const bad=new Image();bad.onerror=()=>{rec('media-no-grant',true);};"
"bad.onload=()=>{rec('media-no-grant',false);};bad.src=location.origin+'/appsite/g1/media.mjpeg';\n"
"await new Promise(r=>setTimeout(r,1200));\n"
"rec('app-origin-ok',location.origin!==ADMIN);\n"
"send('report',{data:results});};\n"
"window.__appProbe();\n"
"</script></body></html>";

static const unsigned char GIF_BYTES[] = {
    0x47,0x49,0x46,0x38,0x39,0x61,0x01,0x00,0x01,0x00,0x80,0x00,0x00,
    0xff,0xff,0xff,0x00,0x00,0x00,0x21,0xf9,0x04,0x01,0x00,0x00,0x00,
    0x00,0x2c,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0x02,0x02,
    0x44,0x01,0x00,0x3b
};

static void reply(int fd, int code, const char *ctype, const char *body, size_t blen)
{
    char head[512];
    int hlen = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        code, code == 200 ? "OK" : "Forbidden", ctype, blen);
    (void)write(fd, head, (size_t)hlen);
    (void)write(fd, body, blen);
}

static void reply_json(int fd, int code, const char *body)
{
    reply(fd, code, "application/json", body, strlen(body));
}

static void handle_admin(int fd, char *method, char *uri, char *origin, aicam_bool_t origin_present)
{
    web_entry_verdict_t verdict = web_host_policy_entry_role(
        HOST_IP ":8080", uri, method, origin, origin_present, WEB_LISTENER_ADMIN);
    if (verdict != WEB_ENTRY_ALLOW) {
        printf("[admin] reject verdict=%d %s %s origin='%s'\n", (int)verdict, method, uri, origin);
        reply_json(fd, 403, "{\"error\":\"forbidden host\"}");
        return;
    }
    if (strcmp(uri, "/admin.html") == 0) {
        reply(fd, 200, "text/html", ADMIN_PAGE, strlen(ADMIN_PAGE));
    } else if (strcmp(uri, "/api/v1/app/sidecar/ui") == 0) {
        reply_json(fd, 200,
                   "{\"state\":\"active\",\"app_id\":\"line-crossing\","
                   "\"app_site_origin\":\"http://" HOST_IP ":8081\","
                   "\"ui_base_path\":\"/appsite/g1/\",\"host_ui_abi\":1,\"generation\":7}");
    } else if (strcmp(uri, "/api/v1/app/config/save") == 0 && strcmp(method, "POST") == 0) {
        g_revision++;
        char body[64];
        snprintf(body, sizeof(body), "{\"revision\":%d}", g_revision);
        printf("[admin] SAVE config -> revision %d\n", g_revision);
        reply_json(fd, 200, body);
    } else if (strcmp(uri, "/api/v1/app/config/revision") == 0) {
        char body[64];
        snprintf(body, sizeof(body), "{\"revision\":%d}", g_revision);
        reply_json(fd, 200, body);
    } else if (strcmp(uri, "/api/v1/echo") == 0 && strcmp(method, "POST") == 0) {
        g_side_effects++;
        printf("[admin] SIDE-EFFECT #%d\n", g_side_effects);
        reply_json(fd, 200, "{\"ok\":true}");
    } else if (strcmp(uri, "/api/v1/count") == 0) {
        char body[96];
        snprintf(body, sizeof(body), "{\"count\":%d,\"revision\":%d,\"media_denied\":%d}",
                 g_side_effects, g_revision, g_media_denied);
        reply_json(fd, 200, body);
    } else {
        reply_json(fd, 404, "{\"error\":\"not found\"}");
    }
}

static void handle_app(int fd, char *method, char *req_uri, char *host)
{
    char path[256];
    char *qm;
    web_entry_verdict_t verdict;

    snprintf(path, sizeof(path), "%s", req_uri);
    qm = strchr(path, '?');
    if (qm) *qm = '\0';
    verdict = web_host_policy_entry_role(
        host, path, method, NULL, AICAM_FALSE, WEB_LISTENER_APP);
    if (verdict != WEB_ENTRY_ALLOW) {
        printf("[app] reject verdict=%d %s %s\n", (int)verdict, method, path);
        reply_json(fd, 403, "{\"error\":\"forbidden host\"}");
        return;
    }
    if (strcmp(path, "/appsite/g1/index.html") == 0) {
        reply(fd, 200, "text/html", APP_PAGE, strlen(APP_PAGE));
        printf("[app] asset served %s\n", path);
    } else if (strcmp(path, "/appsite/g1/media.mjpeg") == 0) {
        if (strstr(req_uri, "grant=TOK123") != NULL) {
            printf("[app] media granted\n");
            reply(fd, 200, "image/gif", (const char *)GIF_BYTES, sizeof(GIF_BYTES));
        } else {
            g_media_denied++;
            printf("[app] media denied #%d\n", g_media_denied);
            reply_json(fd, 403, "{\"error\":\"forbidden host\"}");
        }
    } else {
        reply_json(fd, 404, "{\"error\":\"not found\"}");
    }
}

static void handle(int fd, web_listener_role_t role)
{
    char buf[4096];
    char method[16] = "";
    char full_uri[512] = "";
    char uri[256] = "";
    char host[256] = "";
    char origin[256] = "";
    aicam_bool_t origin_present = AICAM_FALSE;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(fd); return; }
    buf[n] = '\0';
    char *line = strtok(buf, "\r\n");
    if (line) sscanf(line, "%15s %511s", method, full_uri);
    snprintf(uri, sizeof(uri), "%s", full_uri);
    {
        char *qm = strchr(uri, '?');
        if (qm) *qm = '\0';
    }
    for (line = strtok(NULL, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (strncasecmp(line, "Host:", 5) == 0) {
            sscanf(line + 5, "%255[^\r\n]", host);
            char *p = host; while (*p == ' ') p++; memmove(host, p, strlen(p) + 1);
        } else if (strncasecmp(line, "Origin:", 7) == 0) {
            sscanf(line + 7, "%255[^\r\n]", origin);
            char *p = origin; while (*p == ' ') p++; memmove(origin, p, strlen(p) + 1);
            origin_present = AICAM_TRUE;
        }
    }
    if (role == WEB_LISTENER_APP) {
        handle_app(fd, method, full_uri, host);
    } else {
        handle_admin(fd, method, uri, origin, origin_present);
    }
    close(fd);
}

static void *listener(void *arg)
{
    long port = (long)(intptr_t)arg;
    web_listener_role_t role = (port == APP_PORT) ? WEB_LISTENER_APP : WEB_LISTENER_ADMIN;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    int one = 1;
    (void)arg;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, HOST_IP, &a.sin_addr);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 16) != 0) {
        perror("bind"); return NULL;
    }
    printf("[harness] listening %s:%ld role=%d\n", HOST_IP, port, (int)role);
    while (g_keep_running) {
        int fd = accept(s, NULL, NULL);
        if (fd >= 0) handle(fd, role);
    }
    return NULL;
}

int main(void)
{
    pthread_t t1, t2;
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    web_host_policy_begin_admin_update();
    web_host_policy_add_admin_host(HOST_IP);
    web_host_policy_end_admin_update(NULL);
    web_host_policy_set_app_site_authority(HOST_IP ":8081");
    pthread_create(&t1, NULL, listener, (void *)(intptr_t)ADMIN_PORT);
    pthread_create(&t2, NULL, listener, (void *)(intptr_t)APP_PORT);
    printf("[harness] ready admin=%s:%d app=%s:%d\n", HOST_IP, ADMIN_PORT, HOST_IP, APP_PORT);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return 0;
}
