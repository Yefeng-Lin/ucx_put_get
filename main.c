#include "mpi_ops.h"
#include "main.h"

static inline void barrier()
{
    MPI_Barrier(MPI_COMM_WORLD);
}

/* 
 * This will exchange networking information with all other PEs and 
 * register an allocated buffer with the local NIC. Will create endpoints 
 * if they are not already created. 
 */
int reg_buffer(void * buffer, size_t length)
{
    int        i = 0;
    int    error = 0;
    void  **pack = NULL;
    ucs_status_t status;
    ucp_mem_map_params_t mem_map_params;

    rkeys = (ucp_rkey_h *) malloc(sizeof(ucp_rkey_h) * size);
    if (NULL == rkeys) {
        error = ERR_NO_MEMORY;
        goto fail;
    }
    
    remote_addresses = (uint64_t *) malloc(sizeof(uint64_t) * size);
    if (NULL == remote_addresses) {
        error = ERR_NO_MEMORY;
        goto fail_endpoints;
    }
    
    mem_map_params.address    = buffer;
    mem_map_params.length     = length;
    mem_map_params.field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS
                              | UCP_MEM_MAP_PARAM_FIELD_LENGTH;
    status = ucp_mem_map(ucp_context, 
                        &mem_map_params, 
                        &register_buffer);
    if (UCS_OK != status) {
        error = -1;
        goto fail_full;
    }

    error = mpi_buffer_exchange(buffer,
                                &pack,
                                remote_addresses,
                                &register_buffer);
    if (OK != error) {
        goto fail_full;
    }

    /* unpack keys into rkey array */
    for (i = 0; i < size; i++) {
        int rkey_error;

        rkey_error = ucp_ep_rkey_unpack(endpoints[i], 
                                        pack[i], 
                                        &rkeys[i]);
        if (UCS_OK != rkey_error) {
            error = -1;
            goto fail_full;
        }

        ucp_rkey_buffer_release(pack[i]); 
        pack[i] = NULL;
    }

    // NOTE: it's OK to keep pack if going to unpack on other endpoints later
    free(pack);

    return OK;

fail_full:
    free(remote_addresses);
fail_endpoints:
    free(endpoints);
fail:
    free(rkeys);

    register_buffer  = NULL;
    rkeys            = NULL;
    remote_addresses = NULL;

    return error;
}

/*
 * This function creates the ucp endpoints used for communication.
 * This leverages MPI to perform the data exchange.
 */
static inline int create_ucp_endpoints(void)
{
    int error = 0;
    void ** worker_addresses = NULL;
    ucp_ep_params_t ep_params;
    int i;
    
    endpoints = (ucp_ep_h *) malloc(size * sizeof(ucp_ep_h));
    if (NULL == endpoints) {
        return ERR_NO_MEMORY;
    }
    
    error = mpi_worker_exchange(&worker_addresses);
    if (OK != error) {
        free(endpoints);
        return -1;
    }
    
    for (i = 0; i < size; i++) {
        ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
        ep_params.address = (ucp_address_t *) worker_addresses[i];
        error = ucp_ep_create(ucp_worker,
                              &ep_params,
                              &endpoints[i]);
        if (UCS_OK != error) {
            free(endpoints);
            return -1;
        }
        free(worker_addresses[i]);
    }
    free(worker_addresses);
     
    return OK;
}

int comm_init()
{
    ucp_params_t ucp_params;
    ucp_config_t * config;
    ucs_status_t status;
    int error = 0;
    ucp_worker_params_t worker_params; 

    status = ucp_config_read(NULL, NULL, &config); // 1 step
    if (status != UCS_OK) {
        return -1;
    }

    ucp_params.features   = UCP_FEATURE_RMA | UCP_FEATURE_AMO64 | UCP_FEATURE_AMO32;
    ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;

    status = ucp_init(&ucp_params, config, &ucp_context); // 2 step
    if (status != UCS_OK) {
        return -1;
    }

    ucp_config_release(config);
    worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;
    worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    status = ucp_worker_create(ucp_context, 
                               &worker_params, 
                               &ucp_worker); // 3 step
    if (status != UCS_OK) {
        return -1;
    } 

    /* initialize communication channel for exchanges */
    init_mpi();

    /* create our endpoints here */
    error = create_ucp_endpoints(); // 4 step
    if (error != OK) {
        return -1;
    } 

    return 0;
}

int comm_finalize()
{
    barrier();
    ucp_request_param_t req_param = {0};
    ucs_status_ptr_t req;

    req = ucp_worker_flush_nbx(ucp_worker, &req_param);
    if (UCS_OK != req) {
        if (UCS_PTR_IS_ERR(req)) {
            abort();
        } else {
            while (ucp_request_check_status(req) == UCS_INPROGRESS) {
                ucp_worker_progress(ucp_worker);
            }
            ucp_request_free(req);
        }
    }

    for (int i = 0; i < size; i++) {
        if (rkeys[i]) {
            ucp_rkey_destroy(rkeys[i]);
        }

        if (endpoints[i]) {
            ucp_ep_destroy(endpoints[i]);
        }
    }

    free(remote_addresses);
    free(endpoints);
    ucp_mem_unmap(ucp_context, register_buffer);
    ucp_worker_destroy(ucp_worker);
    ucp_cleanup(ucp_context);

    finalize_mpi();
    return 0;
}

int parse_cmd(int argc, char* argv[])
{
    int c = 0;
    while((c = getopt(argc, argv, "o:")) != -1){
        switch (c){
            case 'o':
                if(strcmp(optarg,"put")==0) {
                    ucx_opt = PUT;
                }else if(strcmp(optarg,"get")==0){
                    ucx_opt = GET;
                }else{
                    printf("ERROR argc\n");
                    return 1;
                }
                break;
            default:
                printf("setting ucx option\n");
                return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) 
{
    void * mybuff;
    char * shared_ptr;
    char * sdata;
    char * rdata;

    {
        volatile int i = 0;
        char hostname[256];
        gethostname(hostname, sizeof(hostname));
        printf("PID %d on %s ready for attach\n", getpid(), hostname);
        fflush(stdout);
        while (0 == i)
            sleep(5);
    }

    ucp_request_param_t req_param = {0};
    ucs_status_ptr_t ucp_status;
    ucs_status_ptr_t flush_req;
    
    /* initialize the runtime and communication components */
    comm_init();
    mybuff = malloc(HUGEPAGE);
    sdata = (char *)malloc(HUGEPAGE);
    rdata = (char *)malloc(HUGEPAGE);
    
    barrier();

    /* register memory  */
    reg_buffer(mybuff, HUGEPAGE);
    parse_cmd(argc, argv);
    if(ucx_opt == PUT)
    {
        shared_ptr = (char *)sdata;
        char *tmp1 = "I'm rank 0, put this message to rank 1";
        char *tmp2 = "I'm rank 1, put this message to rank 0";
        if (my_pe == 0) {
            for (int i = 0; i < HUGEPAGE; i++) {
                strcpy(shared_ptr,tmp1);
            }
        }else{
            for (int i = 0; i < HUGEPAGE; i++) {
                strcpy(shared_ptr,tmp2);
            }
        }
        barrier();
        if (my_pe == 0) {
                ucp_status = ucp_put_nbx(endpoints[1], sdata, HUGEPAGE, remote_addresses[1], rkeys[1], &req_param);
        } else {
                ucp_status = ucp_put_nbx(endpoints[0], sdata, HUGEPAGE, remote_addresses[0], rkeys[0], &req_param);
        }
        if (UCS_OK != ucp_status) {
            flush_req = ucp_worker_flush_nbx(ucp_worker, &req_param);
            if (UCS_OK != flush_req) {
                if (UCS_PTR_IS_ERR(flush_req)) {
                    abort();
                } else {
                    while (UCS_INPROGRESS == ucp_request_check_status(flush_req)) {
                        ucp_worker_progress(ucp_worker);
                    }
                    ucp_request_free(flush_req);
                }
            }
            ucp_request_free(ucp_status);
        }
        printf("rank %d : remote process put string - %s\n",my_pe,(char*)mybuff);
    }else if(ucx_opt == GET)
    {
        shared_ptr = (char *)mybuff;
        char *tmp1 = "This rank 0 buf";
        char *tmp2 = "this rank 1 buf";
        if (my_pe == 0) {
            for (int i = 0; i < HUGEPAGE; i++) {
                strcpy(shared_ptr,tmp1);
            }
        }else{
            for (int i = 0; i < HUGEPAGE; i++) {
                strcpy(shared_ptr,tmp2);
            }
        }
        barrier();
        if (my_pe == 0) {
                ucp_status = ucp_get_nbx(endpoints[1], rdata, HUGEPAGE, remote_addresses[1], rkeys[1], &req_param);
        } else {
                ucp_status = ucp_get_nbx(endpoints[0], rdata, HUGEPAGE, remote_addresses[0], rkeys[0], &req_param);
        }
        if (UCS_OK != ucp_status) {
            flush_req = ucp_worker_flush_nbx(ucp_worker, &req_param);
            if (UCS_OK != flush_req) {
                if (UCS_PTR_IS_ERR(flush_req)) {
                    abort();
                } else {
                    while (UCS_INPROGRESS == ucp_request_check_status(flush_req)) {
                        ucp_worker_progress(ucp_worker);
                    }
                    ucp_request_free(flush_req);
                }
            }
            ucp_request_free(ucp_status);
        }
        printf("rank %d : get string from remote - %s\n",my_pe,rdata);
    }
    comm_finalize();
    free(sdata);
    free(rdata);
    free(mybuff);
    return 0;
}
