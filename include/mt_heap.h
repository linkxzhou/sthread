/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _MT_HEAP_H_INCLUDED_
#define _MT_HEAP_H_INCLUDED_

#include "mt_utils.h"

MTHREAD_NAMESPACE_BEGIN

class HeapEntry;
typedef HeapEntry* HeapEntryNode;

enum eOrderType
{
    eOrderDesc = 0,
    eOrderAsc
};

class HeapEntry : public Any, public referenceable
{
public:
    HeapEntry() : m_index_(0) { }

    virtual ~HeapEntry() { }

    virtual time64_t HeapValue() = 0;
    // 迭代处理
    virtual void HeapIterate()
    {
        return ;
    }

    inline int GetIndex()
    {
        // LOG_TRACE("m_index_ = %d", m_index_);
        return m_index_;
    }

    inline int HeapValueCmp(HeapEntry* rhs)
    {
        if (this->HeapValue() == rhs->HeapValue())
        {
            return 0;
        }
        else if (this->HeapValue() > rhs->HeapValue())
        {
            return 1;
        }
        else
        {
            return -1;
        }
    }

    inline void SetIndex(int index)
    {
        m_index_ = index;
    }

private:
    int  m_index_;
};

// 堆的list
template<class T = HeapEntryNode>
class HeapList
{
public:
    typedef T value_type;  
    typedef value_type* pointer;
    typedef pointer* pointer_pointer;

private:
    pointer_pointer m_list_;
    int             m_max_;
    int             m_count_;

public:
    explicit HeapList(int max = 100000)
    {
        m_max_ = mt_max(max, 100000);
        m_list_ = (pointer_pointer)calloc(m_max_ + 1, sizeof(pointer));
        memset(m_list_, 0, (m_max_ + 1) * sizeof(pointer));
        m_count_ = 0;
    }

    virtual ~HeapList()
    {
        // 清理元素的数据
        if (m_list_)
        {
            for (int i = 0; i < m_max_; i++)
            {
                if (m_list_[i] != NULL)
                {
                    safe_delete(m_list_[i]);
                }
            }
            safe_free(m_list_);
        }
        m_max_ = 0;
        m_count_ = 0;
    }

    int HeapResize(int size)
    {
        if (m_max_ >= size)
        {
            return 0;
        }

        pointer_pointer m_list_ = (pointer_pointer)realloc(m_list_, sizeof(pointer) * (size + 1));
        if (NULL == m_list_)
        {
            return -1;
        }

        m_max_ = size;
        return 0;
    }

    int HeapPush(pointer entry)
    {
        if (this->HeapFull())
        {
            return -1;
        }

        if (entry->GetIndex() != 0)
        {
            return -2;
        }

        m_list_[++m_count_] = entry; // 第一个元素不存储数据
        entry->SetIndex(m_count_);
        LOG_DEBUG("index : %d", m_count_);
        this->HeapDown();

        return 0;
    }
    pointer HeapPop()
    {
        if (this->HeapEmpty())
        {
            return NULL;
        }

        HeapEntry* top = (HeapEntry *)(m_list_[1]);
        this->Swap(1, m_count_);
        m_count_--;
        this->HeapDown();
        top->SetIndex(0);

        return (pointer)top;
    }
    int HeapDelete(pointer entry)
    {
        if (this->HeapEmpty())
        {
            return -1;
        }

        int pos = entry->GetIndex();
        if ((pos > m_count_) || (pos <= 0))
        {
            return -2;
        }

        pointer del = m_list_[pos];
        m_list_[pos] = m_list_[m_count_];
        m_list_[pos]->SetIndex(pos);
        m_list_[m_count_] = 0;
        m_count_--;
        this->HeapDown();
        del->SetIndex(0);

        return 0;
    }
    void HeapForeach()
    {
        // LOG_DEBUG("count : %d", m_count_);
        int per = 1;
        for (int i = 1; i <= m_count_; i++)
        {
            // LOG_DEBUG("%llu", m_list_[i]->HeapValue());
            m_list_[i]->HeapIterate();
        }
    }

    int HeapSize()
    {
        return m_count_;
    }

    pointer HeapTop()
    {
        return (m_count_ > 0) ? m_list_[1] : NULL;
    }

private:
    bool HeapFull()
    {
        return (m_count_ >= m_max_);
    }

    bool HeapEmpty()
    {
        return (m_count_ == 0);
    }

    void HeapUp()
    {
        for (int pos = m_count_; pos > 0; pos--)
        {
            this->ReBuildHeap(pos, m_count_, eOrderAsc);
        }
        for (int pos = m_count_; pos > 0; pos--)
        {  
            this->Swap(1, pos);
            this->ReBuildHeap(1, pos - 1, eOrderAsc);
        }  
        
        //LOG_TRACE("HeapForeach:");
        //this->HeapForeach();
    }
    void HeapDown()
    {
        for (int pos = m_count_; pos > 0; pos--)
        {
            this->ReBuildHeap(pos, m_count_, eOrderDesc);
        }
        for (int pos = m_count_; pos > 0; pos--)
        {  
            this->Swap(1, pos);
            this->ReBuildHeap(1, pos - 1, eOrderDesc);
        }  
        //LOG_TRACE("HeapForeach:");
        //this->HeapForeach();
    }
    void ReBuildHeap(int index, int len, eOrderType e)
    {
        int lchild = 2 * index;     //i的左孩子节点序号 
        int rchild = 2 * index + 1; //i的右孩子节点序号 
        int s_son = index;       //临时变量 

        if (s_son > len/2)
        {
            return ;
        }
        // 根据eOrderType调整
        if (lchild <= len)
        {
            if (m_list_[lchild]->HeapValueCmp(m_list_[s_son]) >= 0)
            {
                if (e == eOrderDesc) 
                {
                    s_son = lchild;
                }
            }
            else
            {
                if (e == eOrderAsc) 
                {
                    s_son = lchild;
                }
            }
        }
        if (rchild <= len)
        {
            if (m_list_[rchild]->HeapValueCmp(m_list_[s_son]) >= 0)
            {
                if (e == eOrderDesc) 
                {
                    s_son = rchild;
                }
            }
            else
            {
                if (e == eOrderAsc) 
                {
                    s_son = rchild;
                }
            }
        }

        if (s_son != index)
        {
            this->Swap(index, s_son);
            this->ReBuildHeap(s_son, len, e); //避免调整之后以max为父节点的子树不是堆 
        }
    }

    inline void Swap(int i, int j)
    {
        pointer tmp = m_list_[i];
        m_list_[i] = m_list_[j];
        m_list_[j] = tmp;

        m_list_[i]->SetIndex(i);
        m_list_[j]->SetIndex(j);
    }
};

MTHREAD_NAMESPACE_END

#endif
