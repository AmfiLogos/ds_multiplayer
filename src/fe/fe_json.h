/* fe_json: small JSON DOM for save surgery. Strings and numbers keep their RAW text (as in the file: escapes kept,
 * number lexemes kept), so everything not touched is written back byte for byte in value. Object key order is kept
 * ($type stays first, Newtonsoft TypeNameHandling reads it first). Objects over 16 keys get a hash index. */
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { JN_NULL, JN_FALSE, JN_TRUE, JN_NUM, JN_STR, JN_ARR, JN_OBJ };

typedef struct jv {
    uint8_t t;
    char *s; size_t n;              /* JN_NUM / JN_STR: raw text (no quotes), length */
    struct jv **v; char **k; size_t *kn;   /* JN_ARR: v[cnt]; JN_OBJ: k[cnt] (raw), v[cnt] */
    size_t cnt, cap;
    uint32_t *ix; size_t ixcap;     /* JN_OBJ hash index (0 = empty, else position + 1); NULL = rebuild on demand */
} jv;

jv   *jparse(const char *text, size_t len, char *err, size_t errn);   /* NULL on error (err filled) */
jv   *jload(const char *path, char *err, size_t errn);
char *jdump(const jv *v, int indent, size_t *len);                    /* malloc'd, indent 0 = compact */
int   jsave_new(const jv *v, const char *path);                       /* tmp + rename; -1 if path exists */
void  jfree(jv *v);
jv   *jcopy(const jv *v);

jv   *jnull(void);
jv   *jbool(int b);
jv   *jnum_d(double d);
jv   *jnum_i(long long i);
jv   *jstrv(const char *raw);                                          /* raw = already JSON-escaped text */
jv   *jarr(void);
jv   *jobj(void);

jv   *jget(const jv *o, const char *key);                             /* object member or NULL */
jv   *jpath(const jv *o, const char *dotted);                         /* "A.B.C" through objects */
int   jset(jv *o, const char *key, jv *val);                          /* replace (frees old) or append; takes val */
int   jdel(jv *o, const char *key);                                   /* 1 removed */
jv   *jtake(jv *o, const char *key);                                  /* remove without freeing; NULL if absent */
void  jpush(jv *a, jv *val);
void  jarr_del(jv *a, size_t i);
int   jis(const jv *v, int t);
int   jstreq(const jv *v, const char *s);                             /* JN_STR with exactly this raw text */
double jnumv(const jv *v);                                            /* JN_NUM value, 0 otherwise */
int   jtruthy(const jv *v);
char *jstrdup_raw(const jv *v);                                       /* malloc'd raw copy of a string ("" if not a string) */
int   jequal(const jv *a, const jv *b);                               /* structural, numbers by value */
uint64_t jhash(const jv *v);                                          /* FNV-1a 64 of the canonical text (sorted keys) */
char *jcanon(const jv *v, size_t *len);                               /* sorted keys, compact */
