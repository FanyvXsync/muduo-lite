#pragma once

#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <mutex>
#include <thread>
#include <sys/eventfd.h>

#include <netinet/in.h> // 结构体
#include <arpa/inet.h> // 字节序
#include <sys/socket.h> // 套接字
#include <fcntl.h>

#include <sys/epoll.h>
#include <functional>
#include <unordered_map>


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


class Poller;
class EventLoop;


class Channel
{
using EventCallBack = std::function<void()>;
public:
    Channel(EventLoop *loop , int fd): _fd(fd), _loop(loop) 
    {} // 会有少量修改  因为最后要放到 eventloop 里面去关联

    int Fd() { return _fd ;}

    bool ReadAble()
    {
        return (_events & EPOLLIN); // 看看 读事件监控当年有没有监控读事件
    }

    bool WriteAble()
    {
        return (_events & EPOLLOUT);
    }

    void EnableRead()
    {
        _events |= EPOLLIN;
        // 注意 这里还没放到 epoll 里面, 还是用户态
        // 后面添加 因为 别的接口还没写 eventloop
        Update();
    }

    void EnableWrite()
    {
        _events |= EPOLLIN;
        Update();
    }

    void DisableRead() 
    {
        _events &= ~EPOLLIN;
        Update();
    }

    void DisableWrite()
    {
        _events &= ~EPOLLOUT;
        Update();
    }

    void DisableAll()
    {
        _events = 0; // 直接设置 0 就可以了
        Update();
    }

    void Remove(); // 移除监控  之前说关闭是 fd 还是在 epoll 的红黑树里面, 还是可以随时可以启动的   // 这里是 要直接把节点从红黑树里面拿出来的  // 调 eventloop 里面的接口
   
    
    uint32_t GetEvent() 
    {
        return _events;
    }

    void SetReadCallBack(const EventCallBack& cb) { _read_callback = cb; }
    void SetWrietCallBack(const EventCallBack& cb) { _write_callback = cb; }
    void SetErrorCallBack(const EventCallBack& cb) { _error_callback = cb; }
    void SetCloseCallBack(const EventCallBack& cb) { _close_callback = cb; }
    void SetEventCallBack(const EventCallBack& cb) { _event_callback = cb; }
    

    void HandleEvent() // 事件处理 一旦触发了事件 调用这个, 根据不同的事件来调用不同的事件
    // 自己触发了什么事件 怎么处理 自己决定 eventloop 不管
    {
        if((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            if(_read_callback) _read_callback();
            if(_event_callback) _event_callback();  // 任意事件的处理 是针对读和写来说的  而且放到事件处理之后 刷新活跃度
                                                    // 错误处理我们会直接释放链接
        }
        if(_revents & EPOLLOUT)
        {
            if(_write_callback) _write_callback();
            if(_event_callback) _event_callback();
        }
        // 有可能释放链接的操作 一次只处理一个  安全第一
        if(_revents & EPOLLERR)
        {
            if( _error_callback) _error_callback();
        }
        else if(_revents & EPOLLHUP)
        {
            if(_close_callback) _close_callback();
        }

        // 一旦出错 会释放链接 没有必要再调用任意事件回调  放到关闭释放的前面来调用

    }

    void SetREvents(uint32_t events) 
    {
        _revents = events;
    }  // 设置监控了哪些事件

    void Update();
   

private:
    uint32_t _events; // 当前要监控的事件
    uint32_t _revents; // 监控成功的事件

    // 注意 这个 类 是 要和别的模块结合的
    // 有些成员还定不了  是 Connection 的子模块

    EventCallBack _read_callback;
    EventCallBack _write_callback;
    EventCallBack _error_callback;
    EventCallBack _close_callback;
    EventCallBack _event_callback;

    int _fd;
    EventLoop *_loop;

    // 参数是 Connection 相关的信息, 因为要把 Connection 放到 eventloop 里面
};

#define MAX_EPOLLEVENTS 1024

class Poller
{
private:
    // 对 Epoll 的操作
    void Update(Channel* channel, int op)
    {
        // int epoll_ctl(int _epollfd, int op, int fd,struct epoll_event* op);
        int fd = channel->Fd();

        struct epoll_event ev;
        ev.data.fd = fd;
        //...//
        ev.events = channel->GetEvent();
        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if(ret < 0)
        {
            ERR_LOG("EPOLL_CTEATE FAILED");
            return;
        }    
    
    }
    // 判断是否已经添加事件监控
    bool HasChannel(Channel* channel)
    {
        // 找 hash 表
        auto it = _channels.find(channel->Fd());
        if(it == _channels.end()) 
        {
            return false;
        }

        return true;
    }

public:
    Poller()
    {
        //EPOLL 创建
        _epfd = epoll_create(MAX_EPOLLEVENTS);
    }

    void UpdateEvent(Channel* channel)
    {
        // 先判断是否存在  不存在添加 存在就更新
        if(HasChannel(channel))
        {
           
            Update(channel, EPOLL_CTL_MOD);
        }
        // 添加  EPOLL_CTL_ADD
        else
        {
            Update(channel, EPOLL_CTL_ADD);
            _channels.insert(std::make_pair(channel->Fd(), channel));
        }
        // EPOLL_CTL_MOD
    }
    void RemoveEvent(Channel* channel)
    {
        // 移除 EPOLL
        if(HasChannel(channel)) return;


        Update(channel, EPOLL_CTL_DEL);
        
        // 移除 hash
        auto it = _channels.find(channel->Fd());
        if(it != _channels.end())
        {
            _channels.erase(it);
        }
    }
    
    //开始监控  返回活跃链接
    void Poll(std::vector<Channel*> *active)
    {
        // int epoll_wait(int epfd, struct epoll_event* evs, int maxevents, int timeout);
        int nfds = epoll_wait(_epfd, _evs, MAX_EPOLLEVENTS, -1);
        // active  设置实际就绪事件
        if(nfds < 0)
        {
            if(errno == EINTR) return;
            ERR_LOG("EPOLL_WAIT FAILED");
        }
        for(int i = 0; i < nfds; i++)
        {
            auto it = _channels.find(_evs[i].data.fd);
            assert(it != _channels.end());

            it->second->SetREvents(_evs[i].events);
            active->push_back(it->second);
        }

    }


private:
    int _epfd;
    struct epoll_event _evs[MAX_EPOLLEVENTS];
    std::unordered_map<int, Channel*> _channels;
};





class EventLoop
{
using Functor = std::function<void()>;
//1. 事件监控 Poller 模块
//   事件就绪 进行事件处理
//   (有可能没有事件 阻塞在这里 任务队列了里面的不能执行了)
//2. 执行任务队列里面的任务
//    
//    因为有坑因为 等待 文件描述符 IO 事件就绪, 导致执行流流程阻塞, 这时候 任务队列里面的任务将得不到执行
//    因此要有一个事件通知的东西, 唤醒事件监控的阻塞

public:
    EventLoop()
    :_threadId(std::this_thread::get_id())
    ,_eventfd_channel(new Channel(this, _eventfd))
    ,_eventfd(CreateEventfd())
    {
        // 初始化
        // 创建出 eventfd  和 _channel
        
        // 给 eventfd 添加 可读事件回调函数， 读取 eventfd 事件通知次数
        _eventfd_channel->SetReadCallBack(std::bind(&EventLoop::ReadEventFd, this));
        // 启动 eventfd 的 读事件监控 
        _eventfd_channel->EnableRead();
    }
    void RunInLoop(const Functor &cb) // 判断 要执行的任务是否处于当前线程中, 选择
    {
        if(IsInLoop())
        {
            cb();
        }
        else
        {
            QueueInLoop(cb);
        }
    }
    
    
    void QueueInLoop(const Functor &cb)
    {
        // 加锁压入 注意 作用域
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.push_back(cb);
        }
        // 唤醒有可能因为没有事件就绪而导致的 epoll 阻塞
        // WeakUp(); 其实就是给 eventfd 写入一个数据  这样就不会再阻塞了 不管环境多少次, 一次就会拿完
    
        WeakUp();
    }

    bool IsInLoop()  //  用于是否是 eventloop 对应的线程 
    {
        if(std::this_thread::get_id() == _threadId)
        {
            return true;
        }
        return false;
    }
    void UpdateEvent(Channel* channel) { _poller.UpdateEvent(channel); } // 修改描述符事件监控
    void RemoveEvent(Channel* channel) { _poller.RemoveEvent(channel); }// 移除描述符的监控

    void RunAllTask()
    {
        std::vector<Functor> tmpTasks;
        // 任务全部取出来
        {
            std::unique_lock<std::mutex> _lock(_mutex);          
            tmpTasks.swap(_tasks);
        }
        // 遍历,然后逐个 执行
        for(auto& e: tmpTasks)
        {
            e();
        }

    }

    void Start()
    {
        // 三步走  事件监控 -> 就绪事件处理 // -> 执行任务   后续融合定时器
        std::vector<Channel*> active;
        _poller.Poll(&active);
        // 事件监控什么时候添加的? 自己 的channel 里面调用 ReadAble 等等的添加的
        // 这里直接拿
        for(auto& e: active)
        {
            e->HandleEvent();
        }

        RunAllTask();
    }  

    static int CreateEventfd()
    {
        int ret = eventfd(0, FD_CLOEXEC | FNONBLOCK);
        if(ret < 0)
        {
            ERR_LOG("CREATE EVENTFD FAILED");
            return -1;
        }

        return ret;
    }

    void ReadEventFd()
    {
        uint64_t rd = 0;
        int ret = read(_eventfd, &rd, sizeof rd);
        if(ret < 0)
        {
            if(errno == EINTR || errno == EWOULDBLOCK)
            {
                DBG_LOG("EINTR");
                return; 
            }
            ERR_LOG("READEVENTFD FAILED");
            abort();
        }
    }

    void WeakUp()
    {
        uint64_t wt = 1;
        int ret = write(_eventfd, &wt, sizeof wt);
        if(ret < 0)
        {
            if(errno == EINTR || errno == EWOULDBLOCK)
            {
                DBG_LOG("EINTR");
                return; 
            }
            ERR_LOG("READEVENTFD FAILED");
            abort();
        }
    }

private:
    Poller _poller;
    std::vector<Functor> _tasks;     // 如果是队列的话 一个个加锁, 我们这里用 vector , 再用一个置换, 置换过程进行加锁
    std::mutex _mutex;
    int _eventfd;   //唤醒 事件监控有可能导致的阻塞

//----

    // 如果执行的操作本就在 线程中, 就不需要压入队列了, 可以直接执行
    // 如果执行的操作不在 再加入线程池
    std::thread::id _threadId;
    std::unique_ptr<Channel> _eventfd_channel; // 管理 eventfd  , 注意析构函数里面 delete, 智能指针 unique
    // 还不完整, 还要 TimeQueue
};

void Channel::Update()
{
    _loop->UpdateEvent(this);
}
  
  
void Channel::Remove()
{
    _loop->RemoveEvent(this);
}