#include <bits/stdc++.h>

#define ull unsigned long long
constexpr int LOG_BLOCK_SIZE = 6;
constexpr int L2_SETS = 1024;
constexpr int NUM_L2_TAGS = 8;
constexpr int LOG_L2_SETS = 10;
constexpr ull L2_SET_BITS = 0x3ff;
constexpr int L3_SETS = 2048;
constexpr int NUM_L3_TAGS = 16;
constexpr int LOG_L3_SETS = 11;
constexpr ull L3_SET_BITS = 0x7ff;

class Cache{
    public:
        Cache():
            L2(L2_SETS, std::vector<std::pair<ull, bool>> (NUM_L2_TAGS, {0, false})),
            timeBlockAddedL2(L2_SETS, std::vector <ull> (NUM_L2_TAGS, 0)),
            L3(L3_SETS, std::vector<std::pair<ull, bool>> (NUM_L3_TAGS, {0, false})),
            timeBlockAddedL3(L3_SETS, std::vector <ull> (NUM_L3_TAGS, 0)),
            l2_hits(0), l2_misses(0), l3_hits(0), l3_misses(0) {}

        void simulator(char type, ull addr){
            auto [setL2, tagL2] = decode_address(addr, L2_SET_BITS, LOG_L2_SETS);
            auto [setL3, tagL3] = decode_address(addr, L3_SET_BITS, LOG_L3_SETS);

            if(check_in_cache(setL2, tagL2, L2, NUM_L2_TAGS)){
                l2_hits++;
                update_priority(setL2, tagL2, L2, timeBlockAddedL2, NUM_L2_TAGS);
            }
            else {
                l2_misses++;
                if(check_in_cache(setL3, tagL3, L3, NUM_L3_TAGS)){
                    l3_hits++;
                    update_priority(setL3, tagL3, L3, timeBlockAddedL3, NUM_L3_TAGS);
                    copy_from_L3_and_replace_L2(setL2, tagL2, setL3, tagL3);
                }
            }
        }

    private:

        unsigned long l2_hits, l2_misses, l3_hits, l3_misses;
        std::vector <std::vector<std::pair<ull, bool>>> L2, L3; // tag -> ull, active? -> bool
        std::vector <std::vector<ull>> timeBlockAddedL2, timeBlockAddedL3; // -> for eviction.

        bool check_in_cache(ull st, ull tag, std::vector <std::vector<std::pair<ull, bool>>> &Lx, int maxTags){
            for(int i = 0; i < maxTags; i++){
                if(Lx[st][i].second == true and Lx[st][i].first == tag){
                    return true;
                }
            }
            return false;
        }

        void copy_from_L3_and_replace_L2(ull setL2, ull tagL2, ull setL3, ull tagL3){

        }

        void update_priority(ull st, ull tag, std::vector <std::vector<std::pair<ull, bool>>> &Lx, std::vector <std::vector<ull>> &timeBlockAddedLx, int maxTags){
            auto maxValue = *(std::max_element(timeBlockAddedLx[st].begin(), timeBlockAddedLx[st].end()));
            for(int i = 0; i < maxTags; i++){
                if(Lx[st][i].second == true and Lx[st][i].first == tag){
                    timeBlockAddedLx[st][i] = maxValue + 1;
                }
            }
        }
        std::pair<ull, ull>decode_address(ull addr, ull set_bits, int log_set_size)
        {
            ull st = (addr >> LOG_BLOCK_SIZE) & set_bits;
            ull tag = (addr >> (LOG_BLOCK_SIZE + log_set_size));
            return std::pair<ull, ull>(st, tag);
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