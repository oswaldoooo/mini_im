#include "db/db.h"
#include "socket/socket.h"
#include <ctools/stdlib/array.h>
#include <ctools/stdlib/io.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define MAXBUFFER_SIZE 1 << 10
short msg_ack(const uint32_t, const uint32_t, const u_char*);
enum control_code { CREAT_ROOM,
    SYN = 10,
    ACK = 11,
    CTR = 12
};
enum cli_status {
    UNUSED,
    OK
};
struct room {
    pthread_rwlock_t mutex;
    uint32_t latest_version;
};
struct client {
    uint32_t roomid, latest_version;
    pthread_rwlock_t mutex;
    int response_writer;
    ushort status;
    int conid;
    int cli_id;
};

short read_con(int);
void* listen_writer(void*);
struct client* findclient(int);
short sendto_cli(struct message*, struct client*, size_t);
short deregister_cli(const uint32_t cli_id);
static const uint32_t maxcli_id = 256 * 256 * 256 + 256 * 256 + 256;
static const uint32_t maxroom_id = 256 * 256 + 256;
static struct room* roomlist;
static struct client* clientlist;
static struct array used_client;
static u_char global_buffer[MAXBUFFER_SIZE];
static u_char send_buffer[MAXBUFFER_SIZE];

static int eid;
int main()
{
    int sid = listen_tcp(9000);
    if (sid < 0) {
        fprintf(stderr, "listen tcp failed,socket id %d\n", sid);
        return 1;
    }
    int conid;
    int cliid;
    int roomid;
    u_char global_buffer[1024];
    struct epoll_event seve;
    seve.data.fd = sid;
    seve.events = EPOLLIN;
    eid = epoll_create(5);
    epoll_ctl(eid, EPOLL_CTL_ADD, sid, &seve);
    struct epoll_event eves[5];
    int number;
    newarray(&used_client, sizeof(uint32_t*));
    roomlist = malloc(sizeof(struct room) * maxroom_id);
    clientlist = malloc(sizeof(struct client) * maxcli_id);
    log_print("start listen at port 9000.../\n");
    pthread_t backend_thread;
    pthread_create(&backend_thread, NULL, listen_writer, NULL);
    pthread_detach(backend_thread);
    while (1 == 1) {
        number = epoll_wait(eid, eves, 5, 0);
        if (number > 0) {
            for (size_t i = 0; i < number; i++) {
                if (eves[i].data.fd == sid) {
                    struct epoll_event con_eve;
                    conid = accept(sid, NULL, NULL);
                    if (conid > 0) {
                        con_eve.data.fd = conid;
                        con_eve.events = EPOLLIN;
                        epoll_ctl(eid, EPOLL_CTL_ADD, conid, &con_eve);
                        log_print("accept new connection\n");
                    }
                } else if (eves[i].events & EPOLLIN) {
                    if (read_con(eves[i].data.fd) < 0) { // read con error deregister connection
                        close(eves[i].data.fd);
                        epoll_ctl(eid, EPOLL_CTL_DEL, eves[i].data.fd, NULL);
                    }
                }
            }
        }
        // conid = accept(sid, NULL, NULL);
        // if (conid > 0) {
        //     cliid = rand() % maxcli_id;
        //     global_buffer[0] = (cliid / (256 * 256)) % 256;
        //     global_buffer[1] = (cliid / 256) % 256;
        //     global_buffer[2] = cliid % 256;
        //     if (write(conid, global_buffer, 3) > 0) {
        //     }
        // } else {
        //     log_fprint(stderr, "get connection failed");
        // }
    }

    free(roomlist);
    free(clientlist);
}

inline static struct client* init_cli(uint32_t cli_id)
{
    if (clientlist[cli_id].status == UNUSED) {
        pthread_rwlock_init(&clientlist[cli_id].mutex, NULL);
        clientlist[cli_id].status = OK;
        push_back(&used_client, &cli_id);
        uint32_t* used_cli = get_ele(&used_client, used_client.curr_size - 1);
        if (used_cli != NULL) {
            // printf("the insert client info: id %d roomid %d conid %d\n", clientlist[*used_cli].cli_id, clientlist[*used_cli].roomid, clientlist[*used_cli].conid);
        } else {
            fprintf(stderr, "not find insert client pointer from used_client\n");
        }
    }
    return &clientlist[cli_id];
}
short free_cli(const uint32_t cli_id)
{
    if (clientlist[cli_id].status == OK) {
        pthread_rwlock_destroy(&clientlist[cli_id].mutex);
        clientlist[cli_id].status = UNUSED;
    }
    return 0;
}
inline short read_con(int conid)
{
    memset(global_buffer, 0, MAXBUFFER_SIZE);
    ssize_t lang;
    lang = read(conid, global_buffer, MAXBUFFER_SIZE);
    if (lang <= 0) return -1;
    switch (global_buffer[0]) {
    case SYN: {
        if (lang == 4) {
            uint32_t id = global_buffer[1] * 256 * 256 + global_buffer[2] * 256 + global_buffer[3];
            struct client* cliptr = &clientlist[id];
            log_print("accept client accept server connection\n");
            if (cliptr->status == OK) {
                cliptr->response_writer = conid;
                return 0;
            } else {
                const u_char* ans = writeTo(CTR, (u_char*)"cli id don't existed");
                write(conid, ans, strlen((char*)ans));
                return -3;
            }
        } else if (lang == 3) {
            log_print("accept client send server connection\n");
            uint32_t roomid = global_buffer[1] * 256 + global_buffer[2];
            uint32_t id = rand() % (256 * 256 * 256 + 256 * 256 + 256);
            memmove(global_buffer + 3, global_buffer + 1, 2);
            global_buffer[0] = (id / (256 * 256)) % 256;
            global_buffer[1] = (id / 256) % 256;
            global_buffer[2] = id % 256;
            printf("start register new client %d,roomid %d,conid %d\n", id, roomid, conid);
            lang = write(conid, global_buffer, 5);
            if (lang <= 0) return -2;
            //注册实体连接在服务端
            struct client* cliptr = init_cli(id);
            uint32_t* used_cli = get_ele(&used_client, used_client.curr_size - 1);
            cliptr->roomid = roomid;
            cliptr->cli_id = id;
            cliptr->conid = conid;
            printf("the insert client info: id %d roomid %d conid %d\n", clientlist[*used_cli].cli_id, clientlist[*used_cli].roomid, clientlist[*used_cli].conid);
            printf("init client finished;client id %d,roomid %d,conid %d\n", clientlist[id].cli_id, clientlist[id].roomid, clientlist[id].conid);
        }
    } break;
    case ACK: {
        printf("accept msg,and start find client\n");
        struct client* cliptr = findclient(conid);
        if (cliptr != NULL) {
            printf("accept %d msg,roomid %d\n", cliptr->cli_id, cliptr->roomid);
            short anscode = msg_ack(cliptr->cli_id, cliptr->roomid, global_buffer + 1);
            return (anscode == 0) ? 0 : anscode - 3;
        } else {
            fprintf(stderr, "%d not find the client\n", conid);
            return -5;
        }

    } break;
    case CTR:
        break;
    }
    return 0;
}
inline short msg_ack(const uint32_t cli_id, const uint32_t roomid, const u_char* content)
{
    printf("room %d locked\n", roomid);
    pthread_rwlock_wrlock(&roomlist[roomid].mutex);
    uint32_t maxversion = write_msg_todb(cli_id, roomid, (char*)content);
    if (maxversion > 0) {
        roomlist[roomid].latest_version = maxversion;
        pthread_rwlock_unlock(&roomlist[roomid].mutex);
        clientlist[cli_id].latest_version = maxversion;
        printf("room %d unlocked\n", roomid);
        return 0;
    }
    pthread_rwlock_unlock(&roomlist[roomid].mutex);
    printf("room %d unlocked\n", roomid);
    return -1;
}
inline struct client* findclient(int conid)
{
    uint32_t* final_ptr;
    printf("used client number %zu\n", used_client.curr_size);
    for (size_t i = 0; i < used_client.curr_size; i++) {
        final_ptr = get_ele(&used_client, i);
        if (final_ptr != NULL && clientlist[*final_ptr].status == OK && clientlist[*final_ptr].conid == conid) {
            return &clientlist[*final_ptr];
        }
    }
    return NULL;
}
// backend
inline void* listen_writer(void* args)
{
    printf("backend start running ..../\n");
    uint32_t* pos;
    struct client* cli_ptr;
    struct message* msgarr;
    size_t len;
    while (1 == 1) {
        sleep(1);
        for (size_t i = 0; i < used_client.curr_size; i++) {
            pos = get_ele(&used_client, i);
            cli_ptr = &clientlist[*pos];
            if (cli_ptr->latest_version < roomlist[cli_ptr->roomid].latest_version) {
                msgarr = get_message(cli_ptr->roomid, cli_ptr->latest_version, &len);
                if (msgarr != NULL) {
                    pthread_rwlock_wrlock(&cli_ptr->mutex);
                    printf("client %d get %zu new messge from room %d [%d->%d]\n", cli_ptr->cli_id, len, cli_ptr->roomid, cli_ptr->latest_version, msgarr[len - 1].id);
                    cli_ptr->latest_version = msgarr[len - 1].id;
                    pthread_rwlock_unlock(&cli_ptr->mutex);

                    if (sendto_cli(msgarr, cli_ptr, len) < 0) {
                        // deregister client
                        deregister_cli(cli_ptr->cli_id);
                        close(cli_ptr->conid);
                        close(cli_ptr->response_writer);
                        fprintf(stderr, "deregister client %d\n", cli_ptr->cli_id);
                    }
                    message_free(msgarr, len);
                }
            }
        }
    }
}
inline short sendto_cli(struct message* src, struct client* cli_ptr, size_t msg_num)
{
    ssize_t lang;
    // send syn to client
    u_char syn_msg[] = { 10, msg_num };
    lang = write(cli_ptr->response_writer, syn_msg, 2);
    if (lang <= 0) {
        log_print("connection lost syn\n");
        return -1;
    }
    // send message

    for (size_t i = 0; i < msg_num; i++) {
        milli_sleep(100); // sleep 100 milliseconds and send msg continue
        // printf("precheck data length %zu\n", strlen(src[i].content));
        to_bytes(&src[i], send_buffer, MAXBUFFER_SIZE);
        lang = write(cli_ptr->response_writer, send_buffer, strlen((char*)send_buffer));
        if (lang <= 0) {
            log_print("connection lost ack\n");
            return -1;
        }
    }
    return 0;
}
inline short deregister_cli(const uint32_t cli_id)
{
    epoll_ctl(eid, EPOLL_CTL_DEL, cli_id, NULL);
    return free_cli(cli_id);
}