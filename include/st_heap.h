/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_HEAP_H_INCLUDED_
#define _ST_HEAP_H_INCLUDED_

#include "st_util.h"

ST_NAMESPACE_BEGIN

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
    HeapEntry() : 
        m_index_(0) 
    { }

    virtual ~HeapEntry() 
    { }

    virtual int64_t HeapValue() = 0;

    // 迭代处理
    virtual void HeapIterate()
    {
        return ;
    }

    inline int32_t GetIndex()
    {
        return m_index_;
    }

    inline void SetIndex(int32_t index)
    {
        m_index_ = index;
    }

    inline int32_t HeapValueCmp(HeapEntry *rhs)
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

private:
    int32_t  m_index_;
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
    int32_t        m_max_;
    int32_t        m_count_;

public:
    explicit HeapList(int32_t max = 10240)
    {
        m_max_ = ST_MAX(max, 10240);
        m_list_ = (pointer_pointer)calloc(m_max_ + 1, sizeof(pointer));
        memset(m_list_, 0, (m_max_ + 1) * sizeof(pointer));
        m_count_ = 0;
    }

    virtual ~HeapList()
    {
        // 清理元素的数据
        if (m_list_)
        {
            for (int32_t i = 0; i < m_max_; i++)
            {
                if (m_list_[i] != NULL)
                {
                    st_safe_delete(m_list_[i]);
                }
            }

            st_safe_free(m_list_);
        }

        m_max_ = 0;
        m_count_ = 0;
    }

    inline int32_t HeapResize(int32_t size)
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

    inline int32_t HeapPush(pointer entry)
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
        this->HeapDown();

        return 0;
    }
    
    inline pointer HeapPop()
    {
        if (this->HeapEmpty())
        {
            return NULL;
        }

        HeapEntry *top = (HeapEntry *)(m_list_[1]);
        this->Swap(1, m_count_);
        m_count_--;
        this->HeapDown();
        top->SetIndex(0);

        return (pointer)top;
    }

    inline int32_t HeapDelete(pointer entry)
    {
        if (this->HeapEmpty())
        {
            return -1;
        }

        int32_t pos = entry->GetIndex();
        if ((pos > m_count_) || (pos <= 0))
        {
            return -2;
        }

        pointer ptr = m_list_[pos];
        m_list_[pos] = m_list_[m_count_];
        m_list_[pos]->SetIndex(pos);
        m_list_[m_count_] = 0;
        m_count_--;
        this->HeapDown();
        ptr->SetIndex(0);

        return 0;
    }

    void HeapForeach()
    {
        for (int32_t i = 1; i <= m_count_; i++)
        {
            m_list_[i]->HeapIterate();
        }
    }

    inline int32_t HeapSize()
    {
        return m_count_;
    }

    inline pointer HeapTop()
    {
        return (m_count_ > 0) ? m_list_[1] : NULL;
    }

    inline bool HeapFull()
    {
        return (m_count_ >= m_max_);
    }

    inline bool HeapEmpty()
    {
        return (m_count_ == 0);
    }

private:

    void HeapUp()
    {
        for (int32_t pos = m_count_; pos > 0; pos--)
        {
            this->ReBuildHeap(pos, m_count_, eOrderAsc);
        }

        for (int32_t pos = m_count_; pos > 0; pos--)
        {  
            this->Swap(1, pos);
            this->ReBuildHeap(1, pos - 1, eOrderAsc);
        }
    }

    void HeapDown()
    {
        for (int32_t pos = m_count_; pos > 0; pos--)
        {
            this->ReBuildHeap(pos, m_count_, eOrderDesc);
        }

        for (int32_t pos = m_count_; pos > 0; pos--)
        {  
            this->Swap(1, pos);
            this->ReBuildHeap(1, pos - 1, eOrderDesc);
        } 
    }

    void ReBuildHeap(int32_t index, int32_t len, eOrderType e = eOrderAsc)
    {
        int32_t lchild = 2 * index;     //i的左孩子节点序号 
        int32_t rchild = 2 * index + 1; //i的右孩子节点序号 
        int32_t s_son = index;          //临时变量 

        if (s_son > len / 2)
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

    inline void Swap(int32_t i, int32_t j)
    {
        pointer tmp = m_list_[i];
        m_list_[i] = m_list_[j];
        m_list_[j] = tmp;

        m_list_[i]->SetIndex(i);
        m_list_[j]->SetIndex(j);
    }
};

ST_NAMESPACE_END

#endif
