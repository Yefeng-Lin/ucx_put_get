#ifndef MYTEST_H
#define MYTEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <mpi.h>
#include <ucp/api/ucp.h>

ucp_context_h ucp_context;
ucp_worker_h  ucp_worker;
ucp_ep_h     *endpoints;
ucp_rkey_h   *rkeys;
ucp_mem_h     register_buffer;
uint64_t     *remote_addresses;
int           my_pe;
int           size;

enum ucx_pg_t{
        PUT,
        GET
};

enum ucx_pg_t ucx_opt;

#endif