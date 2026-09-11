#ifndef __WEB_CONFIG_H__
#define __WEB_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#define IS_HTTPS            0
#define HTTPS_PORT          443

/**
 * WEB_DEBUG - Global web debug switch (compile-time).
 * Build with -DWEB_DEBUG=1 (or set to 1 here) to enable:
 *  - [WEBDBG] request/response line per HTTP API: method, uri, status, error_code, duration
 *  - [WSDBG]  websocket control-frame logs (ping/pong)
 * Timestamps are prefixed inline. Mongoose socket logs stay off (too noisy;
 * re-add mg_log_set(MG_LL_DEBUG) in the server tasks if ever needed).
 */
#ifndef WEB_DEBUG
#define WEB_DEBUG 0
#endif

#if IS_HTTPS
#define HTTPS_CERT_STR     "-----BEGIN CERTIFICATE-----\n\
MIIECzCCAvOgAwIBAgIBADANBgkqhkiG9w0BAQsFADCBnzELMAkGA1UEBhMCQ04x\n\
EDAOBgNVBAgMB0JlaWppbmcxEDAOBgNVBAcMB0JlaWppbmcxGDAWBgNVBAoMD015\n\
IE9yZ2FuaXphdGlvbjEWMBQGA1UECwwNSVQgRGVwYXJ0bWVudDEWMBQGA1UEAwwN\n\
MTkyLjE2OC4xMC4xMDEiMCAGCSqGSIb3DQEJARYTYWRtaW5AMTkyLjE2OC4xMC4x\n\
MDAeFw0yNTA5MTcwMjExMjJaFw0yNTEyMTYwMjExMjJaMIGfMQswCQYDVQQGEwJD\n\
TjEQMA4GA1UECAwHQmVpamluZzEQMA4GA1UEBwwHQmVpamluZzEYMBYGA1UECgwP\n\
TXkgT3JnYW5pemF0aW9uMRYwFAYDVQQLDA1JVCBEZXBhcnRtZW50MRYwFAYDVQQD\n\
DA0xOTIuMTY4LjEwLjEwMSIwIAYJKoZIhvcNAQkBFhNhZG1pbkAxOTIuMTY4LjEw\n\
LjEwMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlRGzSpulFPwYJ1cF\n\
XiDVdPCbajEl6Xja6DKI1qjqQXPLhhogom/J4BriwBHlg4zJ8IBh2RS19uP6/ifK\n\
9MTxuNadoLrpUwjOIAgQ1FICAK1FJPd7O0eFq9H8A1iob6nSVBM5YhE8daoExh7G\n\
/LYQABCIdnUVE/zBN93IjGatFy/T/o6y2BJogPQlsav/gXkBNHJGL/nwYV7pv1mq\n\
D+3qeOI+3BQo3Gz//htGi7xOHOmMzQtdZHvp377Fg7f6PcUJewQ8zrRqJZYTtxxM\n\
u3ow6kpMJr2VKPmwdSQuDlz01SevHQ87HCNDaAW10aXFtDrbj8t2gq0Fp6by1O8Y\n\
G3z14QIDAQABo1AwTjAdBgNVHQ4EFgQUAopHsydGJi1zSzV356bcDCNgkAIwHwYD\n\
VR0jBBgwFoAUAopHsydGJi1zSzV356bcDCNgkAIwDAYDVR0TBAUwAwEB/zANBgkq\n\
hkiG9w0BAQsFAAOCAQEAkM8ac354L8btdczpAZRaZk/sdTqvBM0C/DooLcomhPp8\n\
Kp1vlREPcco0vBTn1w6t+zfZZDk8qd4T1pg8A3rnuvxbX5Is15IM3AprYBn3Ayxf\n\
/NHj1gGArBueP172Wi3AJo7/ML5r3tYjMf9lbon1ml2LP/l/OrrwTJfcD/UuyEgr\n\
Ujl2jNOuk/q8ZmA8U9df3dD2p4aedJgZ7FtR6sdEiimR6ooiVq9Dm3NPxv03ghcn\n\
ittfovLxjNoyr1JxgB6n021f/b19Qjus05Wld5J0vsMhr7SsKi9Z0Wff39QbUejR\n\
jTUX5z3irxunmcblbhfZGNDVCjtpM61H41HkBe38Dg==\n\
-----END CERTIFICATE-----\n"
#define HTTPS_KEY_STR      "-----BEGIN PRIVATE KEY-----\n\
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCVEbNKm6UU/Bgn\n\
VwVeINV08JtqMSXpeNroMojWqOpBc8uGGiCib8ngGuLAEeWDjMnwgGHZFLX24/r+\n\
J8r0xPG41p2guulTCM4gCBDUUgIArUUk93s7R4Wr0fwDWKhvqdJUEzliETx1qgTG\n\
Hsb8thAAEIh2dRUT/ME33ciMZq0XL9P+jrLYEmiA9CWxq/+BeQE0ckYv+fBhXum/\n\
WaoP7ep44j7cFCjcbP/+G0aLvE4c6YzNC11ke+nfvsWDt/o9xQl7BDzOtGollhO3\n\
HEy7ejDqSkwmvZUo+bB1JC4OXPTVJ68dDzscI0NoBbXRpcW0OtuPy3aCrQWnpvLU\n\
7xgbfPXhAgMBAAECggEAcDURo0BDY+daewpK1Q6b/lk6cxWptvMsvAmF7Sbapgf2\n\
k+vI0tyYtaMnOXJ/M6VfQDQy8wde7Qewn2zunY49cWfC4QCwrrr7BSttF5TfQwkp\n\
+eh7jySIHsyCCTbMGrlWw8hwsjvNKbifvU3fdMvKgXHwdlItWo0wF9BOrDiBY+iL\n\
Zi9M0KVBg5zHvtHRd4LPaqI+s/I0eXLASgTxv2jyi4FNUs0o5ZoRv1WJEFrVo2xU\n\
HCOsGgqBwZLKClfSHHX3rZ1DEEaqiHLSDNxVdmLv6quC+MXlmTvEvj6RbPixg88b\n\
Z5ZH2LpVWBmSUVXD9pvIv7mY8rygJw7iNU0d6zMAAQKBgQDGhVApds8wrHTW7dSQ\n\
/HozIvJLzOH3qh8Am7RPExxipKHc7jHxdJcuwzcti5l20XMUa6UvRmnUk/xeM0ga\n\
63VxGQGwtVCnAkFTPUAjFvE2yZiACMBBpEDjCOzqGboyWFJm7jdWFOdK9z4mT8+j\n\
9S89KZU6yDOrTg/jsF4LHiYgAQKBgQDAOvOt1gOcQGJVTPLbPIioUBtlC2cuTxS5\n\
wCHR7XWW8DqA+M4OmjGUjEKHGaMzol1+8/sllROoGXSTDiXbt/YOV0NQJeXLf2J1\n\
sqqwqwZkzG/Hee/HpvG9tC/Au5Zz+rBzkXhdguXGCdugPluY63E7MLEAYAeTPPmw\n\
K4d1o1rV4QKBgQCt5NMOLxtYeIg0SMo9YlusdX0mdsatiiB0CPANoCDqK2n5u9CV\n\
v9o6RRgNVk5MbTXP+mcMnTJQ2nxjC6qqofwS4KPBZWHLmUcdofaPhiYvJrHl3USD\n\
e1y3Qvc8LOMT/JfZv+tLBS6BVUfkiV7KMRh7C/TDM+FGui9i8/e852DgAQKBgEU5\n\
taFnjdtKMF9Jm7eqAAik+IiV5618mxkdgNBptEwL7PWfJA2MJ8i4Dgk7CVPB5+ud\n\
D1eEbRS/PgTrNmT+xaR6dmo5i5ySHjIGioOew2mvWZ27YsdCbpIDfqLoqxDPOZYU\n\
0ATU68w6ppX5fuD+AMPxD/zzmCbE8aIoutp5Xm/hAoGAC9ZwBRpwE4RyKQSPWh24\n\
LVU61vaTLDumqAlFOwjCqlMg9hqI8hVFFNiJ9oOKb0Vlngr5l6Zqd/CLvrF/ROOd\n\
2jJgo+c31wsbzOX+rwxb+tukq+IFN2hX7gOLdsSn5hsfjjaEdsMyTgq0+yJ6uAYc\n\
UvhJXTDPOrnV89vQrt2fvkM=\n\
-----END PRIVATE KEY-----\n"
#endif

#ifdef __cplusplus
}
#endif

#endif // __WEB_CONFIG_H__
