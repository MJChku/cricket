/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h> //unlink()
#include <signal.h> //sigaction

#include <cuda_runtime_api.h>
#include <cuda.h>

#include "cd_rpc_prot.h"

#include "cd_common.h"



extern void rpc_cd_prog_1(struct svc_req *rqstp, register SVCXPRT *transp);

void int_handler(int signal) {
    unlink(CD_SOCKET_PATH);
    printf("have a nice day!\n");
    exit(0);
}

bool_t printmessage_1_svc(char *argp, int *result, struct svc_req *rqstp)
{
    printf("string: \"%s\"\n", argp);
    *result = 42;
    return 1;
}

bool_t cuda_malloc_1_svc(size_t argp, ptr_result *result, struct svc_req *rqstp)
{
    printf("cudaMalloc\n");
    result->err = cudaMalloc((void**)&result->ptr_result_u.ptr, argp);
    return 1;
}

bool_t cuda_memcpy_htod_1_svc(uint64_t ptr, mem_data mem, size_t size, int *result, struct svc_req *rqstp)
{
    printf("cudaMemcpyHtoD\n");
    if (size != mem.mem_data_len) {
        fprintf(stderr, "data size mismatch\n");
        return 0;
    }
    *result = cudaMemcpy((void*)ptr, mem.mem_data_val, size, cudaMemcpyHostToDevice);
    return 1;
}

bool_t cuda_memcpy_dtoh_1_svc(uint64_t ptr, size_t size, mem_result *result, struct svc_req *rqstp)
{
    printf("cudaMemcpyDtoH\n");
    //*result = cudaMemcpy((void*)ptr, mem.mem_data_val, size, cudaMemcpyHostToDevice);
    return 1;
}

bool_t cuda_launch_kernel_1_svc(ptr function, rpc_dim3 gridDim, rpc_dim3 blockDim,
                                mem_data args, size_t sharedMem, ptr stream,
                                int *result, struct svc_req *rqstp)
{
    printf("cudaLaunchKernel\n");
    void* cuda_args[] = {};
    CUresult res;
    CUmodule cuModule;
    res = cuModuleLoad(&cuModule, "/home/eiling/projects/cricket/tests/test_kernel");
    printf("%d: res=%d\n", __LINE__, res);
    CUfunction kernel;
    res = cuModuleGetFunction(&kernel, cuModule, "kernel_no_param");
    printf("%d: res=%d\n", __LINE__, res);
    res = cuLaunchKernel(kernel, gridDim.x, gridDim.y, gridDim.z, blockDim.x, blockDim.y, blockDim.z, sharedMem, (void*)stream, cuda_args, NULL);
    printf("%d: res=%d\n", __LINE__, res);
    //*result = cudaMemcpy((void*)ptr, mem.mem_data_val, size, cudaMemcpyHostToDevice);
    return 1;
}

int rpc_cd_prog_1_freeresult (SVCXPRT * a, xdrproc_t b , caddr_t c)
{
    if (b == (xdrproc_t) xdr_str_result) {
        free( ((str_result*)c)->str_result_u.str);
    }
}


int main (int argc, char **argv)
{
    register SVCXPRT *transp;

    struct sigaction act;
    act.sa_handler = int_handler;
    sigaction(SIGINT, &act, NULL);

    transp = svcunix_create(RPC_ANYSOCK, 0, 0, CD_SOCKET_PATH);
    if (transp == NULL) {
        fprintf (stderr, "%s", "cannot create unix service.");
        exit(1);
    }
    if (!svc_register(transp, RPC_CD_PROG, RPC_CD_VERS, rpc_cd_prog_1, 0)) {
        fprintf (stderr, "%s", "unable to register (RPC_PROG_PROG, RPC_PROG_VERS, ).");
        exit(1);
    }
    svc_run ();
    fprintf (stderr, "%s", "svc_run returned");
    unlink(CD_SOCKET_PATH);
    return 0;
}

