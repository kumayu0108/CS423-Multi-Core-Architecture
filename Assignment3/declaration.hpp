#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <deque>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <set>
#include <bitset>
#include <iostream>
#include <memory>
#include <limits>
#include <iomanip>

// #define PRINT_DEBUG
// #define NDEBUG   // uncomment to disable asserts. Need to put this before #include <asserts.h>
#include <assert.h>

#ifndef NDEBUG
#define ASSERT2(expr, print) \
    if(!(expr)) { print; } \
    assert(expr);
#define ASSERT(expr) assert(expr)
#else
#define ASSERT2(expr, print)
#define ASSERT(expr) 
#endif


using std::set, std::unordered_map, std::vector, std::deque, std::bitset, std::map, std::unordered_set, std::pair;
using std::unique_ptr, std::make_unique, std::move;

#define ull unsigned long long
#define timeAddr std::pair<unsigned long, ull>

constexpr int NUM_L1_SETS = (1 << 6);   // each L1 cache
constexpr int NUM_L2_SETS_PER_BANK = (1 << 9);  // in each bank!
constexpr ull L1_SET_BITS = 0x3f;
constexpr int L2_SET_BITS = 0x1ff;
constexpr unsigned NUM_L1_WAYS = 8;
constexpr unsigned NUM_L2_WAYS = 16;
constexpr int MAX_BUF_L1 = 100;
constexpr int LOG_L2_BANKS = 3;
constexpr int MAX_PROC = 64;
constexpr int LOG_BLOCK_SIZE = 6;
constexpr int L2_BANK_BITS = 0x7;
constexpr int NUM_CACHE = 8;
constexpr int NACK_WAIT_CYCLES = 5;
int trace[MAX_PROC];

enum MsgType { INV, INV_ACK, NACK, WB, GET, GETX, PUT, PUTX, UPGR, UPGR_ACK };
enum State {M, E, S, I};

// The data is logged in binary in the format of the following struct
struct LogStruct {
    bool isStore;  // boolean; tells if the current machine access is a store or not
    unsigned int time;  // global time to sync events
    ull addr;     // address (with last LOG_BLOCK_SIZE bits set to 0)

    LogStruct(LogStruct&& other) noexcept : isStore(move(other.isStore)),
        time(move(other.time)),
        addr(move(other.addr)) {}
    LogStruct(const LogStruct& other): isStore(other.isStore), time(other.time), addr(other.addr) {}
};

struct NACKStruct {
    MsgType msg;
    ull waitForNumCycles;

    NACKStruct(): msg(MsgType::GET), waitForNumCycles(0) {}
    NACKStruct(NACKStruct&& other) noexcept: msg(move(other.msg)), waitForNumCycles(move(other.waitForNumCycles)) {}
    // bool operator<(const NACKStruct& other);
};

struct DirEnt {
    bitset<NUM_CACHE> bitVector;
    int ownerId; // could store owner here instead of actually storing it in the bitvector
    bool dirty;
    bool pending; // is a boolean enough, do we need more?
    bool toBeReplaced; // needed for inclusive eviction
    std::string debug_string;

    // DirEnt(): dirty(false), ownerId(-1), pending(false), toBeReplaced(false), debug_string("") { bitVector.reset(); };
    DirEnt() { ASSERT(false); }
    DirEnt(bool dirty, int ownerId, bool pending = false, bool toBeReplaced = false) : dirty(dirty), ownerId(ownerId), pending(pending), toBeReplaced(toBeReplaced) {}
    DirEnt(DirEnt&& other) noexcept: dirty(move(other.dirty)),
        bitVector(move(other.bitVector)),
        ownerId(move(other.ownerId)),
        pending(move(other.pending)),
        toBeReplaced(move(other.toBeReplaced)),
        debug_string(move(other.debug_string)) {}
};

struct cacheBlock {
    ull time;
    State state;
    cacheBlock() : time(0), state(State::I) {}
    cacheBlock(ull time, State state) : time(time), state(state) {};
    cacheBlock& operator=(const cacheBlock& other) {//time(other.time), state(other.state) { return *this;}
        time = other.time;
        state = other.state;
        return *this;
    }
    cacheBlock(cacheBlock && other) noexcept : time(move(other.time)), state(move(other.state)) {}
};

class Processor;    //forward declaration

class Message {
    public:
        // Message id should be here, since before processing a message we need to check if we can process it?
        // commenting for now
        // ull msgId;
        MsgType msgType;
        int from, to; // store from and to cache id.
        bool fromL1; // store if the message comes from l1 or l2. useful in l1 to l1 transfers
        virtual void handle(Processor &proc, bool toL1){}
        Message(const Message&) = delete; // delete copy ctor explicitly since Message is a move only cls.
        Message& operator=(const Message&) = delete; // delete copy assignment ctor too.
        Message() : msgType(MsgType::NACK), from(0), to(0), fromL1(false) {}
        Message(MsgType msgType, int from, int to, bool fromL1) :
            msgType(msgType), from(from), to(to), fromL1(fromL1) {}
        Message(Message && other) noexcept :
            msgType(move(other.msgType)),
            from(move(other.from)),
            to(move(other.to)),
            fromL1(move(other.fromL1)) {}

};
// just serves as an example to show inheritance model.
class Inv : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc, bool toL1);
        Inv(const Inv&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Inv& operator=(const Inv&) = delete; // delete copy assignment ctor too.
        Inv() : Message(), blockAddr(0) {}
        Inv(Inv&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Inv(MsgType msgType, int from, int to, bool fromL1, ull blockId) :
            Message(msgType, from, to, fromL1), blockAddr(blockId) {}
};

class Wb : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        bool inResponseToGet; // was this writeback in response to a Get
        void handle(Processor &proc, bool toL1);
        Wb(const Wb&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Wb& operator=(const Wb&) = delete; // delete copy assignment ctor too.
        Wb() : Message(), blockAddr(0) {}
        Wb(Wb&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)), inResponseToGet(move(other.inResponseToGet)) {}
        Wb(MsgType msgType, int from, int to, bool fromL1, ull blockId, bool inResponseToGet) :
            Message(msgType, from, to, fromL1), blockAddr(blockId), inResponseToGet(inResponseToGet) {}
};

class InvAck : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc, bool toL1);
        InvAck(const InvAck&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        InvAck& operator=(const InvAck&) = delete; // delete copy assignment ctor too.
        InvAck() : Message() {}
        InvAck(InvAck&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        InvAck(MsgType msgType, int from, int to, bool fromL1, ull blockId) :
            Message(msgType, from, to, fromL1), blockAddr(blockId) {}
};

class Get : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc, bool toL1);
        Get(const Get&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Get& operator=(const Get&) = delete; // delete copy assignment ctor too.
        Get() : Message() {}
        Get(Get&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Get(MsgType msgType, int from, int to, bool fromL1, ull blockId) :
            Message(msgType, from, to, fromL1), blockAddr(blockId) {}
};

class Getx : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc, bool toL1);
        Getx(const Getx&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Getx& operator=(const Getx&) = delete; // delete copy assignment ctor too.
        Getx() : Message() {}
        Getx(Getx&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Getx(MsgType msgType, int from, int to, bool fromL1, ull blockId) :
            Message(msgType, from, to, fromL1), blockAddr(blockId) {}
};

class Put: public Message {
    public:
        std::string debug_msg;
        ull blockAddr; // which cache block ADDR
        void handle(Processor &proc, bool toL1);
        Put(const Put&) = delete; // delete copy ctor explicitly,since it's a move only cls.
        Put& operator=(const Put&) = delete; // delete copy assignment ctor too.
        Put() : Message() {}
        Put(Put&& other) noexcept : 
            Message(move(other)), blockAddr(move(other.blockAddr)), debug_msg(move(other.debug_msg)) {}
        Put(MsgType msgType, int from, int to, bool fromL1, ull blockAddr, std::string debug_msg = "") :
        Message(msgType, from, to, fromL1), blockAddr(blockAddr), debug_msg(debug_msg) {}
};

class Putx: public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        int numAckToCollect; // MUST BE AN INTEGER!! CAN GO NEGATIVE INSIDE L1 STRUCT.
        State state;
        void handle(Processor &proc, bool toL1);
        Putx(const Putx&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Putx& operator=(const Putx&) = delete; // delete copy assignment ctor too.
        Putx() : Message() {}
        Putx(Putx&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)), numAckToCollect(move(other.numAckToCollect)),
            state(move(other.state)) {}
        Putx(MsgType msgType, int from, int to, bool fromL1, ull blockId, int numAckToCollect, State state) :
        Message(msgType, from, to, fromL1), blockAddr(blockId), numAckToCollect(numAckToCollect), state(state) {}
};

class Nack: public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        MsgType nackedMsg;
        void handle(Processor &proc, bool toL1);
        Nack(const Nack&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Nack& operator=(const Nack&) = delete; // delete copy assignment ctor too.
        Nack() : Message() {}
        Nack(Nack&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)), nackedMsg(move(other.nackedMsg)) {}
        Nack(MsgType msgType, MsgType nackedMsg, int from, int to, bool fromL1, ull blockId) :
        Message(msgType, from, to, fromL1), blockAddr(blockId), nackedMsg(nackedMsg) {}
};

class Upgr: public Message {
    public:
        ull blockAddr; // which cache block ADDR
        void handle(Processor &proc, bool toL1);
        Upgr(const Upgr&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Upgr& operator=(const Upgr&) = delete; // delete copy assignment ctor too.
        Upgr() : Message() {}
        Upgr(Upgr&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Upgr(MsgType msgType, int from, int to, bool fromL1, ull blockAddr) :
        Message(msgType, from, to, fromL1), blockAddr(blockAddr) {}
};

class UpgrAck: public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        int numAckToCollect; // NEEDS TO BE INT BECAUSE CAN GO NEGATIVE DURING COUNTING.
        void handle(Processor &proc, bool toL1);
        UpgrAck(const UpgrAck&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        UpgrAck& operator=(const UpgrAck&) = delete; // delete copy assignment ctor too.
        UpgrAck() : Message() {}
        UpgrAck(UpgrAck&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)), numAckToCollect(move(other.numAckToCollect)) {}
        UpgrAck(MsgType msgType, int from, int to, bool fromL1, ull blockId, int numAckToCollect) :
        Message(msgType, from, to, fromL1), blockAddr(blockId), numAckToCollect(numAckToCollect) {}
};

class Cache {
    protected:
        // probably need these as public
        vector<unordered_map<ull, cacheBlock>> cacheData;    // addr -> time map
        vector<set<timeAddr>> timeBlockAdded;  // stores time and addr for eviction.
        // unordered_map<ull, State> cacheState; // not required, already maintained in cacheBlock.
        int id; // id of cache
    public:
        unordered_map<ull, NACKStruct> outstandingNacks;
        deque<unique_ptr<Message>> incomingMsg; // incoming messages from L2 and other L1s
        virtual cacheBlock evict_replace(Processor& proc, ull addr, State state) = 0;
        virtual bool check_cache(ull addr) = 0;
        virtual ull set_from_addr(ull addr) = 0;
        // this function only removes this addr from cache & returns if anything got evicted
        virtual bool evict(ull addr) = 0;
        void update_priority(ull addr);
        // need proc since we need to pass it to message.handle() that would be called inside
        virtual bool process(Processor &proc) = 0;
        Cache(int id, int numSets): id(id),
            cacheData(numSets),
            timeBlockAdded(numSets) {}
        Cache(Cache &&other) noexcept : incomingMsg(move(other.incomingMsg)),
            id(move(other.id)),
            cacheData(move(other.cacheData)),
            timeBlockAdded(move(other.timeBlockAdded)),
            outstandingNacks(move(other.outstandingNacks)) {}
};

class L1 : public Cache {
    private:
        friend class Get;
        friend class Put;
        friend class Putx;
        friend class Inv;
        friend class InvAck;
        friend class Nack;
        friend class UpgrAck;
        struct InvAckStruct {
            int numAckToCollect; // MUST BE AN INT, CAN GO BELOW ZERO WHILE COUNTING!
            bool getReceived, getXReceived; // did we receive any Get/GetX while we were waiting for inv acks?
            int to; // if we received Get/GetX, where do I need to send Put.
            InvAckStruct() : numAckToCollect(0), getReceived(false), getXReceived(false), to(0) {}
            InvAckStruct(InvAckStruct && other) noexcept :
                numAckToCollect(move(other.numAckToCollect)), getReceived(move(other.getReceived)), getXReceived(move(other.getXReceived)), to(move(other.to)) {}
        };
        int inputTrace; // from where you would read line to line
        char buffer[MAX_BUF_L1 * sizeof(LogStruct) + 2];
        deque<LogStruct> logs;
        unique_ptr<Message> tempSpace; // when we deque, we need to store the top message.
    public:
        // getReplyWait and getXReplyWait would be used to check if there is a previous Get/Getx request. They would get cleared on Put/Putx.
        unordered_map<ull, std::pair<int, bool>> getReplyWait; // cycle number for debug (int) and if need to send Upgr (bool)
        unordered_set<ull> getXReplyWait;
        unordered_set<ull> upgrReplyWait;
        unordered_map<ull, InvAckStruct> numAckToCollect; // block -> num; used when we want to collect inv acks for Getx request.
        // unordered_set<ull> writeBackAckWait; // wait for wb ack;
        inline ull set_from_addr(ull addr) { return ((addr << LOG_BLOCK_SIZE) & L1_SET_BITS); }
        bool check_cache(ull addr);
        bool evict(ull addr);
        cacheBlock evict_replace(Processor& proc, ull addr, State state);
        int get_llc_bank(ull addr);
        bool process(Processor &proc);
        void process_log(Processor &proc);
        bool check_nacked_requests(Processor &proc);
        L1(int id): Cache(id, NUM_L1_SETS), inputTrace(0), tempSpace(nullptr) {
            std::string tmp = "traces/addrtrace_" + std::to_string(id) + ".out";
            inputTrace = open(tmp.c_str(), O_RDONLY);
            while(true) {
                int num_bytes_read = read(inputTrace, buffer, MAX_BUF_L1 * sizeof(LogStruct));
                if(num_bytes_read == 0) {
                    break;
                }
                int ind = 0;
                while(ind < num_bytes_read and ind + sizeof(LogStruct) <= num_bytes_read){
                    LogStruct *tmp_struct = (LogStruct *)&buffer[ind];
                    logs.push_back(*tmp_struct);
                    ind += sizeof(LogStruct);
                }
            }
            close(inputTrace);
        }
        L1(const L1&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        L1& operator=(const L1&) = delete; // delete copy assignment ctor too.
        L1(L1&& other) noexcept : Cache(move(other)),
            inputTrace(move(other.inputTrace)),
            tempSpace(move(other.tempSpace)),
            numAckToCollect(move(other.numAckToCollect)),
            getReplyWait(move(other.getReplyWait)),
            getXReplyWait(move(other.getXReplyWait)),
            upgrReplyWait(move(other.upgrReplyWait)),
            logs(move(other.logs)) {
                for(int i = 0; i < MAX_BUF_L1 * sizeof(LogStruct) + 2; i++) {
                    buffer[i] = move(other.buffer[i]);
                }
            }
        ~L1(){ ASSERT(logs.empty()); }
};
class LLCBank : public Cache {
    private:
        friend class Get;
        friend class Getx;
        friend class Put;
        friend class InvAck;
        friend class Inv;
        friend class Wb;
        friend class Upgr;
        struct InvAckInclStruct {
            ull blockAddr; // this could depend on the context, in case of inv due to inclusivity, this could represent blockAddr to replace the replacedBlock with.
            int waitForNumMessages;
            set<pair<int, bool>> L1CacheNums; // depend upon context; for LLC inclusivity case, when inv acks are received, LLC would send block to these L1 caches.
            InvAckInclStruct(InvAckInclStruct && other) noexcept :
                blockAddr(move(other.blockAddr)), waitForNumMessages(move(other.waitForNumMessages)), L1CacheNums(move(other.L1CacheNums)) {}
            InvAckInclStruct(ull blockAddr, int waitForNumMessages) :
                blockAddr(blockAddr), waitForNumMessages(waitForNumMessages) {}
            InvAckInclStruct() :
                blockAddr(0), waitForNumMessages(0) {}
        };
        vector<unordered_map<ull, DirEnt>>directory; // blockwise coherence. maps a block address to its entry.
    public:
        unordered_map <ull, InvAckInclStruct> numInvAcksToCollectForIncl; // this would be a map from what messages are outstanding to a map from a struct to how many messages to wait (in case of INV) (required here since L2 needs to wait for Inv Ack for inclusivity)
        inline ull set_from_addr(ull addr) { return ((addr << (LOG_BLOCK_SIZE + LOG_L2_BANKS)) & L2_SET_BITS); }
        bool check_cache(ull addr){ return cacheData[set_from_addr(addr)].contains(addr); }
        cacheBlock evict_replace(Processor& proc, ull addr, State state);
        bool evict(ull addr);
        bool process(Processor &proc);
        void bring_from_mem_and_send_inv(Processor &proc, ull addr, int L1CacheNum, bool Getx = false);
        LLCBank(int id): Cache(id, NUM_L2_SETS_PER_BANK), directory(NUM_L2_SETS_PER_BANK) {}
        LLCBank(const LLCBank&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        LLCBank& operator=(const LLCBank&) = delete; // delete copy assignment ctor too.
        LLCBank(LLCBank &&other) noexcept :
            Cache(move(other)), directory(move(other.directory)), numInvAcksToCollectForIncl(move(other.numInvAcksToCollectForIncl)) {}
        ~LLCBank(){}
};

class Processor {
    private:
        friend class L1;
        friend class LLCBank;
        friend class Inv;
        friend class Put;
        friend class Putx;
        friend class InvAck;
        friend class Get;
        friend class Getx;
        friend class Wb;
        friend class Upgr;
        friend class UpgrAck;
        friend class Nack;
        int numCycles; // number of Cycles
        std::unordered_map<int, int> totL1Accesses; // would be incremented on every process_log call
        std::unordered_map<int, int> totL1Misses; // would be incremented every time check_cache fails in process_log
        std::unordered_map<int, int> totL1UpgrMisses; // would be incremented every time in process_log if isStore == true, block is in cache and is not in M or E state.
        std::unordered_map<int, int> totL2Misses;  // should be incremented whenever GetX/Get reaches L2 and it doesn't have the block.
        std::unordered_map<int, unordered_map<MsgType, ull>> msgReceivedL1, msgReceivedL2;
        vector<L1> L1Caches;
        vector<LLCBank> L2Caches;
    public:
        int nextGlobalMsgToProcess;
        void run();
        Processor(): numCycles(0), nextGlobalMsgToProcess(0), totL1Accesses(0), totL1Misses(0), totL1UpgrMisses(0), totL2Misses(0) {
            for(int i = 0; i < NUM_CACHE; i++) {
                L1Caches.emplace_back(i);
                L2Caches.emplace_back(i);
            }
        }
        Processor(Processor&& other) noexcept:
            numCycles(move(other.numCycles)),
            totL1Accesses(move(other.totL1Accesses)),
            totL1Misses(move(other.totL1Misses)),
            totL1UpgrMisses(move(other.totL1UpgrMisses)),
            totL2Misses(0),
            msgReceivedL1(move(other.msgReceivedL1)),
            msgReceivedL2(move(other.msgReceivedL2)),
            L1Caches(move(other.L1Caches)),
            L2Caches(move(other.L2Caches)),
            nextGlobalMsgToProcess(move(other.nextGlobalMsgToProcess)) {}
};