#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "lwip/errno.h"
#include "lwip/sockets.h"
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/api.h"
#include "Log/debug.h"
#include "iperf_test.h"
#include "mem.h"

#include "lwip/apps/lwiperf.h"

// #define IPERF_PORT          5001
// #define IPERF_BUFSZ         (256 * 1024)

typedef struct {
    uint8_t is_udp;             // -u
    in_addr_t server_ip;        // -c
    in_addr_t bind_ip;          // -B
    int port;                   // -p
    int buf_size;               // -l
    int run_time_seconds;       // -t
    int print_interval_seconds; // -i
} iperf_arg_t;

#define IPERF_DEFAULT_IS_UDP                    0
#define IPERF_DEFAULT_BIND_IP                   0x00000000
#define IPERF_DEFAULT_PORT                      5001
#define IPERF_DEFAULT_BUFSZ                     (8 * 1024)
#define IPERF_DEFAULT_RUN_TIME_SECONDS          10
#define IPERF_DEFAULT_PRINT_INTERVAL_SECONDS    1
/* Blocking-send cap. Without it a send() that straddles the last free TCP
 * window space parks forever on lwIP's write semaphore once the peer has
 * vanished (no ACKs ever re-open the window) — the task then ignores both
 * -t and -x. Timeout is retried, not fatal, so a slow window only delays. */
#define IPERF_SEND_TIMEOUT_MS                   5000
static volatile uint8_t iperf_status[2] = {0};

static void iperf_server_recv(int client_sock, uint8_t *recv_buf, uint32_t rlen, int print_interval_seconds)
{
    int ret = 0;
    float rate = 0.0f;
    uint32_t last_tick = 0, now_tick = 0, diff_tick = 0;
    uint32_t all_rlen = 0;
    fd_set readfds;
    struct timeval tv;

    last_tick = sys_now();
    while (1) {
        if (iperf_status[1] == 2) break;
        FD_ZERO(&readfds);
        FD_SET(client_sock, &readfds);
        tv.tv_sec = (print_interval_seconds * 1000 - pdTICKS_TO_MS(diff_tick)) / 1000;
        tv.tv_usec = ((print_interval_seconds * 1000 - pdTICKS_TO_MS(diff_tick)) % 1000) * 1000;
        ret = select(client_sock + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            LOG_SIMPLE("Select failed!(errno = %d)!", errno);
            break;
        } else if (ret > 0) {
            if (!FD_ISSET(client_sock, &readfds)) {
                LOG_SIMPLE("Not set client_sock!");
                break;
            }
            ret = recv(client_sock, recv_buf, rlen, 0);
            if (ret <= 0) {
                LOG_SIMPLE("Recv failed(errno = %d)!", errno);
                break;
            }
            all_rlen += ret;
            now_tick = sys_now();
            diff_tick = now_tick < last_tick ? ((portMAX_DELAY - last_tick) + now_tick) : (now_tick - last_tick);
        } else {
            now_tick = sys_now();
            diff_tick = pdMS_TO_TICKS(print_interval_seconds * 1000);
        }

        if (pdTICKS_TO_MS(diff_tick) >= print_interval_seconds * 1000) {
            if (all_rlen > 0) {
                rate = ((float)all_rlen) * 1000.0f / 125.0f / 1024.0f / ((float)(pdTICKS_TO_MS(diff_tick)));
                LOG_SIMPLE("[%u]Recv speed = %.4f Mbps.", now_tick, rate);
                all_rlen = 0;
            } else {
                LOG_SIMPLE("[%u]Recv speed = 0 Mbps.", now_tick);
            }
            last_tick = now_tick;
            diff_tick = 0;
        }
    }
}

static int iperf_wait_socket_writable(int sock, uint32_t timeout_ms)
{
    fd_set writefds;
    struct timeval tv;

    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);

    tv.tv_sec = timeout_ms / 1000U;
    tv.tv_usec = (timeout_ms % 1000U) * 1000U;

    return select(sock + 1, NULL, &writefds, NULL, &tv);
}

static void iperf_server(void *args)
{
    int server_sock = -1, client_sock = -1, ret = 0;
    uint8_t *recv_buf = NULL;
    fd_set readfds;
    struct timeval tv;
    socklen_t sin_size = 0;
    iperf_arg_t *iperf_arg = (iperf_arg_t *)args;
    struct sockaddr_in addr = {0};
    if (iperf_arg == NULL) {
        LOG_SIMPLE("Invalid args!");
        iperf_status[1] = 0;
        return;
    }

    LOG_SIMPLE("Start iperf server...");
    recv_buf = hal_mem_alloc(iperf_arg->buf_size, MEM_LARGE);
    if (recv_buf == NULL) {
        LOG_SIMPLE("Malloc recv_buf failed!");
        iperf_status[1] = 0;
        return;
    }
    memset(recv_buf, 0, iperf_arg->buf_size);

    server_sock = socket(AF_INET, (iperf_arg->is_udp ? SOCK_DGRAM : SOCK_STREAM), 0);
    if (server_sock < 0) {
        LOG_SIMPLE("Server socket create failed(errno = %d)!", errno);
        goto iperf_server_end;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = iperf_arg->bind_ip;
    addr.sin_port = htons(iperf_arg->port);
    ret = bind(server_sock, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    if (ret < 0) {
        LOG_SIMPLE("Server socket bind failed(errno = %d)!", errno);
        goto iperf_server_end;
    }

    if (!iperf_arg->is_udp) {
        ret = listen(server_sock, 5);
        if (ret < 0) {
            LOG_SIMPLE("Server socket listen failed(errno = %d)!", errno);
            goto iperf_server_end;
        }

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        while (1) {
            if (iperf_status[1] == 2) break;

            FD_ZERO(&readfds);
            FD_SET(server_sock, &readfds);
            ret = select(server_sock + 1, &readfds, NULL, NULL, &tv);
            if (ret < 0) {
                LOG_SIMPLE("Select failed(errno = %d)!", errno);
                break;
            } else if (ret == 0) continue;

            sin_size = sizeof(addr);
            client_sock = accept(server_sock,(struct sockaddr *)&addr, &sin_size);
            if (client_sock < 0) {
                LOG_SIMPLE("Server socket accept failed!(errno = %d)!", errno);
                goto iperf_server_end;
            }
            LOG_SIMPLE("Client socket accepted!");
            // Receive data
            iperf_server_recv(client_sock, recv_buf, iperf_arg->buf_size, iperf_arg->print_interval_seconds);
        }
    } else {
        // Receive data
        iperf_server_recv(server_sock, recv_buf, iperf_arg->buf_size, iperf_arg->print_interval_seconds);
    }

iperf_server_end:
    if (client_sock >= 0) {
        if (!iperf_arg->is_udp) shutdown(client_sock, SHUT_RDWR);
        close(client_sock);
    }
    if (server_sock >= 0) {
        if (!iperf_arg->is_udp) shutdown(server_sock, SHUT_RDWR);
        close(server_sock);
    }
    hal_mem_free(recv_buf);
    hal_mem_free(iperf_arg);
    LOG_SIMPLE("iperf server stoped.");
    iperf_status[1] = 0;
    vTaskDelete(NULL);
}

static void iperf_client(void *args)
{
    int client_sock = -1, i = 0, ret = 0;
    uint8_t *send_buf = NULL;
    float rate = 0.0f;
    uint32_t start_tick = 0, last_tick = 0, now_tick = 0, diff_tick = 0;
    uint32_t all_slen = 0;
    iperf_arg_t *iperf_arg = (iperf_arg_t *)args;
    struct sockaddr_in addr = {0};
    if (iperf_arg == NULL) {
        LOG_SIMPLE("Invalid args!");
        iperf_status[0] = 0;
        return;
    }

    LOG_SIMPLE("Start iperf client...");
    send_buf = hal_mem_alloc(iperf_arg->buf_size, MEM_LARGE);
    if (send_buf == NULL) {
        LOG_SIMPLE("Malloc send_buf failed");
        iperf_status[0] = 0;
        return;
    }

    for (i = 0; i < iperf_arg->buf_size; i++)
        send_buf[i] = i & 0xff;
    
    client_sock = socket(AF_INET, (iperf_arg->is_udp ? SOCK_DGRAM : SOCK_STREAM), 0);
    if (client_sock < 0) {
        LOG_SIMPLE("Client socket create failed!(errno = %d)", errno);
        goto iperf_client_end;
    }

    if (!iperf_arg->is_udp) {
        int snd_timeout_ms = IPERF_SEND_TIMEOUT_MS;
        setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &snd_timeout_ms, sizeof(snd_timeout_ms));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(lwip_port_rand() % 0xfffe + 1);
    addr.sin_addr.s_addr = iperf_arg->bind_ip;
    ret = bind(client_sock, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    if (ret < 0) {
        LOG_SIMPLE("Client socket bind failed!(errno = %d)", errno);
        goto iperf_client_end;
    }

    addr.sin_port = htons(iperf_arg->port);
    addr.sin_addr.s_addr = iperf_arg->server_ip;
    if (!iperf_arg->is_udp) {
        ret = connect(client_sock, (struct sockaddr *)&addr, sizeof(struct sockaddr));
        if (ret < 0) {
            LOG_SIMPLE("Connect failed!(errno = %d)", errno);
            goto iperf_client_end;
        }
    }
    LOG_SIMPLE("Connect to iperf server successful!");

    // Start streaming
    last_tick = sys_now();
    start_tick = last_tick;
    while (1) {
        if (iperf_status[0] == 2) break;

        if (iperf_arg->is_udp) {
            ret = iperf_wait_socket_writable(client_sock, 200);
            if (ret == 0) {
                LOG_SIMPLE("send delay!");
                osDelay(2);
                continue;
            } else if (ret < 0) {
                LOG_SIMPLE("Select failed!(errno = %d)!", errno);
                goto iperf_client_end;
            }

            ret = sendto(client_sock, send_buf, iperf_arg->buf_size, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr));
        } else {
            uint32_t remain = (uint32_t)iperf_arg->buf_size;
            uint8_t *p = send_buf;

            while (remain > 0) {
                if (iperf_status[0] == 2) break;

                /* Deadline must hold inside the partial-write loop too: a
                 * trickling window would otherwise spin here past -t. */
                now_tick = sys_now();
                diff_tick = now_tick < start_tick ? ((portMAX_DELAY - start_tick) + now_tick) : (now_tick - start_tick);
                if (pdTICKS_TO_MS(diff_tick) >= (uint32_t)iperf_arg->run_time_seconds * 1000)
                    goto iperf_client_end;

                ret = iperf_wait_socket_writable(client_sock, 200);
                if (ret == 0) {
                    LOG_SIMPLE("send delay!");
                    osDelay(2);
                    continue;
                } else if (ret < 0) {
                    LOG_SIMPLE("Select failed!(errno = %d)!", errno);
                    goto iperf_client_end;
                }

                ret = send(client_sock, p, remain, 0);
                if (ret <= 0) break;

                all_slen += (uint32_t)ret;
                p += ret;
                remain -= (uint32_t)ret;
            }
        }
        if (ret <= 0) {
            /* EAGAIN/ETIMEDOUT = window closed or send timeout: retry until
             * -t or -x decides. Anything else (reset, pipe, abort) is a dead
             * peer — bail out. */
            if (errno != EAGAIN && errno != ETIMEDOUT) {
                LOG_SIMPLE("send failed!(errno = %d)", errno);
                goto iperf_client_end;
            }
            LOG_SIMPLE("send delay!");
            osDelay(2);
        } else if (iperf_arg->is_udp) {
            all_slen += (uint32_t)ret;
        }

        now_tick = sys_now();
        diff_tick = now_tick < last_tick ? ((portMAX_DELAY - last_tick) + now_tick) : (now_tick - last_tick);
        if (diff_tick >= pdMS_TO_TICKS(iperf_arg->print_interval_seconds * 1000)) {
            if (all_slen > 0) {
                rate = ((float)all_slen) * 1000.0f / 125.0f / 1024.0f / ((float)pdTICKS_TO_MS(diff_tick));
                LOG_SIMPLE("[%u]Send speed = %.4f Mbps!", now_tick, rate);
                all_slen = 0;
            } else {
                LOG_SIMPLE("[%u]Send speed = 0 Mbps!", now_tick);
            }
            last_tick = now_tick;
        }

        diff_tick = now_tick < start_tick ? ((portMAX_DELAY - start_tick) + now_tick) : (now_tick - start_tick);
        if (pdTICKS_TO_MS(diff_tick) >= iperf_arg->run_time_seconds * 1000) break;
    }

iperf_client_end:
    if (client_sock >= 0) {
        if (!iperf_arg->is_udp) shutdown(client_sock, SHUT_RDWR);
        close(client_sock);
    }
    hal_mem_free(send_buf);
    hal_mem_free(iperf_arg);
    LOG_SIMPLE("iperf client stoped.");
    iperf_status[0] = 0;
    vTaskDelete(NULL);
}

static void iperf_test_help(void)
{
    LOG_SIMPLE("Usage: iperf [-s|-c host] [options]");
    LOG_SIMPLE("       iperf [-h|--help] [-v|--version]\r\n");
    LOG_SIMPLE("Client/Server:");
    LOG_SIMPLE("  -i, --interval  #        seconds between periodic bandwidth reports (default 1 secs)");
    LOG_SIMPLE("  -l, --len       #[KM]    length of buffer to read or write (default 8 KB)");
    LOG_SIMPLE("  -p, --port      #        server port to listen on/connect to (default 5001)");
    LOG_SIMPLE("  -u, --udp                use UDP rather than TCP");
    LOG_SIMPLE("  -B, --bind      <host>   bind to <host>, an interface or multicast address (default 0.0.0.0)");
    LOG_SIMPLE("  -x, --exit               Close the connection and exit\r\n");
    LOG_SIMPLE("Server specific:");
    LOG_SIMPLE("  -s, --server             run in server mode\r\n");
    LOG_SIMPLE("Client specific:");
    LOG_SIMPLE("  -c, --client    <host>   run in client mode, connecting to <host>");
    LOG_SIMPLE("  -t, --time      #        time in seconds to transmit for (default 10 secs)\r\n");
    LOG_SIMPLE("Miscellaneous:");
    LOG_SIMPLE("  -h, --help               print this message and quit");
    LOG_SIMPLE("  -v, --version            print version information and quit\r\n");
}

int iperf_test_cmd_deal(int argc, char* argv[])
{
    int i = 0;
    uint8_t is_valid = 0, is_server = 0, is_exit = 0;
    iperf_arg_t *iperf_arg = NULL;
    if (argc < 2) {
        iperf_test_help();
        return -1;
    }

    iperf_arg = hal_mem_alloc(sizeof(iperf_arg_t), MEM_LARGE);
    if (iperf_arg == NULL) {
        LOG_SIMPLE("Memory alloc failed!");
        return -1;
    }
    memset(iperf_arg, 0, sizeof(iperf_arg_t));
    iperf_arg->is_udp = IPERF_DEFAULT_IS_UDP;
    iperf_arg->bind_ip = IPERF_DEFAULT_BIND_IP;
    iperf_arg->port = IPERF_DEFAULT_PORT;
    iperf_arg->buf_size = IPERF_DEFAULT_BUFSZ;
    iperf_arg->print_interval_seconds = IPERF_DEFAULT_PRINT_INTERVAL_SECONDS;
    iperf_arg->run_time_seconds = IPERF_DEFAULT_RUN_TIME_SECONDS;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--udp") == 0) {
            iperf_arg->is_udp = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--server") == 0) {
            if (is_valid) {
                iperf_test_help();
                hal_mem_free(iperf_arg);
                return -1;
            }
            is_valid = 1;
            is_server = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            iperf_test_help();
            hal_mem_free(iperf_arg);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            LOG_SIMPLE("iperf version 1.0.0 (09 Sep 2025) stm32 lwip\r\n");
            hal_mem_free(iperf_arg);
            return 0;
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--exit") == 0) {
            is_exit = 1;
        } else {
            if ((i + 1) == argc) {
                if (is_exit && (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--client") == 0)) {
                    if (is_valid) {
                        iperf_test_help();
                        hal_mem_free(iperf_arg);
                        return -1;
                    }
                    is_valid = 1;
                    break;
                }
                LOG_SIMPLE("Miss args!");
                hal_mem_free(iperf_arg);
                return -1;
            }
            if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--len") == 0) {
                iperf_arg->buf_size = atoi(argv[i + 1]);
                if (strchr(argv[i + 1], 'k') != NULL || strchr(argv[i + 1], 'K') != NULL) {
                    iperf_arg->buf_size *= 1024;
                } else if (strchr(argv[i + 1], 'm') != NULL || strchr(argv[i + 1], 'M') != NULL) {
                    iperf_arg->buf_size *= (1024 * 1024);
                }
                if (iperf_arg->buf_size <= 0 || iperf_arg->buf_size > 1024 * 1024 * 4) {
                    LOG_SIMPLE("Invalid -l args!");
                    hal_mem_free(iperf_arg);
                    return -1;
                }
                i++;
            } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
                iperf_arg->port = atoi(argv[i + 1]);
                if (iperf_arg->port <= 0 || iperf_arg->port > 65535) {
                    LOG_SIMPLE("Invalid -p args!");
                    hal_mem_free(iperf_arg);
                    return -1;
                }
                i++;
            } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interval") == 0) {
                iperf_arg->print_interval_seconds = atoi(argv[i + 1]);
                if (iperf_arg->print_interval_seconds <= 0) {
                    LOG_SIMPLE("Invalid -i args!");
                    hal_mem_free(iperf_arg);
                    return -1;
                }
                i++;
            } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--time") == 0) {
                iperf_arg->run_time_seconds = atoi(argv[i + 1]);
                if (iperf_arg->run_time_seconds <= 0) {
                    LOG_SIMPLE("Invalid -t args!");
                    hal_mem_free(iperf_arg);
                    return -1;
                }
                i++;
            } else if (strcmp(argv[i], "-B") == 0 || strcmp(argv[i], "--bind") == 0) {
                iperf_arg->bind_ip = inet_addr(argv[i + 1]);
                i++;
            } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--client") == 0) {
                if (is_valid) {
                    iperf_test_help();
                    hal_mem_free(iperf_arg);
                    return -1;
                }
                if (strcmp(argv[i + 1], "-x") == 0 || strcmp(argv[i + 1], "--exit") == 0) {
                    is_exit = 1;
                } else iperf_arg->server_ip = inet_addr(argv[i + 1]);
                is_valid = 1;
                i++;
            }
        }
    }

    if (!is_valid) {
        iperf_test_help();
        hal_mem_free(iperf_arg);
        return -1;
    }

    if (is_server) {
        if (is_exit) {
            /* Accept the stop request from any non-idle state (1 = running,
             * 2 = stop already pending): rejecting status 2 wedged the CLI
             * when the task was blocked and never consumed the first stop. */
            if (iperf_status[1] == 0) {
                LOG_SIMPLE("iperf server already stopped");
                hal_mem_free(iperf_arg);
                return -2;
            } else {
                iperf_status[1] = 2;
                hal_mem_free(iperf_arg);
                return -2;
            }
        } else {
            if (iperf_status[1] != 0) {
                LOG_SIMPLE("iperf server already running");
                hal_mem_free(iperf_arg);
                return -2;
            } else {
                iperf_status[1] = 1;
                sys_thread_new("iperf_server", iperf_server, iperf_arg, 1024, 62);
            }
        }
    } else {
        if (is_exit) {
            if (iperf_status[0] == 0) {
                LOG_SIMPLE("iperf client already stopped");
                hal_mem_free(iperf_arg);
                return -2;
            } else {
                iperf_status[0] = 2;
                hal_mem_free(iperf_arg);
                return -2;
            }
        } else {
            if (iperf_status[0] != 0) {
                LOG_SIMPLE("iperf client already running");
                hal_mem_free(iperf_arg);
                return -2;
            } else {
                iperf_status[0] = 1;
                sys_thread_new("iperf_client", iperf_client, iperf_arg, 1024, 50);
            }
        }
    }
    
    return 0;
}

//:iperf client stop/start 192.168.10.100iperf server stop/start
debug_cmd_reg_t iperf_test_cmd_table[] = {
    {"iperf",    "Iperf test.",      iperf_test_cmd_deal},
};

static void iperf_test_cmd_register(void)
{
    debug_cmdline_register(iperf_test_cmd_table, sizeof(iperf_test_cmd_table) / sizeof(iperf_test_cmd_table[0]));
}

void iperf_test_register(void)
{
    driver_cmd_register_callback("iperf", iperf_test_cmd_register);
}
