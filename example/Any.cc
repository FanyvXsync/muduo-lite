#include <iostream>
#include <typeinfo>
#include <assert.h>


class Any
{
private:
    class Holder
    {
    public:
        virtual ~Holder() {}
        virtual const std::type_info& Type() = 0;// 获取数据类型
        // 克隆   通过 any 构造 any 使用
        virtual Holder* Clone() = 0;
        // 针对当前子类对象自身, 克隆出一个新的子类对象
    };

    template<class T>
    class PlaceHolder : public Holder
    {
    public:
        PlaceHolder(const T val): _val(val) 
        {}

        virtual const std::type_info& Type()
        {
            return typeid(T);
        }

        virtual Holder* Clone()
        {
            return new PlaceHolder(_val);     
        }
    
        T _val;
    };
    
    Holder* _content;

private:
    Any& Swap(Any &other)
    {
        std::swap(_content, other._content);
        return *this;  // 主要是为了能够进行连续交换
    }

public:
    // 构造  空构造
    Any(): _content(nullptr) {}

    // 构造  模版函数  给数据来构造通用容器
    template<class T>
    Any(const T& val): _content(new PlaceHolder<T>(val)) {}

    // 构造  根据any 构造 any  (不能把人家保存的指针拷贝出来, 我们拷贝一下数据出来)
    Any(const Any& other): _content(other._content ? other._content->Clone() : nullptr) {}

    // 析构  销毁  都是 new 出来的 delete 掉就可以了
    ~Any()
    {
        if(_content) delete _content;
    }

    // 获取子类对象中保存的数据, 我们也不知道他返回的类型是什么, 模板函数
    // 返回子类对象 保存数据的指针
    // (想要获取的数据类型 必须和保存的数据类型一致)
    template<class T>
    T* Get()
    {
        assert(typeid(T) == _content->Type());
        return &(dynamic_cast<PlaceHolder<T>*>(_content))->_val;
    }


    // 想给保存的元素 或者 any 赋值  重载 =, 两个
    // swap  交换两个 any 对象, 交换一下 指针即可
    // 临时对象 释放的时候 原先保存的数据也就被释放掉了
    template<class T>
    Any& operator=(const T& val)
    {
        Any(val).Swap(*this);
        return *this;
    }

    // 临时对象 释放 原先 我们保存的值也就被释放掉了
    Any& operator=(const Any& other)
    {
        Any(other).Swap(*this);
        return *this;
    }
    
};

// 测试
class Test
{
public:
    Test() { std:: cout << "构造" << std::endl; }
    Test(const Test& t) { std:: cout << "拷贝" << std::endl; }
    ~Test() { std:: cout << "析构" << std::endl;}
};


// 测测内存泄露  delete 是否是完善的

int main()
{
   Any a;
   
   {
        Test t;
        a = t;
   }

   return 0;
}
