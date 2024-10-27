#include <iostream>
#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>

using namespace std;

const int CBUF_SIZE = 2048;
char userName[CBUF_SIZE];
char CsendBuf[CBUF_SIZE];

int LocalhostSocket;
struct sockaddr_in LocalhostAddr;
u_short ClientPort = 12870;
const char* LocalIP = "127.0.0.1";

// 函数声明
bool createSocket();
void bindAddress();
void connectToServer();
void getUserName();
void handleUserInput();
void sendMessage(const char* input);
void handleQuit();
void* receiveMessages(void*);

bool serverDisconnected = true;

bool createSocket() {
    LocalhostSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (LocalhostSocket == -1) {
        cout << "套接字创建失败" << endl;
        return false;
    }
    return true;
}

void bindAddress() {
    LocalhostAddr.sin_family = AF_INET;
    LocalhostAddr.sin_port = htons(ClientPort);
    inet_pton(AF_INET, LocalIP, &LocalhostAddr.sin_addr);
}

void connectToServer() {
    if (connect(LocalhostSocket, (struct sockaddr*)&LocalhostAddr, sizeof(LocalhostAddr)) < 0) {
        cout << "连接服务器失败" << endl;
        close(LocalhostSocket);
        exit(-1);
    }
    cout << "成功连接,欢迎加入聊天！" << endl;
}

void getUserName() {
    cout << "请输入你的聊天用户名（不要含有空格）: ";
    cin >> userName + 1;
    userName[0] = char(strlen(userName + 1));
    send(LocalhostSocket, userName, sizeof(userName), 0);
    cout<<"@=============== 聊天室 ===============@"<< endl;
}

void sendMessage(const char* input) {
    memset(CsendBuf, 0, CBUF_SIZE);
    snprintf(CsendBuf, CBUF_SIZE, "%s", input);
    send(LocalhostSocket, CsendBuf, strlen(CsendBuf), 0);
}

void handleQuit() {
    char quitMsg[CBUF_SIZE] = "quit";
    send(LocalhostSocket, quitMsg, strlen(quitMsg), 0);
    close(LocalhostSocket);
    cout << "退出聊天，关闭连接" << endl;
}

void handleUserInput() {
    char input[CBUF_SIZE];
    while (1) {
        cin.getline(input, CBUF_SIZE);
        if(!serverDisconnected){
            cout<<"您已与服务器断开连接，无法继续聊天"<<endl;
            cout << "已退出聊天" << endl;
            break;
        }

        if (strcmp(input, "quit") == 0) {
            handleQuit();
            break;
        } else if (strcmp(input, "list") == 0) {
            send(LocalhostSocket, input, strlen(input), 0);  // 请求在线用户列表
        } else if (input[0] == '@') {  // 私聊消息检测
            sendMessage(input);  // 发送私聊消息格式 "@用户名 消息内容"
        } else {
            sendMessage(input);  // 发送消息
        }
    }
}

void* receiveMessages(void*) {
    char buffer[CBUF_SIZE];
    while (true) {
        memset(buffer, 0, CBUF_SIZE);
        int bytesReceived = recv(LocalhostSocket, buffer, CBUF_SIZE, 0);
        if (bytesReceived <= 0) {
            cout << "服务器断开连接" << endl;
            close(LocalhostSocket);
            serverDisconnected = false;
            pthread_exit(nullptr);  // 正常退出线程
        }
        cout << buffer << endl;  // 输出接收到的消息
    }
    return nullptr;
}

int main() {
    if (!createSocket()) {
        return -1;
    }
    bindAddress();
    connectToServer();
    getUserName();

    pthread_t tid;
    pthread_create(&tid, nullptr, receiveMessages, nullptr);

    handleUserInput();
    // close(LocalhostSocket);
    return 0;
}
