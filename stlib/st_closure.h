/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_CLOSURE_H_
#define _ST_CLOSURE_H_

#include "ucontext/st_def.h"

stlib_namespace_begin

class StClosure 
{
public:
    virtual ~StClosure() 
    { }

    virtual void Run() = 0;
};

// 无参数
template <class Funct>
class StClosure0 : public StClosure 
{
public:
    StClosure0(Funct fun) 
    {
        m_fun_ = fun;
    }

    virtual ~StClosure0() 
    { }

    virtual void Run() 
    {
        (*m_fun_)();
    }

private:
    Funct m_fun_;
}; 

template <class Funct, class Arg1>
class StClosure1 : public StClosure 
{
public:
    StClosure1(Funct fun, Arg1 arg1) 
    {
        m_fun_ = fun;
        m_arg1_ = arg1;
    }

    virtual ~StClosure1() 
    { }

    virtual void Run() 
    {
        (*m_fun_)(m_arg1_);
    }
    
private:
    Funct m_fun_;
    Arg1 m_arg1_;
}; 

template <class Funct, class Arg1, class Arg2>
class StClosure2 : public StClosure 
{
public:
    StClosure2(Funct fun, Arg1 arg1, Arg2 arg2) 
    {
        m_fun_ = fun;
        m_arg1_ = arg1;
        m_arg2_ = arg2;
    }

    virtual ~StClosure2() 
    { }

    virtual void Run() 
    {
        (*m_fun_)(m_arg1_, m_arg2_);
    }

private:
    Funct m_fun_;
    Arg1 m_arg1_;
    Arg2 m_arg2_;
}; 

template <class Funct, class Arg1, class Arg2, class Arg3>
class StClosure3: public StClosure 
{
public:
    StClosure3(Funct fun, Arg1 arg1, Arg2 arg2, Arg3 arg3) 
    {
        m_fun_ = fun;
        m_arg1_ = arg1;
        m_arg2_ = arg2;
        m_arg3_ = arg3;
    }

    virtual ~StClosure3() 
    { }

    virtual void Run() 
    {
        (*m_fun_)(m_arg1_, m_arg2_, m_arg3_);
    }

private:
    Funct m_fun_;
    Arg1 m_arg1_;
    Arg2 m_arg2_;
    Arg3 m_arg3_;
}; 

template <class Object, class Funct> 
class ObjectStClosure0 : public StClosure 
{
public:
    ObjectStClosure0(Object* p, Funct fun) 
    {
        m_p_ = p;
        m_fun_ = fun;
    }

    virtual ~ObjectStClosure0() 
    { }

    virtual void Run() 
    {
        (m_p_->*m_fun_)();
    }

private:
    Object *m_p_;
    Funct m_fun_;
}; 

template <class Object, class Funct, class Arg1> 
class ObjectStClosure1 : public StClosure 
{
public:
    ObjectStClosure1(Object* p, Funct fun, 
        Arg1 arg1) 
    {
        m_p_ = p;
        m_fun_ = fun;
        m_arg1_ = arg1;
    }

    virtual ~ObjectStClosure1() 
    { }

    virtual void Run() 
    {
        (m_p_->*m_fun_)(m_arg1_);
    }

private:
    Object *m_p_;
    Funct m_fun_;
    Arg1 m_arg1_;
}; 

template <class Object, class Funct, class Arg1, class Arg2> 
class ObjectStClosure2 : public StClosure 
{
public:
    ObjectStClosure2(Object* p, Funct fun, 
        Arg1 arg1, Arg2 arg2) 
    {
        m_p_ = p;
        m_fun_ = fun;
        m_arg1_ = arg1;
        m_arg2_ = arg2;
    }

    virtual ~ObjectStClosure2() 
    { }
    
    virtual void Run() 
    {
        (m_p_->*m_fun_)(m_arg1_, m_arg2_);
    }

private:
    Object *m_p_;
    Funct m_fun_;
    Arg1 m_arg1_;
    Arg2 m_arg2_;
};

template <class Object, class Funct, class Arg1, class Arg2, class Arg3> 
class ObjectStClosure3 : public StClosure 
{
public:
    ObjectStClosure3(Object* p, Funct fun, 
        Arg1 arg1, Arg2 arg2, Arg3 arg3) 
    {
        m_p_ = p;
        m_fun_ = fun;
        m_arg1_ = arg1;
        m_arg2_ = arg2;
        m_arg3_ = arg3;
    }

    virtual ~ObjectStClosure3() 
    { }
    
    virtual void Run() 
    {
        (m_p_->*m_fun_)(m_arg1_, m_arg2_, m_arg3_);
    }

private:
    Object *m_p_;
    Funct m_fun_;
    Arg1 m_arg1_;
    Arg2 m_arg2_;
    Arg3 m_arg3_;
};

template<class R>
StClosure* NewStClosure(R (*fun)()) 
{
    return new StClosure0<R (*)()>(fun);
}

template<class R, class Arg1>
StClosure* NewStClosure(R (*fun)(Arg1), Arg1 arg1) 
{
    return new StClosure1<R (*)(Arg1), Arg1>(fun, arg1);
}

template<class R, class Arg1, class Arg2>
StClosure* NewStClosure(R (*fun)(Arg1, Arg2), 
    Arg1 arg1, Arg2 arg2) 
{
    return new StClosure2<R (*)(Arg1, Arg2), 
        Arg1, Arg2>(fun, arg1, arg2);
}

template<class R, class Arg1, class Arg2, class Arg3>
StClosure* NewStClosure(R (*fun)(Arg1, Arg2, Arg3), 
    Arg1 arg1, Arg2 arg2, Arg3 arg3) 
{
    return new StClosure3<R (*)(Arg1, Arg2, Arg3), 
        Arg1, Arg2, Arg3>(fun, arg1, arg2, arg3);
}

template<class R, class Object>
StClosure* NewStClosure(Object *object, R (Object::* fun)()) 
{
    return new ObjectStClosure0<Object, R (Object::* )()>(object, fun);
}

template<class R, class Object, class Arg1>
StClosure* NewStClosure(Object *object, 
    R (Object::* fun)(Arg1), Arg1 arg1) 
{
    return new ObjectStClosure1<Object, 
        R (Object::* )(Arg1), Arg1>(object, fun, arg1);
}

template<class R, class Object, class Arg1, class Arg2>
StClosure* NewStClosure(Object *object, 
    R (Object::* fun)(Arg1, Arg2), 
    Arg1 arg1, Arg2 arg2) 
{
    return new ObjectStClosure2<Object, 
        R (Object::*)(Arg1, Arg2), 
        Arg1, Arg2>(object, fun, arg1, arg2);
}

template<class R, class Object, class Arg1, class Arg2, class Arg3>
StClosure* NewStClosure(Object *object, 
    R (Object::* fun)(Arg1, Arg2, Arg3), 
    Arg1 arg1, Arg2 arg2, Arg3 arg3) 
{
    return new ObjectStClosure3<Object, 
        R (Object::*)(Arg1, Arg2, Arg3), 
        Arg1, Arg2, Arg3>(object, fun, arg1, arg2, arg3);
}

stlib_namespace_end

#endif // _ST_CLOSURE_H_