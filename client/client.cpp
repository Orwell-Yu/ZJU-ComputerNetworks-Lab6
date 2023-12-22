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

void cleanup_handler(void *arg)
{
    int *sockfd = (int *)arg;
    close(*sockfd);
}

void *helper_thread(void *arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    int sockfd = *(int *)arg;

    Message msg;
    key_t key = ftok("pipe", 12);
    int msgID = msgget(key, IPC_CREAT | 0666);

    if (msgID < 0)
    {
        printf("[Error] Message queue create fail: %s", strerror(errno));
    }

    //定义了一个清理函数，用于关闭sockfd
    pthread_cleanup_push(cleanup_handler, &sockfd);

    //建立连接时的一次握手
    recv(sockfd, &msg.data, sizeof(msg.data), 0);

    while (true)
    {
        memset(&msg, 0, sizeof(msg));

        size_t temp=recv(sockfd, &msg, sizeof(msg), 0);
        if (temp < 0)
        {
            printf("[Error] helper recv fail, error: %s\n", strerror(errno));
        }

        if (msg.type == REPOST)
        {
            // Extracting client_id and the actual message
            string received_data = string(msg.data);
            size_t colon_pos = received_data.find(":");
            int sender_id = stoi(received_data.substr(0, colon_pos));
            string actual_message = received_data.substr(colon_pos + 1);

            printf("[Info] Receive message from Client %d: %s\n >>", sender_id, actual_message.c_str());
            fflush(stdout);
            continue;
        }else
            msgsnd(msgID, &msg, MAX_SIZE, 0); // 将消息发送到消息队列
    }
    pthread_cleanup_pop(1);
    pthread_exit(NULL);
}

class Client
{
public:
    Client()
    {
        sockfd = -1;
        memset(&server_addr, 0, sizeof(server_addr));

        key_t key = ftok("pipe", 12);
        msgID = msgget(key, IPC_CREAT | 0666);
        if (msgID < 0)
        {
            printf("[Error] Message queue create fail: %s\n", strerror(errno));
            return;
        }

        char msg[MAX_SIZE];
        while (msgrcv(msgID, &msg, MAX_SIZE, 0, IPC_NOWAIT) > 0)
            ;
    }

    ~Client()
    {
        close(sockfd);
    }

    int operation()
    {
        int op;
        printf(
            "Your choice(Press '8' for help):\n "
            ">>");
        scanf("%d", &op);
        return op;
    }

    void run()
    {
        printf("\nPlease choose your operation from the following:\n"
               "1.Connect\n"
               "2.Disconnect\n"
               "3.Get Time\n"
               "4.Get Name\n"
               "5.Get Connect list\n"
               "6.Send Data\n"
               "7.Exit Program\n"
               "8.Help\n");
        while (1)
        {
            int op = operation();
            switch (op)
            {
            case 1:
                ConnectServer();
                break;
            case 2:
                DisconnectServer();
                break;
            case 3:
                GetTime();
                break;
            case 4:
                GetName();
                break;
            case 5:
                GetList();
                break;
            case 6:
                SendData();
                break;
            case 7:
                ExitProgram();
                break;
            case 8:
                Helpinfo();
                break;
            default:
                printf("[Error] Invalid Op!\n");
            }
        }
    }

private:
    int sockfd, msgID; // sockfd marks the connection status, msgID is the message queue id
    char sendline[MAX_SIZE], recvline[MAX_SIZE];
    sockaddr_in server_addr;
    pthread_t thread;

    void ConnectServer()
    {
        if (sockfd != -1)
        {
            printf("[Error] Reconnected!\n");
            return;
        }

        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            printf("[Error] Create socket fail, error: %s\n", strerror(errno));
            return;
        }

        char ip[MAX_SIZE];
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(4517);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(sockfd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            printf("[Error] Connect fail, error: %s\n", strerror(errno));
            return;
        }
        if (pthread_create(&thread, NULL, helper_thread, &sockfd) != 0)
        {
            printf("[Error] Create thread fails!\n");
        }

        printf("[Info] Connected!\n");

        return;
    }

    void DisconnectServer()
    {
        if (sockfd == -1)
        {
            printf("No connection\n");
            return;
        }

        char data = DISCONNECT;
        if (send(sockfd, &data, sizeof(data), 0) < 0)
        {
            printf("[Error] Disconnect send fail, error: %s\n", strerror(errno));
        }

        pthread_cancel(thread);
        // close(sockfd);
        sockfd = -1;
        printf("[Info] Connection closed!\n");

        return;
    }


    void GetTime()
    {

        if (sockfd == -1)
        {
            printf("[Error] No connection detected!\n");
            return;
        }

        if(send_msg(GET_TIME) < 0)
            return;

        Message msg;
        if(recv_msg(&msg, GET_TIME) < 0)
            return;

        // printf("%s", msg.data);

        time_t t;
        sscanf(msg.data, "%ld", &t);
        printf("[Info] Time: %s", ctime(&t));

        return;
    }

    void GetName()
    {
        if (sockfd == -1)
        {
            printf("[Error] No connection detected!\n");
            return;
        }

        if(send_msg(GET_NAME) < 0)
            return;

        Message msg;
        if(recv_msg(&msg, GET_NAME) < 0)
            return;

        printf("[Info] Server Name: %s\n", msg.data);

        return;
    }

    void GetList()
    {
        if (sockfd == -1)
        {
            printf("[Error] No connection detected!\n");
            return;
        }

        if(send_msg(GET_CLIENT_LIST) < 0)
            return;

        Message msg;
        if(recv_msg(&msg, GET_CLIENT_LIST) < 0)
            return;

        printf("[Info] Client list:\n%s", msg.data);

        return;
    }

    void SendData()
    {
        if (sockfd == -1)
        {
            printf("[Error] No connection detected!\n");
            return;
        }

        Message msg;
        msg.type = SEND_MSG;

        int target_client_id; // Assuming the client ID is an integer
        printf("Target Client ID: ");
        scanf("%d", &target_client_id);
        sprintf(msg.data, "%d:", target_client_id); // Storing the client ID directly

        char content[MAX_SIZE - 10]; // Adjusted the size to accommodate the client ID
        printf("Send content: ");
        scanf("%s", content);
        sprintf(msg.data + strlen(msg.data), "%s", content);

        if(send_msg(msg) < 0)
            return;

        memset(&msg, 0, sizeof(msg));
        
        if(recv_msg(&msg, SEND_MSG) < 0)
            return;

        printf("[Info] Server Response: %s\n", msg.data);

        return;
    }

    void Helpinfo()
    {
        printf("\nPlease choose your operation from the following:\n"
                       "1.Connect\n"
                       "2.Disconnect\n"
                       "3.Get Time\n"
                       "4.Get Name\n"
                       "5.Get Connect list\n"
                       "6.Send Data\n"
                       "7.Exit Program\n"
                       "8.Help\n");
        return;
    }

    int send_msg(char cmd)
    {
        if (send(sockfd, &cmd, sizeof(cmd), 0) < 0)
        {
            printf("[Error] send fail, error: %s\n", strerror(errno));
            return -1;
        }
        return 0;
    }

    int send_msg(Message cmd)
    {
        if (send(sockfd, &cmd, sizeof(cmd), 0) < 0)
        {
            printf("[Error] send fail, error: %s\n", strerror(errno));
            return -1;
        }
        return 0;
    }

    int recv_msg(Message *msg, long type)
    {
        if (msgrcv(msgID, msg, MAX_SIZE, type, 0) < 0)
        {
            printf("[Error] recv fail, error: %s\n", strerror(errno));
            return -1;
        }
        return 0;
    }

    void ExitProgram()
    {
        DisconnectServer();
        printf("[Info] Program exited!\n");
        exit(0);
    }
};

int main()
{
    Client c;
    c.run();
}
