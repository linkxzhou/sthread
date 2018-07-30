#include <string.h>
#include <string>
#include <sstream>
#include <iostream>
#include "memcache.h"

static unsigned int check(void* buf, int len)
{
    // LOG_TRACE("buf : %s, len : %d", buf, len);
    return len;
}

class MemcacheIMessage: public IMessage
{
public:
    MemcacheIMessage() : m_request_(NULL), m_request_len_(0),
        m_response_(NULL), m_response_len_(0)
    {
        m_req_msg_ = (struct msg*)malloc(sizeof(struct msg));
        m_rsp_msg_ = (struct msg*)malloc(sizeof(struct msg));
    }

    ~MemcacheIMessage()
    {
        safe_free(m_req_msg_);
        safe_free(m_rsp_msg_);
        safe_free(m_request_);
        m_request_len_ = 0;
    }

    eParseResult SetMemcacheRequest(const char *request)
    {
        m_request_ = strdup(request);
        m_request_len_ = strlen(m_request_);

        m_req_msg_->token = (uint8_t *)m_request_;
        m_req_msg_->pos = (uint8_t *)m_request_;
        m_req_msg_->last = (uint8_t *)(m_request_ + m_request_len_);
        m_req_msg_->end = (uint8_t *)(m_request_ + m_request_len_);
        m_req_msg_->state = SW_START;
        m_req_msg_->keys = array_create(1, sizeof(struct keypos));

        do 
        {
            memcache_parse_req(m_req_msg_);
        } while (m_req_msg_->result == MSG_PARSE_AGAIN);

        LOG_DEBUG("keys : %d", array_n(m_req_msg_->keys));

        return m_req_msg_->result;
    }

    inline const char* GetMemcacheRequest() const
    {
        return m_request_;
    }

    inline int GetMemcacheRequestLen()
    {
        return m_request_len_;
    }

    eParseResult SetMemcacheResponse(const char *response, int len)
    {
        m_response_ = (char *)malloc(len);
        m_response_len_ = len;
        memcpy(m_response_, response, m_response_len_);

        m_rsp_msg_->token = (uint8_t *)m_response_;
        m_rsp_msg_->pos = (uint8_t *)m_response_;
        m_rsp_msg_->last = (uint8_t *)(m_response_ + m_response_len_);
        m_rsp_msg_->end = (uint8_t *)(m_response_ + m_response_len_);
        m_rsp_msg_->state = SW_START;
        m_rsp_msg_->keys = array_create(1, sizeof(struct keypos));

        do 
        {
            memcache_parse_rsp(m_rsp_msg_);
        } while (m_rsp_msg_->result == MSG_PARSE_AGAIN);

        LOG_DEBUG("keys : %d", array_n(m_rsp_msg_->keys));

        return m_rsp_msg_->result;
    }

    inline const char* GetMemcacheResponse() const
    {
        return m_response_;
    }

    inline int GetMemcacheResponseLen()
    {
        return m_response_len_;
    }

private:
    struct msg *m_req_msg_, *m_rsp_msg_;
    char *m_request_, *m_response_;
    int m_request_len_, m_response_len_;
};

class MemcacheIMtAction: public IMtAction
{
public:
    virtual int HandleEncode(void* buf, int& len, IMessage* msg)
    {
        if (msg == NULL)
        {
            return -1;
        }
        if (((MemcacheIMessage *)msg)->GetMemcacheRequestLen() <= 0)
        {
            return -2;
        }
        len = ((MemcacheIMessage *)msg)->GetMemcacheRequestLen();
        memcpy(buf, ((MemcacheIMessage *)msg)->GetMemcacheRequest(), len);
        ((char *)buf)[len] = '\0';        
        
        return 0;
    }
    virtual int HandleInput(void* buf, int len, IMessage* msg)
    {
        LOG_DEBUG("send : %s", ((MemcacheIMessage *)msg)->GetMemcacheRequest());
        LOG_DEBUG("buf : %s, len : %d", (char*)buf, len);

        // 拷贝数据
        eParseResult s = ((MemcacheIMessage *)msg)->SetMemcacheResponse((const char *)buf, len);
        if (s != MSG_PARSE_OK)
        {
            LOG_ERROR("eParseResult s : %d", s);
            return 0;
        }
        LOG_DEBUG("eParseResult s : %d", s);

        return len;
    }
    virtual int HandleProcess(void* buf, int len, IMessage* msg)
    {
        LOG_DEBUG("buf : %s, len : %d", ((MemcacheIMessage *)msg)->GetMemcacheResponse(), 
            ((MemcacheIMessage *)msg)->GetMemcacheResponseLen());

        return 0;
    }
    virtual int HandleError(int err, IMessage* msg)
    {
        return 0;
    }
};

static void* thread_func(void *)
{
    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(11211);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

    int ret = mt_init_frame();
    LOG_TRACE("init ret : %d, servaddr : %p", ret, &servaddr);
    mt_set_hook_flag();

    IMtActionClient *actionclient = GetInstance<IMtActionClient>();

    MemcacheIMessage *msg1 = new MemcacheIMessage();
    eParseResult s = msg1->SetMemcacheRequest("get key1\r\nget key2\r\nset k1 0 900 9\r\nmemcached\r\n");
    if (s != MSG_PARSE_OK)
    {
        return NULL;
    }
    IMtAction* action1 = new MemcacheIMtAction();
    action1->SetIMessagePtr(msg1);
    action1->SetMsgDstAddr(&servaddr);
    action1->SetConnType(eTCP_SHORT_CLIENT_CONN);
    actionclient->Add(action1);

    MemcacheIMessage *msg2 = new MemcacheIMessage();
    s = msg2->SetMemcacheRequest("get k1\r\n");
    if (s != MSG_PARSE_OK)
    {
        return NULL;
    }
    IMtAction* action2 = new MemcacheIMtAction();
    action2->SetIMessagePtr(msg2);
    action2->SetMsgDstAddr(&servaddr);
    action2->SetConnType(eTCP_SHORT_CLIENT_CONN);
    actionclient->Add(action2);
    
    LOG_TRACE("wait thread : %d", GetInstance<Frame>()->m_wait_num_);

    int count = 1;
    while (count-- > 0)
    {
        ret = actionclient->SendRecv(1000); // 设置超时时间1000ms
        LOG_TRACE("ret : %d", ret);
    }

    LOG_TRACE("thread id : %d, frame id : %p", pthread_self(), GetInstance<Frame>());
    return NULL;
}

int main(int argc, char* argv[])
{
    struct msg *msg_;
    msg_ = (struct msg*)malloc(sizeof(struct msg));
    memset(msg_, 0, sizeof(struct msg));

    // const char *str = "get key1\r\nget key2\r\nset k1 0 900 9\nmemcached\r\n";
    // msg_->token = (uint8_t *)str;
    // msg_->pos = (uint8_t *)str;
    // msg_->last = (uint8_t *)(str + strlen(str));
    // msg_->end = (uint8_t *)(str + strlen(str));
    // msg_->state = SW_START;
    // msg_->keys = array_create(1024, 1);
    // do 
    // {
    //     memcache_parse_req(msg_);
    // } while (msg_->result == MSG_PARSE_AGAIN);

    // const char *str1 = "set k1 0 900 9\nmemcached\r\n";
    // msg_->token = (uint8_t *)str1;
    // msg_->pos = (uint8_t *)str1;
    // msg_->last = (uint8_t *)(str1 + strlen(str1));
    // msg_->end = (uint8_t *)(str1 + strlen(str1));
    // msg_->state = SW_START;
    // msg_->keys = array_create(1024, 1);
    // do 
    // {
    //     memcache_parse_rsp(msg_);
    // } while (msg_->result == MSG_PARSE_AGAIN);

    // const char *str2 = "VALUE k1 0 9\r\nmemcached\r\nEND\r\n";
    // msg_->token = (uint8_t *)str2;
    // msg_->pos = (uint8_t *)str2;
    // msg_->last = (uint8_t *)(str2 + strlen(str2));
    // msg_->end = (uint8_t *)(str2 + strlen(str2));
    // msg_->state = SW_START;
    // msg_->keys = array_create(1024, 1);
    // do 
    // {
    //     memcache_parse_rsp(msg_);
    // } while (msg_->result == MSG_PARSE_AGAIN);

    thread_func(NULL);
    
    return 0;
}