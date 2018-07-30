#include <ctype.h>
#include "memcache.h"

#define MEMCACHE_MAX_KEY_LENGTH 250

static bool memcache_storage(struct msg *r)
{
    switch (r->type) 
    {
    case MSG_REQ_MC_SET:
    case MSG_REQ_MC_CAS:
    case MSG_REQ_MC_ADD:
    case MSG_REQ_MC_REPLACE:
    case MSG_REQ_MC_APPEND:
    case MSG_REQ_MC_PREPEND:
        return true;

    default:
        break;
    }

    return false;
}
static bool memcache_cas(struct msg *r)
{
    if (r->type == MSG_REQ_MC_CAS) 
    {
        return true;
    }

    return false;
}
static bool memcache_retrieval(struct msg *r)
{
    switch (r->type) 
    {
    case MSG_REQ_MC_GET:
    case MSG_REQ_MC_GETS:
        return true;
    default:
        break;
    }

    return false;
}
static bool memcache_arithmetic(struct msg *r)
{
    switch (r->type) 
    {
    case MSG_REQ_MC_INCR:
    case MSG_REQ_MC_DECR:
        return true;

    default:
        break;
    }

    return false;
}
static bool memcache_delete(struct msg *r)
{
    if (r->type == MSG_REQ_MC_DELETE) 
    {
        return true;
    }

    return false;
}

static bool memcache_touch(struct msg *r)
{
    if (r->type == MSG_REQ_MC_TOUCH) 
    {
        return true;
    }

    return false;
}

void memcache_parse_req(struct msg *r)
{
    uint8_t *p, *m;
    uint8_t ch;
    eState state = r->state;

    for (p = r->pos; p < r->last; p++) 
    {
        ch = *p;

        switch (state) 
        {
        case SW_START:
            if (ch == ' ') 
            {
                break;
            }
            if (!islower(ch)) 
            {
                goto error;
            }
            /* req_start <- p; type_start <- p */
            r->token = p;
            state = SW_REQ_TYPE;
            break;
        case SW_REQ_TYPE:
            if (ch == ' ' || ch == CR) 
            {
                /* type_end = p - 1 */
                m = r->token;
                r->token = NULL;
                r->type = MSG_UNKNOWN;
                r->narg++;
                switch (p - m) 
                {
                case 3:
                    if (str4cmp(m, 'g', 'e', 't', ' ')) 
                    {
                        r->type = MSG_REQ_MC_GET;
                        break;
                    }
                    if (str4cmp(m, 's', 'e', 't', ' ')) 
                    {
                        r->type = MSG_REQ_MC_SET;
                        break;
                    }
                    if (str4cmp(m, 'a', 'd', 'd', ' ')) 
                    {
                        r->type = MSG_REQ_MC_ADD;
                        break;
                    }
                    if (str4cmp(m, 'c', 'a', 's', ' ')) 
                    {
                        r->type = MSG_REQ_MC_CAS;
                        break;
                    }
                    break;
                case 4:
                    if (str4cmp(m, 'g', 'e', 't', 's')) 
                    {
                        r->type = MSG_REQ_MC_GETS;
                        break;
                    }
                    if (str4cmp(m, 'i', 'n', 'c', 'r')) 
                    {
                        r->type = MSG_REQ_MC_INCR;
                        break;
                    }
                    if (str4cmp(m, 'd', 'e', 'c', 'r')) 
                    {
                        r->type = MSG_REQ_MC_DECR;
                        break;
                    }
                    if (str4cmp(m, 'q', 'u', 'i', 't')) 
                    {
                        r->type = MSG_REQ_MC_QUIT;
                        r->quit = 1;
                        break;
                    }
                    break;
                case 5:
                    if (str5cmp(m, 't', 'o', 'u', 'c', 'h')) 
                    {
                      r->type = MSG_REQ_MC_TOUCH;
                      break;
                    }
                    break;
                case 6:
                    if (str6cmp(m, 'a', 'p', 'p', 'e', 'n', 'd')) 
                    {
                        r->type = MSG_REQ_MC_APPEND;
                        break;
                    }
                    if (str6cmp(m, 'd', 'e', 'l', 'e', 't', 'e')) 
                    {
                        r->type = MSG_REQ_MC_DELETE;
                        break;
                    }
                    break;
                case 7:
                    if (str7cmp(m, 'p', 'r', 'e', 'p', 'e', 'n', 'd')) 
                    {
                        r->type = MSG_REQ_MC_PREPEND;
                        break;
                    }
                    if (str7cmp(m, 'r', 'e', 'p', 'l', 'a', 'c', 'e')) 
                    {
                        r->type = MSG_REQ_MC_REPLACE;
                        break;
                    }
                    break;
                }
                switch (r->type) 
                {
                case MSG_REQ_MC_GET:
                case MSG_REQ_MC_GETS:
                case MSG_REQ_MC_DELETE:
                case MSG_REQ_MC_CAS:
                case MSG_REQ_MC_SET:
                case MSG_REQ_MC_ADD:
                case MSG_REQ_MC_REPLACE:
                case MSG_REQ_MC_APPEND:
                case MSG_REQ_MC_PREPEND:
                case MSG_REQ_MC_INCR:
                case MSG_REQ_MC_DECR:
                case MSG_REQ_MC_TOUCH:
                    if (ch == CR) 
                    {
                        goto error;
                    }
                    state = SW_SPACES_BEFORE_KEY;
                    break;
                case MSG_REQ_MC_QUIT:
                    p = p - 1; /* go back by 1 byte */
                    state = SW_CRLF;
                    break;
                case MSG_UNKNOWN:
                    goto error;
                default:
                    NOT_REACHED();
                }
            } 
            else if (!islower(ch)) 
            {
                goto error;
            }
            break;
        case SW_SPACES_BEFORE_KEY:
            if (ch != ' ') 
            {
                p = p - 1; /* go back by 1 byte */
                r->token = NULL;
                state = SW_KEY;
            }
            break;
        case SW_KEY:
            if (r->token == NULL) 
            {
                r->token = p;
            }
            if (ch == ' ' || ch == CR) 
            {
                struct keypos *kpos;
                int keylen = p - r->token;
                if (keylen > MEMCACHE_MAX_KEY_LENGTH) 
                {
                    LOG_ERROR("parsed bad req %d of type %d with key "
                              "prefix '%.*s...' and length %d that exceeds "
                              "maximum key length", r->id, r->type, 16,
                              r->token, p - r->token);
                    goto error;
                } 
                else if (keylen == 0) 
                {
                    LOG_ERROR("parsed bad req %"PRIu64" of type %d with an "
                              "empty key", r->id, r->type);
                    goto error;
                }
                kpos = (struct keypos *)array_push(r->keys);
                if (kpos == NULL) 
                {
                    goto enomem;
                }
                kpos->start = r->token;
                kpos->end = p;

                // TODO : [test]打印数据
                char t_buffer[1024] = {0};
                memcpy(t_buffer, kpos->start, kpos->end - kpos->start);
                LOG_TRACE("t_buffer : %s", t_buffer);

                r->narg++;
                r->token = NULL;
                /* get next state */
                if (memcache_storage(r)) 
                {
                    state = SW_SPACES_BEFORE_FLAGS;
                } 
                else if (memcache_arithmetic(r) || memcache_touch(r) ) 
                {
                    state = SW_SPACES_BEFORE_NUM;
                } 
                else if (memcache_delete(r)) 
                {
                    state = SW_RUNTO_CRLF;
                } 
                else if (memcache_retrieval(r)) 
                {
                    state = SW_SPACES_BEFORE_KEYS;
                } 
                else 
                {
                    state = SW_RUNTO_CRLF;
                }
                if (ch == CR) 
                {
                    if (memcache_storage(r) || memcache_arithmetic(r)) 
                    {
                        goto error;
                    }
                    p = p - 1; /* go back by 1 byte */
                }
            }
            break;
        case SW_SPACES_BEFORE_KEYS:
            switch (ch) 
            {
            case ' ':
                break;
            case CR:
                state = SW_ALMOST_DONE;
                break;
            default:
                r->token = NULL;
                p = p - 1; /* go back by 1 byte */
                state = SW_KEY;
            }
            break;
        case SW_SPACES_BEFORE_FLAGS:
            if (ch != ' ') 
            {
                if (!isdigit(ch)) 
                {
                    goto error;
                }
                /* flags_start <- p; flags <- ch - '0' */
                r->token = p;
                state = SW_FLAGS;
            }
            break;
        case SW_FLAGS:
            if (isdigit(ch)) 
            {
                /* flags <- flags * 10 + (ch - '0') */
                ;
            } 
            else if (ch == ' ') 
            {
                /* flags_end <- p - 1 */
                r->token = NULL;
                state = SW_SPACES_BEFORE_EXPIRY;
            } 
            else 
            {
                goto error;
            }
            break;
        case SW_SPACES_BEFORE_EXPIRY:
            if (ch != ' ') 
            {
                if (!isdigit(ch)) 
                {
                    goto error;
                }
                /* expiry_start <- p; expiry <- ch - '0' */
                r->token = p;
                state = SW_EXPIRY;
            }
            break;
        case SW_EXPIRY:
            if (isdigit(ch)) 
            {
                /* expiry <- expiry * 10 + (ch - '0') */
                ;
            } 
            else if (ch == ' ') 
            {
                /* expiry_end <- p - 1 */
                r->token = NULL;
                state = SW_SPACES_BEFORE_VLEN;
            } 
            else 
            {
                goto error;
            }
            break;
        case SW_SPACES_BEFORE_VLEN:
            if (ch != ' ') 
            {
                if (!isdigit(ch)) 
                {
                    goto error;
                }
                /* vlen_start <- p */
                r->vlen = (uint32_t)(ch - '0');
                state = SW_VLEN;
            }
            break;
        case SW_VLEN:
            if (isdigit(ch)) 
            {
                r->vlen = r->vlen * 10 + (uint32_t)(ch - '0');
            } 
            else if (memcache_cas(r)) 
            {
                if (ch != ' ') 
                {
                    goto error;
                }
                /* vlen_end <- p - 1 */
                p = p - 1; /* go back by 1 byte */
                r->token = NULL;
                state = SW_SPACES_BEFORE_CAS;
            } 
            else if (ch == ' ' || ch == CR) 
            {
                /* vlen_end <- p - 1 */
                p = p - 1; /* go back by 1 byte */
                r->token = NULL;
                state = SW_RUNTO_CRLF;
            } 
            else 
            {
                goto error;
            }
            break;
        case SW_SPACES_BEFORE_CAS:
            if (ch != ' ') 
            {
                if (!isdigit(ch)) 
                {
                    goto error;
                }
                /* cas_start <- p; cas <- ch - '0' */
                r->token = p;
                state = SW_CAS;
            }
            break;
        case SW_CAS:
            if (isdigit(ch)) 
            {
                /* cas <- cas * 10 + (ch - '0') */
                ;
            } 
            else if (ch == ' ' || ch == CR) 
            {
                /* cas_end <- p - 1 */
                p = p - 1; /* go back by 1 byte */
                r->token = NULL;
                state = SW_RUNTO_CRLF;
            } 
            else 
            {
                goto error;
            }
            break;
        case SW_RUNTO_VAL:
            switch (ch) 
            {
            case LF:
                state = SW_VAL;
                break;
            default:
                goto error;
            }
            break;
        case SW_VAL:
            m = p + r->vlen;
            if (m >= r->last) 
            {
                r->vlen -= (uint32_t)(r->last - p);
                m = r->last - 1;
                p = m;
                break;
            }
            switch (*m) 
            {
            case CR:
                p = m;
                state = SW_ALMOST_DONE;
                break;
            default:
                goto error;
            }
            break;
        case SW_SPACES_BEFORE_NUM:
            if (ch != ' ') 
            {
                if (!(isdigit(ch) || ch == '-')) 
                {
                    goto error;
                }
                /* num_start <- p; num <- ch - '0'  */
                r->token = p;
                state = SW_NUM;
            }
            break;
        case SW_NUM:
            if (isdigit(ch)) 
            {
                /* num <- num * 10 + (ch - '0') */
                ;
            } 
            else if (ch == ' ' || ch == CR) 
            {
                r->token = NULL;
                /* num_end <- p - 1 */
                p = p - 1; /* go back by 1 byte */
                state = SW_RUNTO_CRLF;
            } 
            else 
            {
                goto error;
            }
            break;
        case SW_RUNTO_CRLF:
            switch (ch) 
            {
            case ' ':
                break;
            case 'n':
                if (memcache_storage(r) || memcache_arithmetic(r) || 
                memcache_delete(r) || memcache_touch(r)) 
                {
                    /* noreply_start <- p */
                    r->token = p;
                    state = SW_NOREPLY;
                } 
                else 
                {
                    goto error;
                }
                break;
            case CR:
                if (memcache_storage(r)) 
                {
                    state = SW_RUNTO_VAL;
                } 
                else 
                {
                    state = SW_ALMOST_DONE;
                }
                break;
            default:
                goto error;
            }
            break;
        case SW_NOREPLY:
            switch (ch) 
            {
            case ' ':
            case CR:
                m = r->token;
                if (((p - m) == 7) && str7cmp(m, 'n', 'o', 'r', 'e', 'p', 'l', 'y')) 
                {
                    r->token = NULL;
                    /* noreply_end <- p - 1 */
                    r->noreply = 1;
                    state = SW_AFTER_NOREPLY;
                    p = p - 1; /* go back by 1 byte */
                } else {
                    goto error;
                }
            }
            break;
        case SW_AFTER_NOREPLY:
            switch (ch) 
            {
            case ' ':
                break;
            case CR:
                if (memcache_storage(r)) 
                {
                    state = SW_RUNTO_VAL;
                } 
                else 
                {
                    state = SW_ALMOST_DONE;
                }
                break;
            default:
                goto error;
            }
            break;
        case SW_CRLF:
            switch (ch) 
            {
            case ' ':
                break;
            case CR:
                state = SW_ALMOST_DONE;
                break;
            default:
                goto error;
            }
            break;
        case SW_ALMOST_DONE:
            switch (ch) 
            {
            case LF:
                /* req_end <- p */
                goto done;
            default:
                goto error;
            }
            break;
        case SW_SENTINEL:
        default:
            NOT_REACHED();
            break;

        }
    }

    r->pos = p;
    r->state = state;

    if (r->last == r->end && r->token != NULL) 
    {
        r->pos = r->token;
        r->token = NULL;
        r->result = MSG_PARSE_REPAIR;
    } 
    else 
    {
        r->result = MSG_PARSE_AGAIN;
    }

    LOG_TRACE("[%d:MSG_PARSE_AGAIN,MSG_PARSE_REPAIR]parsed req %ld type %d state %d rpos %d of %d", 
        r->result, r->id, r->type, r->state, r->pos, r->pos);
    return;

done:
    r->pos = p + 1;
    r->state = SW_START;
    r->result = MSG_PARSE_OK;

    LOG_TRACE("[%d,MSG_PARSE_OK]parsed req %ld type %d state %d rpos %d of %d", 
        r->result, r->id, r->type, r->state, r->pos, r->pos);
    return;

enomem:
    r->result = MSG_PARSE_ERROR;
    r->state = state;

    LOG_TRACE("[%d,MSG_PARSE_ERROR]out of memory on parse req %ld type %d state %d", 
        r->result, r->id, r->type, r->state);

    return;

error:
    r->result = MSG_PARSE_ERROR;
    r->state = state;
    errno = EINVAL;

    LOG_TRACE("[%d]parsed bad req %ld res %d type %d state %d", 
        r->result, r->id, r->type, r->state);

    return;
}

void memcache_parse_rsp(struct msg *r)
{
    uint8_t *p, *m;
    uint8_t ch;
    eState state = r->state;

    for (p = r->pos; p < r->last; p++) 
    {
        ch = *p;

        switch (state) 
        {
        case SW_START:
            if (isdigit(ch)) 
            {
                state = SW_RSP_NUM;
            } 
            else 
            {
                state = SW_RSP_STR;
            }
            p = p - 1; /* go back by 1 byte */
            break;

        case SW_RSP_NUM:
            if (r->token == NULL) 
            {
                /* rsp_start <- p; type_start <- p */
                r->token = p;
            }
            if (isdigit(ch)) 
            {
                /* num <- num * 10 + (ch - '0') */
                ;
            } 
            else if (ch == ' ' || ch == CR) 
            {
                /* type_end <- p - 1 */
                r->token = NULL;
                r->type = MSG_RSP_MC_NUM;
                p = p - 1; /* go back by 1 byte */
                state = SW_CRLF;
            } 
            else 
            {
                goto error;
            }
            break;

        case SW_RSP_STR:
            if (r->token == NULL) 
            {
                /* rsp_start <- p; type_start <- p */
                r->token = p;
            }

            if (ch == ' ' || ch == CR) 
            {
                /* type_end <- p - 1 */
                m = r->token;
                /* r->token = NULL; */
                r->type = MSG_UNKNOWN;

                switch (p - m) 
                {
                case 3:
                    if (str4cmp(m, 'E', 'N', 'D', '\r')) 
                    {
                        r->type = MSG_RSP_MC_END;
                        /* end_start <- m; end_end <- p - 1 */
                        r->end = m;
                        break;
                    }
                    break;
                case 5:
                    if (str5cmp(m, 'V', 'A', 'L', 'U', 'E')) 
                    {
                        /*
                         * Encompasses responses for 'get', 'gets' and
                         * 'cas' command.
                         */
                        r->type = MSG_RSP_MC_VALUE;
                        break;
                    }
                    if (str5cmp(m, 'E', 'R', 'R', 'O', 'R')) 
                    {
                        r->type = MSG_RSP_MC_ERROR;
                        break;
                    }
                    break;
                case 6:
                    if (str6cmp(m, 'S', 'T', 'O', 'R', 'E', 'D')) 
                    {
                        r->type = MSG_RSP_MC_STORED;
                        break;
                    }
                    if (str6cmp(m, 'E', 'X', 'I', 'S', 'T', 'S')) 
                    {
                        r->type = MSG_RSP_MC_EXISTS;
                        break;
                    }
                    break;
                case 7:
                    if (str7cmp(m, 'D', 'E', 'L', 'E', 'T', 'E', 'D')) 
                    {
                        r->type = MSG_RSP_MC_DELETED;
                        break;
                    }
                    if (str7cmp(m, 'T', 'O', 'U', 'C', 'H', 'E', 'D')) 
                    {
                        r->type = MSG_RSP_MC_TOUCHED;
                        break;
                    }
                    break;
                case 9:
                    if (str9cmp(m, 'N', 'O', 'T', '_', 'F', 'O', 'U', 'N', 'D')) 
                    {
                        r->type = MSG_RSP_MC_NOT_FOUND;
                        break;
                    }
                    break;
                case 10:
                    if (str10cmp(m, 'N', 'O', 'T', '_', 'S', 'T', 'O', 'R', 'E', 'D')) 
                    {
                        r->type = MSG_RSP_MC_NOT_STORED;
                        break;
                    }
                    break;
                case 12:
                    if (str12cmp(m, 'C', 'L', 'I', 'E', 'N', 'T', '_', 'E', 'R', 'R', 'O', 'R')) 
                    {
                        r->type = MSG_RSP_MC_CLIENT_ERROR;
                        break;
                    }
                    if (str12cmp(m, 'S', 'E', 'R', 'V', 'E', 'R', '_', 'E', 'R', 'R', 'O', 'R')) 
                    {
                        r->type = MSG_RSP_MC_SERVER_ERROR;
                        break;
                    }
                    break;
                }
                switch (r->type) 
                {
                case MSG_UNKNOWN:
                    goto error;
                case MSG_RSP_MC_STORED:
                case MSG_RSP_MC_NOT_STORED:
                case MSG_RSP_MC_EXISTS:
                case MSG_RSP_MC_NOT_FOUND:
                case MSG_RSP_MC_DELETED:
                case MSG_RSP_MC_TOUCHED:
                    state = SW_CRLF;
                    break;
                case MSG_RSP_MC_END:
                    state = SW_CRLF;
                    break;
                case MSG_RSP_MC_VALUE:
                    state = SW_SPACES_BEFORE_KEY;
                    break;
                case MSG_RSP_MC_ERROR:
                    state = SW_CRLF;
                    break;
                case MSG_RSP_MC_CLIENT_ERROR:
                case MSG_RSP_MC_SERVER_ERROR:
                    state = SW_RUNTO_CRLF;
                    break;
                default:
                    NOT_REACHED();
                }
                p = p - 1; /* go back by 1 byte */
            }
            break;
        case SW_SPACES_BEFORE_KEY:
            if (ch != ' ') 
            {
                state = SW_KEY;
                p = p - 1; /* go back by 1 byte */
            }
            break;
        case SW_KEY:
            if (ch == ' ') 
            {
                /* r->token = NULL; */
                state = SW_SPACES_BEFORE_FLAGS;
            }
            break;
        case SW_SPACES_BEFORE_FLAGS:
            if (ch != ' ') 
            {
                if (!isdigit(ch)) 
                {
                    goto error;
                }
                state = SW_FLAGS;
                p = p - 1; /* go back by 1 byte */
            }
            break;
        case SW_FLAGS:
            if (r->token == NULL) 
            {
                /* flags_start <- p */
                /* r->token = p; */
            }
            if (isdigit(ch)) 
            {
                /* flags <- flags * 10 + (ch - '0') */
                ;
            } 
            else if (ch == ' ') 
            {
                /* flags_end <- p - 1 */
                /* r->token = NULL; */
                state = SW_SPACES_BEFORE_VLEN;
            } 
            else 
            {
                goto error;
            }
            break;
        case SW_SPACES_BEFORE_VLEN:
            if (ch != ' ') 
            {
                if (!isdigit(ch)) 
                {
                    goto error;
                }
                p = p - 1; /* go back by 1 byte */
                state = SW_VLEN;
                r->vlen = 0;
            }
            break;
        case SW_VLEN:
            if (isdigit(ch)) 
            {
                r->vlen = r->vlen * 10 + (uint32_t)(ch - '0');
            } 
            else if (ch == ' ' || ch == CR) 
            {
                /* vlen_end <- p - 1 */
                p = p - 1; /* go back by 1 byte */
                /* r->token = NULL; */
                state = SW_RUNTO_CRLF;
            } 
            else 
            {
                goto error;
            }
            break;
        case SW_RUNTO_VAL:
            switch (ch) 
            {
            case LF:
                /* val_start <- p + 1 */
                state = SW_VAL;
                r->token = NULL;
                break;
            default:
                goto error;
            }
            break;
        case SW_VAL:
            m = p + r->vlen;
            if (m >= r->last) 
            {
                r->vlen -= (uint32_t)(r->last - p);
                m = r->last - 1;
                p = m; /* move forward by vlen bytes */
                break;
            }
            switch (*m) 
            {
            case CR:
                /* val_end <- p - 1 */
                p = m; /* move forward by vlen bytes */
                state = SW_VAL_LF;
                break;
            default:
                goto error;
            }
            break;
        case SW_VAL_LF:
            switch (ch) 
            {
            case LF:
                /* state = SW_END; */
                state = SW_RSP_STR;
                break;
            default:
                goto error;
            }
            break;
        case SW_END:
            if (r->token == NULL) 
            {
                if (ch != 'E') 
                {
                    goto error;
                }
                /* end_start <- p */
                r->token = p;
            } 
            else if (ch == CR) 
            {
                /* end_end <- p */
                m = r->token;
                r->token = NULL;
                switch (p - m) 
                {
                case 3:
                    if (str4cmp(m, 'E', 'N', 'D', '\r')) 
                    {
                        r->end = m;
                        state = SW_ALMOST_DONE;
                    }
                    break;
                default:
                    goto error;
                }
            }
            break;
        case SW_RUNTO_CRLF:
            switch (ch) 
            {
            case CR:
                if (r->type == MSG_RSP_MC_VALUE) 
                {
                    state = SW_RUNTO_VAL;
                } 
                else 
                {
                    state = SW_ALMOST_DONE;
                }
                break;
            default:
                break;
            }
            break;
        case SW_CRLF:
            switch (ch) 
            {
            case ' ':
                break;
            case CR:
                state = SW_ALMOST_DONE;
                break;
            default:
                goto error;
            }
            break;
        case SW_ALMOST_DONE:
            switch (ch) 
            {
            case LF:
                /* rsp_end <- p */
                goto done;
            default:
                goto error;
            }
            break;
        case SW_SENTINEL:
        default:
            NOT_REACHED();
            break;
        }
    }

    r->pos = p;
    r->state = state;

    if (r->last == r->end && r->token != NULL) 
    {
        if (state <= SW_RUNTO_VAL || state == SW_CRLF || state == SW_ALMOST_DONE) 
        {
            r->state = SW_START;
        }
        r->pos = r->token;
        r->token = NULL;
        r->result = MSG_PARSE_REPAIR;
    } 
    else 
    {
        r->result = MSG_PARSE_AGAIN;
    }

    LOG_TRACE("[MSG_PARSE_AGAIN,MSG_PARSE_REPAIR]parsed rsp %ld "
        "res %d type %d state %d rpos %d of %d", r->id, r->result, r->type, 
        r->state, r->pos - r->pos, r->last - r->pos);
    return;

done:
    r->pos = p + 1;
    r->state = SW_START;
    r->token = NULL;
    r->result = MSG_PARSE_OK;

    LOG_TRACE("[MSG_PARSE_OK]parsed rsp %ld res %d type %d state %d rpos %d of %d", 
        r->id, r->result, r->type, r->state, r->pos - r->pos, r->last - r->pos);
    return;

error:
    r->result = MSG_PARSE_ERROR;
    r->state = state;
    errno = EINVAL;

    LOG_TRACE("[MSG_PARSE_ERROR]parsed bad rsp %ld res %d type %d state %d", 
        r->id, r->result, r->type, r->state);
    return;
}
