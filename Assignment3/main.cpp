#include "classes.hpp"
#include <vector>

// whatever driver code we need.

int numCaches;
std::vector <L1> L1Caches;
std::vector <LLC_bank> L2Caches;

int main(int argc, char *argv[]){
    if(argc != 2){
        printf("Enter Number of traces to process!");
        exit(1);
    }
    numCaches = atoi(argv[1]);
    for(int i = 0; i < numCaches; i++){
        L1Caches.push_back(L1(i));
        L2Caches.push_back(LLC_bank(i));
    }
}