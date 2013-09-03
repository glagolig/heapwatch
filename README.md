heapwatch
=========

heapwatch is a simple heap profiling tool. It loads on process startup as a preload library
and intercepts heap allocation calls. On signal 31 heapwatch prints out most frequently hit
allocation stacks.

to build binary, type make (by default 32-bit binary heapwatch_32.so is built; modify
Makefile to build 64-bit binary)

to invoke
LD_PRELOAD=./heapwatch_32.so your_app your_app_params

to collect dump
kill -n 31 your_app_pid

dump name is heapwatch.dump



