#include <bits/stdc++.h>

#define ull unsigned long long
//defines a cache block. contains a tag and a valid bit.
#define blk std::pair<ull, bool>
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
            L2(L2_SETS, std::vector<blk> (NUM_L2_TAGS, {0, false})),
            timeBlockAddedL2(L2_SETS, std::vector <ull> (NUM_L2_TAGS, 0)),
            L3(L3_SETS, std::vector<blk> (NUM_L3_TAGS, {0, false})),
            timeBlockAddedL3(L3_SETS, std::vector <ull> (NUM_L3_TAGS, 0)),
            l2_hits(0), l2_misses(0), l3_hits(0), l3_misses(0) {}

        // have a separate simulator function now because couting misses might be problematic? MIGHT CHANGE IN FUTURE
        // virtual void simulator(char type, ull addr) = 0;
        virtual void bring_from_memory(char type, ull addr) = 0;
        virtual void bring_from_llc(char type, ull addr) = 0;

        void simulator(char type, ull addr) {
            auto [setL2, tagL2] = decode_address(addr, L2_SET_BITS, LOG_L2_SETS);
            auto [setL3, tagL3] = decode_address(addr, L3_SET_BITS, LOG_L3_SETS);
            int l2_ind = check_in_cache(setL2, tagL2, L2, NUM_L2_TAGS);
            int l3_ind = check_in_cache(setL3, tagL3, L3, NUM_L3_TAGS);
            if(l2_ind != -1) {
                l2_hits++;
                update_priority(setL2, tagL2, L2, timeBlockAddedL2, NUM_L2_TAGS);
            }
            else if(l2_ind == -1 && l3_ind != -1){
                // hits in llc
                // no idea how this is going to be implemented, signature can change.
                // idea is to update miss and hit counters inside bring_from_llc function. CAN change, if we want to count conflict and cap misses.
                bring_from_llc(type, addr);
            }
            else{
                // bring shit from memory.
                bring_from_memory(type, addr);
            }
        }

    private:

        unsigned long l2_hits, l2_misses, l3_hits, l3_misses;
        std::vector <std::vector<blk>> L2, L3; // tag -> ull, active? -> bool
        std::vector <std::vector<ull>> timeBlockAddedL2, timeBlockAddedL3; // -> for eviction.

        // returns tag index in current set if found & is valid. Else returns -1
        int check_in_cache(ull st, ull tag, std::vector <std::vector<blk>> &Lx, int maxTags){
            for(int i = 0; i < maxTags; i++){
                if(Lx[st][i].second == true and Lx[st][i].first == tag){
                    return i;
                }
            }
            return -1;
        }
        // updates LRU list of times.
        void update_priority(ull st, ull tag, std::vector <std::vector<blk>> &Lx, std::vector <std::vector<ull>> &timeBlockAddedLx, int maxTags){
            auto maxValue = *(std::max_element(timeBlockAddedLx[st].begin(), timeBlockAddedLx[st].end()));
            int tag_index = check_in_cache(st, tag, Lx, maxTags);
            if(tag_index != -1) timeBlockAddedLx[st][tag_index] = maxValue + 1;
        }
        //helper function for decoding addresses
        std::pair<ull, ull> decode_address(ull addr, ull set_bits, int log_set_size){
            ull st = (addr >> LOG_BLOCK_SIZE) & set_bits;
            ull tag = (addr >> (LOG_BLOCK_SIZE + log_set_size));
            return std::pair<ull, ull>(st, tag);
        }
        // evicts a block by simply zeroing out its valid bit, given a tag and set.
        void evict(ull st, ull tag, std::vector <std::vector<blk>> &Lx, int maxTags){
            int tag_index = check_in_cache(st, tag, Lx, maxTags);
            if(tag_index != -1) Lx[st][tag_index].second = false;
        }
        // given a certain set, it uses LRU policy to replace cache block.
        void replace(ull st, ull tag, std::vector <std::vector<blk>> &Lx, std::vector <std::vector<ull>> &timeBlockAddedLx, int maxTags){
            //find first invalid block, fill it, return.
            int replaceIndex = -1;
            for(int i = 0; i < maxTags; i++){
                if(!Lx[st][i].second) // not valid;
                {
                    replaceIndex = i;
                    break;
                }
            }
            // else calculate index of minimum timestamp for LRU replacement.
            if(replaceIndex == -1) replaceIndex = std::min_element(timeBlockAddedLx[st].begin(), timeBlockAddedLx[st].end()) - timeBlockAddedLx[st].begin();
            Lx[st][replaceIndex] = blk(tag, true);
            update_priority(st, tag, Lx, timeBlockAddedLx, maxTags);
        }
};
//TODO: write bring from LLC, bring from memory for each of these caches.
class ExCache : public Cache {};
class IncCache : public Cache {};
class NINECache : public Cache {};

int main(int argc, char *argv[]){
    using std::cout;
    using std::endl;
    using std::vector;
    using std::pair;

    // Cache cache;

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
            // cache.simulator(type, addr);
            return 0;

        }
        fclose(fp);
        printf("Done reading file %d!\n", k);
    }
    return 0;
}