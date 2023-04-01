#include "classes.hpp"
#include <vector>

// whatever driver code we need.

int num_caches;
std::vector <L1> L1_caches;

int main(int argc, char *argv[]){
    if(argc != 2){
        printf("Enter Number of traces to process!");
        exit(1);
    }
    num_caches = atoi(argv[1]);
    for(int i = 0; i < num_caches; i++){
        L1_caches.push_back(L1(i));
    }
}