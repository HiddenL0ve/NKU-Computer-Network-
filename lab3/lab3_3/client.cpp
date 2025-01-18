
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
#include <fcntl.h>
using namespace std;

#define BUF_SIZE 4096
#define SERVER_PORT 8080
#define CLIENT_PORT 5000
#define TIMEOUT_MS 5000 // 超时时间 (毫秒)
#define MAX_RETRIES 500  // 最大重传次数
#define PACKET_LOSS_RATE 0.2 // 丢包率
#define DELAY 50 // 发送延时

// 定义窗口大小
#define WINDOW_SIZE 10

enum PacketType {
    DATA, SYN, SYN_ACK, ACK, FIN, FIN_ACK, END
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

void setNonBlocking(int sockfd) {
    // 获取当前文件描述符的状态标志
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        exit(EXIT_FAILURE);
    }

    // 设置非阻塞模式
    flags |= O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL failed");
        exit(EXIT_FAILURE);
    }
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

void sendPacket(int sockfd, const struct sockaddr_in &serveraddr, socklen_t serveraddr_len, message &sendMsg) {
    // 模拟固定延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY));
    sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&serveraddr, serveraddr_len);
    cout << "Send packet " << sendMsg.seq << ", size: " << sendMsg.len << " bytes, 校验和：" << sendMsg.checksum << endl;
}

bool receiveACK(int sockfd, struct sockaddr_in &serveraddr, socklen_t &serveraddr_len, vector<message> &window, int &cur, chrono::duration<double> &delay) {
    int valread = recvfrom(sockfd, &recvMsg, sizeof(recvMsg), MSG_DONTWAIT, (struct sockaddr *)&serveraddr, &serveraddr_len);
    if (valread > 0 && recvMsg.type == ACK && recvMsg.ack >= window[0].seq + 1) {
        // 滑动窗口
        cout << "received ack=" << recvMsg.ack << ", sliding window for next packet " << endl;
        int slide = recvMsg.ack - window[0].seq;
        for (int i = 0; i < slide; i++) {
            window.erase(window.begin());
            cur--;
        }
        delay = chrono::duration<double>::zero();
        return true;
    }
    return false;
}

int cwnd = 1; // 初始拥塞窗口大小为1
void handleTimeout(int sockfd, const struct sockaddr_in &serveraddr, socklen_t serveraddr_len, vector<message> &window) {
    cout << "Timeout or incorrect ACK. Retrying..." << endl;
    for (int i = 0; i < window.size(); i++) {
        sendPacket(sockfd, serveraddr, serveraddr_len, window[i]);
        cout << "[重新发送] Send packet " << window[i].seq << ", size: " << window[i].len << " bytes, 校验和：" << window[i].checksum << endl;
    }
    cwnd = 1;  // 重置cwnd为1
}

int count = 0;
void transferFile(const char *path) {
    ifstream input_file(path, ios::in | ios::binary);
    if (!input_file) handleError("Failed to open file for reading.");

    vector<message> window;
    chrono::duration<double> delay;

    int ssthresh = WINDOW_SIZE; // 慢启动阈值
    int MSS = WINDOW_SIZE; // 最大段大小
    int retries = 0;
    int cur = 0; // 当前窗口位置

    chrono::duration<double> timeout = chrono::milliseconds(TIMEOUT_MS); // 超时时间

    int duplicateACKs = 0; // 记录收到的重复ACK次数
    int lastAckReceived = -1; // 记录上一个ACK的序列号

    bool inFastRecovery = false; // 是否在快速恢复阶段
    
    while (true) {
        input_file.read(sendMsg.data, BUF_SIZE);
        sendMsg.len = input_file.gcount();

        // 全部发送完毕，等待剩余ack
        if (sendMsg.len == 0) {
            cout << "全部发送完毕，等待剩余ack..." << endl;
            while (window.size() != 0) {
                retries = 0;
                while (retries < MAX_RETRIES) {
                    struct timeval timeout = {TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000};
                    fd_set fds;
                    FD_ZERO(&fds);
                    FD_SET(sockfd, &fds);
                    if (select(sockfd + 1, &fds, nullptr, nullptr, &timeout) > 0) {
                        if (receiveACK(sockfd, serveraddr, serveraddr_len, window, cur, delay)) {
                            break; // 收到ACK，跳出重试
                        }
                    }
                    retries++;
                    handleTimeout(sockfd, serveraddr, serveraddr_len, window);
                }
                if (retries == MAX_RETRIES) {
                    cout << "Failed to receive ACK after " << MAX_RETRIES << " retries. Give up transfer!" << endl;
                    return;
                }
            }
            sendMsg.type = END;
            sendPacket(sockfd, serveraddr, serveraddr_len, sendMsg);
            cout << "文件传输完成！" << endl;
            break;
        }
        // 慢启动阶段
        if (cwnd < ssthresh) {
            cout << "Slow Start阶段: cwnd=" << cwnd << endl;
            cwnd++;
        } else {  // 拥塞避免阶段
            cout << "Congestion Avoidance阶段: cwnd=" << cwnd << endl;
            count++;
            // 每收到一个完整窗口的数据的ACK，cwnd增加1
            if (count == cwnd) {
                cwnd++;
                count = 0;
            }
        }

        // 快速恢复阶段：检测三次重复ACK
        if (duplicateACKs == 3) {
            cout << "Fast Recovery阶段: 三次重复ACK，快速恢复!" << endl;
            ssthresh = cwnd / 2; // 将ssthresh设置为当前cwnd的一半
            cwnd = ssthresh + 3 * MSS;  // cwnd设置为ssthresh + 3个MSS
            // 重传丢失的报文段
            for (int i = 0; i < window.size(); i++) {
                sendPacket(sockfd, serveraddr, serveraddr_len, window[i]);
                cout << "[重新发送] Send packet " << window[i].seq << ", size: " << window[i].len << " bytes, 校验和：" << window[i].checksum << endl;
            }
            duplicateACKs = 0;  // 重置重复ACK计数
            inFastRecovery = true;
            delay = chrono::duration<double>::zero();
        }

        // 超时处理
        if (delay > timeout) {
            cout << "Timeout发生，进行重传..." << endl;
            ssthresh = cwnd / 2;  // 将ssthresh设置为cwnd的一半
            handleTimeout(sockfd, serveraddr, serveraddr_len, window);
        }

        // 窗口已满，阻塞直到接收到 ACK  
        if (window.size() >= cwnd) {
            cout << "窗口已满，阻塞直到接收到 ACK" << endl;
            retries = 0;
            while (retries < MAX_RETRIES) {
                struct timeval timeout = {TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000};
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sockfd, &fds);
                if (select(sockfd + 1, &fds, nullptr, nullptr, &timeout) > 0) {
                    if (receiveACK(sockfd, serveraddr, serveraddr_len, window, cur, delay)) {
                        break; // 收到ACK，跳出重试
                    }
                }
                retries++;
                handleTimeout(sockfd, serveraddr, serveraddr_len, window);
            }
            if (retries == MAX_RETRIES) {
                cout << "Failed to receive ACK after " << MAX_RETRIES << " retries. Give up transfer!" << endl;
                break;
            }
        }

        // 发送数据包
        if (window.size() < cwnd) {
            sendMsg.type = DATA;
            sendMsg.seq = seq;
            sendMsg.checksum = calculateChecksum(sendMsg);
            streamsize bytesRead = sendMsg.len;
            transferredBytes += bytesRead;

            cout << "=======================================================" << endl;
            cout << "window size: " << window.size() << endl;

            window.push_back(sendMsg);

            if (dis(gen) < PACKET_LOSS_RATE) {
                cout << "[模拟丢包] 未发送 packet " << window[cur].seq << endl;
            } else {
                sendPacket(sockfd, serveraddr, serveraddr_len, window[cur]);
            }

            auto cktime_start = chrono::high_resolution_clock::now();
            seq++; // 增加序列号
            cur++;

            int valread = recvfrom(sockfd, &recvMsg, sizeof(recvMsg), MSG_DONTWAIT, (struct sockaddr *)&serveraddr, &serveraddr_len);
            auto cktime_end = chrono::high_resolution_clock::now();

            if (valread > 0 && recvMsg.type == ACK) {
                if (recvMsg.ack >= window[0].seq + 1) {
                    cout << "received ack=" << recvMsg.ack << ", sliding window for next packet " << endl;
                    int slide = recvMsg.ack - window[0].seq;
                    for (int i = 0; i < slide; i++) {
                        window.erase(window.begin());
                        cur--;
                    }
                    delay = chrono::duration<double>::zero();
                    duplicateACKs = 0; // 重置重复ACK计数
                    lastAckReceived = recvMsg.ack; // 更新接收到的ACK
                } else if (recvMsg.ack == lastAckReceived) {
                    duplicateACKs++; // 如果是重复ACK，增加计数
                    cout << "收到重复ACK：" << recvMsg.ack << "，重复ACK计数：" << duplicateACKs << endl;
                }
            } else {
                chrono::duration<double> ck_time = cktime_end - cktime_start;
                delay += ck_time;
                if (delay < timeout) {
                    continue;
                }
                retries = 0;
                while (retries < MAX_RETRIES) {
                    handleTimeout(sockfd, serveraddr, serveraddr_len, window);
                    int valread = recvfrom(sockfd, &recvMsg, sizeof(recvMsg), MSG_DONTWAIT, (struct sockaddr *)&serveraddr, &serveraddr_len);
                    if (valread > 0) {
                        if (recvMsg.type == ACK && recvMsg.ack >= window[0].seq + 1) {
                            cout << "received ack=" << recvMsg.ack << ", continue to send next packet" << endl;
                            int slide = recvMsg.ack - window[0].seq;
                            for (int i = 0; i < slide; i++) {
                                window.erase(window.begin());
                                cur--;
                            }
                            delay = chrono::duration<double>::zero();
                            break;
                        }
                    }
                    retries++;
                }
                if (retries == MAX_RETRIES) {
                    cout << "Failed to receive ACK after " << MAX_RETRIES << " retries. Give up transfer!" << endl;
                    break;
                }
            }
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
    cout << "[Handshake] Send SYN, seq=" << sendMsg.seq <<", ack=" << sendMsg.ack << endl;

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
        cout << "[Handshake] Send ACK, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

        cout << "连接成功!" << endl;
    } else {
        handleError("Failed to establish connection.");
    }

    // ---------- 数据发送 ----------
    cout<<"是否开始传输（y/n）"<<endl;
    char a;
    cin >> a;
    // 记录开始时间
    auto start = chrono::high_resolution_clock::now();
    auto end = start;
    if(a == 'y'){
        // transferFile("send/helloworld.txt");
        // transferFile("send/send_file.txt");
        //  transferFile("send/1.jpg");
        // transferFile("send/2.jpg");
        transferFile("send/3.jpg");

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
    cout << "[Teardown] Send FIN, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

    // 接收 FIN-ACK 包
    while(true){
        memset(&recvMsg,0,sizeof(message));
        recvfrom(sockfd, &recvMsg, sizeof(recvMsg), MSG_DONTWAIT, (struct sockaddr *)&serveraddr, &serveraddr_len);
        if(recvMsg.type == FIN_ACK) break;
    }

    if (recvMsg.type == FIN_ACK) {
        cout << "[Teardown] Received FIN-ACK, seq=" << recvMsg.seq << ", ack=" << recvMsg.ack << endl;
        
        cout << "[Teardown] Received FIN, seq=" << recvMsg.seq << ", ack=" << recvMsg.ack << endl;

        // 发送最后的 ACK
        sendMsg.type = ACK;
        sendMsg.seq = recvMsg.ack;
        sendMsg.ack = recvMsg.seq + 1;
        sendMsg.checksum = calculateChecksum(sendMsg);
        sendto(sockfd, &sendMsg, sizeof(sendMsg), 0, (const struct sockaddr *)&serveraddr, serveraddr_len);
        cout << "[Teardown] Send FIN-ACK, seq=" << sendMsg.seq << ", ack=" << sendMsg.ack << endl;

        cout << "连接断开..." << endl;
    } else {
        cout << "[Teardown] Failed to terminate connection!" << endl;
    }

    close(sockfd);

    cout << "总传输时间: " << duration.count() << " seconds" << endl;
    cout << "总传输字节数: " << transferredBytes << " bytes" << endl;
    cout << "吞吐率: " << throughput << " MB/s" << endl;

    return 0;
}
