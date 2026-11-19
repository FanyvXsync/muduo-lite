#pragma once

#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <unistd.h>

#include <netinet/in.h> // 结构体
#include <arpa/inet.h> // 字节序
#include <sys/socket.h> // 套接字
#include <fcntl.h>


#define INF 0
#define DBG 1
#define ERR 2
#define LOG_LEVEL DBG

#define LOG(level,format, ...) \
do\
{\
    if(level < LOG_LEVEL) break;\
    time_t t = time(0);\
    struct tm* ltm = localtime(&t);\
    char tmp[32] = {0};\
    strftime(tmp, 32, "%H:%M:%S", ltm);\
    fprintf(stdout, "[%s %s:%d]" format "\n", tmp, __FILE__, __LINE__, ##__VA_ARGS__);\
}while(0)

#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)



#define BUFFER_DEFAULT_SIZE 1024


class Buffer
{
private:
    char* Begin()
    {
        return &*_buffer.begin();
    }

public:
    //构造 很重要
    Buffer():_readIdx(0), _writeIdx(0),_buffer(BUFFER_DEFAULT_SIZE)
    {}

    // 获取写入起始地址
    char* WritePosition()
    {
        // 空间起始地址 + 写偏移量
        // 起始元素迭代器  解引用后取地址, 就拿到了空间起始地址
        return Begin() + _writeIdx;
    }
    // 获取读取起始地址
    char* ReadPosition()
    {
        return Begin() + _readIdx;
    }
    // 获取空闲空间大小  前沿(缓冲区末尾)和后沿
    uint64_t TailIdleSize()
    {
        // 总大小 减去写偏移位置
        return _buffer.size() - _writeIdx;
    }
    uint64_t HeadIdleSize()
    {
        return _readIdx;  
    }
    // 获取可读数据大小
    uint64_t ReadAbleSize()
    {
        // 写偏移 - 读偏移
        return _writeIdx - _readIdx;
    }
    // 读偏移向后移动
    void MoveReadOffset(uint64_t len)
    {
        // 但是要注意, len 超过 可读空间大小怎么办?
        assert(len <= ReadAbleSize());
        _readIdx += len;
    }
    // 写偏移向后移动
    void MoveWriteOffset(uint64_t len)
    {
        // 数据总是向后添加的 所以 writeIdx 其实不怎么关心前面
        assert(len <= TailIdleSize());
        _writeIdx += len;
        // 没有挪动数据 没有往回缩
    }
    // 确保可写空间足够  不够了考虑是挪还是扩容
    // (扩容这个东西 有数据拷贝的 本身效率就不高)
    void EnsureWriteSpace(uint64_t len)
    {
        // 三个情况
        if(TailIdleSize() >= len) return ;
        
        // 末尾空间空间不够, 则判断加上起始位置的空闲空间大小是否足够, 够了就移动数据到开始
        if(len <= TailIdleSize() + HeadIdleSize())
        {
            //可读数据 移动到 起始位置
            uint64_t readSzie = ReadAbleSize();
            // std::copy
            std::copy(ReadPosition(), ReadPosition() + readSzie, Begin());
            _readIdx = 0;  // 读偏移归零
            _writeIdx = readSzie; 
        }
        else
        {
            // 总体空间都不够  不移动数据, 直接扩容即可
            // 在 写偏移之后 扩容足够空间
            _buffer.resize(_writeIdx + len);
        }

    
    }


    // 写入数据 
    void Write(const void* data, uint64_t len)
    {
        // 两点 足够空间, 拷贝数据进去
        EnsureWriteSpace(len);
        const char* d = (const char * ) data;
        std::copy(d, d + len, WritePosition()); // 函数模板参数不统一了
        // 移动?

    }
    // 读取数据
    void Read(void* buffer, uint64_t len)
    {
        // 要求 获取的数据大小 必须小于 可读数据大小
        // void* 没有步长的
        assert(len <= ReadAbleSize());
        std::copy(ReadPosition(), ReadPosition() + len, (char*)buffer);        
    
        //
    }
    // 清空缓冲区
    void Clear()
    {
        _writeIdx = 0;
        _readIdx = 0;
    }

    void WriteString(const std::string& data)
    {
        Write(data.c_str(), data.size());
    }

    void WriteBuffer(Buffer& data)
    {
        Write(data.ReadPosition(), data.ReadAbleSize());
    }

    void WriteStringAndPush(const std::string& data)
    {
        WriteString(data);
        MoveWriteOffset(data.size());
    }

    void WriteBufferAndPush(Buffer& data)
    {
        WriteBuffer(data);
        MoveWriteOffset(data.ReadAbleSize());
    }

    std::string ReadAsString(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::string str;
        str.resize(len);

        Read(&str[0], len); // 绕开 const char*
        return str;
    }
    // 读取了 并且把读取的数据 弹出
    void ReadAndPop(void* buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }

    std::string ReadAsStringAndPop(uint64_t len)
    {
        std::string str = ReadAsString(len);
        MoveReadOffset(len);
        return str;
    }
    // write 系列

    // HTTP 特殊的 提取一行数据
    char* FindCRLF()
    {
        // 一个个找把  find   memmem  memchr
        char* res = (char*)memchr(ReadPosition(), '\n', ReadAbleSize());
        return res;
    }

    // 获取一行数据 针对 Ascll 码字符
    std:: string GetLine()
    {
        char* pos = FindCRLF();
        if(pos == nullptr) return "";
        
        return ReadAsString(pos - ReadPosition() + 1); // 把换行字符也取出来    
    }

    std::string GetLineAndPop()
    {
        std::string str = GetLine();
        MoveReadOffset(str.size());

        return str;
    }


private:
    std::vector<char> _buffer;// vector 空间管理
    uint64_t _readIdx; // 读偏移
    uint64_t _writeIdx; // 写偏移
};


#define MAX_LISTEN 1024

class Socket
{
public:
    Socket():_sockfd(-1) {}
    Socket(int sockfd):_sockfd(sockfd){}
    ~Socket(){ Close(); }

    //创建套接字
    bool Create()
    {
        // int socket(int domain, int type, int protoal
        _sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(_sockfd < 0)
        {
            ERR_LOG("CREATE SOCKET FAILED");
            return false;
        }
        return true;
    }
    //绑定地址信息
    bool Bind(const std::string& ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        socklen_t len = sizeof(addr);

        // int bind (int sockfd, struct sockaddr* addr, socklen_t len)
        int ret = bind(_sockfd, (struct sockaddr*) &addr, len);
        if(ret < 0)
        {
            ERR_LOG("BIND SOCKET FAILED");
            return false;
        }
        return true;
    }


    //开始监听
    bool Listen(int backlog = MAX_LISTEN)
    {
        //不限制总数 限制某一时间的并发链接数
        int ret = listen(_sockfd, backlog);
        if(ret < 0)
        {
            ERR_LOG("Listen SOCKET FAILED");
            return false;
        }
        return true;
    }

    //向服务器发起连接
    bool Connect(const std::string& ip, uint16_t port)
    {
        // int connect(int sockfd, struct sockaddr* addr, socklen_t len)
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        socklen_t len = sizeof(addr);

        // int bind (int sockfd, struct sockaddr* addr, socklen_t len)
        int ret = connect(_sockfd, (struct sockaddr*) &addr, len);
        if(ret < 0)
        {
            ERR_LOG("CONNECR SERVER FAILED");
            return false;
        }
        return true;
    
    
    }

    //获取新连接
    int Accept()
    {
        int newfd = accept(_sockfd, NULL, NULL);
        if(newfd < 0)
        {
            ERR_LOG("ACCEPT FAILED");
            return false;
        }

        return newfd;
    }

    //接收数据
    ssize_t Recv(void* buf, size_t len, int flag = 0)
    {
        // ssize recv (_sockfd, buf, size_t len, int flag);
        ssize_t ret = recv(_sockfd, buf, len, flag);
        if(ret <= 0)
        {
            if(errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            ERR_LOG("SOCKET RECV FAILED");
            return -1;
        }

        return ret;
    }

    //发送数据
    ssize_t Send(const void* buf, size_t len, int flag = 0)
    {
        // Send ()
        ssize_t ret = send(_sockfd, buf, len, flag);
        if(ret < 0)
        {
            ERR_LOG("SOCKET SEND FAILED");
            return -1;
        }

        return ret; //实际发送的数据长度 外界根据这个长度来决定如何处理
    }

    ssize_t NonBlockRecv(void* buf, size_t len)
    {
        return Recv(buf, len, MSG_DONTWAIT);
    }
    ssize_t NonBlockSend(void* buf, size_t len)
    {
        return Send(buf, len, MSG_DONTWAIT);
    }
    

    //关闭套接字
    void Close()
    {
        if(_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }
    //创建一个服务端连接
    bool CreateServer(uint16_t port, const std::string& ip = "0.0.0.0", bool flag = false)
    {
        //1. 创建套接字， 2.绑定地址， 3.开始监听 4.设置非阻塞， 5.启动地址重用
        if(!Create()) return false;
        if(flag)NonBlock();
        
        if(!Bind(ip, port)) return false;   
    
        if(!Listen()) return false;

        
        ReuserAddress();
        return true;
    }
    //创建一个客户端连接
    bool CreateClient(uint16_t port, const std::string& ip)
    {
        //1.创建套接字, 2.链接服务器  不用手动绑定端口
        //
        if(!Create()) return false;
        if(!Connect(ip, port)) return false;

        return true;
    }
    //设置套接字选项开启地址端口重用
    void ReuserAddress()
    {
        //setsockopt  设置套接字选项
        // int fd, int level, int optname, void* val, int vallen
        int val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void*)&val, sizeof val);
        val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void*)&val, sizeof val);
    
    }
    //设置套接字阻塞属性设置为非阻塞
    void NonBlock()
    {
        int flag = fcntl(_sockfd, F_GETFL, 0);
        fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
    }

private:
    int _sockfd;
};