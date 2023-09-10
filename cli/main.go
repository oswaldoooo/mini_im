package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
)

var add string
var roomid uint32

const (
	MSG_SYN = 10
	MSG_ACK = 11
	MSG_CTR = 12
)

var needacceptmsg uint8
var debuglog = log.New(os.Stderr, "[debug]", log.Ltime|log.Llongfile)
var output *os.File
var accept_debug *log.Logger

func init() {
	flag.StringVar(&add, "add", "127.0.0.1:9000", "--add xxx")
	var roomidflag uint64
	flag.Uint64Var(&roomidflag, "roomid", 666, "--roomid xxx")
	flag.Parse()
	roomid = uint32(roomidflag)
	if roomid >= 256*256+256 {
		debuglog.Println("[error]roomid is too big!!!!range [0~65792]")
		os.Exit(1)
	}
	var err error
	output, err = os.OpenFile("out.txt", os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		debuglog.Println(err.Error())
		os.Exit(1)
	}
	accept_out, err := os.OpenFile("acceptout.txt", os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		fmt.Println("[error]open accept out failed", err.Error())
		os.Exit(1)
	}
	accept_debug = log.New(accept_out, "[debug]", log.Ltime|log.Llongfile)
}
func main() {
	debuglog.Println("dial to server", add)
	con, err := net.Dial("tcp", add)
	if err == nil {
		var lang int
		buffer := make([]byte, 1024)
		_, err = con.Write([]byte{MSG_SYN, byte((roomid / 256) % 256), byte(roomid % 256)})
		if err != nil {
			fmt.Println("[error]send connection request to host lost")
			os.Exit(1)
		}
		debuglog.Println("read stattus...")
		lang, err = con.Read(buffer)
		if err == nil {
			if lang == 5 {
				var (
					clientid, tiroomid uint32
				)
				clientid = uint32(buffer[0])*256*256 + uint32(buffer[1])*256 + uint32(buffer[2])
				tiroomid = uint32(buffer[3])*256 + uint32(buffer[4])
				if tiroomid != roomid {
					fmt.Printf("[error] roomid %v not equal to server roomid %v\n", roomid, tiroomid)
					os.Exit(1)
				}
				go func() {
					reader := bufio.NewReader(os.Stdin)
					var content string
					var msg []byte = make([]byte, 1024)
					msg[0] = MSG_ACK
					for {
						fmt.Printf("[%v %v] ", clientid, roomid)
						content, err = reader.ReadString('\n')
						if err == nil {
							copy(msg[1:], []byte(content))
							_, err = con.Write(msg[:len(content)+1])
							if err != nil {
								debuglog.Println("[error]send msg failed")
								os.Exit(1)
							}
						}
					}
				}()
				//register accept server
				var (
					newcon net.Conn
				)
				newcon, err = net.Dial("tcp", add)
				if err != nil {
					accept_debug.Println("[error] accept server connect to host failed")
					os.Exit(1)
				}
				_, err = newcon.Write([]byte{MSG_SYN, buffer[0], buffer[1], buffer[2]})
				if err != nil {
					accept_debug.Println("[error]accept server send connection to host lost", err.Error())
					os.Exit(1)
				}
				accept_debug.Println("[debug] start listen on accept server")
				for {
					lang, err = newcon.Read(buffer)
					if err == nil {
						switch buffer[0] {
						case MSG_SYN:
							msg_syn(buffer[1:lang], newcon)
						case MSG_ACK:
							msg_ack(buffer[1:lang], newcon)
						case MSG_CTR:
							debuglog.Println(string(buffer[1:lang]))
						default:
							accept_debug.Println("[error] not supported msg controller", buffer[0])
						}
					} else {
						if err != io.EOF {
							accept_debug.Println("[error]", err.Error())
						} else {
							accept_debug.Println("server lost connection")
						}
						break
					}
				}
			} else {
				debuglog.Println("Header Msg invalid", lang)
			}
		} else {
			debuglog.Println("[error]", err.Error())
		}
	}
}
func msg_syn(src []byte, con net.Conn) {
	needacceptmsg = src[0]
	fmt.Fprintf(output, "[have %v message to accept]", needacceptmsg)
}
func msg_ack(src []byte, con net.Conn) {
	fmt.Fprintln(output, "[from server]", string(src[6:]))
}
