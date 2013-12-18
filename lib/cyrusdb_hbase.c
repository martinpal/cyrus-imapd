/*  cyrusdb_hbase: hbase backend for mailboxes.db
 */

#include <config.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
/* TODO clean up includes */
#include "assert.h"
#include "cyrusdb.h"
#include "exitcodes.h"
#include "map.h"
#include "bsearch.h"
#include "cyr_lock.h"
#include "retry.h"
#include "util.h"
#include "xmalloc.h"
#include "xstrlcpy.h"
#include "xstrlcat.h"
#include "libcyr_cfg.h"

struct db {
    char *fname;
};

struct txn {
    char *fname;
    int fd;
};

static int abort_txn(struct db *db, struct txn *tid)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int init(const char *dbdir __attribute__((unused)),
        int myflags __attribute__((unused)))
{
    syslog(LOG_DEBUG, "%s", __func__);
    return hbase_init(libcyrus_config_getstring(CYRUSOPT_HBASE_HOSTNAMES));
}

static int done(void)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return hbase_done();
}

static int mysync(void)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int myarchive(const char **fnames, const char *dirname)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int myopen(const char *fname, int flags, struct db **ret)
{
    syslog(LOG_DEBUG, "%s", __func__);
    *ret = (struct db *) xzmalloc(sizeof(struct db));
    (*ret)->fname = xstrdup(fname);
    return hbase_open(fname);
}

static int myclose(struct db *db)
{
    syslog(LOG_DEBUG, "%s", __func__);
    free(db->fname);
    free(db);
    return 0;
}

static int fetch(struct db *mydb,
         const char *key, int keylen,
         const char **data, int *datalen,
         struct txn **mytid)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int fetchlock(struct db *db,
             const char *key, int keylen,
             const char **data, int *datalen,
             struct txn **mytid)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int foreach(struct db *db,
           char *prefix, int prefixlen,
           foreach_p *goodp,
           foreach_cb *cb, void *rock,
           struct txn **mytid)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int create(struct db *db,
          const char *key, int keylen,
          const char *data, int datalen,
          struct txn **tid)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int store(struct db *db,
         const char *key, int keylen,
         const char *data, int datalen,
         struct txn **tid)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int delete(struct db *db,
          const char *key, int keylen,
          struct txn **mytid, int force __attribute__((unused)))
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

static int commit_txn(struct db *db, struct txn *tid)
{
    syslog(LOG_DEBUG, "%s", __func__);
    return 0;
}

struct cyrusdb_backend cyrusdb_hbase =
{
    "hbase",            /* name */

    &init,
    &done,
    &mysync,
    &myarchive,

    &myopen,
    &myclose,

    &fetch,
    &fetchlock,
    &foreach,
    &create,
    &store,
    &delete,

    &commit_txn,
    &abort_txn,

    NULL,
    NULL
};
