/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_CLOSURE_H_INCLUDED_
#define _ST_CLOSURE_H_INCLUDED_

class Closure 
{
public:
    virtual ~Closure() { }

    virtual void Run() = 0;
};

// 无参数
template <class Funct>
class Closure0: public Closure 
{
public:
    Closure0(Funct fun) 
    {
        m_fun_ = fun;
    }

    virtual ~Closure0() { }

    virtual void Run() 
    {
        (*m_fun_)();
    }

private:
    Funct m_fun_;
}; 

template <class Funct, class Arg1>
class Closure1: public Closure 
{
public:
    Closure1(Funct fun, Arg1 arg1) 
    {
        m_fun_ = fun;
        m_arg1_ = arg1;
    }

    virtual ~Closure1() { }

    virtual void Run() 
    {
        (*m_fun_)(m_arg1_);
    }
    
private:
    Funct m_fun_;
    Arg1 m_arg1_;
}; 

template <class Funct, class Arg1, class Arg2>
class Closure2: public Closure 
{
public:
    Closure2(Funct fun, Arg1 arg1, Arg2 arg2) 
    {
        m_fun_ = fun;
        m_arg1_ = arg1;
        m_arg2_ = arg2;
    }

    virtual ~Closure2() { }

    virtual void Run() 
    {
        (*m_fun_)(m_arg1_, m_arg2_);
    }

private:
    Funct m_fun_;
    Arg1 m_arg1_;
    Arg2 m_arg2_;
}; 

template <class Object, class Funct> 
class ObjectClosure0: public Closure 
{
public:
    ObjectClosure0(Object* p, Funct fun) 
    {
        m_p_ = p;
        m_fun_ = fun;
    }

    virtual ~ObjectClosure0() { }

    virtual void Run() 
    {
        (m_p_->*m_fun_)();
    }

private:
    Object* m_p_;
    Funct m_fun_;
}; 

template <class Object, class Funct, class Arg1> 
class ObjectClosure1: public Closure 
{
public:
    ObjectClosure1(Object* p, Funct fun, Arg1 arg1) 
    {
        m_p_ = p;
        m_fun_ = fun;
        m_arg1_ = arg1;
    }

    virtual ~ObjectClosure1() { }

    virtual void Run() 
    {
        (m_p_->*m_fun_)(m_arg1_);
    }
private:
    Object* m_p_;
    Funct m_fun_;
    Arg1 m_arg1_;
}; 

template <class Object, class Funct, class Arg1, class Arg2> 
class ObjectClosure2: public Closure 
{
public:
    ObjectClosure2(Object* p, Funct fun, Arg1 arg1, Arg2 arg2) 
    {
        m_p_ = p;
        m_fun_ = fun;
        m_arg1_ = arg1;
        m_arg2_ = arg2;
    }

    virtual ~ObjectClosure2() { }
    
    virtual void Run() 
    {
        (m_p_->*m_fun_)(m_arg1_, m_arg2_);
    }
private:
    Object* m_p_;
    Funct m_fun_;
    Arg1 m_arg1_;
    Arg2 m_arg2_;
};

template<class R>
Closure* NewClosure(R (*fun)()) 
{
    return new Closure0<R (*)()>(fun);
}

template<class R, class Arg1>
Closure* NewClosure(R (*fun)(Arg1), Arg1 arg1) 
{
    return new Closure1<R (*)(Arg1), Arg1>(fun, arg1);
}

template<class R, class Arg1, class Arg2>
Closure* NewClosure(R (*fun)(Arg1, Arg2), Arg1 arg1, Arg2 arg2) 
{
    return new Closure2<R (*)(Arg1, Arg2), Arg1, Arg2>(fun, arg1, arg2);
}

template<class R, class Object>
Closure* NewClosure(Object* object, R (Object::* fun)()) 
{
    return new ObjectClosure0<Object, R (Object::* )()>(object, fun);
}

template<class R, class Object, class Arg1>
Closure* NewClosure(Object* object, R (Object::* fun)(Arg1), Arg1 arg1) 
{
    return new ObjectClosure1<Object, R (Object::* )(Arg1), Arg1>(object, fun, arg1);
}

template<class R, class Object, class Arg1, class Arg2>
Closure* NewClosure(Object* object, R (Object::* fun)(Arg1, Arg2), Arg1 arg1, Arg2 arg2) 
{
    return new ObjectClosure2<Object, R (Object::*)(Arg1, Arg2), Arg1, Arg2>(object, fun, arg1, arg2);
}

#endif // _ST_CLOSURE_H_INCLUDED_