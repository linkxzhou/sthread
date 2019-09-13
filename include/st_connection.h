/*
 * Copyright (C) zhoulv2000@163.com
 */

#ifndef _ST_CONNECTION_H_INCLUDED_
#define _ST_CONNECTION_H_INCLUDED_

#include "st_util.h"
#include "st_item.h"
#include "st_buffer.h"
#include "st_heap_timer.h"

ST_NAMESPACE_BEGIN

class StServerConnection : public StConnectionItem
{ 
public:
    StServerConnection() :
        StConnectionItem()
    { }
    
    virtual int Process(void *manager) = 0;
}; 

class StClientConnection : public StConnectionItem, public TimerEntry
{
public:
    StClientConnection() : 
        StConnectionItem()
    { }

    virtual int32_t CreateSocket(const StNetAddress &addr);

    virtual int32_t OpenConnect(const StNetAddress &addr);
};

template<class ConnT>
class StConnectionManager
{
public:
    typedef ConnT* ConnTPtr;

    ConnTPtr AllocPtr(eConnType type, 
        const StNetAddress *destaddr = NULL, 
        const StNetAddress *srcaddr = NULL)
    {
        NetAddressKey key;
        if ((type & 0x1) == 1 && destaddr != NULL)
        {
            key.SetDestAddr(*destaddr);
        }

        if ((type & 0x1) == 1 && srcaddr != NULL)
        {
            key.SetSrcAddr(*srcaddr);
        }

        ConnTPtr conn = NULL;
        // 是否保持状态
        if ((type & 0x1) == 1)
        {
            conn = (ConnTPtr)(m_hashlist_.HashFindData(&key));
        }

        if (conn == NULL)
        {
            conn = GetInstance< UtilPtrPool<ConnT> >()->AllocPtr();
            conn->SetConnType(type);
            if (destaddr != NULL)
            {
                conn->SetDestAddr(*destaddr);
            }

            if (srcaddr != NULL)
            {
                conn->SetAddr(*srcaddr);
            }

            if ((type & 0x1) == 1)
            {
                key.SetDataPtr((void*)conn);
                int32_t r = m_hashlist_.HashInsert(&key);
                ASSERT(r >= 0);
            }
        }

        ASSERT(conn != NULL);
        return conn;
    }

    void FreePtr(ConnTPtr conn)
    {
        eConnType type = conn->GetConnType();
        if ((type & 0x1) == 1)
        {
            NetAddressKey key;
            key.SetDestAddr(conn->GetDestAddr());
            key.SetSrcAddr(conn->GetAddr());
            m_hashlist_.HashRemove(&key);
        }

        UtilPtrPoolFree(conn);
    }

private:
    HashList<NetAddressKey>   m_hashlist_;
};

ST_NAMESPACE_END

#endif