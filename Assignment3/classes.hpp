#include <fcntl.h>
#include <unistd.h>
#include <deque>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <bitset>
#include <iostream>
#include <memory>
#include <assert.h>

using std::set, std::unordered_map, std::vector, std::deque, std::bitset;
using std::unique_ptr, std::make_unique, std::move;

#define ull unsigned long long
#define timeAddr std::pair<unsigned long, ull>
#define blk std::pair<ull, bool>

constexpr int NUM_L1_SETS = (1 << 6);   // each L1 cache
constexpr ull L1_SET_BITS = 0x3f;
constexpr int NUM_L2_SETS_PER_BANK = (1 << 9);  // in each bank!
constexpr int L2_SET_BITS = 0x1ff;
constexpr int LOG_L2_BANKS = 3;
constexpr int MAX_PROC = 64;
constexpr int LOG_BLOCK_SIZE = 6;
constexpr int MAX_BUF_L1 = 10;
constexpr int NUM_CACHE = 8;
int trace[MAX_PROC];

// The data is logged in binary in the format of the following struct
struct LogStruct {
    bool isStore;  // boolean; tells if the current machine access is a store or not
    unsigned int time;  // global time to sync events
    ull addr;     // address (with last LOG_BLOCK_SIZE bits set to 0)
};

struct DirEnt {
    bitset<NUM_CACHE> bitVector;
    bool dirty;
    bool pending; // is a boolean enough, do we need more?
};

class Processor;    //forward declaration

class Message {
    public:
        enum MsgType { INV, NACK, ACK};
        MsgType msgType;
        int from, to; // store from and to cache id.
        bool fromL1; // store if the message comes from l1 or l2. useful in l1 to l1 transfers
        virtual void handle(Processor &proc){}
        Message(const Message&) = delete; // delete copy ctor explicitly since Message is a move only cls.
        Message& operator=(const Message&) = delete; // delete copy assignment ctor too.
        Message() : msgType(MsgType::NACK), from(0), to(0), fromL1(false) {}
        Message(MsgType msgType, int from, int to, bool fromL1) :
            msgType(msgType), from(from), to(to), fromL1(fromL1) {}
        Message(Message && other) noexcept : msgType(move(other.msgType)),
            from(move(other.from)),
            to(move(other.to)),
            fromL1(move(other.fromL1)) {}

};
// just serves as an example to show inheritance model.
class Inv : public Message {
    ull blockId; // which cache block to be evicted?
    ull msgId; // What message is the invalidation a reply to?
    public:
        void handle(Processor &proc) {}
        Inv(const Inv&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Inv& operator=(const Inv&) = delete; // delete copy assignment ctor too.
        Inv() : Message(), blockId(0), msgId(0) {}
        Inv(Inv&& other) noexcept : Message(move(other)), blockId(move(other.blockId)), msgId(other.msgId) {}
};

class Cache {
    protected:
        deque<unique_ptr<Message>> incomingMsg; // incoming messages from L2 and other L1s
        vector<unordered_map<ull, ull>> cacheData;    // addr -> time map
        vector<set<timeAddr>> timeBlockAdded;  // stores time and addr for eviction.
        int id; // id of cache
        virtual bool check_cache(ull addr) = 0;
        virtual ull set_from_addr(ull addr) = 0;
        // this function only removes this addr from cache & returns if anything got evicted
        bool evict(ull st, ull addr){
            assert(st < cacheData.size());
            if(!cacheData[st].contains(addr)){ return false; }
            int time = cacheData[st][addr];
            assert(timeBlockAdded[st].contains({time, addr}));
            timeBlockAdded[st].erase({time, addr});
            return true;
        }
        // this function replaces this addr from cache and returns the replaced address & flag stating valid block
        blk replace(ull st, ull addr, ull timeAdded, int maxSetSize){
            assert(st < cacheData.size());
            if(cacheData[st].size() < maxSetSize){cacheData[st][addr] = timeAdded; return {0, false};}
            assert(cacheData[st].size() == maxSetSize and timeBlockAdded[st].size() == maxSetSize);
            auto [time_evicted, addr_evicted] = *timeBlockAdded[st].begin();
            assert(time_evicted < timeAdded);
            timeBlockAdded.erase(timeBlockAdded.begin());
            cacheData[st].erase(addr_evicted);
            cacheData[st][addr] = timeAdded;
            timeBlockAdded[st].insert({timeAdded, addr});
            return {addr_evicted, true};
        }
    public:
        // need proc since we need to pass it to message.handle() that would be called inside
        void process(Processor &proc){
            // call message.handle()
        }
        Cache(int id, int numSets): id(id), cacheData(numSets), timeBlockAdded(numSets) {}
        Cache(Cache &&other) noexcept : id(move(other.id)), cacheData(move(other.cacheData)), timeBlockAdded(move(other.timeBlockAdded)) {};
};
class L1 : public Cache {
    private:
        int inputTrace; // from where you would read line to line
        char buffer[MAX_BUF_L1 * sizeof(LogStruct) + 2];
        deque<LogStruct> logs;
        unique_ptr<Message> tempSpace; // when we deque, we need to store the top message.
        inline ull set_from_addr(ull addr) { return ((addr << LOG_BLOCK_SIZE) & L1_SET_BITS); }
        bool check_cache(ull addr){ return cacheData[set_from_addr(addr)].contains(addr); }
        void read_if_reqd(){
            if(!logs.empty()){ return; }
            int numBytesRead = read(inputTrace, buffer, MAX_BUF_L1 * sizeof(LogStruct));
            int ind = 0;
            while(ind < numBytesRead and ind + sizeof(LogStruct) <= numBytesRead){
                LogStruct *tmpStruct = (LogStruct *)&buffer[ind];
                logs.push_back(*tmpStruct);
                ind += sizeof(LogStruct);
            }
        }
    public:
        L1(int id): Cache(id, NUM_L1_SETS), inputTrace(0), tempSpace(nullptr){
            std::string tmp = "traces/addrtrace_" + std::to_string(id) + ".out";
            this->inputTrace = open(tmp.c_str(), O_RDONLY);
            // unique_ptr<Message> mesg = make_unique<Inv>();
            // incomingMsg.push_back(move(mesg));
            // incomingMsg.emplace_back(make_unique<Inv>());
        }
        L1(const L1&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        L1& operator=(const L1&) = delete; // delete copy assignment ctor too.
        L1(L1&& other) noexcept : Cache(move(other)), inputTrace(move(other.inputTrace)), tempSpace(move(other.tempSpace)) {}
        ~L1(){ close(inputTrace); }
};
class LLCBank : public Cache {
    vector<unordered_map<ull, DirEnt>>directory; // blockwise coherence. maps a block to its entry.
    private:
        // class storage structures for  directory??
        inline ull set_from_addr(ull addr) { return ((addr << (LOG_BLOCK_SIZE + LOG_L2_BANKS)) & L2_SET_BITS); }
        bool check_cache(ull addr){ return cacheData[set_from_addr(addr)].contains(addr); }
    public:
        LLCBank(int id): Cache(id, NUM_L2_SETS_PER_BANK), directory(NUM_L2_SETS_PER_BANK) {}
        LLCBank(const LLCBank&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        LLCBank& operator=(const LLCBank&) = delete; // delete copy assignment ctor too.
        LLCBank(LLCBank &&other) noexcept : Cache(move(other)), directory(move(other.directory)) {}
        ~LLCBank(){}
};

class Processor {
    private:
        int numCycles; // number of Cycles
        vector<L1> L1Caches;
        vector<LLCBank> L2Caches;
    public:
        Processor() {
            for(int i = 0; i < NUM_CACHE; i++) {
                L1Caches.emplace_back(i);
                L2Caches.emplace_back(LLCBank(i));
            }
        }
        void run(){
            while(true){
                bool progressMade = false;
                for(int i = 0; i < NUM_CACHE; i++){
                    // process L1
                    L1Caches[i].process(*this);
                }
                for(int i = 0; i < NUM_CACHE; i++){
                    // process L2
                    L2Caches[i].process(*this);
                }
                if(!progressMade){break;}
            }
        }
};