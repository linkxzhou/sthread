#include "memcache.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

using namespace stlib;

/* 一次 memcache 文本请求/响应。解析逻辑仍走 memcache_parse_req/rsp。 */
class MemcacheMsg {
public:
  MemcacheMsg()
      : m_req_(NULL), m_rsp_(NULL), m_request_(NULL), m_response_(NULL),
        m_request_len_(0), m_response_len_(0) {
    m_req_ = (struct msg *)malloc(sizeof(struct msg));
    m_rsp_ = (struct msg *)malloc(sizeof(struct msg));
    if (m_req_ != NULL) {
      memset(m_req_, 0, sizeof(*m_req_));
    }
    if (m_rsp_ != NULL) {
      memset(m_rsp_, 0, sizeof(*m_rsp_));
    }
  }

  ~MemcacheMsg() {
    if (m_req_ != NULL && m_req_->keys != NULL) {
      array_destroy(m_req_->keys);
    }
    if (m_rsp_ != NULL && m_rsp_->keys != NULL) {
      array_destroy(m_rsp_->keys);
    }
    st_safe_free(m_req_);
    st_safe_free(m_rsp_);
    st_safe_free(m_request_);
    st_safe_free(m_response_);
  }

  eParseResult SetRequest(const char *request) {
    if (m_req_ == NULL || request == NULL) {
      return MSG_PARSE_ERROR;
    }
    st_safe_free(m_request_);
    m_request_ = strdup(request);
    if (m_request_ == NULL) {
      return MSG_PARSE_ERROR;
    }
    m_request_len_ = (int)strlen(m_request_);
    m_req_->token = (uint8_t *)m_request_;
    m_req_->pos = (uint8_t *)m_request_;
    m_req_->last = (uint8_t *)(m_request_ + m_request_len_);
    m_req_->end = (uint8_t *)(m_request_ + m_request_len_);
    m_req_->state = SW_START;
    if (m_req_->keys == NULL) {
      m_req_->keys = array_create(1, sizeof(struct keypos));
    } else {
      m_req_->keys->nelem = 0;
    }
    if (m_req_->keys == NULL) {
      return MSG_PARSE_ERROR;
    }
    do {
      memcache_parse_req(m_req_);
    } while (m_req_->result == MSG_PARSE_AGAIN);
    LOG_DEBUG("keys : %d", array_n(m_req_->keys));
    return m_req_->result;
  }

  const char *Request() const { return m_request_; }
  int RequestLen() const { return m_request_len_; }

  /* 用当前已收到的缓冲区重解析。OK 表示至少一条响应完整。 */
  eParseResult FeedResponse(const char *response, int len) {
    if (m_rsp_ == NULL || response == NULL || len < 0) {
      return MSG_PARSE_ERROR;
    }
    st_safe_free(m_response_);
    m_response_ = (char *)malloc((size_t)len + 1);
    if (m_response_ == NULL) {
      return MSG_PARSE_ERROR;
    }
    memcpy(m_response_, response, (size_t)len);
    m_response_[len] = '\0';
    m_response_len_ = len;
    m_rsp_->token = (uint8_t *)m_response_;
    m_rsp_->pos = (uint8_t *)m_response_;
    m_rsp_->last = (uint8_t *)(m_response_ + m_response_len_);
    m_rsp_->end = (uint8_t *)(m_response_ + m_response_len_);
    m_rsp_->state = SW_START;
    if (m_rsp_->keys == NULL) {
      m_rsp_->keys = array_create(1, sizeof(struct keypos));
    } else {
      m_rsp_->keys->nelem = 0;
    }
    if (m_rsp_->keys == NULL) {
      return MSG_PARSE_ERROR;
    }
    do {
      memcache_parse_rsp(m_rsp_);
    } while (m_rsp_->result == MSG_PARSE_AGAIN);
    LOG_DEBUG("keys : %d", array_n(m_rsp_->keys));
    return m_rsp_->result;
  }

  const char *Response() const { return m_response_; }
  int ResponseLen() const { return m_response_len_; }

private:
  struct msg *m_req_;
  struct msg *m_rsp_;
  char *m_request_;
  char *m_response_;
  int m_request_len_;
  int m_response_len_;
};

static int remaining_ms(uint64_t start_ms, int timeout_ms) {
  int used = (int)(Util::TimeMs() - start_ms);
  if (used >= timeout_ms) {
    return 0;
  }
  return timeout_ms - used;
}

static void release_client(StExecClientConnection *conn) {
  if (conn != NULL) {
    Instance<StConnectionManager<StExecClientConnection> >()->FreePtr(conn);
  }
}

/* 短连接：把已解析的请求发出去，读到解析器认为响应完整。 */
static int memcache_exchange(MemcacheMsg *msg, struct sockaddr_in *dst,
                             int timeout_ms) {
  StNetAddr addr(*dst);
  StExecClientConnection *conn = NULL;
  int fd;
  uint64_t start_ms;
  int left;
  ssize_t sent;
  char *buf = NULL;
  int cap = 65535;
  int got = 0;
  int ret = -1;
  eParseResult parsed = MSG_PARSE_ERROR;

  if (msg == NULL || msg->Request() == NULL || msg->RequestLen() <= 0) {
    return -1;
  }
  LOG_DEBUG("send : %s", msg->Request());
  start_ms = Util::TimeMs();
  conn = Instance<StConnectionManager<StExecClientConnection> >()->AllocPtr(
      eTCP_CONN, &addr);
  if (conn == NULL) {
    LOG_ERROR("memcache alloc connection failed");
    return -1;
  }
  left = remaining_ms(start_ms, timeout_ms);
  if (left <= 0) {
    release_client(conn);
    return -1;
  }
  conn->SetTimeout(left);
  fd = conn->Create(addr);
  if (fd < 0) {
    release_client(conn);
    return -1;
  }
  left = remaining_ms(start_ms, timeout_ms);
  sent = st_send(fd, msg->Request(), (size_t)msg->RequestLen(), 0, left);
  if (sent < 0 || (int)sent != msg->RequestLen()) {
    release_client(conn);
    return -1;
  }

  buf = (char *)malloc((size_t)cap + 1);
  if (buf == NULL) {
    release_client(conn);
    return -1;
  }
  while (got < cap) {
    int nread;
    left = remaining_ms(start_ms, timeout_ms);
    if (left <= 0) {
      break;
    }
    nread = st_recv(fd, buf + got, cap - got, 0, left);
    if (nread < 0) {
      break;
    }
    if (nread == 0) {
      break;
    }
    got += nread;
    buf[got] = '\0';
    LOG_DEBUG("buf : %s, len : %d", buf, got);
    parsed = msg->FeedResponse(buf, got);
    if (parsed == MSG_PARSE_OK) {
      LOG_DEBUG("eParseResult s : %d", parsed);
      LOG_DEBUG("buf : %s, len : %d", msg->Response(), msg->ResponseLen());
      ret = 0;
      break;
    }
    if (parsed != MSG_PARSE_AGAIN) {
      LOG_ERROR("eParseResult s : %d", parsed);
      break;
    }
  }
  free(buf);
  release_client(conn);
  return ret;
}

static void *thread_func(void *) {
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(11211);
  servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int ret = mt_init_frame();
  LOG_TRACE("init ret : %d, servaddr : %p", ret, &servaddr);
  mt_set_hook_flag();

  MemcacheMsg msg1;
  eParseResult s = msg1.SetRequest(
      "get key1\r\nget key2\r\nset k1 0 900 9\r\nmemcached\r\n");
  if (s != MSG_PARSE_OK) {
    return NULL;
  }
  MemcacheMsg msg2;
  s = msg2.SetRequest("get k1\r\n");
  if (s != MSG_PARSE_OK) {
    return NULL;
  }

  LOG_TRACE("wait thread : %d", Instance<Frame>()->m_wait_num_);

  ret = memcache_exchange(&msg1, &servaddr, 1000);
  LOG_TRACE("ret : %d", ret);
  ret = memcache_exchange(&msg2, &servaddr, 1000);
  LOG_TRACE("ret : %d", ret);

  LOG_TRACE("thread id : %d, frame id : %p", (int)pthread_self(),
            Instance<Frame>());
  return NULL;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  thread_func(NULL);
  return 0;
}
