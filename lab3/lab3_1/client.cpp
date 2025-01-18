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
#include <random> // 随机数生成器
using namespace std;

#define BUF_SIZE 4096 
#define SERVER_PORT 8080
#define CLIENT_PORT 5000
#define TIMEOUT_MS 5000 // 超时时间 (毫秒)
#define MAX_RETRIES 5  // 最大重传次数
#define PACKET_LOSS_RATE 0.5 // 丢包率
#define DELAY 200 // 发送延时

enum PacketType {
    DATA, SYN, SYN_ACK, ACK, FIN, FIN_ACK
};

struct message {
    PacketType type;
    u_long seq;
    u_long ack;
    u_short len;
    char data[BUF_SIZE];
    u_long checksum;
};

message sendMsg{}, recvMsg{};
u_long seq = 0;

int sockfd;
struct sockaddr_in clientaddr{}, serveraddr{}; 
socklen_t serveraddr_len = sizeof(serveraddr);

streamsize transferredBytes = 0;

constexpr std::array<uint32_t, 256> generateCRC32Table() {
    constexpr uint32_t polynomial = 0xEDB88320;
    std::array<uint32_t, 256> table{};

    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr std::array<uint32_t, 256> crc32_table = generateCRC32Table();

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

// 随机数生成器初始化
std::random_device rd;
std::mt19937 gen(rd());
std::uniform_real_distribution<> dis(0.0, 1.0);

// 模拟丢包函数
bool simulatePacketLoss() {
    return dis(gen) < PACKET_LOSS_RATE;
}

void handleError(const string &message) {
    cerr << message << endl;
    exit(EXIT_FAILURE);
}

void Transfer(const char *path){
    ifstream input_file(path, ios::in | ios::binary);
    if (!input_file) handleError("Failed to open file for reading.");

    while (true) {
        int retries = 0;
        input_file.read(sendMsg.data, BUF_SIZE);
        sendMsg.len = input_file.gcount();
        // 计算传输
        streamsize bytesRead = sendMsg.len;
        transferredBytes += bytesRead;

        sendMsg.type = DATA;
        sendMsg.seq = seq;
        sendMsg.checksum = calculateChecksum(sendMsg);

        cout << "=======================================================" << endl;

        // 等待 ACK
        while (retries < MAX_RETRIES) {
            if (dis(gen) < PACKET_LOSS_RATE) {
                cout << "[模拟丢包] 未发送 packet " << sendMsg.seq << endl;
            } else {
                // 模拟固定延迟
                std::this_thread::sleep_for(std::chrono::milliseconds(DELAY));
                sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&serveraddr, serveraddr_len);
                cout << "Sent packet " << seq << ", size: " << sendMsg.len << " bytes, 校验和：" << sendMsg.checksum << endl;
            }

            // 进行超时等待检查
            struct timeval timeout = {TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000};
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);

            if (select(sockfd + 1, &fds, nullptr, nullptr, &timeout) > 0) {
                // 检查是否收到正确的 ACK
                ssize_t valread = recvfrom(sockfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&serveraddr, &serveraddr_len);
                if (valread > 0 && recvMsg.type == ACK && recvMsg.ack == sendMsg.seq + 1) {
                    cout << "received ack="<< recvMsg.ack <<" for packet " << seq << endl;
                    seq++;
                    break; // 成功收到 ACK，退出循环
                }
            }

            retries++;
            cout << "Timeout or incorrect ACK. Retrying... (" << retries << "/" << MAX_RETRIES << ")" << endl;
        }

        // 超过最大重传次数
        if (retries == MAX_RETRIES) {
            cerr << "Failed to receive ACK after " << MAX_RETRIES << " retries. Giving up on packet " << sendMsg.seq << endl;
        }

        if (sendMsg.len == 0) {
            cout << "文件传输完成！" << endl;
            break;
        }
    }

    input_file.close();
}

int main() {
    const char *server_ip = "127.0.0.1";
    int port = SERVER_PORT;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) handleError("Socket creation error.");

    // 设置客户端地址和端口号  
    clientaddr.sin_family = AF_INET;  
    clientaddr.sin_port = htons(CLIENT_PORT); // 使用自定义的客户端端口  
    clientaddr.sin_addr.s_addr = INADDR_ANY; // 允许任何地址绑定

    // 绑定客户端套接字到指定端口  
    if (bind(sockfd, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) < 0) {  
        handleError("Binding failed.");  
    }

    // 设置服务器地址和端口号
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serveraddr.sin_addr) <= 0) handleError("Invalid address/Address not supported.");

    // ---------- 三次握手 ----------
    // 发送 SYN 包
    sendMsg.type = SYN;
    sendMsg.seq = seq;
    sendMsg.checksum = calculateChecksum(sendMsg);
    sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&serveraddr, serveraddr_len);
    cout << "[Handshake] Sent SYN, seq=" << sendMsg.seq <<", ack=" << sendMsg.ack << endl;

    // 接收 SYN-ACK 包
    recvfrom(sockfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&serveraddr, &serveraddr_len);
    if (recvMsg.type == SYN_ACK && recvMsg.ack == seq + 1) {
        cout << "[Handshake] Received SYN-ACK, seq=" << recvMsg.seq << ", ack=" << recvMsg.ack << endl;

        // 发送 ACK 包
        sendMsg.type = ACK;
        sendMsg.seq = recvMsg.ack;
        sendMsg.ack = recvMsg.seq + 1;
        sendMsg.checksum = calculateChecksum(sendMsg);
        sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&serveraddr, serveraddr_len);
        cout << "[Handshake] Sent ACK, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

        cout << "连接成功!" << endl;
    } else {
        handleError("Failed to establish connection.");
    }

    // 记录开始时间
    auto start = chrono::high_resolution_clock::now();
    auto end = start;

    // ---------- 数据发送 ----------
    cout<<"是否开始传输（y/n）"<<endl;
    char a;
    cin >> a;
    if(a == 'y'){
        // Transfer("send/helloworld.txt");
        // Transfer("send/1.jpg");
        // Transfer("send/2.jpg");
        Transfer("send/3.jpg");

        end = chrono::high_resolution_clock::now();
    }
    else {
        sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&serveraddr, serveraddr_len);
    }

    // 记录结束时间并计算吞吐率
    chrono::duration<double> duration = end - start;
    double throughput = (duration.count()!=0) ? transferredBytes / duration.count() / 1024 / 1024 : 0; // MB/s

    // ---------- 四次挥手 ----------
    // 发送 FIN 包
    sendMsg.type = FIN;
    sendMsg.seq = seq;
    sendMsg.ack = seq;
    sendMsg.checksum = calculateChecksum(sendMsg);
    sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&serveraddr, serveraddr_len);
    cout << "[Teardown] Sent FIN, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

    // 接收 FIN-ACK 包
    recvfrom(sockfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr *)&serveraddr, &serveraddr_len);
    if (recvMsg.type == FIN_ACK) {
        cout << "[Teardown] Received FIN-ACK, seq=" << recvMsg.seq << ", ack=" << recvMsg.ack << endl;
        
        cout << "[Teardown] Received FIN, seq=" << recvMsg.seq << ", ack=" << recvMsg.ack << endl;

        // 发送最后的 ACK
        sendMsg.type = ACK;
        sendMsg.seq = recvMsg.ack;
        sendMsg.ack = recvMsg.seq + 1;
        sendMsg.checksum = calculateChecksum(sendMsg);
        sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&serveraddr, serveraddr_len);
        cout << "[Teardown] Sent FIN-ACK, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

        cout << "连接断开..." << endl;
    } else {
        cerr << "[Teardown] Failed to terminate connection!" << endl;
    }

    close(sockfd);

    cout << "总传输时间: " << duration.count() << " seconds" << endl;
    cout << "总传输字节数: " << transferredBytes << " bytes" << endl;
    cout << "吞吐率: " << throughput << " MB/s" << endl;

    return 0;
}
