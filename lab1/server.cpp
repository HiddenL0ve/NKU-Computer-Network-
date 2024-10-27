#include <iostream>
#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>
#include <algorithm>
#include <string>
#include <ctime>

using namespace std;

const int SERVER_PORT = 12870;
const int BACKLOG = 10;
const int BUFFER_SIZE = 2048;

int serverSocket;
struct sockaddr_in serverAddr;

vector<int> clientSockets;
vector<string> userNames;
pthread_mutex_t clientsMutex;
bool serverRunning = true;  // 控制服务器状态

void broadcastMessage(const string& message, int senderSocket) {
    pthread_mutex_lock(&clientsMutex);
    for (int i = 0; i < clientSockets.size(); i++) {
        if (clientSockets[i] != senderSocket) {
            send(clientSockets[i], message.c_str(), message.size(), 0);
        }
    }
    pthread_mutex_unlock(&clientsMutex);
}

string getTimeStamp() {
    time_t now = time(0);
    tm* localTime = localtime(&now);
    char buffer[80];
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", localTime);
    return string(buffer);
}

void sendPrivateMessage(int clientSocket, const char* targetUser, const char* privateMessage, const char* sender) {
    pthread_mutex_lock(&clientsMutex);

    auto it = find(userNames.begin(), userNames.end(), string(targetUser));
    if (it != userNames.end()) {
        int targetSocket = clientSockets[it - userNames.begin()];
        string privateMsg = "私聊 (" + string(sender) + "): " + privateMessage;
        send(targetSocket, privateMsg.c_str(), privateMsg.size(), 0);
        string confirmationMsg = "消息已发送给 " + string(targetUser);
        send(clientSocket, confirmationMsg.c_str(), confirmationMsg.size(), 0); 
    } else {
        string errorMsg = "用户 " + string(targetUser) + " 不在线或不存在";
        send(clientSocket, errorMsg.c_str(), errorMsg.size(), 0);
    }
    pthread_mutex_unlock(&clientsMutex);
}

void* handleClient(void* arg) {
    int clientSocket = *(int*)arg;
    char buffer[BUFFER_SIZE];
    char userName[50];
    recv(clientSocket, userName, sizeof(userName), 0);
    pthread_mutex_lock(&clientsMutex);
    
    //防止重复用户名
    if (find(userNames.begin(), userNames.end(), string(userName)) != userNames.end()) {
        string errorMsg = "用户名已存在，请重试。";
        send(clientSocket, errorMsg.c_str(), errorMsg.size(), 0);
        close(clientSocket);
        pthread_mutex_unlock(&clientsMutex);
        return nullptr;  // 结束线程
    }
    pthread_mutex_unlock(&clientsMutex);

    string joinMessage = "欢迎" + string(userName) + "加入了聊天";
    broadcastMessage("[" + getTimeStamp() + "]  " + joinMessage, clientSocket);

    pthread_mutex_lock(&clientsMutex);
    clientSockets.push_back(clientSocket);
    userNames.push_back(userName);
    pthread_mutex_unlock(&clientsMutex);

    cout << "[" + getTimeStamp() + "]  " + "用户 " << userName << " 已经连接到服务器" << endl;

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        if (bytesReceived <= 0) {
            cout << "客户端断开连接: " << userName << endl;
            break;
        }

        if (strlen(buffer) > 0) {
            if (buffer[0] == '@') {
                string message(buffer);
                size_t spacePos = message.find(' ');
                if (spacePos != string::npos) {
                    string targetUser = message.substr(1, spacePos - 1);  // 提取目标用户名
                    string privateMessage = message.substr(spacePos + 1); // 提取私聊内容
                    sendPrivateMessage(clientSocket, targetUser.c_str(), privateMessage.c_str(), userName);
                } else {
                    string errorMsg = "无效的私聊格式，使用 @用户名 消息";
                    send(clientSocket, errorMsg.c_str(), errorMsg.size(), 0);
                }
            } 
            else if (strcmp(buffer, "list") == 0) {
                int userCount = userNames.size();
                string onlineUsers = "在线用户人数: " + to_string(userCount) + "\n";
                for (size_t i = 0; i < userNames.size(); ++i) {
                    onlineUsers += to_string(i + 1) + ". " + userNames[i] + "\n";
                }
                send(clientSocket, onlineUsers.c_str(), onlineUsers.size(), 0);
                cout << "[" + getTimeStamp() + "]  " + "用户 " << userName << " 请求用户列表" << endl;
            }
            else if(strcmp(buffer, "quit") == 0) {
                pthread_mutex_lock(&clientsMutex);
                clientSockets.erase(remove(clientSockets.begin(), clientSockets.end(), clientSocket), clientSockets.end());
                userNames.erase(remove(userNames.begin(), userNames.end(), string(userName)), userNames.end());
                pthread_mutex_unlock(&clientsMutex);
                string leaveMessage = string(userName) + " 离开了聊天";
                broadcastMessage( "[" + getTimeStamp() + "]  " + leaveMessage, -1);
                cout << "[" + getTimeStamp() + "]  " + "用户 " << userName << " 退出" << endl;
                break;
            }
            else {
                cout << "[" + getTimeStamp() + "]  " + "(" << userName << "): " << buffer << endl;
                broadcastMessage(string(userName) + ": " + buffer, clientSocket);
            }
        }
    }

    close(clientSocket);

    return nullptr;
}

void sendDummyConnection() {
    int dummySocket = socket(AF_INET, SOCK_STREAM, 0);
    if (dummySocket == -1) {
        cout << "虚拟客户端创建失败" << endl;
        return;
    }

    struct sockaddr_in dummyAddr;
    dummyAddr.sin_family = AF_INET;
    dummyAddr.sin_port = htons(SERVER_PORT);
    dummyAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(dummySocket, (struct sockaddr*)&dummyAddr, sizeof(dummyAddr));
    close(dummySocket);  // 立即关闭连接
}

void* monitorServerInput(void* arg) {
    string input;
    while (true) {
        getline(cin, input); 
        if (input == "exit") {
            serverRunning = false;
            close(serverSocket); 
            sendDummyConnection(); 
            cout << "服务器关闭" << endl;
            break;
        } else{
            broadcastMessage( "[" + getTimeStamp() + "]  " + "[系统消息]  " + input, -1);
        }
    }
    return nullptr;
}

bool createServerSocket() {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        cout << "套接字创建失败" << endl;
        return false;
    }
    return true;
}

void bindAddress() {
    int optval = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
}

void startServer() {
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        cout << "绑定地址失败" << endl;
        close(serverSocket);
        exit(-1);
    }

    if (listen(serverSocket, BACKLOG) == -1) {
        cout << "监听端口失败" << endl;
        close(serverSocket);
        exit(-1);
    }

    cout << "服务器启动，等待客户端连接..." << endl;

    // 创建监听服务器输入的线程
    pthread_t inputThread;
    pthread_create(&inputThread, nullptr, monitorServerInput, nullptr);
    pthread_detach(inputThread);

    while (serverRunning) {  // 检查 serverRunning 状态
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);

        if (!serverRunning) break;  // 检查 serverRunning 状态

        if (clientSocket == -1) {
            cout << "接受客户端连接失败" << endl;
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, nullptr, handleClient, &clientSocket);
        pthread_detach(tid);
    }
}

int main() {
    pthread_mutex_init(&clientsMutex, nullptr);
    if (!createServerSocket()) {
        return -1;
    }

    bindAddress();
    startServer();

    close(serverSocket);  // 服务器退出时关闭 socket
    pthread_mutex_destroy(&clientsMutex);
    return 0;
}
