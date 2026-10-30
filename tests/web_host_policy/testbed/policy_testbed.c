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

#define ADMIN_IP "localhost"
#define APP_IP   "127.0.0.1"
#define PORT     8080

static volatile int g_side_effects;
static volatile int g_stream_admitted;
static volatile int g_stream_rejected;
static volatile int g_keep_running = 1;

static const char *ATTACK_PAGE =
"<!DOCTYPE html><html><body><h1>app-site</h1><script>\n"
"const ADMIN='http://" ADMIN_IP ":8080';\n"
"const results=[];\n"
"function record(name, ok){results.push({name, ok});}\n"
"async function simplePost(){\n"
"  try{const r=await fetch(ADMIN+'/api/v1/echo',{method:'POST',mode:'no-cors',\n"
"    headers:{'Content-Type':'text/plain'},body:'attack'});\n"
"    record('simple-post', r.type==='opaque');\n"
"  }catch(e){record('simple-post', false);}\n"
"}\n"
"async function cachedBasicPost(){\n"
"  try{const r=await fetch(ADMIN+'/api/v1/echo',{method:'POST',mode:'no-cors',\n"
"    headers:{'Content-Type':'text/plain'},body:'attack'});\n"
"    record('basic-cred-post', r.type==='opaque');\n"
"  }catch(e){record('basic-cred-post', false);}\n"
"}\n"
"async function corsPost(){\n"
"  try{const r=await fetch(ADMIN+'/api/v1/echo',{method:'POST',\n"
"    headers:{'Content-Type':'application/json','Authorization':'Basic dXNlcjpwYXNz'},\n"
"    body:'{}'});\n"
"    record('preflight-post', r.status===403);\n"
"  }catch(e){record('preflight-post', true);}\n"
"}\n"
"async function otaPreflight(){\n"
"  try{const r=await fetch(ADMIN+'/api/v1/system/ota/upload',{method:'OPTIONS',\n"
"    headers:{'Access-Control-Request-Method':'POST','Authorization':'Bearer x'}});\n"
"    record('ota-preflight', r.status===403);\n"
"  }catch(e){record('ota-preflight', true);}\n"
"}\n"
"async function toggleGet(){\n"
"  try{const r=await fetch(ADMIN+'/api/v1/ai/toggle',{mode:'no-cors'});\n"
"    record('side-effect-get', r.type==='opaque');\n"
"  }catch(e){record('side-effect-get', false);}\n"
"}\n"
"function formNav(){\n"
"  return new Promise(res=>{\n"
"    const f=document.createElement('form');f.method='POST';f.action=ADMIN+'/api/v1/echo';\n"
"    f.target='probe';const i=document.createElement('input');i.name='x';i.value='1';f.appendChild(i);\n"
"    const fr=document.createElement('iframe');fr.name='probe';fr.style.display='none';\n"
"    document.body.appendChild(fr);document.body.appendChild(f);f.submit();\n"
"    setTimeout(()=>{f.remove();fr.remove();res();},1200);\n"
"  });\n"
"}\n"
"async function apiOnAppHost(){\n"
"  try{const r=await fetch('/api/v1/echo',{method:'POST'});\n"
"    record('api-on-app-host', r.status===403);\n"
"  }catch(e){record('api-on-app-host', false);}\n"
"}\n"
"async function appsiteAsset(){\n"
"  const r=await fetch('/appsite/g1/index.html');\n"
"  record('appsite-asset', r.status===200);\n"
"}\n"
"async function adminReadFromApp(){\n"
"  try{const r=await fetch(ADMIN+'/api/v1/count');\n"
"    record('admin-read-blocked', r.status===403);\n"
"  }catch(e){record('admin-read-blocked', true);}\n"
"}\n"
"window.__runTests=async function(){\n"
"  await appsiteAsset(); await apiOnAppHost(); await adminReadFromApp();\n"
"  await simplePost(); await cachedBasicPost(); await corsPost(); await otaPreflight(); await toggleGet(); await formNav();\n"
"  return JSON.stringify(results);\n"
"};\n"
"</script></body></html>";

static const char *ADMIN_PAGE =
"<!DOCTYPE html><html><body><h1>admin</h1><script>\n"
"window.__adminPost=async function(){\n"
"  const r=await fetch('/api/v1/echo',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});\n"
"  return r.status;\n"
"};\n"
"window.__adminToggle=async function(){\n"
"  const r=await fetch('/api/v1/ai/toggle',{method:'POST',headers:{'Authorization':'Basic YWRtaW46dGVzdGJ3ZA=='}});\n"
"  return r.status;\n"
"};\n"
"</script></body></html>";

static void reply(int fd, int code, const char *ctype, const char *body, const char *extra)
{
    char head[512];
    int blen = (int)strlen(body);
    int hlen = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Connection: close\r\n%s\r\n",
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
    char authorization[256] = "";
    aicam_bool_t origin_present = AICAM_FALSE;
    aicam_bool_t bearer = AICAM_FALSE;
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
        } else if (strncasecmp(line, "Authorization:", 14) == 0) {
            sscanf(line + 14, "%255[^\r\n]", authorization);
            char *p = authorization; while (*p == ' ') p++; memmove(authorization, p, strlen(p) + 1);
        }
    }
    if (strncasecmp(authorization, "Bearer ", 7) == 0) {
        bearer = AICAM_TRUE;
    }

    (void)bearer;
    web_entry_verdict_t verdict = web_host_policy_entry(
        host, uri, method, origin, origin_present);

    if (strcmp(uri, "/api/v1/system/ota/upload") == 0 ||
        strcmp(uri, "/api/v1/files/upload") == 0) {
        if (strcmp(method, "OPTIONS") == 0) {
            printf("[policy] STREAM preflight host='%s' origin='%s'\n", host, origin);
            if (web_host_policy_classify(host) == WEB_HOST_ADMIN &&
                web_host_policy_origin_allowed(origin, host) == AICAM_TRUE) {
                char extra[512];
                snprintf(extra, sizeof(extra),
                         "Access-Control-Allow-Origin: %.200s\r\n"
                         "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                         "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
                         origin);
                reply(fd, 204, "text/plain", "", extra);
            } else {
                reply(fd, 403, "application/json", "{\"error\":\"preflight forbidden\"}", NULL);
            }
            close(fd); return;
        }
        if (verdict == WEB_ENTRY_ALLOW) {
            if (strcmp(authorization, "Basic YWRtaW46dGVzdGJ3ZA==") == 0) {
                g_stream_admitted++;
                printf("[policy] STREAM admitted #%d host='%s' origin='%s' auth='[ok]'\n",
                       g_stream_admitted, host, origin);
                reply(fd, 200, "application/json", "{\"stream\":\"started\"}", NULL);
            } else {
                g_stream_rejected++;
                printf("[policy] STREAM unauthorized #%d host='%s' origin='%s'\n",
                       g_stream_rejected, host, origin);
                reply(fd, 401, "application/json", "{\"error\":\"unauthorized\"}", NULL);
            }
        } else {
            printf("[policy] STREAM blocked verdict=%d host='%s' origin='%s' auth='%s'\n",
                   (int)verdict, host, origin, authorization);
            reply(fd, 403, "application/json",
                  verdict == WEB_ENTRY_CROSS_SITE
                      ? "{\"error\":\"cross-site forbidden\"}"
                      : "{\"error\":\"forbidden host\"}",
                  NULL);
        }
        close(fd); return;
    }

    if (verdict == WEB_ENTRY_UNKNOWN_HOST || verdict == WEB_ENTRY_APP_HOST_API) {
        printf("[policy] host rejected verdict=%d host='%s' uri=%s\n", (int)verdict, host, uri);
        reply(fd, 403, "application/json", "{\"error\":\"forbidden host\"}", NULL);
        close(fd); return;
    }
    if (verdict == WEB_ENTRY_CROSS_SITE) {
        printf("[policy] CSRF host='%s' origin='%s' %s %s auth='%s'\n", host, origin, method, uri, authorization);
        reply(fd, 403, "application/json", "{\"error\":\"cross-site forbidden\"}", NULL);
        close(fd); return;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        if (web_host_policy_classify(host) == WEB_HOST_ADMIN &&
            web_host_policy_origin_allowed(origin, host) == AICAM_TRUE) {
            char extra[512];
            snprintf(extra, sizeof(extra),
                     "Access-Control-Allow-Origin: %.200s\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
                     origin);
            reply(fd, 204, "text/plain", "", extra);
            printf("[policy] preflight host='%s' origin='%s' -> allowed\n", host, origin);
        } else {
            printf("[policy] preflight host='%s' origin='%s' -> 403\n", host, origin);
            reply(fd, 403, "application/json", "{\"error\":\"preflight forbidden\"}", NULL);
        }
        close(fd); return;
    }

    if (strcmp(uri, "/api/v1/echo") == 0 && strcmp(method, "POST") == 0) {
        g_side_effects++;
        printf("[policy] SIDE-EFFECT #%d host='%s' origin='%s'\n", g_side_effects, host, origin);
        reply(fd, 200, "application/json", "{\"ok\":true}", NULL);
    } else if (strcmp(uri, "/api/v1/ai/toggle") == 0 &&
               (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0)) {
        g_side_effects++;
        printf("[policy] SIDE-EFFECT(toggle) #%d host='%s' origin='%s'\n", g_side_effects, host, origin);
        reply(fd, 200, "application/json", "{\"ai\":false}", NULL);
    } else if (strcmp(uri, "/api/v1/count") == 0) {
        char body[96];
        snprintf(body, sizeof(body),
                 "{\"count\":%d,\"stream_admitted\":%d,\"stream_rejected\":%d}",
                 g_side_effects, g_stream_admitted, g_stream_rejected);
        reply(fd, 200, "application/json", body, NULL);
    } else if (strncmp(uri, "/appsite/", 9) == 0) {
        reply(fd, 200, "text/html", ATTACK_PAGE, NULL);
    } else if (strcmp(uri, "/admin.html") == 0) {
        reply(fd, 200, "text/html", ADMIN_PAGE, NULL);
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
    a.sin_port = htons(PORT);
    inet_pton(AF_INET, ip, &a.sin_addr);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 16) != 0) {
        perror(ip); return NULL;
    }
    printf("[testbed] listening %s:%d\n", ip, PORT);
    while (g_keep_running) {
        int fd = accept(s, NULL, NULL);
        if (fd >= 0) handle(fd);
    }
    return NULL;
}

int main(void)
{
    pthread_t t1, t2;
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    web_host_policy_begin_admin_update();
    web_host_policy_add_admin_host(ADMIN_IP);
    web_host_policy_add_admin_host("testbed-admin.local");
    web_host_policy_end_admin_update("testbed-admin.local");
    web_host_policy_set_app_site_host(APP_IP);
    pthread_create(&t1, NULL, listener, (void *)ADMIN_IP);
    pthread_create(&t2, NULL, listener, (void *)APP_IP);
    printf("[testbed] ready\n");
    fflush(stdout);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return 0;
}
