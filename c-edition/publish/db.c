#include "db/db.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
pthread_rwlock_t dbmutex;
static const char dbname[] = "test.db";
static const char insert_and_getmaxid[] = "begin;\ninsert into `%d` (sendid,content)values(%d,'%s');select MAX(id) from `%d`;\ncommit;";
int init_db()
{
    pthread_rwlock_init(&dbmutex, NULL);
    return 0;
}
int callback(void* args, int colcount, char** colval, char** colname)
{
    for (size_t i = 0; i < colcount; i++) printf("[%s] %s\n", colname[i], colval[i]);
    return 0;
}
int init_table(sqlite3* db, const uint32_t roomid, const char* after_do_query, int (*call_back)(void*, int, char**, char**), void* args)
{
    char query[1224];
    char* errmsg;
    memset(query, 0, 1224);
    snprintf(query, 1224, "CREATE Table `%d`(id INTEGER PRIMARY KEY autoincrement NOT NULL,sendid INTEGER NOT NULL,content VARCHAR(1000) NOT NULL);\n%s\ncommit;", roomid, after_do_query);
    if (sqlite3_exec(db, query, call_back, args, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "init table %d failed,err %s\nquery\n%s\n", roomid, errmsg, query);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}
int find_max_version(void* args, int colcount, char** colval, char** colname)
{
    if (colcount > 0) *(uint32_t*)args = atoi(colval[0]);
    // for (size_t i = 0; i < colcount; i++) {

    // }
    return 0;
}
int find_from_sqlite(const char* query)
{
    sqlite3* sqldb;
    sqlite3_stmt* stm;
    char* errmsg;
    if (sqlite3_open(dbname, &sqldb) != SQLITE_OK) return -1;
    // if (sqlite3_prepare(sqldb, query, 0, &stm, 0) < 0) return -2;
    if (sqlite3_exec(sqldb, query, callback, 0, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "exec sql failed,err %s;query \n%s\n", errmsg, query);
        return -1;
    }
    sqlite3_close(sqldb);
    return 0;
}
int write_msg_todb(const uint32_t sendid, const uint32_t roomid, const char* content)
{
    pthread_rwlock_wrlock(&dbmutex);
    sqlite3* sqldb;
    uint32_t maxversion;
    char* errmsg;
    char query[1024];
    memset(query, 0, 1024);
    snprintf(query, 1024, insert_and_getmaxid, roomid, sendid, content, roomid);
    if (sqlite3_open(dbname, &sqldb) != SQLITE_OK) return -1;
    if (sqlite3_exec(sqldb, query, find_max_version, &maxversion, &errmsg) != SQLITE_OK) {
        memset(query, 0, 1024);
        snprintf(query, 1024, "insert into `%d` (sendid,content)values(%d,'%s');\nselect MAX(id) from `%d`;", roomid, sendid, content, roomid);
        if (init_table(sqldb, roomid, query, find_max_version, &maxversion) == -1) {
            fprintf(stderr, "write to database error %s\n", errmsg);
            sqlite3_free(errmsg);
            sqlite3_close(sqldb);
            pthread_rwlock_unlock(&dbmutex);
            return -1;
        }
        sqlite3_free(errmsg);
    }
    sqlite3_close(sqldb);
    pthread_rwlock_unlock(&dbmutex);
    return maxversion;
}
struct message* get_message(const uint32_t roomid, const uint32_t version, size_t* len)
{
    char query[70];
    memset(query, 0, 70);
    snprintf(query, 70, "select id,sendid,content from `%d` where id > %d", roomid, version);
    sqlite3* sqldb;
    if (sqlite3_open(dbname, &sqldb) != SQLITE_OK) {
        return NULL;
    }
    char** result;
    char* errmsg;
    int rowcount;
    int colcount;
    pthread_rwlock_rdlock(&dbmutex);
    if (sqlite3_get_table(sqldb, query, &result, &rowcount, &colcount, &errmsg) != SQLITE_OK) {
        pthread_rwlock_unlock(&dbmutex);
        fprintf(stderr, "sqlite get table failed,%s\n", errmsg);
        sqlite3_free_table(result);
        sqlite3_free(errmsg);
        return NULL;
    }
    pthread_rwlock_unlock(&dbmutex);
    struct message* ans = malloc(sizeof(struct message) * rowcount);
    *len = rowcount;
    size_t datalen;
    for (size_t i = 0; i < rowcount; i++) {
        ans[i].id = atoi(result[colcount]);
        ans[i].sender_id = atoi(result[colcount + 1]);
        datalen = strlen(result[colcount + 2]);
        ans[i].content = malloc(datalen);
        memset(ans[i].content, 0, datalen);
        memmove(ans[i].content, result[colcount + 2], datalen);
        if (strlen(ans[i].content) > datalen) {
            memset(ans[i].content + datalen, 0, strlen(ans[i].content) - datalen);
        }
        // printf("get new message from database ,length %zu,real data length %zu\n", datalen, strlen(ans[i].content));
        ans[i].roomid = roomid;
    }
    sqlite3_free_table(result);
    return ans;
}
void to_bytes(struct message* msg, u_char* dst, size_t len)
{
    memset(dst, 0, len);
    dst[0] = 11;
    dst[1] = msg->id;
    dst[2] = (msg->roomid / 256) % 256;
    dst[3] = msg->roomid % 256;
    dst[4] = (msg->sender_id / (256 * 256)) % 256;
    dst[5] = (msg->sender_id / 256) % 256;
    dst[6] = msg->sender_id % 256;
    printf("prepare send msg length %zu\n", strlen(msg->content));
    memmove(dst + 7, msg->content, strlen(msg->content));
}
void message_free(struct message* msgarr, size_t len)
{
    for (size_t i = 0; i < len; i++) free(msgarr[i].content);
    free(msgarr);
}