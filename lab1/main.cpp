//#include "stdafx.h"
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>

//该命令是静态链接Ws2_32.lib库，Codeblocks使用的MingGW不支持该写法，可以在设置里手动加上libws2_32.a
//#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //HTTP服务器端口
#define CACHE_NUM 100 //最大cache数量

#define INVALID_USERS "invalid_users.txt" //被屏蔽的用户
#define INVALID_WEBSITES "invalid_websites.txt" //被屏蔽的网站
#define FISHING_WEB_SRC "http://www.tsinghua.edu.cn/" //钓鱼的源网站
#define FISHING_WEB_DEST "http://www.hit.edu.cn/" //钓鱼的目的网站
#define FISHING_WEB_HOST "www.hit.edu.cn" //钓鱼目的地址的主机名



//HTTP重要头部数据
struct HttpHeader
{
    char method[4]; //POST或者GET，注意有些为CONNECT，本实验暂不考虑
    char url[1024]; //请求的url
    char host[1024]; //目标主机
    char cookie[1024 * 10]; //cookie
    HttpHeader()
    {
        ZeroMemory(this,sizeof(HttpHeader));
    }
};

//缓存数据
struct Cache
{
    char url[1024];  //url地址
    char time[40];   //上次更新时间
    char buffer[MAXSIZE];   //缓存的内容
    Cache()
    {
        ZeroMemory(this, sizeof(Cache));
    }
} cache[CACHE_NUM];

//将s1里所有的s2修改为s3
char* modify(char* s1, char* s2, char* s3)
{
    char* p, * from, * to, * start = s1;
    int c1, c2 = strlen(s2), c3 = strlen(s3), c; //各串长度及计数器
    if (c2 == 0)
        return s1;
    while (true) //修改所有的s2
    {
        c1 = strlen(start);
        p = strstr(start, s2); //定位s2出现位置

        if (p == NULL) //s1不含s2
            return s1;

        if (c2 > c3) //串往前移
        {
            from = p + c2;
            to = p + c3;
            c = c1 - c2 - (p - start) + 1;
            while (c--)
                *to++ = *from++;
        }
        else if (c2 < c3) //串往后移
        {
            from = start + c1;
            to = from + (c3 - c2);
            c = from - p - c2 + 1;
            while (c--)
                *to-- = *from--;
        }

        if (c3) //修改s2
        {
            from = s3, to = p, c = c3;
            while (c--)
                *to++ = *from++;
        }
        start = p + c3; //新的查找位置
    }
}

int last_cache = -1;
const char * ims = "If-Modified-Since: ";

BOOL InitSocket();
void ParseHttpHead(char *buffer,HttpHeader * httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket,char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
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
    printf("代理服务器正在启动\n");
    printf("初始化...\n");
    if(!InitSocket())
    {
        printf("socket初始化失败\n");
        return -1;
    }
    printf("代理服务器正在运行，监听端口%d\n", ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET;
    ProxyParam *lpProxyParam;
    HANDLE hThread;
    sockaddr_in addr;
    int addr_len = sizeof(SOCKADDR);

    //代理服务器不断监听
    while(true)
    {
        BOOL forbid = FALSE;
        acceptSocket = accept(ProxyServer, (SOCKADDR*)&addr, &addr_len);
        //用户过滤：不允许某些用户访问外网
        char invalid_users[MAXSIZE];
        ZeroMemory(invalid_users, MAXSIZE);
        FILE * in = NULL;
        if ((in = fopen(INVALID_USERS, "r")) != NULL) //读取被屏蔽的用户
        {
            fread(invalid_users, sizeof(char), MAXSIZE, in);
            fclose(in);
        }
        char *p;
        char *delim = "\n";
        p = strtok(invalid_users, delim);
        while(p) //将请求连接的用户与被屏蔽的用户逐个比对
        {
            if (addr.sin_addr.s_addr == inet_addr(p))
            {
                printf("用户访问受限\n");
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
    //关闭套接字
    closesocket(ProxyServer);
    //解除与套接字库的绑定
    WSACleanup();
    return 0;
}

//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket()
{
    //加载套接字库（必须）
    WORD wVersionRequested;
    WSADATA wsaData;
    //套接字加载时错误提示
    int err;
    //版本 2.2
    wVersionRequested = MAKEWORD(2, 2);
    //加载dll文件Scoket库
    err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0)
    {
        //找不到winsock.dll
        printf("加载winsock失败，错误代码为: %d\n", WSAGetLastError());
        return FALSE;
    }
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) !=2)
    {
        printf("不能找到正确的winsock版本\n");
        WSACleanup();
        return FALSE;
    }
    //AF_INET使用IPv4 Internet协议
    //SOCK_STREAM为流式套接字，提供面向连接的稳定数据传输（TCP连接）
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
    if(INVALID_SOCKET == ProxyServer)
    {
        printf("创建套接字失败，错误代码为：%d\n",WSAGetLastError());
        return FALSE;
    }
    ProxyServerAddr.sin_family = AF_INET;
    ProxyServerAddr.sin_port = htons(ProxyPort); //将整型变量从主机字节顺序转变成网络字节顺序
    ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    if(bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)
    {
        printf("绑定套接字失败\n");
        return FALSE;
    }
    if(listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("监听端口%d失败", ProxyPort);
        return FALSE;
    }
    return TRUE;
}

//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: 线程执行函数
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

    //获取客户端发送的HTTP数据报文
    recvSize = recv(((ProxyParam *)lpParameter) -> clientSocket, Buffer, MAXSIZE, 0);
    if(recvSize <= 0)
    {
        goto error;
    }

    //复制缓存，以便用于解析HTTP头部
    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer, recvSize + 1);
    memcpy(CacheBuffer, Buffer, recvSize);

    //解析HTTP头部
    ParseHttpHead(CacheBuffer, httpHeader);
    delete CacheBuffer;

    //网站过滤：不允许访问某些网站
    char invalid_websites[MAXSIZE];
    ZeroMemory(invalid_websites, MAXSIZE);
    if ((in = fopen(INVALID_WEBSITES, "r")) != NULL) //读取被屏蔽的网站
    {
        fread(invalid_websites, sizeof(char), MAXSIZE, in);
        fclose(in);
    }
    char *p;
    p = strtok(invalid_websites, delim);
    while(p) //将请求访问的网站与被屏蔽的网站逐个比对
    {
        if (strcmp (httpHeader->url, p) == 0)
        {
            printf("该网站已被屏蔽\n");
            goto error;
        }
        p = strtok(NULL,delim);
    }

    //网站引导：将用户对某个网站的访问引导至一个模拟网站（钓鱼）
    if (strstr(httpHeader->url, FISHING_WEB_SRC) != NULL)
    {
        //修改HTTP请求报文
        modify(Buffer, httpHeader->url, FISHING_WEB_DEST);
        modify(Buffer, httpHeader->host, FISHING_WEB_HOST);
        printf("修改后的HTTP报文如下：\n%s\n", Buffer);
        //先将原本的host和url清空，再重新赋值
        ZeroMemory(httpHeader -> host, 1024);
        ZeroMemory(httpHeader -> url, 1024);
        memcpy(httpHeader -> host, FISHING_WEB_HOST, strlen(FISHING_WEB_HOST));
        memcpy(httpHeader -> url, FISHING_WEB_DEST, strlen(FISHING_WEB_DEST));
        printf("成功从源网站：%s转到目的网站：%s \n", FISHING_WEB_SRC, FISHING_WEB_DEST);
    }

    //代理连接服务器
    if(!ConnectToServer(&((ProxyParam *)lpParameter) -> serverSocket, httpHeader -> host))
    {
        printf("连接目标服务器失败\n");
        goto error;
    }
    printf("代理连接主机%s成功\n", httpHeader -> host);

    //使用GET方法时，判断缓存中是否有请求访问的网站
    if (!strcmp(httpHeader -> method, "GET"))
        for (i = 0; i < CACHE_NUM; i++)
            if (strlen(cache[i].url) != 0 && !strcmp(cache[i].url, httpHeader -> url))
            {
                //缓存命中时，修改Http报文，加入"If-Modified-Since: "字段
                printf("缓存命中，正在验证网站是否修改过...\n");
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

    //将客户端发送的HTTP数据报文转发给目标服务器
    ret = send(((ProxyParam*)lpParameter) -> serverSocket, Buffer, strlen(Buffer) + 1, 0);

    //等待目标服务器返回数据
    recvSize = recv(((ProxyParam*)lpParameter) -> serverSocket, Buffer, MAXSIZE, 0);
    if (recvSize <= 0)
    {
        goto error;
    }
    /*printf("\n代理从目标服务器收到的信息\n");
    printf("%s", Buffer);
    printf("\n----------------------------------------------\n");*/

    //服务器返回报文中的状态码为304时，说明网站未更新
    if (!memcmp(&Buffer[9], "304", 3)) //决策是304
    {
        printf("网站未修改过，将由缓存提供网站信息\n");
        ret = send(((ProxyParam*)lpParameter) -> clientSocket, cache[i].buffer, sizeof(cache[i].buffer), 0);
        if (ret != SOCKET_ERROR)
        {
            printf("成功返回缓存\n");
        }
    }
    else
    {
        //服务器返回报文中的状态码为200时，说明网站更新过或是没有缓存过
        if (!strcmp(httpHeader->method, "GET") && !memcmp(&Buffer[9], "200", 3))
        {
            char Buffer2[MAXSIZE];
            memcpy(Buffer2, Buffer, sizeof(Buffer));
            const char* delim = "\r\n";
            char* p = strtok(Buffer2, delim);
            bool flag = false;
            while (p)
            {
                //确定需添加的缓存内容
                if (strlen(p) >= 15 && !memcmp(p, "Last-Modified: ", 15))
                {
                    flag = true;
                    break;
                }
                p = strtok(NULL, delim);
            }
            //添加缓存
            if (flag)
            {
                last_cache++;
                last_cache %= CACHE_NUM;
                memcpy(cache[last_cache].url, httpHeader->url, sizeof(httpHeader->url));
                memcpy(cache[last_cache].time, p + 15, strlen(p) - 15);
                memcpy(cache[last_cache].buffer, Buffer, sizeof(Buffer));
                printf("成功添加缓存\n");
            }
        }

        //将目标服务器返回的数据转发给客户端
        ret = send(((ProxyParam*)lpParameter) -> clientSocket, Buffer, sizeof(Buffer), 0);
    }

    //错误处理
error:
    printf("关闭套接字\n");
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
// Qualifier: 解析TCP报文中的HTTP头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char *buffer, HttpHeader * httpHeader)
{
    char *p;
    const char * delim = "\r\n";
    p = strtok(buffer, delim); //第一次调用，提取第一行，第一个参数为被分解的字符串
    printf("%s\n", p);
    if(p[0] == 'G')  //GET方式
    {
        memcpy(httpHeader -> method, "GET", 3);
        memcpy(httpHeader -> url, &p[4], strlen(p) - 13); //"GET"和"HTTP/1.1"各占3个和8个，URL前后各有1个空格，一共13个
    }
    else if(p[0] == 'P')  //POST方式
    {
        memcpy(httpHeader -> method, "POST", 4);
        memcpy(httpHeader -> url, &p[5], strlen(p) - 14); //"POST"和"HTTP/1.1"各占4个和8个，URL前后各有1个空格，一共14个
    }
    printf("客户端请求访问的URL是：%s\n", httpHeader -> url);
    p = strtok(NULL, delim); //第二次调用，第一个参数设为 NULL，将从上次提取结束的地方开始
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
// Qualifier: 根据主机创建目标服务器套接字，并连接
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
