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
const int SEQ_SIZE = 10;
const int RECV_WINDOW_SIZE = 4;

void printTips() {
	printf("----------------------------------------\n");
	printf("|          输入 \"sr[X][Y]\" 测试SR协议服务器 -> 客户端的数据传输\n");
	printf("|          [X] [0,1] 模拟数据包丢失的概率\n");
	printf("|          [Y] [0,1] 模拟ACK丢失的概率\n");
	printf("----------------------------------------\n");
}

//************************************ 
// Method:      lossInLossRatio 
// FullName:    lossInLossRatio 
// Access:      public   
// Returns:     BOOL 
// Qualifier:  根据丢失率随机生成一个数字，判断是否丢失,丢失则返回TRUE，否则返回 FALSE
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

int main(int argc, char* argv[])
{
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;
	wVersionRequested = MAKEWORD(2, 2);    //版本 2.2 
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
	SOCKET ClientServer = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN ServerSocketAddr;
	ServerSocketAddr.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	ServerSocketAddr.sin_family = AF_INET;
	ServerSocketAddr.sin_port = htons(SERVER_PORT);
	//接收缓冲区 
	char buffer[BUFFER_SIZE];
	ZeroMemory(buffer, sizeof(buffer));
	int addr_len = sizeof(SOCKADDR);
	printTips();
	int ret;
	char cmd[128];

	float packet_loss_ratio = 0.2;  //默认包丢失率 0.2 
	float ack_loss_ratio = 0.2;  //默认 ACK 丢失率 0.2 
	//用时间作为随机种子，放在循环的最外面 
	srand((unsigned)time(NULL));

	while (true) {
		gets_s(buffer);
		ret = sscanf(buffer, "%s%f%f", &cmd, &packet_loss_ratio, &ack_loss_ratio);
		//开始 SR 测试，使用 SR 协议实现 UDP 可靠文件传输 
		if (!strcmp(cmd, "sr")) {
			printf("----------测试SR协议数据传输----------\n");
			printf("丢包率为 %.2f，ACK报文丢失率为%.2f\n", packet_loss_ratio, ack_loss_ratio);
			int waitCount = 0;
			int stage = 0;
			BOOL is_loss;
			unsigned char u_code;//状态码 
			unsigned short seq;//包的序列号 
			unsigned short recvSeq;//已确认的最大序列号 
			unsigned short waitSeq;//等待的序列号 ，窗口大小为10，这个为最小的值

			char recv_buffer[RECV_WINDOW_SIZE][BUFFER_SIZE];
			for (int i = 0; i < RECV_WINDOW_SIZE; i++)
				ZeroMemory(recv_buffer[i], sizeof(recv_buffer[i]));
			bool ack_send[SEQ_SIZE];
			for (int i = 0; i < SEQ_SIZE; i++) {
				ack_send[i] = false;
			}

			std::ofstream out_result;
			out_result.open("receive.txt", std::ios::out | std::ios::trunc);
			if (!out_result.is_open()) {
				printf("文件打开失败！！！\n");
				continue;
			}
			sendto(ClientServer, "sr", strlen("sr") + 1, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
			while (true)
			{
				//等待 server 回复设置 UDP 为阻塞模式 
				recvfrom(ClientServer, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ServerSocketAddr, &addr_len);
				switch (stage) {
				case 0://等待握手阶段 
					u_code = (unsigned char)buffer[0];
					if (u_code == 205)
					{
						printf("\n----------确认建立连接，准备接受数据----------\n");
						buffer[0] = 200;
						buffer[1] = '\0';
						sendto(ClientServer, buffer, 2, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
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
					//随机法模拟包是否丢失 
					is_loss = lossInLossRatio(packet_loss_ratio);
					if (is_loss) {
						printf("\n分组%d丢失\n", seq);
						continue;
					}
					else {
						printf("\n成功接收到分组%d\n", seq);
					}
					// 如果是期待的分组，发送ACK
					if ((waitSeq <= seq && seq <= waitSeq + RECV_WINDOW_SIZE) ||
						(waitSeq >= seq && waitSeq + RECV_WINDOW_SIZE >= SEQ_SIZE && (waitSeq + RECV_WINDOW_SIZE) % SEQ_SIZE >= seq)) {
						printf("是期待的分组，接受分组%d\n", seq);
						ack_send[seq] = true;
						memcpy(recv_buffer[seq - waitSeq], &buffer[1], sizeof(buffer));
						if (waitSeq == seq) {
							// 向上层传输
							for (int i = 0; i < SEQ_SIZE; i++) {
								int index = (waitSeq + i) % SEQ_SIZE;
								if (ack_send[index]) {
									printf("向上层传送分组%d\n", index);
									out_result << recv_buffer[i];
									ZeroMemory(recv_buffer, sizeof(recv_buffer[i]));
									ack_send[index] = false;
								}
								else {
									waitSeq = index % SEQ_SIZE;
									break;
								}
							}
							printf("----------接收窗口：[%d, %d]----------\n\n", waitSeq, (waitSeq + RECV_WINDOW_SIZE - 1) % SEQ_SIZE);
							buffer[0] = seq + 1;
							recvSeq = seq;
							buffer[1] = '\0';
						}
						if (recvSeq == 255) {
							buffer[0] = seq + 1;
							buffer[1] = '\0';
						}
					}
					// 如果不是期待的分组，直接丢弃
					else {
						printf("不是期待的分组，丢弃分组%d\n", seq);
						if (recvSeq != 255) {
							buffer[0] = seq + 1;
							buffer[1] = '\0';
						}
						else break;
					}
					is_loss = lossInLossRatio(ack_loss_ratio);
					if (is_loss) {
						if (recvSeq != 255) printf("ACK%d丢失\n", (unsigned char)buffer[0] - 1);
						else printf("ACK%d丢失\n", (unsigned char)buffer[0] - 1);
						continue;
					}
					else {
						if (recvSeq != 255) {
							printf("发送ACK%d\n", (unsigned char)buffer[0] - 1);
							sendto(ClientServer, buffer, 2, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
						}
						else {
							printf("发送ACK%d\n", (unsigned char)buffer[0] - 1);
							sendto(ClientServer, buffer, 2, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
						}
					}
					break;
				}
				Sleep(500);
			}
		success:
			out_result.close();
		}
		sendto(ClientServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&ServerSocketAddr, sizeof(SOCKADDR));
		ret = recvfrom(ClientServer, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ServerSocketAddr, &addr_len);
		printTips();
	}
	//关闭套接字 
	closesocket(ClientServer);
	WSACleanup();
	return 0;
}