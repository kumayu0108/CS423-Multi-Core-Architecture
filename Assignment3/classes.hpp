#include <fcntl.h>
#include <unistd.h>
#include <deque>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>

using std::set, std::unordered_map, std::vector, std::deque;

#define ull unsigned long long
#define timeAddr std::pair<unsigned long, ull>

constexpr int NUM_L1_SETS = (1 << 6);   // each L1 cache
constexpr ull L1_SET_BITS = 0x3f;
constexpr int NUM_L2_SETS_PER_BANK = (1 << 9);  // in each bank!
constexpr int L2_SET_BITS = 0x1ff;
constexpr int LOG_L2_BANKS = 3;
constexpr int MAX_PROC = 64;
constexpr int LOG_BLOCK_SIZE = 6;
int trace[MAX_PROC];

// The data is logged in binary in the format of the following struct
struct LogStruct {
    bool isStore;  // boolean; tells if the current machine access is a store or not
    unsigned int time;  // global time to sync events
    ull addr;     // address (with last LOG_BLOCK_SIZE bits set to 0)
};

class Message {
    int from, to; // store from and to cache id.
    bool fromL1; // store if the message comes from l1 or l2. useful in l1 to l1 transfers
};
// just serves as an example to show inheritance model.
class Inv : public Message {
    ull blockId; // which cache block to be evicted?
};
class Cache {
    protected:
        deque<Message> incomingMsg; // incoming messages from L2 and other L1s
        vector<unordered_map<ull, ull>> CacheData;    // addr -> time map
        vector <set<timeAddr>> timeBlockAdded;  // stores time and addr for eviction.
        virtual bool check_cache(ull addr) = 0;
        virtual ull set_from_addr(ull addr) = 0;
        int id; // id of cache

    public:
        Cache(int id, int NUM_SETS): id(id), CacheData(NUM_SETS), timeBlockAdded(NUM_SETS) {};
};
class L1 : public Cache {
    int inputTrace; // from where you would read line to line
    Message *tempSpace; // when we deque, we need to store the top message.
    inline ull set_from_addr(ull addr) { return ((addr << LOG_BLOCK_SIZE) & L1_SET_BITS); }
    bool check_cache(ull addr){ return CacheData[set_from_addr(addr)].contains(addr); }
    public:
        L1(int id): Cache(id, NUM_L1_SETS), tempSpace(nullptr) {
            std::string tmp = "traces/addrtrace_" + std::to_string(id) + ".out";
            this->inputTrace = open(tmp.c_str(), O_RDONLY);
        }
        ~L1(){ close(inputTrace); }
};
class LLC_bank : public Cache {
    // class storage structures for  directory??
    inline ull set_from_addr(ull addr) { return ((addr << (LOG_BLOCK_SIZE + LOG_L2_BANKS)) & L2_SET_BITS); }
    bool check_cache(ull addr){ return CacheData[set_from_addr(addr)].contains(addr); }
    public:
        LLC_bank(int id): Cache(id, NUM_L2_SETS_PER_BANK) {}
        ~LLC_bank(){}
};

class Processor {
    int  numCaches; // number of L1 and LLC cache;
    vector<L1> L1Caches;
    vector<LLC_bank> L2Caches;
    public:
        Processor() = delete; // delete default constructor
        Processor(int nCaches): numCaches(nCaches) {
            for(int i = 0; i < numCaches; i++) {
                L1 ith_L1 = L1(i);
                LLC_bank ith_L2 = LLC_bank(i);
                L1Caches.push_back(ith_L1);
                L2Caches.push_back(ith_L2);
            }
        }
};