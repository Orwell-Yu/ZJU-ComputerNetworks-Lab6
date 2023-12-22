#include <iostream>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <mutex>
#include <cstring>
#include <vector>
#include <pthread.h>

enum msgType
{
    CONNECT = 1,
    DISCONNECT,
    GET_TIME,
    GET_NAME,
    GET_CLIENT_LIST,
    SEND_MSG,
    REPOST
};

const int MAX_SIZE = 256;

struct Message
{
    long type;
    char data[MAX_SIZE - 2];
};

using namespace std;

#define SERVER_PORT 4517
#define MAX_QUEUE_LENGTH 20

typedef struct sock_addr_port
{                // 句柄、地址和端口号
    int sock;    // 句柄
    string addr; // 地址
    int port;    // 端口号
} sock_addr_port;

vector<sock_addr_port> clients;
mutex mtx;

void send_msg(Message *mes, int connectionfd)
{
    if (send(connectionfd, mes, sizeof(*mes), 0) < 0)
    {
        cout << "[Error] Send failed, Error Number is " << errno << endl;
    }
    return;
}


static void *thread_handle(void *cfd)
{
    int connectionfd = *(int *)cfd;                                      // 连接句柄
    char handshake_message[] = "\0";                                     // 握手信息
    send(connectionfd, handshake_message, sizeof(handshake_message), 0); // 发送握手信息
    bool connected = true;                                               // 标志位
    Message mes_rec;                                                     // 接收消息
    while (connected)
    {
        size_t temp = recv(connectionfd, &mes_rec, sizeof(mes_rec), 0); // 接收消息
        if (temp < 0)
        {
            cout << "[Error] Receive failed, Error Number is " << errno << endl;
            continue;
        }
        mtx.lock(); // 加锁
        switch (mes_rec.type)
        {
            case DISCONNECT:
            {
                for (int i = 0; i < clients.size(); i++)
                {
                    if (clients[i].sock == connectionfd)
                    {
                        std::vector<sock_addr_port>::iterator it = clients.begin() + i;
                        clients.erase(it);
                        break;
                    }
                }
                cout << "[Info] Disconnect Client " << connectionfd << endl;
                close(connectionfd);
                connected = false;
                break;
            }
            case GET_TIME:
            {
                time_t t;
                time(&t);
                Message mes;
                mes.type = GET_TIME;
                sprintf(mes.data, "%ld", t);
                send_msg(&mes, connectionfd);
                cout << "[Info] Client " << connectionfd << " gets time" << endl;
                break;
            }
            case GET_NAME:
            {
                Message mes;
                mes.type = GET_NAME;
                gethostname(mes.data, sizeof(mes.data));
                send_msg(&mes, connectionfd);
                cout << "[Info] Client " << connectionfd << " gets server name" << endl;
                break;
            }
            case GET_CLIENT_LIST:
            {
                Message mes;
                cout << "[Info] Client " << connectionfd << " gets client lists" << endl;
                mes.type = GET_CLIENT_LIST;
                for (int i = 0; i < clients.size(); i++)
                {
                    string c = "Client" + to_string(clients[i].sock) + ", adress: " + clients[i].addr + ":" + to_string(clients[i].port) + "\n";
                    char tmp[256] = {};
                    strcpy(tmp, c.c_str());
                    memcpy(mes.data + strlen(mes.data), tmp, c.length());
                }
                string lastLine = "Current Client ID: " + to_string(connectionfd) + "\n";
                char lastLineTmp[256] = {};
                strcpy(lastLineTmp, lastLine.c_str());
                memcpy(mes.data + strlen(mes.data), lastLineTmp, lastLine.length());

                send_msg(&mes, connectionfd);
                break;
            }
            case SEND_MSG:
            {
                string received_data = string(mes_rec.data);
                size_t colon_pos = received_data.find(":");
                cout << "[Info] Client" << connectionfd << " send message to: Client" << stoi(received_data.substr(0, colon_pos)) << endl;
                int target = -1;
                for (int i = 0; i < clients.size(); i++)
                {
                    if (clients[i].sock == stoi(received_data.substr(0, colon_pos)))
                    {
                        target = i;
                        break;
                    }
                }


                Message mes;
                mes.type = SEND_MSG;
                if (target == -1)
                {
                    sprintf(mes.data, "Can not find client");
                }
                else
                {
                    sprintf(mes.data, "Forward success");
                    Message mes_fwd;
                    mes_fwd.type = REPOST;
                    // Include connectionfd as part of the message
                    string messageWithID = to_string(connectionfd) + ":" + received_data.substr(colon_pos + 1);;
                    strcpy(mes_fwd.data, messageWithID.c_str());
                    send_msg(&mes_fwd, clients[target].sock);
                }
                
                send_msg(&mes, connectionfd);
                break;
            }
        }
        memset(&mes_rec, 0, sizeof(mes_rec));
        mtx.unlock();
    }
    return NULL;
}
int main()
{
    cout << "Program Start" << endl;
    cout << "Loading..." << endl;
    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);                   // 创建套接字
    struct sockaddr_in serv_addr;                                      // 服务器地址
    memset(&serv_addr, 0, sizeof(serv_addr));                          // 每个字节都用0填充，防止有旧数据
    serv_addr.sin_family = AF_INET;                                    // 使用IPv4地址
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);                     // 允许任何IP地址连接到服务器
    serv_addr.sin_port = htons(SERVER_PORT);                           // 服务器端口号
    bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)); // 绑定套接字
    listen(serv_sock, MAX_QUEUE_LENGTH);                               // 监听套接字
    cout << "Listening..." << endl;
    while (1)
    {
        struct sockaddr_in clnt_addr;                                                                                                     // 客户端地址
        socklen_t clnt_addr_size = sizeof(clnt_addr);                                                                                     // 客户端地址长度
        int clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);                                                // 接受连接
        sock_addr_port clnt;                                                                                                              // 客户端
        clnt.sock = clnt_sock;                                                                                                            // 句柄
        clnt.addr = inet_ntoa(clnt_addr.sin_addr);                                                                                        // 地址
        clnt.port = ntohs(clnt_addr.sin_port);                                                                                            // 端口号
        clients.push_back(clnt);                                                                                                          // 添加到客户端列表
        cout << "[Info] Get connect from " + clnt.addr + ":" + to_string(clnt.port) + ", its handler is " + to_string(clnt_sock) << endl; // 打印连接信息
        pthread_t conn_thread;                                                                                                            // 连接线程
        pthread_create(&conn_thread, NULL, thread_handle, &clnt_sock);                                                                    // 创建连接线程
    }
    return 0;
}