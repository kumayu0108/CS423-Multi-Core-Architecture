#include "classes.hpp"
#include <vector>

// whatever driver code we need.
int numCaches;

int main(int argc, char *argv[]){
    if(argc != 2){
        printf("Enter Number of traces to process!");
        exit(1);
    }

    numCaches = atoi(argv[1]);
    Processor proc(numCaches);
    // proc.run();
}