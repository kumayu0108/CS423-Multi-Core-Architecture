#include <bits/stdc++.h>

//TODO: Separate into header file

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
        unsigned long l2_hits, l2_misses, l3_hits, l3_misses, time = 0;
        Cache():
            L2(L2_SETS, std::vector<blk> (NUM_L2_TAGS, {0, false})),
            timeBlockAddedL2(L2_SETS, std::vector <ull> (NUM_L2_TAGS, 0)),
            L3(L3_SETS, std::vector<blk> (NUM_L3_TAGS, {0, false})),
            timeBlockAddedL3(L3_SETS, std::vector <ull> (NUM_L3_TAGS, 0)),
            l2_hits(0), l2_misses(0), l3_hits(0), l3_misses(0) {}

        virtual void bring_from_memory(ull addr, ull setL2, ull tagL2, ull setL3, ull tagL3) = 0;
        virtual void bring_from_llc(ull addr, ull setL2, ull tagL2, ull setL3, ull tagL3) = 0;

        void simulator(char type, ull addr) {
            if(static_cast<int>(type) == 0) { return; } // if write perm miss, treat as hit, ignore.
            if(!futureAccesses.empty()){
                futureAccesses[addr].erase(futureAccesses[addr].begin());
            }
            auto [setL2, tagL2] = decode_address(addr, L2_SET_BITS, LOG_L2_SETS);
            auto [setL3, tagL3] = decode_address(addr, L3_SET_BITS, LOG_L3_SETS);
            int l2_ind, l3_ind;
            if((l2_ind = check_in_cache(setL2, tagL2, L2, NUM_L2_TAGS)) != -1) {
                l2_hits++;
                update_priority(setL2, tagL2, NUM_L2_TAGS);
            }
            else if((l3_ind = check_in_cache(setL3, tagL3, L3, NUM_L3_TAGS)) != -1)
                bring_from_llc(addr, setL2, tagL2, setL3, tagL3);
            else
                bring_from_memory(addr, setL2, tagL2, setL3, tagL3);
            time++;
        }
    protected:
        std::vector <std::vector<blk>> L2, L3; // tag -> ull, active? -> bool
        std::vector <std::vector<ull>> timeBlockAddedL2, timeBlockAddedL3; // -> for eviction.
        std::unordered_map <ull, std::set<ull>> futureAccesses; // for storing future access times

        // evicts a block by simply zeroing out its valid bit, given a tag and set.
        void evict(ull st, ull tag, std::vector <std::vector<blk>> &Lx, int maxTags){
            int tag_index = check_in_cache(st, tag, Lx, maxTags);
            if(tag_index != -1) Lx[st][tag_index].second = false;
        }
        // given a certain set, it uses LRU policy to replace cache block.
        //returns address evicted, along with a bool denoting if it actually existed or if it was an empty slot.
        blk replace(ull st, ull tag, std::vector <std::vector<blk>> &Lx, std::vector <std::vector<ull>> &timeBlockAddedLx, int maxTags){
            //find first invalid block, fill it, return.
            int replaceIndex = -1;
            blk retBlock = {0, false};
            for(int i = 0; i < maxTags; i++){
                if(!Lx[st][i].second) // not valid;
                {
                    replaceIndex = i;
                    break;
                }
            }
            // else calculate index of minimum timestamp for LRU replacement.
            // all blocks are valid, so we're actually evicting an existing block.
            if(replaceIndex == -1) {
                replaceIndex = std::min_element(timeBlockAddedLx[st].begin(), timeBlockAddedLx[st].end()) - timeBlockAddedLx[st].begin();
                retBlock.second = true;
            }
            //get address evicted.
            retBlock.first = get_addr(st, Lx[st][replaceIndex].first, maxTags);
            Lx[st][replaceIndex] = blk(tag, true);
            update_priority(st, tag, maxTags);
            return retBlock;
        }
        //helper function for decoding addresses
        std::pair<ull, ull> decode_address(ull addr, ull set_bits, int log_set_size){
            ull st = (addr >> LOG_BLOCK_SIZE) & set_bits;
            ull tag = (addr >> (LOG_BLOCK_SIZE + log_set_size));
            return std::pair<ull, ull>(st, tag);
        }
        // returns tag index in current set if found & is valid. Else returns -1
        virtual int check_in_cache(ull st, ull tag, std::vector <std::vector<blk>> &Lx, int maxTags){
            for(int i = 0; i < maxTags; i++){
                if(Lx[st][i].second == true and Lx[st][i].first == tag){
                    return i;
                }
            }
            return -1;
        }
        //helper function to get address from set and tag bits
        ull get_addr(ull st, ull tag, int maxTags){
            if(maxTags == NUM_L2_TAGS){
                return ((tag << (LOG_BLOCK_SIZE + LOG_L2_SETS)) | (st << LOG_BLOCK_SIZE));
            }
            else {
                return ((tag << (LOG_BLOCK_SIZE + LOG_L3_SETS)) | (st << LOG_BLOCK_SIZE));
            }
        }
        // updates LRU list of times.
        virtual void update_priority(ull st, ull tag, int maxTags){
            auto &Lx = (maxTags == NUM_L2_TAGS) ? L2 : L3;
            auto &timeBlockAddedLx = (maxTags == NUM_L2_TAGS) ? timeBlockAddedL2 : timeBlockAddedL3;
            auto maxValue = *(std::max_element(timeBlockAddedLx[st].begin(), timeBlockAddedLx[st].end()));
            int tag_index = check_in_cache(st, tag, Lx, maxTags);
            if(tag_index != -1) timeBlockAddedLx[st][tag_index] = maxValue + 1;
        }
    private:
};

class ExCache : public Cache {
    private:
        // evicts a block from L2, puts in a block with setL2 set and tagL2 tag.
        void evict_replace_l2(ull setL2, ull tagL2){
            auto [replacedAddrL2, valid] = replace(setL2, tagL2, L2, timeBlockAddedL2, NUM_L2_TAGS); // replaces the block in L2
            if(!valid){ return; } // if evicted block was invalid, simply return.
            // if evicted block was valid, allocate it in L3. replace block in L3 cache.
            auto [replacedSetL3, replacedTagL3] = decode_address(replacedAddrL2, L3_SET_BITS, LOG_L3_SETS);
            replace(replacedSetL3, replacedTagL3, L3, timeBlockAddedL3, NUM_L3_TAGS);
        }
        void bring_from_llc(ull addr, ull setL2, ull tagL2, ull setL3, ull tagL3){
            l2_misses++;
            l3_hits++;
            evict(setL3, tagL3, L3, NUM_L3_TAGS); // evicts from L3
            evict_replace_l2(setL2, tagL2);
        }
        void bring_from_memory(ull addr, ull setL2, ull tagL2, ull setL3, ull tagL3){
            l2_misses++;
            l3_misses++;
            evict_replace_l2(setL2, tagL2);
        }
};
class IncCache : public Cache {
    private:
        void bring_from_llc(ull addr, ull setL2, ull tagL2, ull setL3, ull tagL3){
            l2_misses++;
            l3_hits++;
            replace(setL2, tagL2, L2, timeBlockAddedL2, NUM_L2_TAGS);
        }
        void bring_from_memory(ull addr, ull setL2, ull tagL2, ull setL3, ull tagL3){
            l2_misses++;
            l3_misses++;
            auto [replacedAddrL3, validL3] = replace(setL3, tagL3, L3, timeBlockAddedL3, NUM_L3_TAGS); // evict from L3 first.
            auto [replacedSetL2, replacedTagL2] = decode_address(replacedAddrL3, L2_SET_BITS, LOG_L2_SETS); // decode addr wrt L2;
            evict(replacedSetL2, replacedTagL2, L2, NUM_L2_TAGS); // invalidate corresponding entry in L2.
            replace(setL2, tagL2, L2, timeBlockAddedL2, NUM_L2_TAGS); // evict from L2.
        }
};
class NINECache : public Cache {
    private:
        void evict_replace_l2(ull setL2, ull tagL2){
            auto [replacedAddrL2, valid] = replace(setL2, tagL2, L2, timeBlockAddedL2, NUM_L2_TAGS); // evict from L2
            if(!valid) { return; } // if invalid block evicted, just return
            auto [replacedSetL3, replacedTagL3] = decode_address(replacedAddrL2, L3_SET_BITS, LOG_L3_SETS); // else get set and tag to try wb in L3.
            if(check_in_cache(replacedSetL3, replacedTagL3, L3, NUM_L3_TAGS) != -1) { return ; } // if already exists in L3, dont do anything.
            replace(replacedSetL3, replacedTagL3, L3, timeBlockAddedL3, NUM_L3_TAGS); // else replace block in L3
        }
        void bring_from_llc(ull addr, ull setL2, ull tagL2, ull setL3, ull tagL3){
            l2_misses++;
            l3_hits++;
            evict_replace_l2(setL2, tagL2);
        }
        void bring_from_memory(ull addr, ull setL2, ull tagL2, ull setL3, ull tagL3){
            l2_misses++;
            l3_misses++;
            replace(setL3, tagL3, L3, timeBlockAddedL3, NUM_L3_TAGS); // evict from L3 first.
            evict_replace_l2(setL2, tagL2); // then evict from L2;
        }
};

class LRUCacheFully : public Cache {
    private:
        // greater comparator for comparing pair <ull, ull>
        struct cmp {
            bool operator() (std::pair<ull, ull> p1, std::pair<ull, ull> p2) const {
                return p1.first > p2.first;
            }
        };
        std::unordered_map <ull, ull> faL3;
        std::set <std::pair<ull, ull>, cmp > timeForNextAccessL3;
        // checks in cache
        int check_in_cache(ull st, ull tag, std::vector <std::vector<blk>> &Lx, int maxTags){
            if(maxTags == NUM_L2_TAGS){
                for(int i = 0; i < maxTags; i++){
                    if(Lx[st][i].second == true and Lx[st][i].first == tag){
                        return i;
                    }
                }
            }
            else {
                ull addr = get_addr(st, tag, maxTags);
                if(faL3.find(addr) != faL3.end()){return 1;}
            }
            return -1;
        }
        // updates LRU list of times.
        virtual void update_priority(ull st, ull tag, int maxTags){
            if(maxTags == NUM_L2_TAGS){
                auto maxValue = *(std::max_element(timeBlockAddedL2[st].begin(), timeBlockAddedL2[st].end()));
                int tag_index = check_in_cache(st, tag, L2, maxTags);
                if(tag_index != -1) timeBlockAddedL2[st][tag_index] = maxValue + 1;
            }
            else {
                ull addr = get_addr(st, tag, maxTags);
                auto it = timeForNextAccessL3.find({faL3[addr], addr});
                // assert(faL3[tag] == time);
                timeForNextAccessL3.erase(it);
                timeForNextAccessL3.insert({*futureAccesses[addr].begin(), addr});
                faL3[addr] = *futureAccesses[addr].begin();
            }
        }
        void bring_from_llc(ull addr, ull tag){
            // increase respective counters
            l2_misses++;
            l3_hits++;
            
        }
        void bring_from_memory(ull addr, ull tag){
            // increase respective counters
            l2_misses++;
            l3_misses++;
            
        }
    public:
        LRUCacheFully() : Cache() {}
};

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
    ExCache excache;
    IncCache incache;
    NINECache ninecache;
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
            // std::cout << (int)i_or_d << " " << (int)type << " " << addr << " " << pc << std::endl;
            // Process the entry
            addr = ((addr >> LOG_BLOCK_SIZE) << LOG_BLOCK_SIZE);
            excache.simulator(type, addr);
            incache.simulator(type, addr);
            ninecache.simulator(type, addr);
        }
        fclose(fp);
        printf("Done reading file %d!\n", k);
    }
    cout << "l2_hits:" << excache.l2_hits << " l2_misses:" << excache.l2_misses << " l3_hits:" << excache.l3_hits << " l3_misses:" << excache.l3_misses << " l2_total:" << excache.l2_hits + excache.l2_misses << " l3_total:" << excache.l3_hits + excache.l3_misses << endl;
    cout << "l2_hits:" << incache.l2_hits << " l2_misses:" << incache.l2_misses << " l3_hits:" << incache.l3_hits << " l3_misses:" << incache.l3_misses << " l2_total:" << incache.l2_hits + incache.l2_misses << " l3_total:" << incache.l3_hits + incache.l3_misses << endl;
    cout << "l2_hits:" << ninecache.l2_hits << " l2_misses:" << ninecache.l2_misses << " l3_hits:" << ninecache.l3_hits << " l3_misses:" << ninecache.l3_misses << " l2_total:" << ninecache.l2_hits + ninecache.l2_misses << " l3_total:" << ninecache.l3_hits + ninecache.l3_misses << endl;

    return 0;
}