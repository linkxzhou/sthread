#ifndef _MEMCACHE_H_
#define _MEMCACHE_H_

#include "array.h"

#define LF                  (uint8_t) 10
#define CR                  (uint8_t) 13
#define CRLF                "\x0d\x0a"
#define CRLF_LEN            (sizeof("\x0d\x0a") - 1)
#define NOT_REACHED()       ASSERT(0)

#define LOG_ERROR

#define NELEMS(a)           ((sizeof(a)) / sizeof((a)[0]))

#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))

#define SQUARE(d)           ((d) * (d))
#define VAR(s, s2, n)       (((n) < 2) ? 0.0 : ((s2) - SQUARE(s)/(n)) / ((n) - 1))
#define STDDEV(s, s2, n)    (((n) < 2) ? 0.0 : sqrt(VAR((s), (s2), (n))))

#define MSG_TYPE_CODEC(ACTION)                                                                      \
    ACTION( UNKNOWN )                                                                               \
    ACTION( REQ_MC_GET )                       /* memcache retrieval requests */                    \
    ACTION( REQ_MC_GETS )                                                                           \
    ACTION( REQ_MC_DELETE )                    /* memcache delete request */                        \
    ACTION( REQ_MC_CAS )                       /* memcache cas request and storage request */       \
    ACTION( REQ_MC_SET )                       /* memcache storage request */                       \
    ACTION( REQ_MC_ADD )                                                                            \
    ACTION( REQ_MC_REPLACE )                                                                        \
    ACTION( REQ_MC_APPEND )                                                                         \
    ACTION( REQ_MC_PREPEND )                                                                        \
    ACTION( REQ_MC_INCR )                      /* memcache arithmetic request */                    \
    ACTION( REQ_MC_DECR )                                                                           \
    ACTION( REQ_MC_TOUCH )                     /* memcache touch request */                         \
    ACTION( REQ_MC_QUIT )                      /* memcache quit request */                          \
    ACTION( RSP_MC_NUM )                       /* memcache arithmetic response */                   \
    ACTION( RSP_MC_STORED )                    /* memcache cas and storage response */              \
    ACTION( RSP_MC_NOT_STORED )                                                                     \
    ACTION( RSP_MC_EXISTS )                                                                         \
    ACTION( RSP_MC_NOT_FOUND )                                                                      \
    ACTION( RSP_MC_END )                                                                            \
    ACTION( RSP_MC_VALUE )                                                                          \
    ACTION( RSP_MC_DELETED )                   /* memcache delete response */                       \
    ACTION( RSP_MC_TOUCHED )                   /* memcache touch response */                        \
    ACTION( RSP_MC_ERROR )                     /* memcache error responses */                       \
    ACTION( RSP_MC_CLIENT_ERROR )                                                                   \
    ACTION( RSP_MC_SERVER_ERROR )                                                                   \

#define DEFINE_ACTION(_name) MSG_##_name,
typedef enum msg_type 
{
    MSG_TYPE_CODEC(DEFINE_ACTION)
} msg_type_t;
#undef DEFINE_ACTION

typedef enum 
{
    SW_START,
    SW_REQ_TYPE,
    SW_SPACES_BEFORE_KEY,
    SW_KEY,
    SW_SPACES_BEFORE_KEYS,
    SW_SPACES_BEFORE_FLAGS,
    SW_FLAGS,
    SW_SPACES_BEFORE_EXPIRY,
    SW_EXPIRY,
    SW_SPACES_BEFORE_VLEN,
    SW_VLEN,
    SW_SPACES_BEFORE_CAS,
    SW_CAS,
    SW_RUNTO_VAL,
    SW_VAL,
    SW_SPACES_BEFORE_NUM,
    SW_NUM,
    SW_RUNTO_CRLF,
    SW_CRLF,
    SW_NOREPLY,
    SW_AFTER_NOREPLY,
    SW_ALMOST_DONE,
    SW_SENTINEL
} eState;

struct keypos 
{
    uint8_t             *start;           /* key start pos */
    uint8_t             *end;             /* key end pos */
};

struct msg 
{
    // TAILQ_ENTRY(msg)     c_tqe;           /* link in client q */
    // TAILQ_ENTRY(msg)     s_tqe;           /* link in server q */
    // TAILQ_ENTRY(msg)     m_tqe;           /* link in send q / free q */

    uint64_t             id;              /* message id */
    struct msg           *peer;           /* message peer */
    struct conn          *owner;          /* message owner - client | server */

    uint32_t             mlen;            /* message length */
    int64_t              start_ts;        /* request start timestamp in usec */

    eState               state;           /* current parser state */
    uint8_t              *pos;            /* parser position marker */
    uint8_t              *last;            /* parser position marker */
    uint8_t              *token;          /* token marker */

    msg_type_t           type;            /* message type */

    struct array         *keys;           /* array of keypos, for req */

    uint32_t             vlen;            /* value length (memcache) */
    uint8_t              *end;            /* end marker (memcache) */

    uint8_t              *narg_start;     /* narg start (redis) */
    uint8_t              *narg_end;       /* narg end (redis) */
    uint32_t             narg;            /* # arguments (redis) */
    uint32_t             rnarg;           /* running # arg used by parsing fsa (redis) */
    uint32_t             rlen;            /* running length in parsing fsa (redis) */
    uint32_t             integer;         /* integer reply value (redis) */

    struct msg           *frag_owner;     /* owner of fragment message */
    uint32_t             nfrag;           /* # fragment */
    uint32_t             nfrag_done;      /* # fragment done */
    uint64_t             frag_id;         /* id of fragmented message */
    struct msg           **frag_seq;      /* sequence of fragment message, map from keys to fragments*/

    err_t                err;             /* errno on error? */
    unsigned             error:1;         /* error? */
    unsigned             ferror:1;        /* one or more fragments are in error? */
    unsigned             request:1;       /* request? or response? */
    unsigned             quit:1;          /* quit request? */
    unsigned             noreply:1;       /* noreply? */
    unsigned             noforward:1;     /* not need forward (example: ping) */
    unsigned             done:1;          /* done? */
    unsigned             fdone:1;         /* all fragments are done? */
    unsigned             swallow:1;       /* swallow response? */
    unsigned             redis:1;         /* redis? */
};

#ifdef NC_LITTLE_ENDIAN

#define str4cmp(m, c0, c1, c2, c3)                                                          \
    (*(uint32_t *) m == ((c3 << 24) | (c2 << 16) | (c1 << 8) | c0))

#define str5cmp(m, c0, c1, c2, c3, c4)                                                      \
    (str4cmp(m, c0, c1, c2, c3) && (m[4] == c4))

#define str6cmp(m, c0, c1, c2, c3, c4, c5)                                                  \
    (str4cmp(m, c0, c1, c2, c3) &&                                                          \
        (((uint32_t *) m)[1] & 0xffff) == ((c5 << 8) | c4))

#define str7cmp(m, c0, c1, c2, c3, c4, c5, c6)                                              \
    (str6cmp(m, c0, c1, c2, c3, c4, c5) && (m[6] == c6))

#define str8cmp(m, c0, c1, c2, c3, c4, c5, c6, c7)                                          \
    (str4cmp(m, c0, c1, c2, c3) &&                                                          \
        (((uint32_t *) m)[1] == ((c7 << 24) | (c6 << 16) | (c5 << 8) | c4)))

#define str9cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8)                                      \
    (str8cmp(m, c0, c1, c2, c3, c4, c5, c6, c7) && m[8] == c8)

#define str10cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9)                                 \
    (str8cmp(m, c0, c1, c2, c3, c4, c5, c6, c7) &&                                          \
        (((uint32_t *) m)[2] & 0xffff) == ((c9 << 8) | c8))

#define str11cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10)                            \
    (str10cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9) && (m[10] == c10))

#define str12cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11)                       \
    (str8cmp(m, c0, c1, c2, c3, c4, c5, c6, c7) &&                                          \
        (((uint32_t *) m)[2] == ((c11 << 24) | (c10 << 16) | (c9 << 8) | c8)))

#else

#define str4cmp(m, c0, c1, c2, c3)                                                          \
    (m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3)

#define str5cmp(m, c0, c1, c2, c3, c4)                                                      \
    (str4cmp(m, c0, c1, c2, c3) && (m[4] == c4))

#define str6cmp(m, c0, c1, c2, c3, c4, c5)                                                  \
    (str5cmp(m, c0, c1, c2, c3, c4) && m[5] == c5)

#define str7cmp(m, c0, c1, c2, c3, c4, c5, c6)                                              \
    (str6cmp(m, c0, c1, c2, c3, c4, c5) && m[6] == c6)

#define str8cmp(m, c0, c1, c2, c3, c4, c5, c6, c7)                                          \
    (str7cmp(m, c0, c1, c2, c3, c4, c5, c6) && m[7] == c7)

#define str9cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8)                                      \
    (str8cmp(m, c0, c1, c2, c3, c4, c5, c6, c7) && m[8] == c8)

#define str10cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9)                                 \
    (str9cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8) && m[9] == c9)

#define str11cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10)                            \
    (str10cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9) && m[10] == c10)

#define str12cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11)                       \
    (str11cmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10) && m[11] == c11)

#endif

#define str3icmp(m, c0, c1, c2)                                                             \
    ((m[0] == c0 || m[0] == (c0 ^ 0x20)) &&                                                 \
     (m[1] == c1 || m[1] == (c1 ^ 0x20)) &&                                                 \
     (m[2] == c2 || m[2] == (c2 ^ 0x20)))

#define str4icmp(m, c0, c1, c2, c3)                                                         \
    (str3icmp(m, c0, c1, c2) && (m[3] == c3 || m[3] == (c3 ^ 0x20)))

#define str5icmp(m, c0, c1, c2, c3, c4)                                                     \
    (str4icmp(m, c0, c1, c2, c3) && (m[4] == c4 || m[4] == (c4 ^ 0x20)))

#define str6icmp(m, c0, c1, c2, c3, c4, c5)                                                 \
    (str5icmp(m, c0, c1, c2, c3, c4) && (m[5] == c5 || m[5] == (c5 ^ 0x20)))

#define str7icmp(m, c0, c1, c2, c3, c4, c5, c6)                                             \
    (str6icmp(m, c0, c1, c2, c3, c4, c5) &&                                                 \
     (m[6] == c6 || m[6] == (c6 ^ 0x20)))

#define str8icmp(m, c0, c1, c2, c3, c4, c5, c6, c7)                                         \
    (str7icmp(m, c0, c1, c2, c3, c4, c5, c6) &&                                             \
     (m[7] == c7 || m[7] == (c7 ^ 0x20)))

#define str9icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8)                                     \
    (str8icmp(m, c0, c1, c2, c3, c4, c5, c6, c7) &&                                         \
     (m[8] == c8 || m[8] == (c8 ^ 0x20)))

#define str10icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9)                                \
    (str9icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8) &&                                     \
     (m[9] == c9 || m[9] == (c9 ^ 0x20)))

#define str11icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10)                           \
    (str10icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9) &&                                \
     (m[10] == c10 || m[10] == (c10 ^ 0x20)))

#define str12icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11)                      \
    (str11icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10) &&                           \
     (m[11] == c11 || m[11] == (c11 ^ 0x20)))

#define str13icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12)                 \
    (str12icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11) &&                      \
     (m[12] == c12 || m[12] == (c12 ^ 0x20)))

#define str14icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13)            \
    (str13icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12) &&                 \
     (m[13] == c13 || m[13] == (c13 ^ 0x20)))

#define str15icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14)       \
    (str14icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13) &&            \
     (m[14] == c14 || m[14] == (c14 ^ 0x20)))

#define str16icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14, c15)  \
    (str15icmp(m, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14) &&       \
     (m[15] == c15 || m[15] == (c15 ^ 0x20)))

void memcache_parse_req(struct msg *r);
void memcache_parse_rsp(struct msg *r);
bool memcache_failure(struct msg *r);
void memcache_pre_coalesce(struct msg *r);
void memcache_post_coalesce(struct msg *r);

#endif
