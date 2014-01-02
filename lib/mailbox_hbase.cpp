#include <syslog.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include <boost/lexical_cast.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include "Hbase.h"

// util.h
#define BIT32_MAX 4294967295U

#if UINT_MAX == BIT32_MAX
typedef unsigned int bit32;
#elif ULONG_MAX == BIT32_MAX
typedef unsigned long bit32;
#elif USHRT_MAX == BIT32_MAX
typedef unsigned short bit32;
#else
#error dont know what to use for bit32
#endif

struct buf {
    char *s;
    unsigned len;
    unsigned alloc;
    int flags;
};

#ifdef HAVE_LONG_LONG_INT
typedef unsigned long long int bit64;
typedef unsigned long long int modseq_t;
#define MODSEQ_FMT "%llu"
#define atomodseq_t(s) strtoull(s, NULL, 10)
#else
typedef unsigned long int modseq_t;
#define MODSEQ_FMT "%lu"
#define atomodseq_t(s) strtoul(s, NULL, 10)
#endif
// end util.h
// quota.h
#ifdef HAVE_LONG_LONG_INT
typedef unsigned long long int uquota_t;
typedef long long int quota_t;
#define UQUOTA_T_FMT     "%llu"
#define QUOTA_T_FMT      "%lld"
#define QUOTA_REPORT_FMT "%8llu"
#else
typedef unsigned long uquota_t;
typedef long quota_t;
#define UQUOTA_T_FMT     "%lu"
#define QUOTA_T_FMT      "%ld"
#define QUOTA_REPORT_FMT "%8lu"
#endif
// end quota.h
// mailbox.h
#define MAX_USER_FLAGS (16*8)

struct index_header {
    /* track if it's been changed */
    int dirty;

    /* header fields */
    bit32 generation_no;
    int format;
    int minor_version;
    uint32_t start_offset;
    uint32_t record_size;
    uint32_t num_records;
    time_t last_appenddate;
    uint32_t last_uid;
    uquota_t quota_mailbox_used;
    time_t pop3_last_login;
    uint32_t uidvalidity;

    uint32_t deleted;
    uint32_t answered;
    uint32_t flagged;

    uint32_t options;
    uint32_t leaked_cache_records;
    modseq_t highestmodseq;
    modseq_t deletedmodseq;
    uint32_t exists;
    time_t first_expunged;
    time_t last_repack_time;

    bit32 header_file_crc;
    bit32 sync_crc;

    uint32_t recentuid;
    time_t recenttime;

    uint32_t header_crc;
};

struct mailbox {
    int index_fd;
    int cache_fd;
    int lock_fd;
    int header_fd;

    const char *index_base;
    unsigned long index_len;	/* mapped size */
    struct buf cache_buf;
    unsigned long cache_len;	/* mapped size */

    int index_locktype; /* 0 = none, 1 = shared, 2 = exclusive */

    ino_t header_file_ino;
    bit32 header_file_crc;

    time_t index_mtime;
    ino_t index_ino;
    size_t index_size;
    int need_cache_refresh;

    /* Information in mailbox list */
    char *name;
    int mbtype;
    char *part;
    char *acl;

    struct index_header i;

    /* Information in header */
    char *uniqueid;
    char *quotaroot;
    char *flagname[MAX_USER_FLAGS];

    /* change management */
    int modseq_dirty;
    int header_dirty;
    int cache_dirty;
    int quota_dirty;
    int has_changed;
    time_t last_updated; /* for appends*/
    quota_t quota_previously_used; /* for quota change */
};
// end mailbox.h
// message_guid.h
#define MESSAGE_GUID_SIZE         (20)    /* Size of GUID byte sequence */

enum guid_status {
    GUID_UNKNOWN = -1, /* Unknown if GUID is [non-]NULL (not yet tested) */
    GUID_NULL =	    0, /* GUID is NULL */
    GUID_NONNULL =  1, /* GUID is non-NULL */
};

struct message_guid {
    enum guid_status status;
    unsigned char value[MESSAGE_GUID_SIZE];
};
// end message_guid.h
// message.h
struct ibuf {
    char *start, *end, *last;
};

struct body {
    /* Content-* header information */
    char *type;
    char *subtype;
    struct param *params;
    char *id;
    char *description;
    char *encoding;
    char *md5;
    char *disposition;
    struct param *disposition_params;
    struct param *language;
    char *location;

    /* Location/size information */
    long header_offset;
    long header_size;
    long header_lines;
    long content_offset;
    long content_size;
    long content_lines;
    long boundary_size;		/* Size of terminating boundary */
    long boundary_lines;

    int numparts;		/* For multipart types */
    struct body *subpart;	/* For message/rfc822 and multipart types */

    /*
     * Other header information.
     * Only meaningful for body-parts at top level or
     * enclosed in message/rfc-822
     */
    char *date;
    char *subject;
    struct address *from;
    struct address *sender;
    struct address *reply_to;
    struct address *to;
    struct address *cc;
    struct address *bcc;
    char *in_reply_to;
    char *message_id;
    char *received_date;

    /*
     * Cached headers.  Only filled in at top-level
     */
    struct ibuf cacheheaders;

    /*
     * decoded body.  Filled in as needed.
     */
    char *decoded_body;

    /* Message GUID. Only filled in at top level */
    struct message_guid guid;
};
// end message.h
// replacement for lib/map_nommap.c
void read_file(const char * from, char **base, unsigned long *len, const char *name)
{
    char *p;
    int n, left;
    struct stat sbuf;
    char buf[80];
    unsigned long newlen;
    int fd;

    syslog(LOG_DEBUG, "%s %s", __func__, from);
    fd = open(from, O_RDONLY, 0666);
    if (fd == -1) {
	syslog(LOG_ERR, "IOERROR: opening %s: %m", from);
	exit(-1);
    }

    if (fstat(fd, &sbuf) == -1) {
        syslog(LOG_ERR, "IOERROR: fstating %s file", name);
        exit(-1);
    }
    newlen = sbuf.st_size;

    /* Need a larger buffer */
    *len = newlen;
    *base = (char*)malloc(*len);

    lseek(fd, 0L, 0);
    left = newlen;
    p = (char*) *base;

    while (left) {
	n = read(fd, p, left);
	if (n <= 0) {
            syslog(LOG_ERR, "IOERROR: reading %s file", name);
	    exit(-1);
	}
	p += n;
	left -= n;
    }
    close(fd);
}
// end replacement for lib/map_nommap.c


using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

using namespace apache::hadoop::hbase::thrift;

extern "C" {
    int hbase_mailbox_open_index(const char *fname, struct mailbox *mailbox);
    int hbase_mailbox_index_read_header(struct mailbox *mailbox, unsigned char *buf, int size);
    int hbase_mailbox_index_read_record(struct mailbox *mailbox, uint32_t recno, unsigned char *buf, int size);
    int hbase_mailbox_index_update_header(struct mailbox *mailbox, unsigned char *buf, int size);
    int hbase_mailbox_index_append(struct mailbox *mailbox, uint32_t uid, unsigned char *buf, int size);
    int hbase_mailbox_index_wipe_record(struct mailbox *mailbox, uint32_t uid);
    int hbase_mailbox_copyfile(struct mailbox *mailbox, const char *from, uint32_t uid, struct body **body);
    int hbase_mailbox_readfile(struct mailbox *mailbox, uint32_t uid, unsigned char **buf, int *size);
    int hbase_mailbox_open_cache(const char *fname, struct mailbox *mailbox);
    int hbase_mailbox_cache_read_record(struct mailbox *mailbox, uint32_t recno, char **buf, unsigned *len);
    int hbase_mailbox_cache_update_header(struct mailbox *mailbox, unsigned char *buf, int size);
    int hbase_mailbox_cache_append(struct mailbox *mailbox, uint32_t uid, unsigned char *buf, int size);
    int hbase_mailbox_header_read(struct mailbox *mailbox, unsigned char **buf, int *len);
    int hbase_mailbox_header_update(struct mailbox *mailbox, unsigned char *buf, int size);
}

typedef std::vector<std::string> StrVec;
typedef std::vector<ColumnDescriptor> ColVec;

static const std::string TABLE("message_store");
static const std::string CF_MESSAGES("messages:");
static const std::string CF_INDEX("index:");
static const std::string CF_CACHE("cache:");
static const std::string CF_HEADER("header:");

static const std::string SEPARATOR("\t");

// from cyrusdb_hbase_thrift.cpp
extern struct hbase_engine {
    boost::shared_ptr<TSocket>                socket;
    boost::shared_ptr<TTransport>         transport;
    boost::shared_ptr<TProtocol>            protocol;
    boost::shared_ptr<HbaseClient>        client;
} dbengine;

static void ensure_table_exists()
{
    // creates the message store tabel to make developers life easier
    // FIXME remove this function from production code - table must be created and configured during deployment, not at runtime
    bool found = false;
    StrVec tables;
    try {
        dbengine.client->getTableNames(tables);
        for (StrVec::const_iterator it = tables.begin(); it != tables.end(); ++it) {
            syslog(LOG_DEBUG, "HBase: Found %s", it->c_str());
            if (TABLE == *it) {
                found = true;
                break;
            }
        }
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        assert(0);
    }
    if (found) return;

    // Create the table with column families:
    // - messages:
    // - index:
    // - cache:
    // - header
    ColVec columns;
    columns.push_back(ColumnDescriptor());
    columns.back().name = CF_MESSAGES;
    columns.back().maxVersions = 1;
    columns.back().compression = "GZ";
    columns.push_back(ColumnDescriptor());
    columns.back().name = CF_INDEX;
    columns.back().maxVersions = 1;
    columns.push_back(ColumnDescriptor());
    columns.back().name = CF_CACHE;
    columns.back().maxVersions = 1;
    columns.push_back(ColumnDescriptor());
    columns.back().name = CF_HEADER;
    columns.back().maxVersions = 1;

    syslog(LOG_DEBUG, "HBase: Creating table %s", TABLE.c_str());
    try {
        dbengine.client->createTable(TABLE, columns);
    } catch (const AlreadyExists &ae) {
        syslog(LOG_ERR, "HBase: %s", ae.message.c_str());
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        assert(0);
    }
}

int hbase_mailbox_open_index(const char *fname, struct mailbox *mailbox)
{
    syslog(LOG_DEBUG, "%s: %s", __func__, fname);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);
    ensure_table_exists();

    // nothing to do ...
    return 0;
}

int hbase_mailbox_index_read_header(struct mailbox *mailbox, unsigned char *buf, int size)
{
    syslog(LOG_DEBUG, "%s", __func__);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + "header";
    std::vector<TRowResult> rowResult;
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    try {
        dbengine.client->getRow(rowResult, TABLE, key, dummyAttributes);
        if (rowResult.size()!=1) return -1;

        std::string &value = rowResult[0].columns.find(CF_INDEX + "data")->second.value;
        memcpy(buf, value.data(), size);
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return -1;
    }
    return 0;
}

int hbase_mailbox_index_read_record(struct mailbox *mailbox, uint32_t recno, unsigned char *buf, int size)
{
    syslog(LOG_DEBUG, "%s", __func__);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + boost::lexical_cast<std::string>(recno);
    std::vector<TRowResult> rowResult;
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    try {
        dbengine.client->getRow(rowResult, TABLE, key, dummyAttributes);
        if (rowResult.size()!=1) return -1;

        std::string &value = rowResult[0].columns.find(CF_INDEX + "data")->second.value;
        memcpy(buf, value.data(), size);
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return -1;
    }
    return 0;
}

int hbase_mailbox_index_update_header(struct mailbox *mailbox, unsigned char *buf, int size)
{
    syslog(LOG_DEBUG, "%s", __func__);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + "header";
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    std::vector<Mutation> mutations;
    mutations.push_back(Mutation());
    mutations.back().column = CF_INDEX + "data";
    std::string data((char*)buf, size);
    mutations.back().value = data;
    dbengine.client->mutateRow(TABLE, key, mutations, dummyAttributes);
    return 0;
}

int hbase_mailbox_index_append(struct mailbox *mailbox, uint32_t uid, unsigned char *buf, int size)
{
    syslog(LOG_DEBUG, "%s %d", __func__, uid);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + boost::lexical_cast<std::string>(uid);
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    std::vector<Mutation> mutations;
    mutations.push_back(Mutation());
    mutations.back().column = CF_INDEX + "data";
    std::string data((char*)buf, size);
    mutations.back().value = data;
    dbengine.client->mutateRow(TABLE, key, mutations, dummyAttributes);
    return 0;
}

int hbase_mailbox_index_wipe_record(struct mailbox *mailbox, uint32_t uid)
{
    syslog(LOG_DEBUG, "%s %d", __func__, uid);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + boost::lexical_cast<std::string>(uid);
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    std::vector<Mutation> mutations;
    mutations.push_back(Mutation());
    mutations.back().column = CF_INDEX + "data";
    mutations.back().isDelete = true;
    dbengine.client->mutateRow(TABLE, key, mutations, dummyAttributes);
    return 0;
}

extern "C" int message_parse_mapped(const char *msg_base, unsigned long msg_len, struct body *body); // message.c
int hbase_mailbox_copyfile(struct mailbox *mailbox, const char *from, uint32_t uid, struct body **body)
{
    syslog(LOG_DEBUG, "%s %d %s", __func__, uid, from);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + boost::lexical_cast<std::string>(uid);
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    char *src_base = 0;
    unsigned long src_size = 0;
    read_file(from, &src_base, &src_size, from);

    std::vector<Mutation> mutations;
    mutations.push_back(Mutation());
    mutations.back().column = CF_MESSAGES + "data";
    std::string data((char*)src_base, src_size);
    mutations.back().value = data;
    dbengine.client->mutateRow(TABLE, key, mutations, dummyAttributes);
    if (body) {
        message_parse_mapped(src_base, src_size, *body);
    }
    free(src_base);
    return 0;
}

int hbase_mailbox_readfile(struct mailbox *mailbox, uint32_t uid, unsigned char **buf, int *size)
{
    syslog(LOG_DEBUG, "%s %d", __func__, uid);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + boost::lexical_cast<std::string>(uid);
    std::vector<TRowResult> rowResult;
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    try {
        dbengine.client->getRow(rowResult, TABLE, key, dummyAttributes);
        if (rowResult.size()!=1) return -1;

        std::string &value = rowResult[0].columns.find(CF_MESSAGES + "data")->second.value;
        *size = value.size();
        *buf = (unsigned char*)malloc(*size);
        memcpy(*buf, value.data(), *size);
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return -1;
    }
    return 0;
}

int hbase_mailbox_open_cache(const char *fname, struct mailbox *mailbox)
{
    syslog(LOG_DEBUG, "%s: %s", __func__, fname);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    // nothing to do
}

int hbase_mailbox_cache_read_record(struct mailbox *mailbox, uint32_t uid, char **buf, unsigned *len)
{
    syslog(LOG_DEBUG, "%s", __func__);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + boost::lexical_cast<std::string>(uid);
    std::vector<TRowResult> rowResult;
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    try {
        dbengine.client->getRow(rowResult, TABLE, key, dummyAttributes);
        if (rowResult.size()!=1) return -1;

        std::string &value = rowResult[0].columns.find(CF_CACHE + "data")->second.value;
        if (buf && len) {
            *len = value.size();
            *buf = (char*)malloc(*len); // FIXME leaks
            memcpy(*buf, value.data(), *len);
        }
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return -1;
    }
    return 0;
}

int hbase_mailbox_cache_update_header(struct mailbox *mailbox, unsigned char *buf, int size)
{
    syslog(LOG_DEBUG, "%s", __func__);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + "header";
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    std::vector<Mutation> mutations;
    mutations.push_back(Mutation());
    mutations.back().column = CF_CACHE + "data";
    std::string data((char*)buf, size);
    mutations.back().value = data;
    dbengine.client->mutateRow(TABLE, key, mutations, dummyAttributes);
    return 0;
}

int hbase_mailbox_cache_append(struct mailbox *mailbox, uint32_t uid, unsigned char *buf, int size)
{
    syslog(LOG_DEBUG, "%s %d", __func__, uid);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    key += SEPARATOR + boost::lexical_cast<std::string>(uid);
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    std::vector<Mutation> mutations;
    mutations.push_back(Mutation());
    mutations.back().column = CF_CACHE + "data";
    std::string data((char*)buf, size);
    mutations.back().value = data;
    dbengine.client->mutateRow(TABLE, key, mutations, dummyAttributes);
    return 0;
}

int hbase_mailbox_header_read(struct mailbox *mailbox, unsigned char **buf, int *len)
{
    syslog(LOG_DEBUG, "%s", __func__);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    std::vector<TRowResult> rowResult;
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    try {
        dbengine.client->getRow(rowResult, TABLE, key, dummyAttributes);
        if (rowResult.size()!=1) return -1;

        std::string &value = rowResult[0].columns.find(CF_HEADER + "data")->second.value;
        if (buf && len) {
            *len = value.size();
            *buf = (unsigned char*)malloc(*len);
            memcpy(*buf, value.data(), *len);
        }
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return -1;
    }
    return 0;
}

int hbase_mailbox_header_update(struct mailbox *mailbox, unsigned char *buf, int size)
{
    syslog(LOG_DEBUG, "%s", __func__);
    syslog(LOG_DEBUG, "index_base: %s", mailbox->index_base);
    syslog(LOG_DEBUG, "name: %s", mailbox->name);
    syslog(LOG_DEBUG, "part: %s", mailbox->part);
    syslog(LOG_DEBUG, "acl: %s", mailbox->acl);
    syslog(LOG_DEBUG, "uniqueid: %s", mailbox->uniqueid);
    syslog(LOG_DEBUG, "quotaroot: %s", mailbox->quotaroot);
    syslog(LOG_DEBUG, "flagname: %s", mailbox->flagname);

    std::string key(mailbox->name);
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    std::vector<Mutation> mutations;
    mutations.push_back(Mutation());
    mutations.back().column = CF_HEADER + "data";
    std::string data((char*)buf, size);
    mutations.back().value = data;
    dbengine.client->mutateRow(TABLE, key, mutations, dummyAttributes);
    return 0;
}
