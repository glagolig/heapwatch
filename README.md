heapwatch
=========

heapwatch is a simple heap profiling tool. It loads on process startup as a preload library
and intercepts heap allocation calls. On signal 31 heapwatch prints out most frequently hit
allocation stacks.
