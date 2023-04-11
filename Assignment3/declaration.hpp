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
#include <assert.h>
#include <limits>

using std::set, std::unordered_map, std::vector, std::deque, std::bitset, std::map, std::unordered_set;
using std::unique_ptr, std::make_unique, std::move;

#define ull unsigned long long
#define timeAddr std::pair<unsigned long, ull>
#define blk std::pair<ull, bool>

constexpr int NUM_L1_SETS = (1 << 6);   // each L1 cache
constexpr ull L1_SET_BITS = 0x3f;
constexpr unsigned NUM_L1_WAYS = 8;
constexpr int NUM_L2_SETS_PER_BANK = (1 << 9);  // in each bank!
constexpr int L2_SET_BITS = 0x1ff;
constexpr unsigned NUM_L2_WAYS = 16;
constexpr int LOG_L2_BANKS = 3;
constexpr int MAX_PROC = 64;
constexpr int LOG_BLOCK_SIZE = 6;
constexpr int MAX_BUF_L1 = 10;
constexpr int NUM_CACHE = 8;
int trace[MAX_PROC];

enum MsgType { INV, INV_ACK, NACK, WB, WB_ACK, GET, GETX, PUT, PUTX};

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
    ull blockAddr;

    NACKStruct(): msg(MsgType::GET), blockAddr(0) {}
    NACKStruct(NACKStruct&& other) noexcept: msg(move(other.msg)), blockAddr(move(other.blockAddr)) {}
    bool operator<(const NACKStruct& other);
};

struct DirEnt {
    bitset<NUM_CACHE> bitVector;
    int ownerId; // could store owner here instead of actually storing it in the bitvector
    bool dirty;
    bool pending; // is a boolean enough, do we need more?

    DirEnt(): dirty(true), ownerId(0), pending(false) { bitVector.reset(); };
    DirEnt(DirEnt&& other) noexcept: dirty(move(other.dirty)),
        bitVector(move(other.bitVector)),
        ownerId(move(other.ownerId)),
        pending(move(other.pending)) {}
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

class Put: public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc, bool toL1);
        Put(const Put&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Put& operator=(const Put&) = delete; // delete copy assignment ctor too.
        Put() : Message() {}
        Put(Put&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Put(MsgType msgType, int from, int to, bool fromL1, ull blockId) :
        Message(msgType, from, to, fromL1), blockAddr(blockId) {}
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

class Cache {
    protected:
        // probably need these as public
        enum State {M, E, S, I};
        struct outstandingMsgStruct {
            ull blockAddr; // this could depend on the context, in case of inv due to inclusivity, this could represent blockAddr to replace the replacedBlock with.
            int waitForNumMessages;
            unordered_set<int> L1CacheNums; // depend upon context; for LLC inclusivity case, when inv acks are received, LLC would send block to these L1 caches.
            outstandingMsgStruct(outstandingMsgStruct && other) noexcept :
                blockAddr(move(other.blockAddr)), waitForNumMessages(move(other.waitForNumMessages)), L1CacheNums(move(other.L1CacheNums)) {}
            outstandingMsgStruct(ull blockAddr, int waitForNumMessages) :
                blockAddr(blockAddr), waitForNumMessages(waitForNumMessages) {}
            outstandingMsgStruct() :
                blockAddr(0), waitForNumMessages(0) {}
        };
        vector<unordered_map<ull, ull>> cacheData;    // addr -> time map
        vector<set<timeAddr>> timeBlockAdded;  // stores time and addr for eviction.
        map<NACKStruct, int> outstandingNacks;
        unordered_map<ull, State> cacheState;
        int id; // id of cache
    public:
        unordered_map<ull, int> numInvToCollect;    // map from block address to number of invalidations to collect
        unordered_map <MsgType, unordered_map <ull, outstandingMsgStruct>> outstadingMessages; // this would be a map from what messages are outstanding to a map from a struct to how many messages to wait (in case of INV) (required here since L2 needs to wait for Inv Ack for inclusivity)
        deque<unique_ptr<Message>> incomingMsg; // incoming messages from L2 and other L1s
        virtual bool check_cache(ull addr) = 0;
        virtual ull set_from_addr(ull addr) = 0;
        bool evict(ull addr);
        blk replace(ull addr, ull timeAdded, int maxSetSize);
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
            numInvToCollect(move(other.numInvToCollect)),
            outstandingNacks(move(other.outstandingNacks)),
            outstadingMessages(move(other.outstadingMessages))  {}
};

class L1 : public Cache {
    private:
        int inputTrace; // from where you would read line to line
        char buffer[MAX_BUF_L1 * sizeof(LogStruct) + 2];
        deque<LogStruct> logs;
        unique_ptr<Message> tempSpace; // when we deque, we need to store the top message.
        void read_if_reqd(){
            if(!logs.empty()){ return; }
            int num_bytes_read = read(inputTrace, buffer, MAX_BUF_L1 * sizeof(LogStruct));
            int ind = 0;
            while(ind < num_bytes_read and ind + sizeof(LogStruct) <= num_bytes_read){
                LogStruct *tmp_struct = (LogStruct *)&buffer[ind];
                logs.push_back(*tmp_struct);
                ind += sizeof(LogStruct);
            }
        }
    public:
        inline ull set_from_addr(ull addr) { return ((addr << LOG_BLOCK_SIZE) & L1_SET_BITS); }
        bool check_cache(ull addr){ return cacheData[set_from_addr(addr)].contains(addr); }
        bool process(Processor &proc);
        L1(int id): Cache(id, NUM_L1_SETS), inputTrace(0), tempSpace(nullptr) {
            std::string tmp = "traces/addrtrace_" + std::to_string(id) + ".out";
            this->inputTrace = open(tmp.c_str(), O_RDONLY);
        }
        L1(const L1&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        L1& operator=(const L1&) = delete; // delete copy assignment ctor too.
        L1(L1&& other) noexcept : Cache(move(other)),
            inputTrace(move(other.inputTrace)),
            tempSpace(move(other.tempSpace)),
            logs(move(other.logs)) {
                for(int i = 0; i < MAX_BUF_L1 * sizeof(LogStruct) + 2; i++) {
                    buffer[i] = move(other.buffer[i]);
                }
            }
        ~L1(){ close(inputTrace); }
};
class LLCBank : public Cache {
    private:
        friend class Get;
        vector<unordered_map<ull, DirEnt>>directory; // blockwise coherence. maps a block address to its entry.
    public:
        inline ull set_from_addr(ull addr) { return ((addr << (LOG_BLOCK_SIZE + LOG_L2_BANKS)) & L2_SET_BITS); }
        bool check_cache(ull addr){ return cacheData[set_from_addr(addr)].contains(addr); }
        bool process(Processor &proc);
        void bring_from_mem_and_send_inv(Processor &proc, ull addr, int L1CacheNum);
        LLCBank(int id): Cache(id, NUM_L2_SETS_PER_BANK), directory(NUM_L2_SETS_PER_BANK) {}
        LLCBank(const LLCBank&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        LLCBank& operator=(const LLCBank&) = delete; // delete copy assignment ctor too.
        LLCBank(LLCBank &&other) noexcept : Cache(move(other)), directory(move(other.directory)) {}
        ~LLCBank(){}
};

class Processor {
    private:
        friend class LLCBank;
        friend class Inv;
        friend class InvAck;
        friend class Get;
        int numCycles; // number of Cycles
        vector<L1> L1Caches;
        vector<LLCBank> L2Caches;
    public:
        int nextGlobalMsgToProcess;
        void run(){
            while(true){
                bool progressMade = false;
                for(int i = 0; i < NUM_CACHE; i++){
                    // process L1
                    progressMade |= L1Caches[i].process(*this);
                }
                for(int i = 0; i < NUM_CACHE; i++){
                    // process L2
                    progressMade |= L2Caches[i].process(*this);
                }
                numCycles++;
                if(!progressMade){break;}
            }
        }
        Processor(): numCycles(0), nextGlobalMsgToProcess(0) {
            for(int i = 0; i < NUM_CACHE; i++) {
                L1Caches.emplace_back(i);
                L2Caches.emplace_back(LLCBank(i));
            }
        }
        Processor(Processor&& other) noexcept:
            numCycles(move(other.numCycles)),
            L1Caches(move(other.L1Caches)),
            L2Caches(move(other.L2Caches)),
            nextGlobalMsgToProcess(move(other.nextGlobalMsgToProcess)) {}
};