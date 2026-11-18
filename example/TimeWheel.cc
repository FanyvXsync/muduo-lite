#include <iostream>
#include <memory>
#include <cstdint>
#include <unistd.h>
#include <functional>
#include <vector>
#include <unordered_map>

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;

class TimerTask  //定时器任务
{

public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc& cb)  // 这个 id 保持唯一性 不能自己生成, 多线程生成重复的 需要外界给
    :_id(id), _timeout(delay), _task_cb(cb), _iscannel(false)
    {
    }
    
    void SetRelease(const ReleaseFunc &rf)  // 释放要怎执行 他也不知道传一个进来
    {
        _release = rf;
    }

    void Cannel()
    {
        _iscannel = true;
    }
    
    ~TimerTask()
    {
        if(!_iscannel) _task_cb();  
        _release(); 
    }

    uint32_t DelayTime()
    {
        return _timeout;
    }

private:
    uint64_t _id;//首先要能找的着   任务可能很多
    uint32_t _timeout;//应该什么时候被执行  刷新的时候延迟固定的事件就可以了
    bool _iscannel;

    TaskFunc _task_cb;
    ReleaseFunc _release;
};

class TimerWheel
{
    using WeakTask = std::weak_ptr<TimerTask>;    // 二次添加同一个定时器对象 找到他们共同的计数器 ?
    using PtrTask = std::shared_ptr<TimerTask>;   // 时间轮里面的对象是智能指针
public:
    
    // 构造
    TimerWheel() :_capacity(60), _tick(0), _wheel(_capacity) 
    {}

    // 添加定时任务  添加进来的是一个可调用对象, 但是我们要进行封装成一个 timertask 对象
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));// 新建智能指针对象
        //std::cout << "----\n"; 
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));// 设置 Release , 用到 bind  this 指针 和 id
        //std::cout << "----\n"; 
        // 设置 hash 表, 注意 这里面不能放 shared_ptr, 如果放了, 引用计数 永远不会为 0 了
        _timer[id] = WeakTask(pt);
        //std::cout << "----\n"; 
        // 这里也要 添加 _wheel  tick + delay 的位置
        _wheel[(_tick + delay) % _capacity].push_back(pt);
    }

    // 定时任务刷新
    void TimerRefresh(uint64_t id)
    {
        auto it = _timer.find(id); // 先找找这个定时任务有没有 找着了再刷新
        if(it == _timer.end()) 
        {
            std::cerr << "fresh not find" << std::endl;
            return;
        }
        // 通过保存的定时器对象的 weakptr  , 构造出一个 shared_ptr  添加到 轮子里面
        // 如何? 成员函数  lock  , 获取 wptr 的 sptr
        PtrTask pt = it->second.lock();
        int delay = pt->DelayTime();
        _wheel[(_tick + delay) % _capacity].push_back(pt);
        // 重新添加到轮子里面 放到哪里呢? 超时时间 接口获取
        // 添加到 tick + delay 的位置  但是注意是环形的
    }
    // 秒针总要滴答滴答走吧 执行任务了
    void RunTimerTask() // 应该每秒执行一次 相当于 秒针走了一次
    {
        // tick 环形走 走到哪里 释放哪里
        _tick = (_tick + 1) % _capacity;
        // 清空数组 就会把所有管理定时器对象 的 shared_ptr 释放掉
        _wheel[_tick].clear();
    }

    // 取消定时任务
    // 不能从 时间轮里面考虑 因为管理对象也要被销毁 相当于是提前执行任务了
    // 从任务本身出发  iscannel, 析构里面就不执行了
    void TimerCannel(uint64_t id)
    {
        // 先找到
        auto it = _timer.find(id); 
        if(it == _timer.end()) 
        {
            std::cerr << "cannel not find" << std::endl;
            return;
        }
        // shared_ptr 管理的对象存在 就可以取消了
        PtrTask pt = it->second.lock();
        pt->Cannel();
    }

private:
    void RemoveTimer(uint64_t id) // 删除   而且不能直接传进去  这时候就用到 bind
    {
        auto it = _timer.find(id); 
        if(it == _timer.end())
        {
            std::cerr << "remove not find" << std::endl;
            return;
        }

        _timer.erase(it);
    }


private:
    
    int _tick; //什么时候执行 tick  -- 秒针  走到哪里释放哪里 就相当于执行哪里的任务
    int _capacity; //表盘最大数量  --  其实就是最大延迟时间
    std::unordered_map<uint64_t, WeakTask> _timer;//哈希表  定时器 id , 和 weak, 注意这个 weak 也需要在hash 里面去掉
    std::vector<std::vector<PtrTask>> _wheel;  //二维数组
    // ************析构顺序    构造顺序**********
};

//测试部分
class Test
{
public:
    Test ()
    {
        std::cout << "构造" << std::endl;
    }

    ~Test()
    {
        std::cout << "析构" << std::endl;
    } 
};

void DleTest(Test *t)
{
    delete t;
}

int main()
{
    TimerWheel tw;

    Test* t = new Test();
    tw.TimerAdd(888, 5, std::bind(DleTest, t));

    std::cout << "----\n"; 

    for(int i = 0; i < 5; i++)
    {
        sleep(1);

        tw.TimerRefresh(888);
        tw.RunTimerTask();
    
        std::cout << "刷新了一下定时任务 5 秒后执行" << std::endl;
    }

    tw.TimerCannel(888);

    while(1)
    {
        sleep(1);
        std::cout << "------------------" << std::endl;
        tw.RunTimerTask();
    }

    return 0;
}