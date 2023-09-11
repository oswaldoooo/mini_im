#include "ctools/stdlib/io.h"
#include "socket/socket.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
static u_char global_buffer_sec[1024];
int connect_tcp(const char* host, const int port)
{
    int sockid = socket(AF_INET, SOCK_STREAM, 0);
    if (sockid < 0) return sockid;
    struct sockaddr_in sc;
    sc.sin_family = AF_INET;
    sc.sin_port = htons(port);
    sc.sin_addr.s_addr = inet_addr(host);
    if (connect(sockid, (struct sockaddr*)&sc, sizeof(struct sockaddr_in)) < 0) return -1;
    return sockid;
}
int listen_tcp(const int port)
{
    int sockid = socket(AF_INET, SOCK_STREAM, 0);
    if (sockid < 0) return sockid;
    struct sockaddr_in sc;
    sc.sin_port = htons(port);
    sc.sin_family = AF_INET;
    sc.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockid, (struct sockaddr*)&sc, sizeof(struct sockaddr_in)) < 0) return -2;
    if (listen(sockid, 0) < 0) return -3;
    return sockid;
}
ssize_t write_to_client(int cli_id, uint8_t act_code, const u_char* content)
{
    size_t len = strlen((char*)content);
    u_char final_content[1 + len];
    final_content[0] = act_code;
    memmove(final_content + 1, content, len);
    return write(cli_id, final_content, len + 1);
}
const u_char* writeTo(uint8_t code, const u_char* content)
{
    memset(global_buffer_sec, 0, 1024);
    global_buffer_sec[0] = code;
    memmove(global_buffer_sec + 1, content, strlen((char*)content));
    return global_buffer_sec;
}
void milli_sleep(unsigned long sleep_time)
{
    struct timespec spc;
    spc.tv_sec = sleep_time / 1000;
    spc.tv_nsec = (sleep_time % 1000) * 1000000;
    nanosleep(&spc, &spc);
}