#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h> 
#include <time.h> 
#include <WinSock2.h> 
#include <fstream> 

#pragma comment(lib,"ws2_32.lib") 
#define SERVER_PORT  12340  //端口号 
#define SERVER_IP    "0.0.0.0"  //IP 地址 
const int BUFFER_SIZE = 1026;    //缓冲区大小，（以太网中 UDP 的数据帧中包长度应小于 1480 字节） 
const int SEND_WINDOW_SIZE = 3;
const int SEQ_SIZE = 10;
const int PACKET_SIZE = 1024;
int currentSeq;//当前数据包的 seq 
int currentAck;//当前等待确认的 ack 
int totalSeq;//收到的包的总数 
int totalPacket;//需要发送的包总数 
int turn;
int ack[SEQ_SIZE];

//************************************
// Method:    SeqIsAvailable
// FullName:  SeqIsAvailable
// Access:    public
// Returns:   bool
// Qualifier: 判断当前序号是否在发送窗口内
//************************************
bool SeqIsAvailable() {
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
	currentSeq = currentAck;
	totalSeq = turn * SEQ_SIZE + currentSeq;
}

//************************************
// Method:    ackHandler
// FullName:  ackHandler
// Access:    public 
// Returns:   void
// Qualifier: 收到 ack，逐个确认，取数据帧的第一个字节
// 由于发送数据时，第一个字节（序列号）为 0（ASCII）时发送失败，因此加一了，此处需要减一还原
// Parameter: char buf
//************************************
void ackHandler(char buf) {
	unsigned char index = (unsigned char)buf - 1;
	printf("收到ACK%d\n", index);
	ack[index] = 1;
	int shift = 0;  //窗口滑动步长
	if (index == currentAck) {
		for (int i = 0; i < SEQ_SIZE; i++) {
			int cur = (currentAck + i) % SEQ_SIZE;
			if (ack[cur] == 1) {
				ack[cur] = 0;
				shift++;
			}
			else {
				if (currentAck + shift >= SEQ_SIZE) turn++;
				currentAck = (currentAck + shift) % SEQ_SIZE;
				currentSeq = currentAck;
				totalSeq = SEQ_SIZE * turn + currentSeq;
				break;
			}
		}
	}
	printf("----------发送窗口：[%d, %d]----------\n\n", currentAck, (currentAck + SEND_WINDOW_SIZE - 1) % SEQ_SIZE);
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

	int recvSize;
	for (int i = 0; i < SEQ_SIZE; i++) ack[i] = 0;

	while (true) {
		//非阻塞接收，若没有收到数据，返回值为-1 
		recvSize = recvfrom(ServerSocket, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&ClientSocketAddr), &addr_len);
		if (recvSize < 0) {
			Sleep(200);
			continue;
		}
		if (strcmp(buffer, "sr") == 0) {
			printf("----------测试SR协议数据传输----------\n");
			printf("----------服务器正在与客户端建立连接----------\n");
			ZeroMemory(buffer, sizeof(buffer));
			int waitCount = 0;
			int stage = 0;
			bool flag = true;
			int turn = 0;
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
							printf("\n----------连接已建立，开始文件传输----------\n");
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
					if (SeqIsAvailable() && totalSeq < totalPacket) {
						if (ack[currentSeq] == 0 || ack[currentSeq] == -1) {
							if (ack[currentSeq] == -1) printf("\n----------等待ACK报文超时！----------\n"); //-1表示是重发
							else ack[currentSeq] = -1;
							//发送给客户端的序列号从 1 开始 
							buffer[0] = currentSeq + 1;
							memcpy(&buffer[1], data + PACKET_SIZE * totalSeq, PACKET_SIZE);
							printf("发送分组%d\n", currentSeq);
							sendto(ServerSocket, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&ClientSocketAddr, sizeof(SOCKADDR));
							currentSeq = (currentSeq + 1) % SEQ_SIZE;
							totalSeq++;
							Sleep(500);
						}
						else {
							currentSeq = (currentSeq + 1) % SEQ_SIZE;
							totalSeq++;
							break;
						}
					}
					else if (totalSeq >= totalPacket && totalPacket % SEQ_SIZE == currentAck) {
						memcpy(buffer, "good bye\0", 9);
						printf("\n----------数据传输完毕----------\n");
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
		sendto(ServerSocket, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&ClientSocketAddr, sizeof(SOCKADDR));
		Sleep(500);
	}
	//关闭套接字，卸载库 
	closesocket(ServerSocket);
	WSACleanup();
	return 0;
}