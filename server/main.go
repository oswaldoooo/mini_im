package main

import (
	"context"
	"database/sql"
	"flag"
	"fmt"
	"log"
	"math/rand"
	"net"
	"os"
	"strconv"
	"sync"
	"time"

	_ "github.com/mattn/go-sqlite3"
)

// struct
type config struct {
	Port int `yaml:"Port"`
}
type client struct {
	roomid          uint32
	last_reply_time time.Time
	last_verion     uint32
	mutex           sync.RWMutex
	response_writer *response_Writer
}
type response_Writer struct {
	con  net.Conn
	done chan struct{}
}
type room struct {
	latestversion uint32
	mutex         sync.RWMutex
}
type message struct {
	id             int
	roomid, sendid uint32 //msgid,roomid,send userid
	content        string
}

// variables
const (
	TIMEOUTMAX = time.Duration(1000) * time.Millisecond
	MSG_SYN    = 10
	MSG_ACK    = 11
	MSG_CTR    = 12
)
const (
	DATA_OUT_OF_RANGE = "data out of range"
)
const (
	QUERY    = 90
	QUERYROW = 91
	EXEC     = 92
)

var (
	cnf      config
	errorlog = log.New(os.Stderr, "[error]", log.Ltime|log.Llongfile)
	debuglog = log.New(os.Stdout, "[debug]", log.Ltime|log.Llongfile)
	// db         *sql.DB
	clientlist = make(map[uint32]*client)
	roomlist   = make(map[uint32]*room)
	dbmutex    sync.RWMutex //database mutex
)

func init() {
	flag.IntVar(&cnf.Port, "Port", 9000, "--cnf.Port xxx")
	flag.Parse()
}
func main() {
	if cnf.Port < 0 {
		errorlog.Println("Port", cnf.Port, " is invalid")
		os.Exit(1)
	}
	rand.Seed(time.Now().UnixNano()) //set rand seed
	listener, err := net.Listen("tcp", ":"+strconv.Itoa(cnf.Port))
	if err == nil {
		var con net.Conn
		for {
			con, err = listener.Accept()
			if err == nil {
				go readconse(con)
			} else {
				errorlog.Println(err.Error())
			}
		}
	} else {
		errorlog.Println(err.Error())
	}
}

// socket
func readconse(con net.Conn) {
	defer con.Close()

	buffer := make([]byte, 1024)
	var (
		lang      int
		err       error
		resp      []byte
		id        uint32
		cli       *client
		is_accept bool = false
	)
	defer func() {
		if !is_accept {
			debuglog.Println("start deregister main", id, "process")
		} else {
			debuglog.Println("start deregister accept", id, "process")
		}
		if !is_accept && cli != nil && cli.response_writer != nil {
			debuglog.Println(id, "send end signal to accept process")
			cli.response_writer.done <- struct{}{}
		}
		if _, ok := clientlist[id]; ok {
			if !is_accept {
				debuglog.Println(id, "send server deregistered")
				delete(clientlist, id)
			} else {
				debuglog.Println(id, "accept server deregistered")
			}

		}
	}()
	for {
		lang, err = con.Read(buffer)
		if err == nil {
			switch buffer[0] {
			case MSG_SYN:
				if lang == 4 { //注册收信连接
					is_accept = true
					id = uint32(buffer[1])*256*256 + uint32(buffer[2])*256 + uint32(buffer[3])
					var ok bool
					if cli, ok = clientlist[id]; ok {
						debuglog.Println("register client", id, "accept server")
						cli.response_writer = &response_Writer{con: con, done: make(chan struct{})}
						var msgarr []message
						tck := time.NewTicker(1 * time.Second)
						for {
							select {
							case <-cli.response_writer.done:
								debuglog.Println(id, "accept done signal")
								return
							case <-tck.C:
								roomlist[cli.roomid].mutex.RLock()
								if cli.last_verion < roomlist[cli.roomid].latestversion {
									debuglog.Printf("client %v find new message", id)
									roomlist[cli.roomid].mutex.RUnlock()
									// roomlist[cli.roomid].mutex.Lock()
									msgarr = getmessage(cli.last_verion, cli.roomid)
									debuglog.Printf("client %v find %d new messages", id, len(msgarr))
									if len(msgarr) > 0 {
										err = sendtocli(&msgarr, con)
										if err != nil {
											errorlog.Printf("cli accept server %v closed by client", id)
											return
										} else {
											cli.mutex.Lock()
											cli.last_verion = roomlist[cli.roomid].latestversion
											cli.mutex.Unlock()
											debuglog.Printf("client %v latest version update finished", id)
										}
									}
									// roomlist[cli.roomid].mutex.Unlock()
								} else {
									roomlist[cli.roomid].mutex.RUnlock()
								}
							}
						}
					}
				} else if lang == 3 { //注册连接
					id = rand.Uint32() % (256*256*256 + 256*256 + 256)
					resp = make([]byte, 5)
					resp[0] = byte((id / (256 * 256)) % 256)
					resp[1] = byte((id / 256) % 256)
					resp[2] = byte(id % 256)
					copy(resp[3:5], buffer[1:3])
					_, err = con.Write(resp)
					if err != nil {
						debuglog.Println("connection closed by client")
						break
					}
					clientlist[id] = &client{roomid: uint32(buffer[1])*256 + uint32(buffer[2]), last_reply_time: time.Now()}
					cli = clientlist[id]
					if _, ok := roomlist[cli.roomid]; !ok {
						roomlist[cli.roomid] = &room{}
						debuglog.Println("create new room", cli.roomid)
					}
					debuglog.Printf("register client %v send server", id)
				}
			case MSG_ACK: //客户端向服务端发送信息
				if !writetomessage(buffer[1:lang], cli.roomid, id) {
					err = cli.write(MSG_CTR, []byte("send message failed unknown error"))
					if err != nil {
						errorlog.Println(err.Error())
						break
					}
				}
			case MSG_CTR: //切换房间等操作

			}
		} else {
			debuglog.Println("client", id, "closed connection")
			return
		}
	}
}

// get the latest message that client not read
func getmessage(version, roomid uint32) []message {
	var msgarr = []message{}
	var msg *message
	debuglog.Println("room list read locked")
	roomlist[roomid].mutex.RLock()
	defer roomlist[roomid].mutex.RUnlock()
	defer fmt.Println("room list read unlocked")
	rwsany, err := writeTodatabase(QUERY, "select id,sendid,content from `"+strconv.FormatUint(uint64(roomid), 10)+"` where id>?", version)
	if err == nil {
		debuglog.Println("write to database finished")
		rws := rwsany.(*sql.Rows)
		for rws.Next() {
			debuglog.Println("read message...")
			msgarr = append(msgarr, message{roomid: roomid})
			msg = &msgarr[len(msgarr)-1]
			err = rws.Scan(&msg.id, &msg.sendid, &msg.content)
			if err != nil {
				debuglog.Printf("select id,sendid,content from `"+strconv.FormatUint(uint64(roomid), 10)+"` where id>%v", version)
				errorlog.Printf("read room %v msg failed,version %v, err %s", roomid, version, err.Error())
				return msgarr
			}
		}
	}
	return msgarr
}

// send unread messages to client
func sendtocli(src *[]message, con net.Conn) error {
	if len(*src) == 0 {
		return nil
	}
	_, err := con.Write([]byte{MSG_SYN, byte(len(*src))})
	if err == nil {
		debuglog.Println("prepare send", len(*src))
		for _, ele := range *src {
			time.Sleep(10 * time.Millisecond) //sleep 10 milliseconds
			_, err = con.Write(ele.tobytes())
			if err != nil {
				return err
			} else {
				debuglog.Println("send message", string(ele.tobytes()[7:]))
			}
		}
	}
	return err
}

// system level
// message = id[1]+roomid[2]+sendcliid[3]+content
func (s *message) tobytes() []byte {
	ans := make([]byte, 7+len(s.content))
	ans[0] = MSG_ACK
	ans[1] = byte(s.id)
	ans[2] = byte((s.roomid / 256) % 256)
	ans[3] = byte(s.roomid % 256)
	ans[4] = byte((s.sendid / (256 * 256)) % 256)
	ans[5] = byte((s.sendid / 256) % 256)
	ans[6] = byte(s.sendid % 256)
	copy(ans[7:], []byte(s.content))
	return ans
}
func (s *client) write(code uint8, v []byte) error {
	ans := make([]byte, 1+len(v))
	ans[0] = code
	copy(ans[1:], v)
	if s.response_writer == nil {
		return fmt.Errorf("not set response writer")
	}
	_, err := s.response_writer.con.Write(ans)
	return err
}
func writetomessage(message []byte, roomid, sendid uint32) bool {
	debuglog.Printf("client %v locked room %v", sendid, roomid)
	roomlist[roomid].mutex.Lock()
	defer roomlist[roomid].mutex.Unlock()
	defer debuglog.Printf("client %v unlocked room %v", sendid, roomid)
	_, err := writeTodatabase(EXEC, "insert into `"+strconv.FormatUint(uint64(roomid), 10)+"` (sendid,content)values(?,?)", sendid, string(message))
	if err == nil {
		debuglog.Printf("insert into `%v` (sendid,content)values(%v,'%v')", roomid, sendid, string(message))
		rowany, _ := writeTodatabase(QUERYROW, "select MAX(id) from `"+strconv.FormatUint(uint64(roomid), 10)+"`")
		var maxversion uint32
		row := rowany.(*sql.Row)
		if row.Scan(&maxversion) == nil {
			roomlist[roomid].latestversion = maxversion
			clientlist[sendid].last_verion = maxversion
		} else {
			errorlog.Println("get maxversion failed")
		}
		return true
	} else {
		errorlog.Println("insert data error", err.Error())
		_, err = writeTodatabase(EXEC, "create table `"+strconv.FormatUint(uint64(roomid), 10)+"` (id INTEGER PRIMARY KEY autoincrement NOT NULL,sendid INTEGER NOT NULL,content VARCHAR(1000))")
		if err == nil {
			_, err = writeTodatabase(EXEC, "insert into `"+strconv.FormatUint(uint64(roomid), 10)+"` (sendid,content)values(?,?)", sendid, string(message))
			if err == nil {
				rowany, _ := writeTodatabase(QUERYROW, "select MAX(id) from `"+strconv.FormatUint(uint64(roomid), 10)+"`")
				var maxversion uint32
				row := rowany.(*sql.Row)
				if row.Scan(&maxversion) == nil {
					roomlist[roomid].latestversion = maxversion
				} else {
					errorlog.Println("get maxversion failed")
				}
				return true
			} else {
				errorlog.Println(err.Error())
			}
		} else {
			errorlog.Printf("insert data to room %v failed,err=%s", roomid, err.Error())
		}

	}
	return false
}

// interact with database
func writeTodatabase(mod uint8, query string, args ...any) (any, error) {
	defer fmt.Printf("write to database finished,mod %d\n", mod)
	fmt.Println("start exec sql...")
	db, err := sql.Open("sqlite3", "data.db")
	if err == nil {
		defer db.Close()
		if mod == QUERY {
			dbmutex.RLock()
			defer dbmutex.RUnlock()
			fmt.Println("do query")
			return db.QueryContext(context.Background(), query, args...)
		} else if mod == QUERYROW {
			dbmutex.RLock()
			defer dbmutex.RUnlock()
			fmt.Println("do query row")
			return db.QueryRowContext(context.Background(), query, args...), nil
		} else if mod == EXEC {
			dbmutex.Lock()
			defer dbmutex.Unlock()
			fmt.Println("do exec")
			return db.ExecContext(context.Background(), query, args...)
		}
	}
	return nil, err

}
