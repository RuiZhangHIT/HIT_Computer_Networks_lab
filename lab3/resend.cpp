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
 	int dest;  //Ŀ��IP��ַ
 	int nexthop;  //��һ����ַ
 	int masklen;  //���볤��
};

vector<route_table> mytable;
 
//��ʼ��·�ɱ�
void stud_Route_Init()
{
	mytable.clear();
	return;
}
 
//���·����Ϣ
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
 	int headLength = pBuffer[0] & 0xf;  //�ײ�����
 	int TTL = (int)pBuffer[8];  //����ʱ��
 	int headCheckSum = ntohs(*(unsigned short*)(pBuffer + 10));  //�ײ�У���
 	int dstAddr = ntohl(*(unsigned int*)(pBuffer + 16));  //Ŀ��IP��ַ
 
	//Ŀ��IP��ַ���ڱ�����ַ������ֱ�ӽ���
 	if (dstAddr == getIpv4Address())
 	{	
  		fwd_LocalRcv(pBuffer, length);
  		return 0;
 	}
 
 	//���TTL�Ѽ���0,ȴ���ɱ�������,������һ����ʱ�ˣ�����
 	if(TTL <= 0)
 	{
  		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_TTLERROR);
  		return 1;
 	}
 	
 	vector<route_table>::iterator t;
	int maxlen = 0, nexthop = 0;
	char *buffer=new char[length];
	//����·�ɱ����ƥ�������һ��IP��ַ
 	for(t = mytable.begin(); t != mytable.end(); t++)
 	{	
 		//�������ƥ���·����Ϣ�������볤�ȴ���֮ǰ���ҵ���Ϣ�����볤��
  		if((t -> dest & ((1 << 31) >> (t -> masklen - 1))) == (dstAddr & ((1 << 31) >> (t -> masklen - 1))) && t -> masklen > maxlen)
  		{
			//��ǰ·����ϢΪ����ƥ��
			maxlen = t -> masklen;
			nexthop = t -> nexthop;
   			memcpy(buffer, pBuffer, length);
   			//TTL - 1
   			buffer[8]--;
   			//���¼����ײ�У���
			unsigned short sum = 0; 
			unsigned short tempSum = 0; 
			unsigned short newCheckSum = 0;
			//���ײ�ÿ16λ��Ϊһ����������������ͣ����λ��λ�ӻ�ĩλ
			for (int i = 0; i < 2 * headLength; i++){
				if(i != 5)  //�������ײ�У���λ�õ���������ȫ0���㣩
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
	
	if (maxlen != 0)  //���ҵ�·����Ϣ��������һ��
	{
   		fwd_SendtoLower(buffer, length, nexthop);
   		return 0;
	}
	else  //û�в鵽·����Ϣ������
	{
 		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_NOROUTE);
 		return 1;
	}
}
