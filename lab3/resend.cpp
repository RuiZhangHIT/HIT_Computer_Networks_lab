/*
* THIS FILE IS FOR IP FORWARD TEST
*/
#include "sysInclude.h"
#include<vector>
using std::vector;

// system support
extern void fwd_LocalRcv(char *pBuffer, int length);

extern void fwd_SendtoLower(char *pBuffer, int length, unsigned int nexthop);

extern void fwd_DiscardPkt(char *pBuffer, int type);

extern unsigned int getIpv4Address( );

// implemented by students

struct route_table
{
 	int dest;  //目的IP地址
 	int nexthop;  //下一跳地址
 	int masklen;  //掩码长度
};

vector<route_table> mytable;
 
//初始化路由表
void stud_Route_Init()
{
	mytable.clear();
	return;
}
 
//添加路由信息
void stud_route_add(stud_route_msg *proute)
{
 	route_table t;
	
 	t.dest = ntohl(proute -> dest);
	t.nexthop = ntohl(proute -> nexthop);
 	t.masklen = proute -> masklen;
 	
 	mytable.push_back(t);
	return;
}
 
int stud_fwd_deal(char *pBuffer, int length)
{
 	int headLength = pBuffer[0] & 0xf;  //首部长度
 	int TTL = (int)pBuffer[8];  //生存时间
 	int headCheckSum = ntohs(*(unsigned short*)(pBuffer + 10));  //首部校验和
 	int dstAddr = ntohl(*(unsigned int*)(pBuffer + 16));  //目的IP地址
 
	//目的IP地址等于本机地址，本地直接接收
 	if (dstAddr == getIpv4Address())
 	{	
  		fwd_LocalRcv(pBuffer, length);
  		return 0;
 	}
 
 	//如果TTL已减至0,却不由本机接收,代表到这一跳超时了，丢弃
 	if(TTL <= 0)
 	{
  		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_TTLERROR);
  		return 1;
 	}
 	
 	vector<route_table>::iterator t;
	int maxlen = 0, nexthop = 0;
	char *buffer=new char[length];
	//遍历路由表，按最长匹配查找下一跳IP地址
 	for(t = mytable.begin(); t != mytable.end(); t++)
 	{	
 		//如果存在匹配的路由信息，且掩码长度大于之前查找到信息的掩码长度
  		if((t -> dest & ((1 << 31) >> (t -> masklen - 1))) == (dstAddr & ((1 << 31) >> (t -> masklen - 1))) && t -> masklen > maxlen)
  		{
			//当前路由信息为最优匹配
			maxlen = t -> masklen;
			nexthop = t -> nexthop;
   			memcpy(buffer, pBuffer, length);
   			//TTL - 1
   			buffer[8]--;
   			//重新计算首部校验和
			unsigned short sum = 0; 
			unsigned short tempSum = 0; 
			unsigned short newCheckSum = 0;
			//将首部每16位化为一个数，反码运算求和，最高位进位加回末位
			for (int i = 0; i < 2 * headLength; i++){
				if(i != 5)  //不考虑首部校验和位置的数（当作全0计算）
				{
					tempSum = ((unsigned char)buffer[i * 2] << 8) + (unsigned char)buffer[i * 2 + 1];
					if (0xffff - sum < tempSum)
						sum = sum + tempSum + 1;
					else
						sum = sum + tempSum;
				}
			}
			newCheckSum = htons(0xffff - sum);  
			memcpy(buffer + 10, &newCheckSum, 2);
  		}
 	}
	
	if (maxlen != 0)  //查找到路由信息，传给下一跳
	{
   		fwd_SendtoLower(buffer, length, nexthop);
   		return 0;
	}
	else  //没有查到路由信息，丢弃
	{
 		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_NOROUTE);
 		return 1;
	}
}
