#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <cuda_runtime.h>

#include "intercomm.h"
#include "datatypes.h"

/**
 * Create a helper process and assign the mrcudaGPU to it.
 * @param mrcudaGPU the GPU information to assign to the created process.
 * @param helperProgPath the path to the helper application.
 * @param gpuID the ID of the GPU the helper application will use.
 * @return a ptr to the created process on success; NULL otherwise.
 */
MHelperProcess_t *mhelper_create(MRCUDAGPU_t *mrcudaGPU, const char *helperProgPath, int gpuID)
{
    int rPipePair[2], wPipePair[2];
    MHelperProcess_t *mhelperProcess;
    pid_t pid;
    char gpuIDStr[15];

    if (pipe(rPipePair) != 0)
        goto __mhelper_create_err_0;
    if (pipe(wPipePair) != 0)
        goto __mhelper_create_err_1;
    if ((mhelperProcess = malloc(sizeof(MHelperProcess_t))) == NULL)
        goto __mhelper_create_err_2;
    pid = fork();
    if (pid == 0) {   // child process
        close(wPipePair[1]);
        close(rPipePair[0]);
        dup2(wPipePair[0], fileno(stdin));
        dup2(rPipePair[1], fileno(stdout));
        sprintf(gpuIDStr, "%d", gpuID);
        execl(helperProgPath, helperProgPath, gpuIDStr, "\0");
        perror("Helper Program Exec");
        _exit(EXIT_FAILURE);
    }
    else if (pid < 0)   // error; cannot fork
        goto __mhelper_create_err_3;
    else {  // parent process
        close(wPipePair[0]);
        close(rPipePair[1]);
        mhelperProcess->readPipe = rPipePair[0];
        mhelperProcess->writePipe = wPipePair[1];
        mhelperProcess->pid = pid;
        if (mhelper_int_init(&(mhelperProcess->handle), mhelperProcess) != 0)
            goto __mhelper_create_err_3;
        mrcudaGPU->mhelperProcess = mhelperProcess;
        return mhelperProcess;
    }

__mhelper_create_err_3:
    free(mhelperProcess);
__mhelper_create_err_2:
    close(wPipePair[0]);
    close(wPipePair[1]);
__mhelper_create_err_1:
    close(rPipePair[0]);
    close(rPipePair[1]);
__mhelper_create_err_0:
    return NULL;
}

/**
 * Destroy the helper process.
 * @param process the process to be destroyed.
 * @return 0 on success; another number otherwise.
 */
int mhelper_destroy(MHelperProcess_t *process)
{
    int ret = kill(process->pid, SIGQUIT);
    if (ret == 0)
        free(process);
    return ret;
}

/**
 * Ask the process to execute the command.
 * @param process the process that will execute the specified command.
 * @param command the command to be executed on the process.
 * @return the result of the execution.
 */
MHelperResult_t mhelper_call(MHelperProcess_t *process, MHelperCommand_t command)
{
    ssize_t n;
    size_t remainingSize = sizeof(MHelperCommand_t);
    char *buf = (char *)&command;
    MHelperResult_t result;

    while (remainingSize > 0) {
        n = write(process->writePipe, buf, remainingSize);
        if (n < 0)
            goto __mhelper_call_err_0;
        remainingSize -= n;
        buf += n;
    }

    remainingSize = sizeof(MHelperResult_t);
    buf = (char *)&result;
    while (remainingSize > 0) {
        n = read(process->readPipe, buf, remainingSize);
        if (n < 0)
            goto __mhelper_call_err_0;
        remainingSize -= n;
        buf += n;
    }
    if (result.id != command.id || result.type != command.type)
        goto __mhelper_call_err_0;
    return result;

__mhelper_call_err_0:
    result.id = command.id;
    result.type = command.type;
    result.internalError = -1;
    result.cudaError = cudaSuccess;
    return result;
}

/**
 * Generate a unique ID for a command to be used with the specified mrcudaGPU.
 * @param mrcudaGPU a ptr to a MRCUDAGPU_t
 * @return a unique ID
 */
int mhelper_generate_command_id(MRCUDAGPU_t *mrcudaGPU)
{
    return (rand() << 4) | mrcudaGPU->virtualNumber;
}

