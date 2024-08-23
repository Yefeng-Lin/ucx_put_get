CC=mpicc
CFLAGS=-O2 -g -Wall
LDFLAGS=-lucp -luct -lucs -lucm 

.PHONY: all clean
all: ucx_put

ucx_put: main.c mpi_ops.c 
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS} -g  -gdwarf-2

clean:
	rm ucx_put
