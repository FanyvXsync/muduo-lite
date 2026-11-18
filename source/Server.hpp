#pragma once

#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>


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