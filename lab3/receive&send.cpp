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
	int version = pBuffer[0] >> 4;  //�汾��
    	int headLength = pBuffer[0] & 0xf;  //�ײ�����
	int TTL = (unsigned short)pBuffer[8];  //����ʱ��
	int dstAddr = ntohl(*(unsigned int *)(pBuffer + 16));  //Ŀ��IP��ַ
	
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
	//���ײ�ÿ16λ��Ϊһ����������������ͣ����λ��λ�ӻ�ĩλ
	for (int i = 0; i < headLength * 2; i++){
		tempSum = ((unsigned char)pBuffer[i * 2] << 8) + (unsigned char)pBuffer[i * 2 + 1];
		if (0xffff - sum < tempSum)
			sum = sum + tempSum + 1;
		else
			sum = sum + tempSum;
	}
	//����Ͳ�Ϊ0xffff�����
	if (sum != 0xffff){
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_CHECKSUM_ERROR);
		return 1;
	}
	//���ϲ㽻��
 	ip_SendtoUp(pBuffer,length); 
	return 0;
}

int stud_ip_Upsend(char *pBuffer,unsigned short len,unsigned int srcAddr,
				   unsigned int dstAddr,byte protocol,byte ttl)
{
	//��ʼ��IP���ݱ��ײ�
	char *IPBuffer = (char *)malloc((20 + len) * sizeof(char));
	memset(IPBuffer, 0, len + 20);
 	//��ʼ���汾�š��ײ����ȡ����ݱ����ȡ�TTL���ϲ�Э���
	IPBuffer[0] = 0x45;
	unsigned short totalLength =  htons(len + 20);
	memcpy(IPBuffer + 2, &totalLength, 2);
	IPBuffer[8] = ttl;
	IPBuffer[9] = protocol;
	//��ʼ��ԴIP��ַ��Ŀ��IP��ַ
	unsigned int src = htonl(srcAddr);  
	unsigned int dis = htonl(dstAddr);  
	memcpy(IPBuffer + 12, &src, 4); 
	memcpy(IPBuffer + 16, &dis, 4);
	//�������ײ�У���
	unsigned short sum = 0; 
	unsigned short tempSum = 0; 
	unsigned short headCheckSum = 0;
	//���ײ�ÿ16λ��Ϊһ����������������ͣ����λ��λ�ӻ�ĩλ
	for (int i = 0; i < 10; i++){
		tempSum = ((unsigned char)IPBuffer[i*2]<<8) + (unsigned char)IPBuffer[i*2 + 1];
		if (0xffff - sum < tempSum)
			sum = sum + tempSum + 1;
		else
			sum = sum + tempSum;
	}
	headCheckSum = htons(0xffff - sum);  
	memcpy(IPBuffer + 10, &headCheckSum, 2);  
	//���ײ����渽������
	memcpy(IPBuffer + 20, pBuffer, len); 
    	//���ͷ���
	ip_SendtoLower(IPBuffer, len + 20);  
	return 0;
}
