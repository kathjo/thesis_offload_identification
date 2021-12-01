// This is idealized representation of a target program for the analysis pass
// The aim is for it to be simple enough to understand quickly,
// while also capturing the kind of nested loops which the pass is designed to identify.

// to obtain the input IR for the pass from this run:
// $LLVM_DIR/bin/clang -O3 -emit-llvm -S -fno-discard-value-names  irregular_nested_loop.cpp -o irregular.ll
// example execution: ./irregular_nested_loop.o 16 256 masks4096.txt
// perf stat records a cache miss rate for this execution of about 40%

#include <stdio.h>
#include <stdlib.h>

void applymasks(int *masks, int *node, int &size, int &limit) {
    int stride = 0;
    for (int i = 0; i < limit; i++) {
        for (int j = 0 ; j < size; j++) {
            if ( masks[stride+j] > 0) {
                node[j] += 1;
            }
        }
        stride += size;
    }
}

int main( int argc, char** argv) {

    int limit = atoi(argv[1]);
    int size = atoi(argv[2]);

    char *input_f = argv[3];
    FILE *fp = fopen(input_f,"r");
    int maskvar;
    long masksize = size * limit;
    int *masks = (int *) malloc(sizeof(int) *masksize);
    int *node = (int *) malloc(sizeof(int) *size);

    for (int i = 0; i < limit; i++) {
        for (int j = 0 ; j < size; j++) {
            fscanf(fp,"%i",&maskvar);
            masks[size*i+j] = (maskvar == 0);
        }
    }

    for (int j = 0 ; j < size; j++) {
        node[j] = 0;
    }

    applymasks(masks, node, size, limit);

    int nodesum = 0;
    for (int j = 0 ; j < size; j++) {
        nodesum += node[j];
    }

    return nodesum;
}