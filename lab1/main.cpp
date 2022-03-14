//#include "stdafx.h"
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>

//�������Ǿ�̬����Ws2_32.lib�⣬Codeblocksʹ�õ�MingGW��֧�ָ�д�����������������ֶ�����libws2_32.a
//#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65507 //�������ݱ��ĵ���󳤶�
#define HTTP_PORT 80 //HTTP�������˿�
#define CACHE_NUM 100 //���cache����

#define INVALID_USERS "invalid_users.txt" //�����ε��û�
#define INVALID_WEBSITES "invalid_websites.txt" //�����ε���վ
#define FISHING_WEB_SRC "http://www.tsinghua.edu.cn/" //�����Դ��վ
#define FISHING_WEB_DEST "http://www.hit.edu.cn/" //�����Ŀ����վ
#define FISHING_WEB_HOST "www.hit.edu.cn" //����Ŀ�ĵ�ַ��������



//HTTP��Ҫͷ������
struct HttpHeader
{
    char method[4]; //POST����GET��ע����ЩΪCONNECT����ʵ���ݲ�����
    char url[1024]; //�����url
    char host[1024]; //Ŀ������
    char cookie[1024 * 10]; //cookie
    HttpHeader()
    {
        ZeroMemory(this,sizeof(HttpHeader));
    }
};

//��������
struct Cache
{
    char url[1024];  //url��ַ
    char time[40];   //�ϴθ���ʱ��
    char buffer[MAXSIZE];   //���������
    Cache()
    {
        ZeroMemory(this, sizeof(Cache));
    }
} cache[CACHE_NUM];

//��s1�����е�s2�޸�Ϊs3
char* modify(char* s1, char* s2, char* s3)
{
    char* p, * from, * to, * start = s1;
    int c1, c2 = strlen(s2), c3 = strlen(s3), c; //�������ȼ�������
    if (c2 == 0)
        return s1;
    while (true) //�޸����е�s2
    {
        c1 = strlen(start);
        p = strstr(start, s2); //��λs2����λ��

        if (p == NULL) //s1����s2
            return s1;

        if (c2 > c3) //����ǰ��
        {
            from = p + c2;
            to = p + c3;
            c = c1 - c2 - (p - start) + 1;
            while (c--)
                *to++ = *from++;
        }
        else if (c2 < c3) //��������
        {
            from = start + c1;
            to = from + (c3 - c2);
            c = from - p - c2 + 1;
            while (c--)
                *to-- = *from--;
        }

        if (c3) //�޸�s2
        {
            from = s3, to = p, c = c3;
            while (c--)
                *to++ = *from++;
        }
        start = p + c3; //�µĲ���λ��
    }
}

int last_cache = -1;
const char * ims = "If-Modified-Since: ";

BOOL InitSocket();
void ParseHttpHead(char *buffer,HttpHeader * httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket,char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

//������ز���
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//�����µ����Ӷ�ʹ�����߳̽��д������̵߳�Ƶ���Ĵ����������ر��˷���Դ
//����ʹ���̳߳ؼ�����߷�����Ч��
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};

struct ProxyParam
{
    SOCKET clientSocket;
    SOCKET serverSocket;
};

int main(int argc,  char* argv[])
{
    printf("�����������������\n");
    printf("��ʼ��...\n");
    if(!InitSocket())
    {
        printf("socket��ʼ��ʧ��\n");
        return -1;
    }
    printf("����������������У������˿�%d\n", ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET;
    ProxyParam *lpProxyParam;
    HANDLE hThread;
    sockaddr_in addr;
    int addr_len = sizeof(SOCKADDR);

    //������������ϼ���
    while(true)
    {
        BOOL forbid = FALSE;
        acceptSocket = accept(ProxyServer, (SOCKADDR*)&addr, &addr_len);
        //�û����ˣ�������ĳЩ�û���������
        char invalid_users[MAXSIZE];
        ZeroMemory(invalid_users, MAXSIZE);
        FILE * in = NULL;
        if ((in = fopen(INVALID_USERS, "r")) != NULL) //��ȡ�����ε��û�
        {
            fread(invalid_users, sizeof(char), MAXSIZE, in);
            fclose(in);
        }
        char *p;
        char *delim = "\n";
        p = strtok(invalid_users, delim);
        while(p) //���������ӵ��û��뱻���ε��û�����ȶ�
        {
            if (addr.sin_addr.s_addr == inet_addr(p))
            {
                printf("�û���������\n");
                forbid = TRUE;
                break;
            }
            p = strtok(NULL,delim);
        }

        lpProxyParam = new ProxyParam;
        if(lpProxyParam == NULL || forbid == TRUE)
        {
            continue;
        }
        lpProxyParam -> clientSocket = acceptSocket;
        hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
        CloseHandle(hThread);
        Sleep(200);
    }
    //�ر��׽���
    closesocket(ProxyServer);
    //������׽��ֿ�İ�
    WSACleanup();
    return 0;
}

//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: ��ʼ���׽���
//************************************
BOOL InitSocket()
{
    //�����׽��ֿ⣨���룩
    WORD wVersionRequested;
    WSADATA wsaData;
    //�׽��ּ���ʱ������ʾ
    int err;
    //�汾 2.2
    wVersionRequested = MAKEWORD(2, 2);
    //����dll�ļ�Scoket��
    err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0)
    {
        //�Ҳ���winsock.dll
        printf("����winsockʧ�ܣ��������Ϊ: %d\n", WSAGetLastError());
        return FALSE;
    }
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) !=2)
    {
        printf("�����ҵ���ȷ��winsock�汾\n");
        WSACleanup();
        return FALSE;
    }
    //AF_INETʹ��IPv4 InternetЭ��
    //SOCK_STREAMΪ��ʽ�׽��֣��ṩ�������ӵ��ȶ����ݴ��䣨TCP���ӣ�
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
    if(INVALID_SOCKET == ProxyServer)
    {
        printf("�����׽���ʧ�ܣ��������Ϊ��%d\n",WSAGetLastError());
        return FALSE;
    }
    ProxyServerAddr.sin_family = AF_INET;
    ProxyServerAddr.sin_port = htons(ProxyPort); //�����ͱ����������ֽ�˳��ת��������ֽ�˳��
    ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    if(bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)
    {
        printf("���׽���ʧ��\n");
        return FALSE;
    }
    if(listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("�����˿�%dʧ��", ProxyPort);
        return FALSE;
    }
    return TRUE;
}

//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: �߳�ִ�к���
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter)
{
    char Buffer[MAXSIZE];
    char* CacheBuffer;
    ZeroMemory(Buffer, MAXSIZE);
    SOCKADDR_IN clientAddr;
    int length = sizeof(SOCKADDR_IN);
    int recvSize;
    int ret;
    FILE * in = NULL;
    char * delim = "\r\n";
    int i = 0;
    HttpHeader* httpHeader = new HttpHeader();

    //��ȡ�ͻ��˷��͵�HTTP���ݱ���
    recvSize = recv(((ProxyParam *)lpParameter) -> clientSocket, Buffer, MAXSIZE, 0);
    if(recvSize <= 0)
    {
        goto error;
    }

    //���ƻ��棬�Ա����ڽ���HTTPͷ��
    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer, recvSize + 1);
    memcpy(CacheBuffer, Buffer, recvSize);

    //����HTTPͷ��
    ParseHttpHead(CacheBuffer, httpHeader);
    delete CacheBuffer;

    //��վ���ˣ����������ĳЩ��վ
    char invalid_websites[MAXSIZE];
    ZeroMemory(invalid_websites, MAXSIZE);
    if ((in = fopen(INVALID_WEBSITES, "r")) != NULL) //��ȡ�����ε���վ
    {
        fread(invalid_websites, sizeof(char), MAXSIZE, in);
        fclose(in);
    }
    char *p;
    p = strtok(invalid_websites, delim);
    while(p) //��������ʵ���վ�뱻���ε���վ����ȶ�
    {
        if (strcmp (httpHeader->url, p) == 0)
        {
            printf("����վ�ѱ�����\n");
            goto error;
        }
        p = strtok(NULL,delim);
    }

    //��վ���������û���ĳ����վ�ķ���������һ��ģ����վ�����㣩
    if (strstr(httpHeader->url, FISHING_WEB_SRC) != NULL)
    {
        //�޸�HTTP������
        modify(Buffer, httpHeader->url, FISHING_WEB_DEST);
        modify(Buffer, httpHeader->host, FISHING_WEB_HOST);
        printf("�޸ĺ��HTTP�������£�\n%s\n", Buffer);
        //�Ƚ�ԭ����host��url��գ������¸�ֵ
        ZeroMemory(httpHeader -> host, 1024);
        ZeroMemory(httpHeader -> url, 1024);
        memcpy(httpHeader -> host, FISHING_WEB_HOST, strlen(FISHING_WEB_HOST));
        memcpy(httpHeader -> url, FISHING_WEB_DEST, strlen(FISHING_WEB_DEST));
        printf("�ɹ���Դ��վ��%sת��Ŀ����վ��%s \n", FISHING_WEB_SRC, FISHING_WEB_DEST);
    }

    //�������ӷ�����
    if(!ConnectToServer(&((ProxyParam *)lpParameter) -> serverSocket, httpHeader -> host))
    {
        printf("����Ŀ�������ʧ��\n");
        goto error;
    }
    printf("������������%s�ɹ�\n", httpHeader -> host);

    //ʹ��GET����ʱ���жϻ������Ƿ���������ʵ���վ
    if (!strcmp(httpHeader -> method, "GET"))
        for (i = 0; i < CACHE_NUM; i++)
            if (strlen(cache[i].url) != 0 && !strcmp(cache[i].url, httpHeader -> url))
            {
                //��������ʱ���޸�Http���ģ�����"If-Modified-Since: "�ֶ�
                printf("�������У�������֤��վ�Ƿ��޸Ĺ�...\n");
                recvSize -= 2;
                memcpy(Buffer + recvSize, ims, 19);
                recvSize += 19;
                memcpy(Buffer + recvSize, cache[i].time, strlen(cache[i].time));
                recvSize += strlen(cache[i].time);
                Buffer[recvSize++] = '\r';
                Buffer[recvSize++] = '\n';
                Buffer[recvSize++] = '\r';
                Buffer[recvSize++] = '\n';
                break;
            }

    //���ͻ��˷��͵�HTTP���ݱ���ת����Ŀ�������
    ret = send(((ProxyParam*)lpParameter) -> serverSocket, Buffer, strlen(Buffer) + 1, 0);

    //�ȴ�Ŀ���������������
    recvSize = recv(((ProxyParam*)lpParameter) -> serverSocket, Buffer, MAXSIZE, 0);
    if (recvSize <= 0)
    {
        goto error;
    }
    /*printf("\n�����Ŀ��������յ�����Ϣ\n");
    printf("%s", Buffer);
    printf("\n----------------------------------------------\n");*/

    //���������ر����е�״̬��Ϊ304ʱ��˵����վδ����
    if (!memcmp(&Buffer[9], "304", 3)) //������304
    {
        printf("��վδ�޸Ĺ������ɻ����ṩ��վ��Ϣ\n");
        ret = send(((ProxyParam*)lpParameter) -> clientSocket, cache[i].buffer, sizeof(cache[i].buffer), 0);
        if (ret != SOCKET_ERROR)
        {
            printf("�ɹ����ػ���\n");
        }
    }
    else
    {
        //���������ر����е�״̬��Ϊ200ʱ��˵����վ���¹�����û�л����
        if (!strcmp(httpHeader->method, "GET") && !memcmp(&Buffer[9], "200", 3))
        {
            char Buffer2[MAXSIZE];
            memcpy(Buffer2, Buffer, sizeof(Buffer));
            const char* delim = "\r\n";
            char* p = strtok(Buffer2, delim);
            bool flag = false;
            while (p)
            {
                //ȷ������ӵĻ�������
                if (strlen(p) >= 15 && !memcmp(p, "Last-Modified: ", 15))
                {
                    flag = true;
                    break;
                }
                p = strtok(NULL, delim);
            }
            //��ӻ���
            if (flag)
            {
                last_cache++;
                last_cache %= CACHE_NUM;
                memcpy(cache[last_cache].url, httpHeader->url, sizeof(httpHeader->url));
                memcpy(cache[last_cache].time, p + 15, strlen(p) - 15);
                memcpy(cache[last_cache].buffer, Buffer, sizeof(Buffer));
                printf("�ɹ���ӻ���\n");
            }
        }

        //��Ŀ����������ص�����ת�����ͻ���
        ret = send(((ProxyParam*)lpParameter) -> clientSocket, Buffer, sizeof(Buffer), 0);
    }

    //������
error:
    printf("�ر��׽���\n");
    printf("----------------------------------------------\n\n");
    Sleep(200);
    closesocket(((ProxyParam*)lpParameter)->clientSocket);
    closesocket(((ProxyParam*)lpParameter)->serverSocket);
    delete lpParameter;
    _endthreadex(0);
    return 0;
}

//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public
// Returns: void
// Qualifier: ����TCP�����е�HTTPͷ��
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char *buffer, HttpHeader * httpHeader)
{
    char *p;
    const char * delim = "\r\n";
    p = strtok(buffer, delim); //��һ�ε��ã���ȡ��һ�У���һ������Ϊ���ֽ���ַ���
    printf("%s\n", p);
    if(p[0] == 'G')  //GET��ʽ
    {
        memcpy(httpHeader -> method, "GET", 3);
        memcpy(httpHeader -> url, &p[4], strlen(p) - 13); //"GET"��"HTTP/1.1"��ռ3����8����URLǰ�����1���ո�һ��13��
    }
    else if(p[0] == 'P')  //POST��ʽ
    {
        memcpy(httpHeader -> method, "POST", 4);
        memcpy(httpHeader -> url, &p[5], strlen(p) - 14); //"POST"��"HTTP/1.1"��ռ4����8����URLǰ�����1���ո�һ��14��
    }
    printf("�ͻ���������ʵ�URL�ǣ�%s\n", httpHeader -> url);
    p = strtok(NULL, delim); //�ڶ��ε��ã���һ��������Ϊ NULL�������ϴ���ȡ�����ĵط���ʼ
    while(p)
    {
        switch(p[0])
        {
        case 'H': //Host
            memcpy(httpHeader -> host, &p[6], strlen(p) - 6);
            break;
        case 'C': //Cookie
            if(strlen(p) > 8)
            {
                char header[8];
                ZeroMemory(header, sizeof(header));
                memcpy(header, p, 6);
                if(!strcmp(header, "Cookie"))
                {
                    memcpy(httpHeader -> cookie, &p[8], strlen(p) - 8);
                }
            }
            break;
        default:
            break;
        }
        p = strtok(NULL,delim);
    }
}

//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public
// Returns: BOOL
// Qualifier: ������������Ŀ��������׽��֣�������
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET *serverSocket, char *host)
{
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(HTTP_PORT);
    HOSTENT *hostent = gethostbyname(host);
    if(!hostent)
    {
        return FALSE;
    }
    in_addr Inaddr = *( (in_addr*) *hostent -> h_addr_list);
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
    *serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(*serverSocket == INVALID_SOCKET)
    {
        return FALSE;
    }
    if(connect(*serverSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        closesocket(*serverSocket);
        return FALSE;
    }
    return TRUE;
}
