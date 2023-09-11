#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
struct message {
    uint32_t id, sender_id, roomid;
    char* content;
};
void to_bytes(struct message*, u_char*, size_t);
int exec_db(const char*);
int find_from_sqlite(const char* query);
int write_msg_todb(const uint32_t sendid, const uint32_t roomid, const char* content);
int init_db();
// need use message_free result
struct message* get_message(const uint32_t roomid, const uint32_t version, size_t* len);
// free message array,if len eaual -1,it's a normal message pointer
void message_free(struct message*, size_t);
