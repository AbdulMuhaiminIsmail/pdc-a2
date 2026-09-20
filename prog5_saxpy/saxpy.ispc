
// Task count is fixed at 64 in the shipped code. It is exposed here so the
// granularity can be swept without editing the source; the default value
// reproduces the shipped configuration exactly.
#ifndef SAXPY_TASKS
#define SAXPY_TASKS 64
#endif

export void saxpy_ispc(uniform int N,
                       uniform float scale,
                            uniform float X[],
                            uniform float Y[],
                            uniform float result[])
{
    foreach (i = 0 ... N) {           
        result[i] = scale * X[i] + Y[i];
    }
}

task void saxpy_ispc_task(uniform int N,
                               uniform int span,
                               uniform float scale,
                               uniform float X[], 
                               uniform float Y[],
                               uniform float result[])
{

    uniform int indexStart = taskIndex * span;
    uniform int indexEnd = min(N, indexStart + span);

    foreach (i = indexStart ... indexEnd) {
        result[i] = scale * X[i] + Y[i];
    }
}

export void saxpy_ispc_withtasks(uniform int N,
                               uniform float scale,
                               uniform float X[],
                               uniform float Y[],
                               uniform float result[])
{

    // Round up so the final task absorbs the remainder rather than leaving
    // elements unprocessed when SAXPY_TASKS does not divide N.
    uniform int span = (N + SAXPY_TASKS - 1) / SAXPY_TASKS;
    uniform int numTasks = (N + span - 1) / span;

    launch[numTasks] saxpy_ispc_task(N, span, scale, X, Y, result);
}
