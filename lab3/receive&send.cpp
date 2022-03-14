/*
* THIS FILE IS FOR IP TEST
*/
// system support
#include "sysInclude.h"

extern void ip_DiscardPkt(char *pBuffer,int type);

extern void ip_SendtoLower(char *pBuffer,int length);

extern void ip_SendtoUp(char *pBuffer,int length);

extern unsigned int getIpv4Address();

// implemented by students

int stud_ip_recv(char *pBuffer,unsigned short length)
{
	int version = pBuffer[0] >> 4;  //版本号
    	int headLength = pBuffer[0] & 0xf;  //首部长度
	int TTL = (unsigned short)pBuffer[8];  //生存时间
	int dstAddr = ntohl(*(unsigned int *)(pBuffer + 16));  //目的IP地址
	
	if (version != 4){
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_VERSION_ERROR);
		return 1;
	}
	
	if (headLength < 5){
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_HEADLEN_ERROR);
		return 1;
	}
	
	if (TTL <= 0){
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_TTL_ERROR);
		return 1;
	}
	
	if (dstAddr != getIpv4Address() && dstAddr != 0xffffffff){
		ip_DiscardPkt(pBuffer,STUD_IP_TEST_DESTINATION_ERROR);  
		return 1;
	}
	
	unsigned short sum = 0; 
	unsigned short tempSum = 0; 
	//将首部每16位化为一个数，反码运算求和，最高位进位加回末位
	for (int i = 0; i < headLength * 2; i++){
		tempSum = ((unsigned char)pBuffer[i * 2] << 8) + (unsigned char)pBuffer[i * 2 + 1];
		if (0xffff - sum < tempSum)
			sum = sum + tempSum + 1;
		else
			sum = sum + tempSum;
	}
	//反码和不为0xffff则出错
	if (sum != 0xffff){
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_CHECKSUM_ERROR);
		return 1;
	}
	//向上层交付
 	ip_SendtoUp(pBuffer,length); 
	return 0;
}

int stud_ip_Upsend(char *pBuffer,unsigned short len,unsigned int srcAddr,
				   unsigned int dstAddr,byte protocol,byte ttl)
{
	//初始化IP数据报首部
	char *IPBuffer = (char *)malloc((20 + len) * sizeof(char));
	memset(IPBuffer, 0, len + 20);
 	//初始化版本号、首部长度、数据报长度、TTL和上层协议号
	IPBuffer[0] = 0x45;
	unsigned short totalLength =  htons(len + 20);
	memcpy(IPBuffer + 2, &totalLength, 2);
	IPBuffer[8] = ttl;
	IPBuffer[9] = protocol;
	//初始化源IP地址和目的IP地址
	unsigned int src = htonl(srcAddr);  
	unsigned int dis = htonl(dstAddr);  
	memcpy(IPBuffer + 12, &src, 4); 
	memcpy(IPBuffer + 16, &dis, 4);
	//最后计算首部校验和
	unsigned short sum = 0; 
	unsigned short tempSum = 0; 
	unsigned short headCheckSum = 0;
	//将首部每16位化为一个数，反码运算求和，最高位进位加回末位
	for (int i = 0; i < 10; i++){
		tempSum = ((unsigned char)IPBuffer[i*2]<<8) + (unsigned char)IPBuffer[i*2 + 1];
		if (0xffff - sum < tempSum)
			sum = sum + tempSum + 1;
		else
			sum = sum + tempSum;
	}
	headCheckSum = htons(0xffff - sum);  
	memcpy(IPBuffer + 10, &headCheckSum, 2);  
	//在首部后面附上数据
	memcpy(IPBuffer + 20, pBuffer, len); 
    	//发送分组
	ip_SendtoLower(IPBuffer, len + 20);  
	return 0;
}
