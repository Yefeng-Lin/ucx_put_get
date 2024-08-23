#ifndef MPI_OPS_H
#define MPI_OPS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ucp/api/ucp.h>
#include <mpi.h>

// 4 kB page
#define PAGESIZE   (1<<12)
// 2 MB page
#define HUGEPAGE   (1<<21) 

struct data_exchange {
    size_t pack_size;
    uint64_t remote;
};

extern ucp_context_h ucp_context;
extern ucp_worker_h ucp_worker;
extern ucp_ep_h * endpoints;
extern ucp_rkey_h * rkeys;
extern ucp_mem_h register_buffer;
extern uint64_t * remote_addresses;

extern int my_pe;
extern int size;

typedef enum {
    OK                         =   0,

    /* Failure codes */
    ERR_NO_MEMORY              =  -1,
    ERR_BIND_MEM_ERROR         =  -2,
    ERR_NOT_IMPLEMENTED        =  -3,
    ERR_UNSUPPORTED            =  -4,
    ERR_INVALID_FREE_ATTEMPT   =  -5,

    ERR_LAST                   = -100
} errors_t ;

int init_mpi(void);
int finalize_mpi(void);

void create_mpi_datatype(void);

int mpi_buffer_exchange(void * buffer,
                        void *** pack_param,
                        uint64_t * remotes,
                        void * register_buffer);

int mpi_worker_exchange(void *** param_worker_addrs);

#endif