#include <bits/stdc++.h>

#define L2_SETS 1024
#define LOG_L2_SETS 10
#define L3_SETS 2048
#define LOG_L3_SETS 11
#define LOG_BLOCK_SIZE 6
#define L2_SET_BITS 0x3ff
#define L3_SET_BITS 0x7ff
#define ull unsigned long long

class Cache{
    public:
        Cache(){
            L2.resize(L2_SETS, std::vector<std::pair<int, bool>> (8, {0, false}));
            L3.resize(L3_SETS, std::vector<std::pair<int, bool>> (16, {0, false}));
        }
        
        void simulator(char type, ull addr){
            ull setL2 = (addr >> LOG_BLOCK_SIZE) & L2_SET_BITS;
            ull tagL2 = (addr >> (LOG_BLOCK_SIZE + LOG_L2_SETS));
            ull setL3 = (addr >> LOG_BLOCK_SIZE) & L3_SET_BITS;
            ull tagL3 = (addr >> (LOG_BLOCK_SIZE + LOG_L3_SETS));
            
            if(check_in_l2(setL2, tagL2)){

            }
            else {

            }
        }

    private:
        unsigned long l2_hits, l2_misses, l3_hits, l3_misses;
        std::vector <std::vector<std::pair<int, bool>>> L2;
        std::vector <std::vector<std::pair<int, bool>>> L3;

        bool check_in_l2(ull setL2, ull tagL2){

        }

        bool check_in_l3(ull setL3, ull tagL3){

        }
};

int main(int argc, char *argv[]){
    using std::cout;
    using std::endl;
    using std::vector;
    using std::pair;

    Cache cache;

    FILE *fp;
    char input_name[256];
    int numtraces = atoi(argv[2]);
    char i_or_d;
    char type;
    ull addr;
    unsigned pc;
    for (int k=0; k<numtraces; k++) {
        sprintf(input_name, "traces/%s_%d", argv[1], k);
        fp = fopen(input_name, "rb");
        std::cout << input_name << "\n";
        assert(fp != NULL);

        while (!feof(fp)) {
            fread(&i_or_d, sizeof(char), 1, fp);
            fread(&type, sizeof(char), 1, fp);
            fread(&addr, sizeof(ull), 1, fp);
            fread(&pc, sizeof(unsigned), 1, fp);
            std::cout << (int)i_or_d << " " << (int)type << " " << addr << " " << pc << std::endl;
            // Process the entry
            cache.simulator(type, addr);
            return 0;

        }
        fclose(fp);
        printf("Done reading file %d!\n", k);
    }
    return 0;
}
/*
2^19 / (2^6 * 8)
*/