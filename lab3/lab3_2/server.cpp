#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <cstdint>
#include <array>
#include <random>
using namespace std;

#define BUF_SIZE 4096
#define SERVER_PORT 8080  
#define CLIENT_PORT 5000

// 定义包类型
enum PacketType {
    DATA,    // 数据包
    SYN,     // 同步包（建立连接）
    SYN_ACK, // 同步确认包
    ACK,     // 确认包
    FIN,     // 终止连接
    FIN_ACK,  // 终止确认包
    END
};

// 数据包结构体
struct message {
    PacketType type;  // 包类型
    u_long seq;       // 序列号
    u_long ack;       // 确认号
    u_short len;      // 数据长度
    char data[BUF_SIZE]; // 数据
    u_long checksum;  // 校验和
};

int sockfd;
struct sockaddr_in servaddr{}, cliaddr{};
socklen_t cliaddr_len = sizeof(cliaddr);

message recvMsg{}, sendMsg{};
u_long expectedSeq = 0;

u_long calculateChecksum(const message &msg) {
    uint32_t checksum = 0;  // 初始化校验和为0
    const uint8_t *data = reinterpret_cast<const uint8_t *>(&msg);
    size_t length = sizeof(msg) - sizeof(msg.checksum);

    // 对消息的每个字节进行XOR运算
    for (size_t i = 0; i < length; ++i) {
        checksum ^= data[i];  // 将字节与当前校验和进行异或
    }

    return checksum;  // 返回最终的XOR校验和
}

// 打印错误信息并退出
void handleError(const string &message) {
    cerr << message << endl;
    exit(EXIT_FAILURE);
}

void Transfer(const char *path){
    ofstream output_file(path, ios::out | ios::binary);
    if (!output_file) {
        handleError("Failed to open file for writing.");
    }

    while (true) {
        memset(&recvMsg,0,sizeof(message));
        int valread = recvfrom(sockfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&cliaddr, &cliaddr_len);
        if(recvMsg.len==0 && recvMsg.seq == 0){
            cout<<"没有文件传输！"<<endl;
            break;
        }

        if (valread <= 0) {
            cerr << "Connection error or client disconnected." << endl;
            break;
        }

        if (recvMsg.type == END) {
            cout << "文件接收完成！" << endl;
            break;
        }

        // 校验校验和
        u_long receivedChecksum = recvMsg.checksum;
        if (calculateChecksum(recvMsg) != receivedChecksum) {
            cout << "=======================================================" << endl;
            cout << "Checksum error for packet " << recvMsg.seq << ". Retry..." << endl;
            continue;
        }
        if (recvMsg.seq == expectedSeq) {
            cout << "=======================================================" << endl;
            cout << "Received packet " << recvMsg.seq << ", size: " << recvMsg.len << " bytes" << endl;

            // // 模拟固定延迟
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // 发送 ACK
            sendMsg.type = ACK;
            sendMsg.ack = recvMsg.seq + 1;
            sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&cliaddr, cliaddr_len);
            expectedSeq++;

            cout << "校验和："<< calculateChecksum(recvMsg) << " 校验和正确" << ", Send ack="<< sendMsg.ack << endl;
            
            if (recvMsg.len > 0) {
                output_file.write(recvMsg.data, recvMsg.len);
            }
            continue;
        }
        // else {
        //     // 发送 ACK 以重传
        //     sendMsg.type = ACK;
        //     sendMsg.ack = expectedSeq;
        //     sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&cliaddr, cliaddr_len);
        // }
    }
    output_file.close();
}

int main() {
    const char *client_ip = "127.0.0.1";
    int port = CLIENT_PORT;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        handleError("Socket creation error.");
    }
    // 设置服务器地址和端口号
    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(SERVER_PORT);
    
    // 绑定服务器套接字到指定端口
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        close(sockfd);
        handleError("Bind failed.");
    }

    // 设置客户端地址和端口号
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, client_ip, &cliaddr.sin_addr) <= 0) handleError("Invalid address/Address not supported.");

    cout << "Server start... " << endl;

    // ---------- 三次握手 ----------
    // 接收 SYN 包
    recvfrom(sockfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&cliaddr, &cliaddr_len);
    if (recvMsg.type == SYN) {
        cout << "[Handshake] Received SYN, seq=" << recvMsg.seq << ", ack=" << recvMsg.ack << endl;

        // 发送 SYN-ACK 包
        sendMsg.type = SYN_ACK;
        sendMsg.seq = 0;
        sendMsg.ack = recvMsg.seq + 1;
        sendMsg.checksum = calculateChecksum(sendMsg);
        sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&cliaddr, cliaddr_len);

        cout << "[Handshake] Send SYN-ACK, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

        // 接收 ACK 包
        recvfrom(sockfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&cliaddr, &cliaddr_len);
        if (recvMsg.type == ACK && recvMsg.ack == sendMsg.seq + 1) {
            cout << "[Handshake] Received ACK, seq=" << recvMsg.seq << ", ack=" << recvMsg.ack << endl;
            cout << "连接成功!" << endl;
        } else {
            handleError("Failed to establish connection.");
        }
    }

    // ---------- 数据接收 ----------
        // Transfer("receive/received_file.txt");
        // Transfer("receive/helloworld.txt");
        // Transfer("receive/1.jpg");
        // Transfer("receive/2.jpg");
        Transfer("receive/3.jpg");

    // ---------- 四次挥手 ----------
    // 接收 FIN 包
    recvfrom(sockfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&cliaddr, &cliaddr_len);
    if (recvMsg.type == FIN) {
        cout << "[Teardown] Received FIN, seq=" << recvMsg.seq << ", ack=" << recvMsg.ack << endl;

        // 发送 FIN-ACK 包
        sendMsg.type = FIN_ACK;
        sendMsg.seq = recvMsg.ack;
        sendMsg.ack = recvMsg.seq + 1;
        sendMsg.checksum = calculateChecksum(sendMsg);
        sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&cliaddr, cliaddr_len);

        cout << "[Teardown] Send FIN-ACK, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

        // 发送 FIN 包
        cout << "[Teardown] Send FIN, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

        // 接收最后的 ACK
        recvfrom(sockfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&cliaddr, &cliaddr_len);
        if (recvMsg.type == ACK) {
            cout << "[Teardown] Received FIN-ACK, seq=" << recvMsg.seq <<", ack=" << recvMsg.ack << endl;
            cout << "客户端断开连接..." << endl;
        } else {
            cerr << "[Teardown] Failed to terminate connection!" << endl;
        }
    }

    close(sockfd);
    return 0;
}
