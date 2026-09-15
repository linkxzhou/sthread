/*
 * Copyright (C) zhoulv2000@163.com
 */

#include "st_action.h"
#include <stdlib.h>
#include <string.h>

struct ActionCheckCtx {
  IMtAction *action;
};

static ActionCheckCtx g_st_action_check_ctx = {NULL};

static int32_t ActionCheckLength(void *buf, int len) {
  IMtAction *action = g_st_action_check_ctx.action;
  if (action == NULL) {
    return len;
  }
  return action->HandleInput(buf, len, action->GetIMessagePtr());
}

int IMtActionClient::SendRecv(int timeout) {
  int ret = 0;
  for (IMtActionList::iterator iter = m_action_list_.begin();
       iter != m_action_list_.end(); ++iter) {
    IMtAction *action = *iter;
    if (action == NULL) {
      continue;
    }
    struct sockaddr_in *dst = action->GetMsgDstAddr();
    if (dst == NULL) {
      action->SetErrno(eERR_DEST_ADDR_ERROR);
      action->HandleError(eERR_DEST_ADDR_ERROR, action->GetIMessagePtr());
      continue;
    }

    int bufsize = action->GetMsgBufferSize();
    char *sendbuf = (char *)malloc(bufsize);
    char *recvbuf = (char *)malloc(bufsize);
    if (sendbuf == NULL || recvbuf == NULL) {
      st_safe_free(sendbuf);
      st_safe_free(recvbuf);
      action->SetErrno(eERR_MEMORY_ERROR);
      action->HandleError(eERR_MEMORY_ERROR, action->GetIMessagePtr());
      continue;
    }
    memset(sendbuf, 0, bufsize);
    memset(recvbuf, 0, bufsize);

    int sendlen = bufsize;
    ret = action->HandleEncode(sendbuf, sendlen, action->GetIMessagePtr());
    if (ret < 0 || sendlen <= 0) {
      action->SetErrno(eERR_ENCODE_ERROR);
      action->HandleError(eERR_ENCODE_ERROR, action->GetIMessagePtr());
      st_safe_free(sendbuf);
      st_safe_free(recvbuf);
      continue;
    }

    int recvlen = bufsize;
    eConnType ctype = action->GetConnType();
    if (IS_UDP_CONN(ctype)) {
      ret = udp_sendrecv(dst, sendbuf, sendlen, recvbuf, recvlen, timeout);
    } else {
      g_st_action_check_ctx.action = action;
      bool keeplive = IS_KEEPLIVE(ctype);
      ret = tcp_sendrecv(dst, sendbuf, sendlen, recvbuf, recvlen, timeout,
                         ActionCheckLength, keeplive);
      g_st_action_check_ctx.action = NULL;
    }

    if (ret < 0) {
      action->SetErrno(eERR_RECV_FAIL);
      action->HandleError(ret, action->GetIMessagePtr());
    } else {
      if (IS_UDP_CONN(ctype)) {
        action->HandleInput(recvbuf, recvlen, action->GetIMessagePtr());
      }
      action->HandleProcess(recvbuf, recvlen, action->GetIMessagePtr());
    }

    st_safe_free(sendbuf);
    st_safe_free(recvbuf);
  }
  m_action_list_.clear();
  return 0;
}
