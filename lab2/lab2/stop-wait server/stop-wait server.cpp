#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h> 
#include <time.h> 
#include <WinSock2.h> 
#include <fstream> 
#pragma comment(lib,"ws2_32.lib") 
#define SERVER_PORT  12340  //端口号 
#define SERVER_IP    "0.0.0.0"  //IP 地址 
const int BUFFER_SIZE = 1026;    //缓冲区大小，（以太网中 UDP 的数据帧中包长度应小于 1480 字节） 
const int SEND_WINDOW_SIZE = 1;
const int SEQ_SIZE = 2;
const int PACKET_SIZE = 1024;
int currentSeq;//当前数据包的 seq 
int currentAck;//当前等待确认的 ack 
int totalSeq;//收到的包的总数 
int totalPacket;//需要发送的包总数 


//************************************
// Method:    lossInLossRatio
// FullName:  lossInLossRatio
// Access:    public
// Returns:   BOOL
// Qualifier: 根据丢失率随机生成一个数字，判断是否丢失,丢失则返回TRUE，否则返回 FALSE
// Parameter: float loss_ratio [0,1]
//************************************
BOOL lossInLossRatio(float loss_ratio) {
	int lossBound = (int)(loss_ratio * 100);
	int r = rand() % 101;
	if (r <= lossBound) {
		return TRUE;
	}
	return FALSE;
}

//************************************
// Method:    seqIsAvailable
// FullName:  seqIsAvailable
// Access:    public
// Returns:   bool
// Qualifier: 判断当前序号是否在发送窗口内
//************************************
bool seqIsAvailable() {
	if (currentSeq - currentAck < 0 && currentSeq + SEQ_SIZE - currentAck >= SEND_WINDOW_SIZE) {
		return false;
	}
	if (currentSeq - currentAck >= 0 && currentSeq - currentAck >= SEND_WINDOW_SIZE) {
		return false;
	}
	return true;
}

//************************************
// Method:    timeoutHandler
// FullName:  timeoutHandler
// Access:    public
// Returns:   void
// Qualifier: 超时重传处理函数
//************************************
void timeoutHandler() {
	printf("\n----------等待ACK报文超时！----------\n");
	if (currentSeq >= currentAck) totalSeq = totalSeq - (currentSeq - currentAck);
	else totalSeq = totalSeq - (currentSeq + SEQ_SIZE - currentAck);
	currentSeq = currentAck;
}

//************************************
// Method:    ackHandler
// FullName:  ackHandler
// Access:    public 
// Returns:   void
// Qualifier: 收到 ack，确认，取数据帧的第一个字节
//由于发送数据时，第一个字节（序列号）为 0（ASCII）时发送失败，因此加一了，此处需要减一还原
// Parameter: char buf
//************************************
void ackHandler(char buf) {
	unsigned char index = (unsigned char)buf - 1;
	printf("收到ACK%d\n", index);
	currentAck = (index + 1) % SEQ_SIZE;
}

int main(int argc, char* argv[])
{
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;
	wVersionRequested = MAKEWORD(2, 2);  //版本 2.2 
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll 
		printf("初始化套接字失败，错误代码为 %d\n", err);
		return -1;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
	}
	SOCKET ServerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	//设置套接字为非阻塞模式 
	int iMode = 1; //1：非阻塞，0：阻塞 
	ioctlsocket(ServerSocket, FIONBIO, (u_long FAR*) & iMode);//非阻塞设置 
	SOCKADDR_IN ServerSocketAddr;   //服务器地址 
	ServerSocketAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	ServerSocketAddr.sin_family = AF_INET;
	ServerSocketAddr.sin_port = htons(SERVER_PORT);
	err = bind(ServerSocket, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
	if (err) {
		err = GetLastError();
		printf("端口 %d 绑定套接字失败，错误代码为 %d\n", SERVER_PORT, err);
		WSACleanup();
		return -1;
	}
	printf("----------服务器准备就绪----------\n");
	SOCKADDR_IN ClientSocketAddr;   //客户端地址 
	int addr_len = sizeof(SOCKADDR);
	char buffer[BUFFER_SIZE]; //数据发送接收缓冲区 
	ZeroMemory(buffer, sizeof(buffer));
	//将测试数据读入内存 
	HANDLE fhadle = CreateFileA("../send.txt", 0, 0, NULL, OPEN_ALWAYS, 0, 0);
	int file_size = GetFileSize(fhadle, 0);
	totalPacket = file_size / PACKET_SIZE + 1;
	char* data = new char[file_size];
	ZeroMemory(data, file_size);
	std::ifstream icin;
	icin.open("../send.txt");
	icin.read(data, file_size);
	icin.close();

	int ret;
	float packet_loss_ratio = 0.2;  //默认包丢失率 0.2 
	float ack_loss_ratio = 0.2;  //默认 ACK 丢失率 0.2 
	//用时间作为随机种子，放在循环的最外面 
	srand((unsigned)time(NULL));

	int recvSize;
	while (true) {
		int iMode = 1; //1：非阻塞，0：阻塞 
		ioctlsocket(ServerSocket, FIONBIO, (u_long FAR*) & iMode);//非阻塞设置 
		//非阻塞接收，若没有收到数据，返回值为-1 
		recvSize = recvfrom(ServerSocket, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&ClientSocketAddr), &addr_len);
		if (recvSize < 0) {
			Sleep(200);
			continue;
		}
		if (strcmp(buffer, "receive") == 0) {
			printf("----------测试停等协议服务器 -> 客户端的数据传输----------\n");
			printf("----------服务器与客户端建立连接----------\n");
			ZeroMemory(buffer, sizeof(buffer));
			int waitCount = 0;
			int stage = 0;
			bool flag = true;
			while (flag) {
				switch (stage) {
				case 0:  // 服务器向客户端发送状态码205
					buffer[0] = 205;
					sendto(ServerSocket, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&ClientSocketAddr, sizeof(SOCKADDR));
					Sleep(100);
					stage = 1;
					break;
				case 1://等待接收 200 阶段，没有收到则计数器+1，超时则放弃此次“连接”，等待从第一步开始
					recvSize = recvfrom(ServerSocket, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&ClientSocketAddr), &addr_len);
					if (recvSize < 0) {
						waitCount++;
						if (waitCount > 20) {
							flag = false;
							printf("连接超时！\n");
							break;
						}
						Sleep(500);
						continue;
					}
					else {
						if ((unsigned char)buffer[0] == 200) {
							printf("----------连接已建立，开始文件传输----------\n");
							printf("文件大小是 %d B，分组的大小是 %d B，分组的个数是 %d 个\n", file_size, PACKET_SIZE, totalPacket);
							currentSeq = 0;
							currentAck = 0;
							totalSeq = 0;
							waitCount = 0;
							stage = 2;
						}
					}
					break;
				case 2://数据传输阶段
					if (seqIsAvailable() && totalSeq < totalPacket) {
						//发送给客户端的序列号从 1 开始 
						buffer[0] = currentSeq + 1;
						memcpy(&buffer[1], data + PACKET_SIZE * totalSeq, PACKET_SIZE);
						printf("\n发送分组%d\n", currentSeq);
						sendto(ServerSocket, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ClientSocketAddr, sizeof(SOCKADDR));
						currentSeq = (currentSeq + 1) % SEQ_SIZE;
						totalSeq++;
						Sleep(500);
					}
					else if (totalSeq >= totalPacket && totalPacket % SEQ_SIZE == currentAck) {
						memcpy(buffer, "good bye\0", 9);
						printf("----------数据传输完毕----------\n");
						flag = false;
						break;
					}
					//等待ACK
					recvSize = recvfrom(ServerSocket, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&ClientSocketAddr), &addr_len);
					if (recvSize < 0) {
						waitCount++;
						//20 次等待 ack 则超时重传 
						if (waitCount > 20)
						{
							timeoutHandler();
							waitCount = 0;
						}
					}
					else {
						//收到 ack 
						ackHandler(buffer[0]);
						waitCount = 0;
					}
					Sleep(500);
					break;
				}
			}
		}
		if (strcmp(buffer, "send") == 0) {
			printf("\n----------测试停等协议服务器 <- 客户端的数据传输----------\n");
			printf("丢包率为 %.2f，ACK报文丢失率为%.2f\n", packet_loss_ratio, ack_loss_ratio);
			printf("\n----------服务器与客户端建立连接----------\n");
			ZeroMemory(buffer, sizeof(buffer));
			int waitCount = 0;
			int stage = 0;
			bool flag = true;
			BOOL is_loss;
			unsigned short seq;//包的序列号 
			unsigned short recvSeq;//已确认的最大序列号 
			unsigned short waitSeq;//等待的序列号 ，窗口大小为10，这个为最小的值
			char recv_buffer[BUFFER_SIZE];
			ZeroMemory(recv_buffer, sizeof(recv_buffer));

			std::ofstream out_result;
			out_result.open("receive.txt", std::ios::out | std::ios::trunc);
			if (!out_result.is_open()) {
				printf("文件打开失败！！！\n");
				continue;
			}
			while (true) {
				switch (stage) {
				case 0:  // 服务器向客户端发送状态码205
					buffer[0] = 205;
					sendto(ServerSocket, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&ClientSocketAddr, sizeof(SOCKADDR));
					Sleep(100);
					stage = 1;
					break;
				case 1://等待接收 200 阶段，没有收到则计数器+1，超时则放弃此次“连接”，等待从第一步开始
					recvSize = recvfrom(ServerSocket, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&ClientSocketAddr), &addr_len);
					if (recvSize < 0) {
						waitCount++;
						if (waitCount > 20) {
							flag = false;
							printf("连接超时！\n");
							break;
						}
						Sleep(500);
						continue;
					}
					else {
						if ((unsigned char)buffer[0] == 200) {
							printf("\n----------连接已建立，准备接收数据----------\n");
							waitCount = 0;
							waitSeq = 0;
							recvSeq = 255;
							stage = 2;
							int iMode = 0; //1：非阻塞，0：阻塞 
							ioctlsocket(ServerSocket, FIONBIO, (u_long FAR*) & iMode);//阻塞设置 
						}
					}
					break;
				case 2:
					recvfrom(ServerSocket, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ClientSocketAddr, &addr_len);
					if (!memcmp(buffer, "good bye\0", 9)) {
						printf("\n----------数据传输成功----------\n");
						goto success;
					}
					seq = (unsigned short)buffer[0] - 1;
					seq = seq % SEQ_SIZE;
					//随机法模拟包是否丢失 
					is_loss = lossInLossRatio(packet_loss_ratio);
					if (is_loss) {
						printf("\n分组%d丢失\n", seq);
						continue;
					}
					else {
						printf("\n成功接收到分组%d\n", seq);
					}
					// 如果是期待的分组，接收并向上层传输
					if (waitSeq == seq) {
						waitSeq++;
						if (waitSeq == SEQ_SIZE) waitSeq = 0;
						printf("是期待的分组，接收分组%d\n", seq);

						// 向上层传输
						memcpy(recv_buffer, &buffer[1], sizeof(buffer));
						out_result << recv_buffer;
						ZeroMemory(recv_buffer, sizeof(recv_buffer));
						buffer[0] = seq + 1;
						recvSeq = seq;
						buffer[1] = '\0';
					}
					// 如果不是期待的分组，直接丢弃
					else {
						printf("不是期待的分组，丢弃分组%d\n", seq);
						if (recvSeq != 255) {
							buffer[0] = recvSeq + 1;
							buffer[1] = '\0';
						}
						else break;
					}
					is_loss = lossInLossRatio(ack_loss_ratio);
					if (is_loss) {
						if (recvSeq != 255) printf("ACK%d丢失\n", (unsigned char)buffer[0] - 1);
						continue;
					}
					else {
						if (recvSeq != 255) {
							printf("发送ACK%d，期望收到分组%d\n", (unsigned char)buffer[0] - 1, (unsigned char)buffer[0] % SEQ_SIZE);
							sendto(ServerSocket, buffer, 2, 0, (SOCKADDR*)&ClientSocketAddr, sizeof(SOCKADDR));
						}
					}
					break;
				}
				Sleep(500);
			}
		success:
			out_result.close();
		}
		sendto(ServerSocket, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&ClientSocketAddr, sizeof(SOCKADDR));
		Sleep(500);
	}
	//关闭套接字，卸载库 
	closesocket(ServerSocket);
	WSACleanup();
	return 0;
}
