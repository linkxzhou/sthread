#include <stdio.h>
#include <string.h>
#include <string>

class Node
{
public:
    Node() : m_key_(0), m_value_(0), m_count_(0)
    { }
    
public:
    int m_key_;
    int m_value_;
    int m_count_;
};

class LFUCache
{
public:
    LFUCache(int capacity) : m_capacity_(capacity), m_used_(0)
    {
        m_node_arr_ = new Node[m_capacity_];
    }
    
    int get(int key) 
    {
        for (int i = m_used_ - 1; i >= 0; i--)
        {
            if (m_node_arr_[i].m_key_ == key)
            {
                int v = m_node_arr_[i].m_value_;
                m_node_arr_[i].m_count_++;
                upLastNode(i);
                return v;
            }
        }
        
        return -1;
    }
    
    void put(int key, int value) 
    {
        // 如果有相同的则直接返回
        for (int i = 0; i < m_used_; i++)
        {
            if (m_node_arr_[i].m_key_ == key)
            {
            	m_node_arr_[i].m_value_ = value;
            	m_node_arr_[i].m_count_++;
                upLastNode(i);
                return ;
            }
        }
        
        int idx = 0;
        if (m_used_ >= m_capacity_)
        {
            // 找到count最小值，然后移除掉
            int i = 1, count = m_node_arr_[0].m_count_;
            idx = 0;
            for (; i < m_used_; i++)
            {
                if (count > m_node_arr_[i].m_count_)
                {
                    count = m_node_arr_[i].m_count_;
                    idx = i;
                }
            }
            m_used_--;
        }
        else
        {
            for (int i = 0; i < m_capacity_; i++)
            {
                if (m_node_arr_[i].m_value_ <= 0)
                {
                    idx = i;
                    break;
                }
            }
        }
        
        m_node_arr_[idx].m_key_ = key;
        m_node_arr_[idx].m_value_ = value;
        m_node_arr_[idx].m_count_ = 1;
        m_used_++;
        upLastNode(idx);
        
        return ;
    }

    void print()
    {
    	for (int i = 0; i < m_used_; i++)
    	{
    		fprintf(stdout, "(%d,%d,%d) ", m_node_arr_[i].m_key_, m_node_arr_[i].m_value_, m_node_arr_[i].m_count_);
    	}
        fprintf(stdout, "\n");
    }

    // 将最近访问的往上移动
    void upLastNode(int idx)
    {        
        Node up = m_node_arr_[idx];
        int i = idx;
        for (; i < m_used_ - 1; i++)
        {
            m_node_arr_[i] = m_node_arr_[i + 1];
        }
        
        m_node_arr_[i] = up;
    }
    
private:
    int m_capacity_, m_used_;
    Node *m_node_arr_;
};

class LRUCache 
{
public:
    LRUCache(int capacity) : m_capacity_(capacity), m_used_(0)
    {
        m_node_arr_ = new Node[m_capacity_];
    }
    
    int get(int key) 
    {
        for (int i = m_used_ - 1; i >= 0; i--)
        {
            if (m_node_arr_[i].m_key_ == key)
            {
                int v = m_node_arr_[i].m_value_;
                upLastNode(i);
                return v;
            }
        }
        
        return -1;
    }
    
    void put(int key, int value) 
    {
        // 如果有相同的则直接返回
        for (int i = 0; i < m_used_; i++)
        {
            if (m_node_arr_[i].m_key_ == key)
            {
            	m_node_arr_[i].m_value_ = value;
            	upLastNode(i);
                return ;
            }
        }
        
        int idx = 0;
        if (m_used_ >= m_capacity_)
        {
            // 移除第一个
            int i = 0;
            for (; i < m_used_ - 1; i++)
            {
                m_node_arr_[i] = m_node_arr_[i+1];
            }
            m_used_--;
            idx = i;
        }
        else
        {
            for (int i = 0; i < m_capacity_; i++)
            {
                if (m_node_arr_[i].m_value_ <= 0)
                {
                    idx = i;
                    break;
                }
            }
        }
        
        m_node_arr_[idx].m_key_ = key;
        m_node_arr_[idx].m_value_ = value;
        m_used_++;
        
        return ;
    }
    
    // 将最近访问的往上移动
    void upLastNode(int idx)
    {        
        Node up = m_node_arr_[idx];
        int i = idx;
        for (; i < m_used_ - 1; i++)
        {
            m_node_arr_[i] = m_node_arr_[i + 1];
        }
        
        m_node_arr_[i] = up;
    }

    void print()
    {
    	for (int i = 0; i < m_used_; i++)
    	{
    		fprintf(stdout, "(%d,%d,%d) ", m_node_arr_[i].m_key_, m_node_arr_[i].m_value_, m_node_arr_[i].m_count_);
    	}
        fprintf(stdout, "\n");
    }
    
private:
    int m_capacity_, m_used_;
    Node *m_node_arr_;
};

int main()
{
    // LRUCache *c = new LRUCache(3);
    // c->put(1, 1);
    // c->print();
    // c->put(2, 2);
    // c->print();
    // c->put(3, 3);
    // c->print();
    // c->put(4, 4);
    // c->print();
    // c->get(4);
    // c->print();
    // c->get(3);
    // c->print();
    // c->get(2);
    // c->print();
    // c->get(1);
    // c->print();
    // c->put(5, 5);
    // c->print();

    // LFUCache *c = new LFUCache(2);
    // c->put(1, 1);
    // c->print();
    // c->put(2, 2);
    // c->print();
    // c->get(1);
    // c->print();
    // c->put(3, 3);
    // c->print();
    // c->get(2);
    // c->print();
    // c->get(3);
    // c->print();
    // c->put(4, 4);
    // c->print();
    // c->get(1);
    // c->print();
    // c->get(3);
    // c->print();
    // c->get(4);
    // c->print();

    LFUCache *c = new LFUCache(2);
    c->put(2, 1);
    c->print();
    c->put(3, 2);
    c->print();
    c->get(3);
    c->print();
    c->get(2);
    c->print();
    c->put(4, 3);
    c->print();
    c->get(2);
    c->print();
    c->get(3);
    c->print();
    c->get(4);
    c->print();

    return 0;
}