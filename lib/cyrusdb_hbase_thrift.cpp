#include <syslog.h>

#include <iostream>
#include <string>

#include <boost/lexical_cast.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include "Hbase.h"

// FIXME
// cyrusdb.h cannot be included into C++ source unless sanitized (using 'delete' as variable name)
// therefore pasting the part we need here
enum cyrusdb_ret {
    CYRUSDB_OK = 0,
    CYRUSDB_DONE = 1,
    CYRUSDB_IOERROR = -1,
    CYRUSDB_AGAIN = -2,
    CYRUSDB_EXISTS = -3,
    CYRUSDB_INTERNAL = -4,
    CYRUSDB_NOTFOUND = -5
};

typedef int foreach_p(void *rock,
              const char *key, int keylen,
              const char *data, int datalen);

typedef int foreach_cb(void *rock,
               const char *key, int keylen,
               const char *data, int datalen);
// end cyrusdb.h

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

using namespace apache::hadoop::hbase::thrift;

extern "C" {
    int hbase_init(const char *hostnames);
    int hbase_done();
    int hbase_open(const char *hostnames);
    int hbase_fetch(const char *in_fname,
                    const char *in_key, int keylen,
                    const char **out_data, int *datalen);
    int hbase_foreach(const char *in_fname,
                      char *in_prefix, int prefixlen,
                      foreach_p *goodp,
                      foreach_cb *cb, void *rock);
    int hbase_store(const char *in_fname,
                    const char *in_key, int keylen,
                    const char *in_data, int datalen);
}

static int dbinit = 0;
struct hbase_engine {
    boost::shared_ptr<TSocket>                socket;
    boost::shared_ptr<TTransport>         transport;
    boost::shared_ptr<TProtocol>            protocol;
    boost::shared_ptr<HbaseClient>        client;
} dbengine;

typedef std::vector<std::string> StrVec;
typedef std::vector<ColumnDescriptor> ColVec;

std::string fname_to_tablename(const std::string &fname)
{
    return fname.substr(fname.rfind('/')+1);
}

int hbase_init(const char *hostnames)
{
    syslog(LOG_DEBUG, "%s %s", __func__, hostnames);
    if (dbinit++) return 0;

    // TODO only one hostname, extend for more hostnames later
    std::string hostname(hostnames);
    int port = 9090;
    if ( hostname.find(":") != std::string::npos ) {
        port = boost::lexical_cast<int>(hostname.substr(hostname.find(":")+1));
        hostname.resize(hostname.find(":"));
    }
    syslog(LOG_DEBUG, "HBase: Connecting to %s:%d", hostname.c_str(), port);

    bool isFramed = false;
    dbengine.socket.reset(new TSocket(hostname, port));

    if (isFramed) {
        dbengine.transport.reset(new TFramedTransport(dbengine.socket));
    } else {
        dbengine.transport.reset(new TBufferedTransport(dbengine.socket));
    }
    dbengine.protocol.reset(new TBinaryProtocol(dbengine.transport));
    dbengine.client.reset(new HbaseClient(dbengine.protocol));

    try {
        dbengine.transport->open();
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return 1;
    }
    return 0;
}

int hbase_done()
{
    syslog(LOG_DEBUG, "%s", __func__);
    if (--dbinit) return 0;
    dbengine.client.reset();
    dbengine.protocol.reset();
    dbengine.transport->close();
    dbengine.transport.reset();
    dbengine.socket.reset();
    return 0;
}

int hbase_open(const char *in_fname)
{
    syslog(LOG_DEBUG, "%s %s", __func__, in_fname);
    // Scan all tables, look for the demo table and delete it.
    std::string fname(in_fname);
    fname = fname_to_tablename(fname);
    bool found = false;
    syslog(LOG_DEBUG, "HBase: Scanning tables...");
    StrVec tables;
    try {
        dbengine.client->getTableNames(tables);
        for (StrVec::const_iterator it = tables.begin(); it != tables.end(); ++it) {
            syslog(LOG_DEBUG, "HBase: Found %s", it->c_str());
            if (fname == *it) {
                found = true;
                break;
            }
        }
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return 1;
    }
    syslog(LOG_DEBUG, "HBase: ... done scanning tables");
    if (found) return 0;
    // Create the demo table with one column family (entry:)
    ColVec columns;
    columns.push_back(ColumnDescriptor());
    columns.back().name = "entry:";
    columns.back().maxVersions = 1;

    syslog(LOG_DEBUG, "HBase: Creating table %s", fname.c_str());
    try {
        dbengine.client->createTable(fname, columns);
    } catch (const AlreadyExists &ae) {
        syslog(LOG_ERR, "HBase: %s", ae.message.c_str());
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return 1;
    }
    return 0;
}

int hbase_fetch(const char *in_fname,
                const char *in_key, int keylen,
                const char **out_data, int *datalen)
{
    syslog(LOG_DEBUG, "%s %s", __func__, in_fname);
    std::string fname(in_fname);
    fname = fname_to_tablename(fname);
    std::string key(in_key, keylen);
    syslog(LOG_DEBUG, "%s %s", __func__, key.c_str());
    std::vector<TRowResult> rowResult;
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    try {
        dbengine.client->getRow(rowResult, fname, key, dummyAttributes);
        if (rowResult.size()!=1) return CYRUSDB_NOTFOUND;

        std::string &value = rowResult[0].columns.find("entry:data")->second.value;
        *out_data = (char *)malloc(value.size()); // FIXME leaks
        memcpy((char *)*out_data, value.data(), value.size());
        *datalen = value.size();
        syslog(LOG_DEBUG, "%s found", __func__);
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        *out_data = NULL;
        *datalen = 0;
        return CYRUSDB_INTERNAL;
    }
    return CYRUSDB_OK;
}

int hbase_foreach(const char *in_fname,
                  char *in_prefix, int prefixlen,
                  foreach_p *goodp,
                  foreach_cb *cb, void *rock)
{
    syslog(LOG_DEBUG, "%s %s", __func__, in_fname);
    std::string fname(in_fname);
    fname = fname_to_tablename(fname);
    std::string prefix(in_prefix, prefixlen);
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    StrVec columnNames;
    columnNames.push_back("entry:");
    try {
        int scanner = dbengine.client->scannerOpenWithPrefix(fname, prefix, columnNames, dummyAttributes);
        while (true) {
            std::vector<TRowResult> rowResult;
            dbengine.client->scannerGet(rowResult, scanner);
            if (rowResult.size() == 0)
                break;
            std::string &key = rowResult[0].row;
            std::string &value = rowResult[0].columns.find("entry:data")->second.value;
            syslog(LOG_DEBUG, "entry %s %s", key.c_str(), value.c_str());
            if (!goodp || goodp(rock, key.data(), key.size(), value.data(), value.size())) {
                int r = cb(rock, key.data(), key.size(), value.data(), value.size());
                if (r != 0) {
                    if (r < 0) {
                        syslog(LOG_ERR, "DBERROR: foreach cb() failed");
                    }
                    break;
                }
            }
        }
    } catch (const TException &tx) {
        syslog(LOG_ERR, "DBERROR: %s", tx.what());
        return CYRUSDB_INTERNAL;
    }
    return 0;
}

int hbase_store(const char *in_fname,
                const char *in_key, int keylen,
                const char *in_data, int datalen)
{
    syslog(LOG_DEBUG, "%s %s", __func__, in_fname);
    std::string fname(in_fname);
    fname = fname_to_tablename(fname);
    std::string key(in_key, keylen);
    const std::map<Text, Text>  dummyAttributes; // see HBASE-6806 HBASE-4658

    std::vector<Mutation> mutations;
    mutations.push_back(Mutation());
    mutations.back().column = "entry:data";
    if (in_data) {
        std::string data(in_data, datalen);
        mutations.back().value = data;
    } else {
        mutations.back().isDelete = true;
    }
    dbengine.client->mutateRow(fname, key, mutations, dummyAttributes);
    return 0;
}
