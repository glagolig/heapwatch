#all: SimplePreload_64.so SimplePreload_32.so
all: heapwatch_32.so

heapwatch_64.so: Preload.c StackStorage.c BoundedPriQueue.c
	gcc -g -shared -fPIC -m64 Preload.c StackStorage.c BoundedPriQueue.c -o heapwatch_64.so -ldl -lpthread

heapwatch_32.so: Preload.c StackStorage.c BoundedPriQueue.c
	gcc -g -shared -fPIC -m32 Preload.c StackStorage.c BoundedPriQueue.c -o heapwatch_32.so -ldl -lpthread




