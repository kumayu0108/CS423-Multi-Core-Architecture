#include <bits/stdc++.h>

//TODO: Separate into header file

#define ull unsigned long long
//defines a cache block. contains a tag and a valid bit.
#define blk std::pair<ull, bool>
constexpr int LOG_BLOCK_SIZE = 6;

constexpr ull L2_ASSOC = 8192;
constexpr ull L3_ASSOC = 32768;

class Cache{
    private:
        // greater comparator for comparing pair <ull, ull>
        struct cmp {
            bool operator() (std::pair<ull, ull> p1, std::pair<ull, ull> p2) const {
                return p1.first > p2.first;
            }
        };
        // updates LRU list of times.
        void update_priority_lru(ull tag, std::unordered_map <ull, ull> &Lx, std::set <std::pair<ull, ull>> &timeBlockAddedLx){
            evict_lru(tag, Lx, timeBlockAddedLx);
            Lx[tag] = time;
            timeBlockAddedLx.insert({time, tag});
        }
        void update_priority_belady(ull tag, std::unordered_map <ull, ull> &Lx, std::set <std::pair<ull, ull>, cmp > timeForNextAccessLx){
            timeForNextAccessLx.erase({time, tag});
            timeForNextAccessLx.insert({*futureAccesses[tag].begin(), tag});
            Lx[tag] = time;
        }
    public:
        enum replace_pol {LRU, Belady};
        Cache(replace_pol pol):
            l2_hits(0), l2_misses(0), l3_hits(0), l3_misses(0), time(0), pol(pol) {

            }

        virtual void bring_from_memory(ull addr, ull tag) = 0;
        virtual void bring_from_llc(ull addr, ull tag) = 0;

        void simulator(char type, ull addr) {
            if(static_cast<int>(type) == 0) { return; } // if write perm miss, treat as hit, ignore.
            auto tag = decode_address(addr);
            if(pol == replace_pol::Belady) {
                futureAccesses[tag].erase(futureAccesses[tag].begin());
            }
            if(check_in_cache(tag, L2)) {
                l2_hits++;
                if(pol == replace_pol::LRU){
                    update_priority_lru(tag, L2, timeBlockAddedL2);
                }
                else {
                    update_priority_belady(tag, L2, timeForNextAccessL2);
                }
            }
            else if(check_in_cache(tag, L3))
                bring_from_llc(addr, tag);
            else
                bring_from_memory(addr, tag);
            time++;
        }
    protected:
        unsigned long l2_hits, l2_misses, l3_hits, l3_misses, time;
        replace_pol pol;
        std::unordered_map <ull, ull> L2, L3; // tag -> time map
        std::set <std::pair<ull, ull>> timeBlockAddedL2, timeBlockAddedL3; // (time, tag) -> for eviction.
        std::set <std::pair<ull, ull>, cmp > timeForNextAccessL2, timeForNextAccessL3; // storing {time for next acess, tag} for eviction
        std::unordered_map <ull, std::set<ull>> futureAccesses; // for storing future access times
        // evicts a block by simply zeroing out its valid bit, given a tag and set.
        void evict_lru(ull tag, std::unordered_map <ull, ull> &Lx, std::set <std::pair<ull, ull>> &timeBlockAddedLx){
            timeBlockAddedLx.erase({Lx[tag], tag});
            Lx.erase(tag);
        }
        void evict_belady(ull tag, std::unordered_map <ull, ull> &Lx, std::set <std::pair<ull, ull>, cmp > &timeForNextAccessLx){
            timeForNextAccessLx.erase({Lx[tag], tag});
            Lx.erase(tag);
        }
        // given a certain set, it uses LRU policy to replace cache block and updates priority
        // returns tag evicted along with a bool denoting if it actually existed or if it was an empty slot.
        blk replace_lru(ull tag, std::unordered_map <ull, ull> &Lx, std::set <std::pair<ull, ull>> &timeBlockAddedLx, ull maxTags){
            //find first invalid block, fill it, return.
            blk retBlock = {0, false};
            if(Lx.size() == maxTags) {
                retBlock.second = true;
                //get tag evicted.
                auto [prevTime, replacedTag] = *timeBlockAddedLx.begin();
                timeBlockAddedLx.erase(timeBlockAddedLx.begin());
                retBlock.first = replacedTag;
                Lx.erase(replacedTag);
            }
            Lx[tag] = time;
            timeBlockAddedLx.insert({time, tag});
            return retBlock;
        }
        // given a certain set, it uses BELADY policy to replace cache block and updates priority
        // returns tag evicted along with a bool denoting if it actually existed or if it was an empty slot.
        blk replace_belady(ull tag, std::unordered_map <ull, ull> &Lx, std::set <std::pair<ull, ull>, cmp > &timeForNextAccessLx, ull maxTags){
            //find first invalid block, fill it, return.
            blk retBlock = {0, false};
            if(Lx.size() == maxTags) {
                retBlock.second = true;
                //get tag evicted.
                auto [prevTime, replacedTag] = *timeForNextAccessLx.begin();
                timeForNextAccessLx.erase(timeForNextAccessLx.begin());
                retBlock.first = replacedTag;
                Lx.erase(replacedTag);
            }
            Lx[tag] = time;
            timeForNextAccessLx.insert({*futureAccesses[tag].begin(), tag});
            return retBlock;
        }
        //helper function for decoding addresses
        ull decode_address(ull addr){
            return (addr >> LOG_BLOCK_SIZE);
        }
        // returns tag index in current set if found & is valid. Else returns -1
        bool check_in_cache(ull tag, std::unordered_map <ull, ull> &Lx){
            if(Lx.find(tag) != Lx.end()){
                return true;
            }
            return false;
        }
};

class LRUCache : public Cache {
    private:
        void bring_from_llc(ull addr, ull tag){
            // increase respective counters
            l2_misses++;
            l3_hits++;
            replace_lru(tag, L2, timeBlockAddedL2, L2_ASSOC);   // replace block in L2 no need to replace again in L3 as it has the block
        }
        void bring_from_memory(ull addr, ull tag){
            // increase respective counters
            l2_misses++;
            l3_misses++;
            auto [replacedTagL3, validL3] = replace_lru(tag, L3, timeBlockAddedL3, L3_ASSOC);   // replace block in L3
            if(validL3){
                evict_lru(replacedTagL3, L2, timeBlockAddedL2); // evict replaced L3 block from L2.
            }
            replace_lru(tag, L2, timeBlockAddedL2, L2_ASSOC);   // replace block in L2
        }
    public:
        LRUCache() : Cache(replace_pol::LRU) {}
};

class BeladyCache : public Cache {
    private:
        void bring_from_llc(ull addr, ull tag){
            l2_misses++;
            l3_hits++;
            replace_lru(tag, L2, timeBlockAddedL2, L2_ASSOC);   // replace block in L2 no need to replace again in L3 as it has the block
        }
        void bring_from_memory(ull addr, ull tag){
            // increase respective counters
            l2_misses++;
            l3_misses++;
            auto [replacedTagL3, validL3] = replace_lru(tag, L3, timeBlockAddedL3, L3_ASSOC);   // replace block in L3
            if(validL3){
                evict_lru(replacedTagL3, L2, timeBlockAddedL2); // evict replaced L3 block from L2.
            }
            replace_lru(tag, L2, timeBlockAddedL2, L2_ASSOC);   // replace block in L2
        }
    public:
        BeladyCache(char *argv[]) : Cache(replace_pol::Belady) {
            FILE *fp;
            char input_name[256];
            int numtraces = atoi(argv[2]);
            char i_or_d;
            char type;
            ull addr;
            unsigned pc;
            unsigned int time = 0;
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
                    futureAccesses[decode_address(addr)].insert(time);
                    time++;
                }
                fclose(fp);
            }
        }
};


int main(int argc, char *argv[]){
    using std::cout;
    using std::endl;
    using std::vector;
    using std::pair;

    LRUCache lrucache;
    BeladyCache belcache(argv);

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
            // Process the entry
            lrucache.simulator(type, addr);
            belcache.simulator(type, addr);
        }
        fclose(fp);
        printf("Done reading file %d!\n", k);
    }
    return 0;
}