// Number of tasks mandelbrot_ispc_withtasks launches. Overridable at compile
// time (-DMANDEL_TASKS=n) so the count can be swept without editing this file
// between runs; see measure_prog3.sh. The default is the value the sweep
// found best on the declared machine.
#ifndef MANDEL_TASKS
#define MANDEL_TASKS 200
#endif


static inline int mandel(float c_re, float c_im, int count) {
    float z_re = c_re, z_im = c_im;
    int i;
    for (i = 0; i < count; ++i) {

        if (z_re * z_re + z_im * z_im > 4.f)
           break;

        float new_re = z_re*z_re - z_im*z_im;
        float new_im = 2.f * z_re * z_im;
        z_re = c_re + new_re;
        z_im = c_im + new_im;
    }

    return i;
}

export void mandelbrot_ispc(uniform float x0, uniform float y0, 
                            uniform float x1, uniform float y1,
                            uniform int width, uniform int height, 
                            uniform int maxIterations,
                            uniform int output[])
{
    float dx = (x1 - x0) / width;
    float dy = (y1 - y0) / height;

    foreach (j = 0 ... height, i = 0 ... width) {
            float x = x0 + i * dx;
            float y = y0 + j * dy;

            int index = j * width + i;
            output[index] = mandel(x, y, maxIterations);
    }
}

// slightly different kernel to support tasking
task void mandelbrot_ispc_task(uniform float x0, uniform float y0, 
                               uniform float x1, uniform float y1,
                               uniform int width, uniform int height,
                               uniform int rowsPerTask,
                               uniform int maxIterations,
                               uniform int output[])
{

    // taskIndex is an ISPC built-in
    
    uniform int ystart = taskIndex * rowsPerTask;
    uniform int yend = ystart + rowsPerTask;

    // The shipped code assumes rowsPerTask divides height exactly, which only
    // holds for task counts that divide 800. Clamping lets any count be
    // launched without reading or writing past the end of the image.
    if (yend > height)
        yend = height;
    
    uniform float dx = (x1 - x0) / width;
    uniform float dy = (y1 - y0) / height;
    
    foreach (j = ystart ... yend, i = 0 ... width) {
            float x = x0 + i * dx;
            float y = y0 + j * dy;
            
            int index = j * width + i;
            output[index] = mandel(x, y, maxIterations);
    }
}

export void mandelbrot_ispc_withtasks(uniform float x0, uniform float y0,
                                      uniform float x1, uniform float y1,
                                      uniform int width, uniform int height,
                                      uniform int maxIterations,
                                      uniform int output[])
{

    // The shipped code launches 2 tasks, which leaves six of this machine's
    // eight hardware contexts idle and caps the result near 2x the
    // single-core ISPC figure. Launching many more tasks than cores both fills
    // every context and balances the load: the Mandelbrot set's cost varies
    // enormously by row, so fine-grained tasks let the runtime's work queue
    // hand a new task to whichever core finishes first.
    uniform int numTasks = MANDEL_TASKS;
    if (numTasks > height)
        numTasks = height;
    if (numTasks < 1)
        numTasks = 1;

    // Round up so the tasks cover every row, then recompute the count so no
    // task is launched with nothing to do.
    uniform int rowsPerTask = (height + numTasks - 1) / numTasks;
    numTasks = (height + rowsPerTask - 1) / rowsPerTask;

    launch[numTasks] mandelbrot_ispc_task(x0, y0, x1, y1,
                                     width, height,
                                     rowsPerTask,
                                     maxIterations,
                                     output);
}
