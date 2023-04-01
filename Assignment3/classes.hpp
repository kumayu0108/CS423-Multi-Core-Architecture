#include <fcntl.h>
#include <unistd.h>
#include <deque>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>

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
        std::deque<Message> incomingMsg; // incoming messages from L2 and other L1s
        virtual bool check_cache(ull addr) = 0;
        int id; // id of cache
};
class L1 : public Cache {
    int inputTrace; // from where you would read line to line
    Message *tempSpace; // when we deque, we need to store the top message.
    std::vector<std::unordered_map<ull, ull>> L1CacheData;    // addr -> time map
    std::vector<std::set<timeAddr>> timeBlockAddedL1;   // stores time and addr for eviction.
    inline ull set_from_addr(ull addr) { return ((addr << LOG_BLOCK_SIZE) & L1_SET_BITS); }
    bool check_cache(ull addr){ return L1CacheData[set_from_addr(addr)].contains(addr); }
    public:
        L1(int id){
            this->id = id;
            std::string tmp = "traces/addrtrace_" + std::to_string(id) + ".out";
            this->inputTrace = open(tmp.c_str(), O_RDONLY);
            this->tempSpace = nullptr;
            this->L1CacheData.resize(NUM_L1_SETS);
            this->timeBlockAddedL1.resize(NUM_L1_SETS);
        }
        ~L1(){
            close(inputTrace);
        }
};
class LLC_bank : public Cache {
    // class storage structures for  directory??
    std::vector<std::unordered_map<ull, ull>> L2CacheData;    // addr -> time map
    std::vector <std::set<timeAddr>> timeBlockAddedL2;  // stores time and addr for eviction.
    inline ull set_from_addr(ull addr) { return ((addr << (LOG_BLOCK_SIZE + LOG_L2_BANKS)) & L2_SET_BITS); }
    bool check_cache(ull addr){ return L2CacheData[set_from_addr(addr)].contains(addr); }
    public:
        LLC_bank(int id){
            this->id = id;
            this->L2CacheData.resize(NUM_L2_SETS_PER_BANK);
            this->timeBlockAddedL2.resize(NUM_L2_SETS_PER_BANK);
        }
        ~LLC_bank(){}
};