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

static char g_admin_ip[48];
static char g_app_ip[48];
static int g_port;
static volatile int g_keep_running = 1;
static volatile int g_side_effects;
static volatile int g_app_asset_hits;

static const char *ADMIN_PAGE_FMT =
"<!DOCTYPE html><html><body><h1>idle</h1><script>\n"
"const APP_ORIGIN='http://%s:%d';\n"
"const siteOf=(o)=>{const h=o.hostname.toLowerCase();"
"if(/^((25[0-5]|2[0-4]\\\\d|1?\\\\d?\\\\d)\\\\.){3}(25[0-5]|2[0-4]\\\\d|1?\\\\d?\\\\d)$/.test(h)||h.startsWith('[')||h==='localhost')return h;"
"const l=h.split('.');return l.length<=2?l.join('.'):l.slice(-2).join('.');};\n"
"function isCrossSite(a,b){try{const A=new URL(a),B=new URL(b);"
"if(A.protocol!=='http:'&&A.protocol!=='https:')return false;"
"if(B.protocol!=='http:'&&B.protocol!=='https:')return false;"
"if(A.origin===B.origin)return false;return siteOf(A)!==siteOf(B);}catch(e){return false;}}\n"
"function servable(p,appId,adminOrigin){"
"if(p.state!=='active')return 'state';"
"if(!p.app_site_origin||!p.ui_base_path)return 'origin-missing';"
"if(p.app_id!==appId)return 'appid';"
"if(p.host_ui_abi!==1)return 'abi';"
"let o;try{o=new URL(p.app_site_origin).origin}catch(e){return 'url'};"
"if(o!==p.app_site_origin)return 'origin-form';"
"if(adminOrigin===o)return 'same-origin';"
"if(!isCrossSite(adminOrigin,o))return 'same-site';"
"return 'PASS';}\n"
"window.__runScheme=async function(){\n"
"  const r=await fetch('/api/v1/app/sidecar/ui');const p=await r.json();\n"
"  const v=servable(p,'line-crossing',window.location.origin);\n"
"  let iframeLoaded=false,iframeBlocked=false;\n"
"  if(v==='PASS'){const f=document.createElement('iframe');"
"  f.sandbox='allow-scripts';f.src=p.app_site_origin+p.ui_base_path+'index.html';"
"  f.onload=()=>{iframeLoaded=true;};f.onerror=()=>{iframeBlocked=true;};"
"  document.body.appendChild(f);await new Promise(res=>setTimeout(res,3500));f.remove();}\n"
"  const out=document.createElement('pre');out.id='scheme-result';\n"
"  out.textContent=JSON.stringify({verdict:v,iframeLoaded,iframeBlocked,"
"adminOrigin:window.location.origin,appOrigin:p.app_site_origin});\n"
"  document.body.appendChild(out);document.querySelector('h1').textContent='SCHEME-DONE';\n"
"};\n"
"</script></body></html>";

static const char *APP_PAGE =
"<!DOCTYPE html><html><body><h1>app-site</h1><script>\n"
"const ADMIN='http://%s:%d';\n"
"const results=[];function rec(n,ok){results.push({n,ok});}\n"
"async function adminRead(){try{await fetch(ADMIN+'/api/v1/count');rec('admin-read',false);}"
"catch(e){rec('admin-read',true);}}\n"
"async function adminWrite(){try{const r=await fetch(ADMIN+'/api/v1/echo',{method:'POST',mode:'no-cors',body:'x'});rec('admin-write',r.type==='opaque');}"
"catch(e){rec('admin-write',true);}}\n"
"window.__appProbe=async function(){await adminRead();await adminWrite();"
"document.querySelector('h1').textContent='APP|'+JSON.stringify(results);};\n"
"window.__appProbe();\n"
"</script></body></html>";

static void reply(int fd, int code, const char *ctype, const char *body, const char *extra)
{
    char head[640];
    int blen = (int)strlen(body);
    int hlen = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Connection: close\r\nX-Content-Type-Options: nosniff\r\n%s\r\n",
        code, code == 200 ? "OK" : (code == 204 ? "No Content" : "Forbidden"),
        ctype, blen, extra ? extra : "");
    (void)write(fd, head, (size_t)hlen);
    (void)write(fd, body, (size_t)blen);
}

static void handle(int fd)
{
    char buf[4096];
    char method[16] = "";
    char uri[256] = "";
    char host[256] = "";
    char origin[256] = "";
    aicam_bool_t origin_present = AICAM_FALSE;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(fd); return; }
    buf[n] = '\0';
    char *line = strtok(buf, "\r\n");
    if (line) sscanf(line, "%15s %255s", method, uri);
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

    web_entry_verdict_t verdict = web_host_policy_entry(
        host, uri, method, origin, origin_present);

    if (strncmp(uri, "/appsite/", 9) == 0 && verdict == WEB_ENTRY_ALLOW) {
        g_app_asset_hits++;
        char app_page[4096];
        snprintf(app_page, sizeof(app_page), APP_PAGE, g_admin_ip, g_port);
        printf("[scheme] APP asset #%d host='%s' %s\n", g_app_asset_hits, host, uri);
        reply(fd, 200, "text/html", app_page, NULL);
        close(fd); return;
    }

    if (verdict != WEB_ENTRY_ALLOW) {
        printf("[scheme] reject verdict=%d host='%s' %s %s\n", (int)verdict, host, method, uri);
        reply(fd, 403, "application/json",
              verdict == WEB_ENTRY_CROSS_SITE
                  ? "{\"error\":\"cross-site forbidden\"}"
                  : "{\"error\":\"forbidden host\"}",
              NULL);
        close(fd); return;
    }

    if (strcmp(uri, "/admin.html") == 0) {
        char page[8192];
        snprintf(page, sizeof(page), ADMIN_PAGE_FMT, g_app_ip, g_port);
        reply(fd, 200, "text/html", page, NULL);
    } else if (strcmp(uri, "/api/v1/app/sidecar/ui") == 0) {
        char body[512];
        snprintf(body, sizeof(body),
                 "{\"state\":\"active\",\"app_id\":\"line-crossing\","
                 "\"app_site_origin\":\"http://%s:%d\",\"ui_base_path\":\"/appsite/g1/\","
                 "\"host_ui_abi\":1,\"generation\":7}",
                 g_app_ip, g_port);
        reply(fd, 200, "application/json", body, NULL);
    } else if (strcmp(uri, "/api/v1/echo") == 0 && strcmp(method, "POST") == 0) {
        g_side_effects++;
        printf("[scheme] SIDE-EFFECT #%d host='%s' origin='%s'\n", g_side_effects, host, origin);
        reply(fd, 200, "application/json", "{\"ok\":true}", NULL);
    } else if (strcmp(uri, "/api/v1/count") == 0) {
        char body[64];
        snprintf(body, sizeof(body), "{\"count\":%d,\"app_assets\":%d}",
                 g_side_effects, g_app_asset_hits);
        reply(fd, 200, "application/json", body, NULL);
    } else {
        reply(fd, 404, "application/json", "{\"error\":\"not found\"}", NULL);
    }
    close(fd);
}

static void *listener(void *arg)
{
    const char *ip = (const char *)arg;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)g_port);
    inet_pton(AF_INET, ip, &a.sin_addr);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 16) != 0) {
        perror(ip); return NULL;
    }
    printf("[scheme] listening %s:%d\n", ip, g_port);
    while (g_keep_running) {
        int fd = accept(s, NULL, NULL);
        if (fd >= 0) handle(fd);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t t1, t2 = 0;
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s <admin_ip> <app_ip> <port> [nobind]\n", argv[0]);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    snprintf(g_admin_ip, sizeof(g_admin_ip), "%s", argv[1]);
    snprintf(g_app_ip, sizeof(g_app_ip), "%s", argv[2]);
    g_port = atoi(argv[3]);
    web_host_policy_begin_admin_update();
    web_host_policy_add_admin_host(g_admin_ip);
    web_host_policy_add_admin_host("scheme-admin.local");
    web_host_policy_end_admin_update("scheme-admin.local");
    web_host_policy_set_app_site_host(g_app_ip);
    printf("[scheme] admin=%s app=%s port=%d classify(app)=%d classify(admin)=%d\n",
           g_admin_ip, g_app_ip, g_port,
           (int)web_host_policy_classify(g_app_ip),
           (int)web_host_policy_classify(g_admin_ip));
    pthread_create(&t1, NULL, listener, (void *)g_admin_ip);
    if (argc < 5 || strcmp(argv[4], "nobind") != 0) {
        pthread_create(&t2, NULL, listener, (void *)g_app_ip);
    }
    printf("[scheme] ready\n");
    pthread_join(t1, NULL);
    if (t2 != 0) {
        pthread_join(t2, NULL);
    }
    return 0;
}
