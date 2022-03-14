#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h> 
#include <WinSock2.h> 
#include <time.h> 
#include <fstream> 
#pragma comment(lib,"ws2_32.lib") 
#define SERVER_PORT 12340
#define SERVER_IP "127.0.0.1"
const int BUFFER_SIZE = 1026;
const int SEND_WINDOW_SIZE = 1;
const int SEQ_SIZE = 2;
const int PACKET_SIZE = 1024;
int currentSeq;//当前数据包的 seq 
int currentAck;//当前等待确认的 ack 
int totalSeq;//收到的包的总数 
int totalPacket;//需要发送的包总数 

void printTips() {
	printf("----------------------------------------\n");
	printf("|          输入 \"receive[X][Y]\" 测试停等协议服务器 -> 客户端的数据传输\n");
	printf("|          输入 \"send[X][Y]\" 测试停等协议服务器 <- 客户端的数据传输\n");
	printf("|          [X] [0,1] 模拟数据包丢失的概率\n");
	printf("|          [Y] [0,1] 模拟ACK丢失的概率\n");
	printf("----------------------------------------\n");
}

//************************************ 
// 
// Method:      lossInLossRatio 
// FullName:    lossInLossRatio 
// Access:      public   
// Returns:     BOOL 
//  Qualifier:  根据丢失率随机生成一个数字，判断是否丢失,丢失则返回TRUE，否则返回 FALSE
// Parameter:   float lossRatio [0,1] 
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
	wVersionRequested = MAKEWORD(2, 2);     //版本 2.2   
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll 
		printf("初始化套接字失败，错误代码为 %d\n", err);
		return 1;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
	}
	SOCKET ClientSocket = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN ServerSocketAddr;
	ServerSocketAddr.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	ServerSocketAddr.sin_family = AF_INET;
	ServerSocketAddr.sin_port = htons(SERVER_PORT);
	//接收缓冲区 
	char buffer[BUFFER_SIZE];
	ZeroMemory(buffer, sizeof(buffer));
	HANDLE fhadle = CreateFileA("../send.txt", 0, 0, NULL, OPEN_ALWAYS, 0, 0);
	int file_size = GetFileSize(fhadle, 0);  // 文件大小
	totalPacket = file_size / PACKET_SIZE + 1;  // 总分组数
	char* data = new char[file_size];
	ZeroMemory(data, file_size);
	std::ifstream icin;
	icin.open("../send.txt");
	icin.read(data, file_size);
	icin.close();
	int addr_len = sizeof(SOCKADDR);
	printTips();

	int ret;
	char cmd[128];
	float packet_loss_ratio = 0.2;  // 默认包丢失率 0.2 
	float ack_loss_ratio = 0.2;  // 默认 ACK 丢失率 0.2
	// 用时间作为随机种子，放在循环的最外面 
	srand((unsigned)time(NULL));

	int recvSize;
	while (true) {
		int iMode = 0; //1：非阻塞，0：阻塞 
		ioctlsocket(ClientSocket, FIONBIO, (u_long FAR*) & iMode); // 阻塞设置 
		gets_s(buffer);
		ret = sscanf(buffer, "%s%f%f", &cmd, &packet_loss_ratio, &ack_loss_ratio);
		//开始 GBN 测试，使用 GBN 协议实现 UDP 可靠文件传输 
		if (!strcmp(cmd, "receive")) {
			printf("\n----------测试GBN协议服务器 -> 客户端的数据传输----------\n");
			printf("丢包率为 %.2f，ACK报文丢失率为%.2f\n", packet_loss_ratio, ack_loss_ratio);
			int waitCount = 0;
			int stage = 0;
			BOOL is_loss;
			unsigned char state_code; //状态码 
			unsigned short seq; //接收到的分组的序列号 
			unsigned short recvSeq; // 已确认的最大序列号 
			unsigned short waitSeq; // 期待的序列号 

			char recv_buffer[BUFFER_SIZE];
			ZeroMemory(recv_buffer, sizeof(recv_buffer));

			std::ofstream out_result;
			out_result.open("receive.txt", std::ios::out | std::ios::trunc);
			if (!out_result.is_open()) {
				printf("文件打开失败！！！\n");
				continue;
			}
			sendto(ClientSocket, "receive", strlen("receive") + 1, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
			while (true)
			{
				//等待 server 回复设置 UDP 为阻塞模式 
				recvfrom(ClientSocket, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ServerSocketAddr, &addr_len);
				switch (stage) {
				case 0://等待握手阶段 
					state_code = (unsigned char)buffer[0];
					if (state_code == 205)
					{
						printf("\n----------确认建立连接，准备接收数据----------\n");
						buffer[0] = 200;
						buffer[1] = '\0';
						sendto(ClientSocket, buffer, 2, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
						stage = 1;
						recvSeq = 255;
						waitSeq = 0;
					}
					break;
				case 1://等待接收数据阶段 
					if (!memcmp(buffer, "good bye\0", 9)) {
						printf("\n----------数据接收成功----------\n");
						goto success;
					}
					seq = (unsigned short)buffer[0] - 1;
					seq = seq % SEQ_SIZE;
					// 随机模拟分组是否丢失 
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
					// 随机模拟ACK报文是否丢失
					is_loss = lossInLossRatio(ack_loss_ratio);
					if (is_loss) {
						if (recvSeq != 255) printf("ACK%d丢失\n", (unsigned char)buffer[0] - 1);
						continue;
					}
					else {
						if (recvSeq != 255) {
							printf("发送ACK%d，期望收到分组%d\n", (unsigned char)buffer[0] - 1, (unsigned char)buffer[0] % SEQ_SIZE);
							sendto(ClientSocket, buffer, 2, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
						}
					}
					break;
				}
				Sleep(500);
			}
		success:
			out_result.close();
		}
		if (!strcmp(cmd, "send")) {
			printf("----------测试停等协议服务器 <- 客户端的数据传输----------\n");
			int waitCount = 0;
			int stage = 0;
			bool flag = true;
			sendto(ClientSocket, "send", strlen("send") + 1, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
			while (flag)
			{
				//等待 server 回复设置 UDP 为阻塞模式 
				switch (stage) {
				case 0://等待握手阶段 
					recvfrom(ClientSocket, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ServerSocketAddr, &addr_len);
					if ((unsigned char)buffer[0] == 205)
					{
						printf("\n----------确认建立连接，准备发送数据----------\n");
						printf("文件大小是 %d B，分组的大小是 %d B，分组的个数是 %d 个\n", file_size, PACKET_SIZE, totalPacket);
						buffer[0] = 200;
						buffer[1] = '\0';
						sendto(ClientSocket, buffer, 2, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
						stage = 1;
						currentSeq = 0;
						currentAck = 0;
						totalSeq = 0;
						waitCount = 0;
						int iMode = 1; //1：非阻塞，0：阻塞 
						ioctlsocket(ClientSocket, FIONBIO, (u_long FAR*) & iMode);//非阻塞设置 
					}
					break;
				case 1:
					if (seqIsAvailable() && totalSeq < totalPacket) {
						//发送给客户端的序列号从 1 开始 
						buffer[0] = currentSeq + 1;
						memcpy(&buffer[1], data + PACKET_SIZE * totalSeq, PACKET_SIZE);
						printf("\n发送分组%d\n", currentSeq);
						sendto(ClientSocket, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
						currentSeq = (currentSeq + 1) % SEQ_SIZE;
						totalSeq++;
						Sleep(500);
					}
					else if (totalSeq >= totalPacket && totalPacket % SEQ_SIZE == currentAck) {
						memcpy(buffer, "good bye\0", 9);
						printf("\n----------数据传输完毕----------\n");
						flag = false;
						break;
					}
					//等待ACK
					recvSize = recvfrom(ClientSocket, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&ServerSocketAddr), &addr_len);
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
		sendto(ClientSocket, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
		ret = recvfrom(ClientSocket, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ServerSocketAddr, &addr_len);
		printTips();
	}
	//关闭套接字 
	closesocket(ClientSocket);
	WSACleanup();
	return 0;
}
