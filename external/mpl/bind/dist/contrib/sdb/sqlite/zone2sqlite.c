/*	$NetBSD: zone2sqlite.c,v 1.3 2019/01/09 16:55:10 christos Exp $	*/

/*
 * Copyright (C) 2007, 2016  Internet Systems Consortium, Inc. ("ISC")
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdlib.h>
#include <string.h>

#include <isc/buffer.h>
#include <isc/mem.h>
#include <isc/print.h>
#include <isc/result.h>

#include <dns/db.h>
#include <dns/dbiterator.h>
#include <dns/fixedname.h>
#include <isc/hash.h>
#include <dns/name.h>
#include <dns/rdata.h>
#include <dns/rdataset.h>
#include <dns/rdatasetiter.h>
#include <dns/rdatatype.h>
#include <dns/result.h>

#include <sqlite3.h>

#ifndef UNUSED
#define UNUSED(x)  (void)&(x)
#endif

/*
 * Generate an SQLite table from a zone.
 */

typedef struct _dbinfo {
    sqlite3 *db;
    char *filename;
    char *table;
} dbinfo_t;

dbinfo_t dbi = { NULL, NULL, NULL };


static void
closeandexit(int status)
{
    if (dbi.db) {
	sqlite3_close(dbi.db);
	dbi.db = NULL;
    }
    exit(status);
}

static void
check_result(isc_result_t result, const char *message)
{
    if (result != ISC_R_SUCCESS) {
	fprintf(stderr, "%s: %s\n", message,
		isc_result_totext(result));
	closeandexit(1);
    }
}

static isc_result_t
db_connect(dbinfo_t *dbi)
{
    if (sqlite3_open(dbi->filename, &dbi->db) == SQLITE_OK) {
	return (ISC_R_SUCCESS);
    } else {
	/* a connection is returned even if the open fails */
	sqlite3_close(dbi->db);
	dbi->db = NULL;
	return (ISC_R_FAILURE);
    }
}

static int
add_rdata_cb(void *parm, int cc, char **cv, char **cn)
{
    UNUSED(parm);
    UNUSED(cc);
    UNUSED(cv);
    UNUSED(cn);

    return 0;
}


static void
addrdata(dns_name_t *name, dns_ttl_t ttl, dns_rdata_t *rdata)
{
    unsigned char namearray[DNS_NAME_MAXTEXT + 1];
    unsigned char typearray[20];
    unsigned char dataarray[2048];
    isc_buffer_t b;
    isc_result_t result;
    char *sql;
    char *errmsg = NULL;
    int res;
    
    isc_buffer_init(&b, namearray, sizeof(namearray) - 1);
    result = dns_name_totext(name, true, &b);
    check_result(result, "dns_name_totext");
    namearray[isc_buffer_usedlength(&b)] = 0;
    
    isc_buffer_init(&b, typearray, sizeof(typearray) - 1);
    result = dns_rdatatype_totext(rdata->type, &b);
    check_result(result, "dns_rdatatype_totext");
    typearray[isc_buffer_usedlength(&b)] = 0;
    
    isc_buffer_init(&b, dataarray, sizeof(dataarray) - 1);
    result = dns_rdata_totext(rdata, NULL, &b);
    check_result(result, "dns_rdata_totext");
    dataarray[isc_buffer_usedlength(&b)] = 0;
    
    sql = sqlite3_mprintf(
	"INSERT INTO %Q (NAME, TTL, RDTYPE, RDATA)"
	" VALUES ('%q', %d, '%q', '%q') ",
	dbi.table,
	namearray, ttl, typearray, dataarray);
    printf("%s\n", sql);
    res = sqlite3_exec(dbi.db, sql, add_rdata_cb, NULL, &errmsg);
    sqlite3_free(sql);

    if (res != SQLITE_OK) {
	fprintf(stderr, "INSERT failed: %s\n", errmsg);
	closeandexit(1);
    }
}

int
main(int argc, char *argv[])
{
    char *sql;
    int res;
    char *errmsg = NULL;
    char *porigin, *zonefile;
    dns_fixedname_t forigin, fname;
    dns_name_t *origin, *name;
    dns_db_t *db = NULL;
    dns_dbiterator_t *dbiter;
    dns_dbnode_t *node;
    dns_rdatasetiter_t *rdsiter;
    dns_rdataset_t rdataset;
    dns_rdata_t rdata = DNS_RDATA_INIT;
    isc_mem_t *mctx = NULL;
    isc_buffer_t b;
    isc_result_t result;

    if (argc != 5) {
	printf("usage: %s <zone> <zonefile> <dbfile> <dbtable>\n", argv[0]);
	exit(1);
    }
    
    porigin  = argv[1];
    zonefile = argv[2];

    dbi.filename = argv[3];
    dbi.table    = argv[4];
    
    dns_result_register();
    
    result = isc_mem_create(0, 0, &mctx);
    check_result(result, "isc_mem_create");
    
    isc_buffer_init(&b, porigin, strlen(porigin));
    isc_buffer_add(&b, strlen(porigin));
    origin = dns_fixedname_initname(&forigin);
    result = dns_name_fromtext(origin, &b, dns_rootname, 0, NULL);
    check_result(result, "dns_name_fromtext");
    
    db = NULL;
    result = dns_db_create(mctx, "rbt", origin, dns_dbtype_zone,
			   dns_rdataclass_in, 0, NULL, &db);
    check_result(result, "dns_db_create");
    
    result = dns_db_load(db, zonefile, dns_masterformat_text, 0);
    if (result == DNS_R_SEENINCLUDE)
	result = ISC_R_SUCCESS;
    check_result(result, "dns_db_load");

    printf("Connecting to '%s'\n", dbi.filename);
    
    if ((result = db_connect(&dbi)) != ISC_R_SUCCESS) {
	fprintf(stderr, "Connection to database '%s' failed\n",
		dbi.filename);
	closeandexit(1);
    }
    
    sql = sqlite3_mprintf("DROP TABLE %Q ", dbi.table);
    printf("%s\n", sql);
    res = sqlite3_exec(dbi.db, sql, NULL, NULL, &errmsg);
    sqlite3_free(sql);
#if 0
    if (res != SQLITE_OK) {
	fprintf(stderr, "DROP TABLE %s failed: %s\n",
		dbi.table, errmsg);
    }
#endif

#if 0    
    sql = sqlite3_mprintf(sql, "BEGIN TRANSACTION");
    printf("%s\n", sql);
    res = sqlite3_exec(dbi.db, sql, NULL, NULL, &errmsg);
    sqlite3_free(sql);
    if (res != SQLITE_OK) {
	fprintf(stderr, "BEGIN TRANSACTION failed: %s\n", errmsg);
	closeandexit(1);
    }
#endif
    
    sql = sqlite3_mprintf(
	"CREATE TABLE %Q "
	"(NAME TEXT, TTL INTEGER, RDTYPE TEXT, RDATA TEXT) ",
	dbi.table);
    printf("%s\n", sql);
    res = sqlite3_exec(dbi.db, sql, NULL, NULL, &errmsg);
    sqlite3_free(sql);
    if (res != SQLITE_OK) {
	fprintf(stderr, "CREATE TABLE %s failed: %s\n",
		dbi.table, errmsg);
	closeandexit(1);
    }
    
    dbiter = NULL;
    result = dns_db_createiterator(db, 0, &dbiter);
    check_result(result, "dns_db_createiterator()");
    
    result = dns_dbiterator_first(dbiter);
    check_result(result, "dns_dbiterator_first");
    
    name = dns_fixedname_initname(&fname);
    dns_rdataset_init(&rdataset);
    dns_rdata_init(&rdata);
    
    while (result == ISC_R_SUCCESS) {
	node = NULL;
	result = dns_dbiterator_current(dbiter, &node, name);
	if (result == ISC_R_NOMORE)
	    break;
	check_result(result, "dns_dbiterator_current");
	
	rdsiter = NULL;
	result = dns_db_allrdatasets(db, node, NULL, 0, &rdsiter);
	check_result(result, "dns_db_allrdatasets");

	result = dns_rdatasetiter_first(rdsiter);

	while (result == ISC_R_SUCCESS) {
	    dns_rdatasetiter_current(rdsiter, &rdataset);
	    result = dns_rdataset_first(&rdataset);
	    check_result(result, "dns_rdataset_first");
	    while (result == ISC_R_SUCCESS) {
		dns_rdataset_current(&rdataset, &rdata);
		addrdata(name, rdataset.ttl, &rdata);
		dns_rdata_reset(&rdata);
		result = dns_rdataset_next(&rdataset);
	    }
	    dns_rdataset_disassociate(&rdataset);
	    result = dns_rdatasetiter_next(rdsiter);
	}
	dns_rdatasetiter_destroy(&rdsiter);
	dns_db_detachnode(db, &node);
	result = dns_dbiterator_next(dbiter);
    }

#if 0
    sql = sqlite3_mprintf(sql, "COMMIT TRANSACTION ");
    printf("%s\n", sql);
    res = sqlite3_exec(dbi.db, sql, NULL, NULL, &errmsg);
    sqlite3_free(sql);
    if (res != SQLITE_OK) {
	fprintf(stderr, "COMMIT TRANSACTION failed: %s\n", errmsg);
	closeandexit(1);
    }
#endif
    
    dns_dbiterator_destroy(&dbiter);
    dns_db_detach(&db);
    isc_mem_destroy(&mctx);

    closeandexit(0);

    exit(0);
}
