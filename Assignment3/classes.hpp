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

    LogStruct(LogStruct&& other) noexcept : isStore(move(other.isStore)),
        time(move(other.time)),
        addr(move(other.addr)) {}
    LogStruct(const LogStruct& other): isStore(other.isStore), time(other.time), addr(other.addr) {}
};

struct DirEnt {
    bitset<NUM_CACHE> bitVector;
    int ownerId; // could store owner here instead of actually storing it in the bitvector
    bool dirty;
    bool pending; // is a boolean enough, do we need more?
};


class Processor;    //forward declaration

class Message {
    public:
        ull msgId; // Message id should be here, since before processing a message we need to check if we can process it?
        enum MsgType { INV, INV_ACK, NACK, WB, WB_ACK, GET, GETX, PUT, PUTX};
        MsgType msgType;
        int from, to; // store from and to cache id.
        bool fromL1; // store if the message comes from l1 or l2. useful in l1 to l1 transfers
        virtual void handle(Processor &proc){}
        Message(const Message&) = delete; // delete copy ctor explicitly since Message is a move only cls.
        Message& operator=(const Message&) = delete; // delete copy assignment ctor too.
        Message() : msgType(MsgType::NACK), from(0), to(0), fromL1(false), msgId(0) {}
        Message(MsgType msgType, int from, int to, bool fromL1, ull msgId) :
            msgType(msgType), from(from), to(to), fromL1(fromL1), msgId(msgId) {}
        Message(Message && other) noexcept : msgType(move(other.msgType)),
            from(move(other.from)),
            to(move(other.to)),
            fromL1(move(other.fromL1)),
            msgId(move(other.msgId)) {}

};
// just serves as an example to show inheritance model.
class Inv : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc);
        Inv(const Inv&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Inv& operator=(const Inv&) = delete; // delete copy assignment ctor too.
        Inv() : Message(), blockAddr(0) {}
        Inv(Inv&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Inv(MsgType msgType, int from, int to, bool fromL1, ull msgId, ull blockId) :
            Message(msgType, from, to, fromL1, msgId), blockAddr(blockId) {}
};
class InvAck : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc);
        InvAck(const Inv&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        InvAck& operator=(const Inv&) = delete; // delete copy assignment ctor too.
        InvAck() : Message() {}
        InvAck(Inv&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        InvAck(MsgType msgType, int from, int to, bool fromL1, ull msgId, ull blockId) :
            Message(msgType, from, to, fromL1, msgId), blockAddr(blockId) {}
};

class Get : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc);
        Get(const Inv&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Get& operator=(const Inv&) = delete; // delete copy assignment ctor too.
        Get() : Message() {}
        Get(Inv&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Get(MsgType msgType, int from, int to, bool fromL1, ull msgId, ull blockId) :
            Message(msgType, from, to, fromL1, msgId), blockAddr(blockId) {}
};

class Put : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc);
        Put(const Inv&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Put& operator=(const Inv&) = delete; // delete copy assignment ctor too.
        Put() : Message() {}
        Put(Inv&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Put(MsgType msgType, int from, int to, bool fromL1, ull msgId, ull blockId) :
            Message(msgType, from, to, fromL1, msgId), blockAddr(blockId) {}
};

class Cache {
    protected:
        // probably need these as public
        enum State {M, E, S, I};
        State state;
        vector<unordered_map<ull, ull>> cacheData;    // addr -> time map
        vector<set<timeAddr>> timeBlockAdded;  // stores time and addr for eviction.
        int nextGlobalMsgToProcess;
        int id; // id of cache
    public:
        unordered_map<ull, int> numInvToCollect;    // map from block address to number of invalidations to collect
        bool lastMsgProcessed;
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
        deque<unique_ptr<Message>> incomingMsg; // incoming messages from L2 and other L1s
        // need proc since we need to pass it to message.handle() that would be called inside
        virtual bool process(Processor &proc) = 0;
        Cache(int id, int numSets): id(id), cacheData(numSets), timeBlockAdded(numSets), nextGlobalMsgToProcess(0), numInvToCollect(0), lastMsgProcessed(false) {}
        Cache(Cache &&other) noexcept : id(move(other.id)),
            cacheData(move(other.cacheData)),
            timeBlockAdded(move(other.timeBlockAdded)),
            nextGlobalMsgToProcess(move(other.nextGlobalMsgToProcess)),
            state(move(other.state)), 
            numInvToCollect(move(other.numInvToCollect)),
            lastMsgProcessed(move(other.lastMsgProcessed)) {}
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
        L1(int id): Cache(id, NUM_L1_SETS), inputTrace(0), tempSpace(nullptr){
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
        // AYUSH: instead of vector of map, directory could only be a map of address to DirEnt
        unordered_map<ull, DirEnt>directory; // blockwise coherence. maps a block address to its entry.
    public:
        inline ull set_from_addr(ull addr) { return ((addr << (LOG_BLOCK_SIZE + LOG_L2_BANKS)) & L2_SET_BITS); }
        bool check_cache(ull addr){ return cacheData[set_from_addr(addr)].contains(addr); }
        bool process(Processor &proc);
        LLCBank(int id): Cache(id, NUM_L2_SETS_PER_BANK), directory(NUM_L2_SETS_PER_BANK) {}
        LLCBank(const LLCBank&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        LLCBank& operator=(const LLCBank&) = delete; // delete copy assignment ctor too.
        LLCBank(LLCBank &&other) noexcept : Cache(move(other)), directory(move(other.directory)) {}
        ~LLCBank(){}
};

class Processor {
    private:
        friend class Inv;
        friend class InvAck;
        friend class Get;
        int numCycles; // number of Cycles
        vector<L1> L1Caches;
        vector<LLCBank> L2Caches;
    public:
        Processor(): numCycles(0) {
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
                    progressMade |= L1Caches[i].process(*this);
                }
                for(int i = 0; i < NUM_CACHE; i++){
                    // process L2
                    progressMade |= L2Caches[i].process(*this);
                }
                if(!progressMade){break;}
            }
        }
};

// this wrapper would convert message to required type by taking ownership and then returns ownership
#define CALL_HANDLER(MSG_TYPE) \
    unique_ptr<MSG_TYPE> inv_msg(static_cast<MSG_TYPE *>(msg.release())); \
    inv_msg->handle(proc); \
    msg.reset(static_cast<Message *>(inv_msg.release()))

bool L1::process(Processor &proc){
    if(incomingMsg.empty()){    // process message from trace
        read_if_reqd();
        if(logs.empty() or nextGlobalMsgToProcess < logs.front().time){ return false; }

    }
    else {
        if(nextGlobalMsgToProcess < incomingMsg.front()->msgId){ return false; } // AYUSH: or should we then process messages from trace?
        auto msg = move(incomingMsg.front());
        incomingMsg.pop_front();
        // cast unique pointer of base class to derived class with appropriate variables
        switch (msg->msgType) {
            case Message::MsgType::INV: {
                CALL_HANDLER(Inv);
                break;
            }
            case Message::MsgType::INV_ACK:{
                CALL_HANDLER(Inv);
                break;
            }

            case Message::MsgType::GET:{
                CALL_HANDLER(Inv);
                break;
            }

            case Message::MsgType::NACK:{
                break;
            }

            case Message::MsgType::GETX:{
                break;
            }

            case Message::MsgType::PUT:{
                break;
            }

            case Message::MsgType::PUTX:{
                break;
            }

            case Message::MsgType::WB:{
                break;
            }

            case Message::MsgType::WB_ACK:{
                break;
            }
        }
        if(!lastMsgProcessed){
            incomingMsg.push_front(move(msg));
        }
    }
    return false;
}

bool LLCBank::process(Processor &proc){
    if(nextGlobalMsgToProcess < incomingMsg.front()->msgId){ return false; } // AYUSH: or should we then process messages from trace?
    auto msg = move(incomingMsg.front());
    incomingMsg.pop_front();
    // cast unique pointer of base class to derived class with appropriate variables
    switch (msg->msgType) {
        case Message::MsgType::INV: {
            CALL_HANDLER(Inv);
            break;
        }
        case Message::MsgType::INV_ACK:{
            CALL_HANDLER(Inv);
            break;
        }

        case Message::MsgType::GET:{
            CALL_HANDLER(Inv);
            break;
        }

        case Message::MsgType::NACK:{
            break;
        }

        case Message::MsgType::GETX:{
            break;
        }

        case Message::MsgType::PUT:{
            break;
        }

        case Message::MsgType::PUTX:{
            break;
        }

        case Message::MsgType::WB:{
            break;
        }

        case Message::MsgType::WB_ACK:{
            break;
        }
    }
    if(!lastMsgProcessed){
        incomingMsg.push_front(move(msg));
    }
    return false;
}

void Inv::handle(Processor &proc){
    auto &l1 = proc.L1Caches[to];
    // evict from L1
    l1.evict(l1.set_from_addr(blockAddr), blockAddr);
    if(fromL1){    // if the message is sent by L1, it means it is because someone requested S/I -> M
        // generate the inv ack message to be sent to the 'from' L1 cache as directory informs cache about the receiver of ack in this way.
        unique_ptr<Message> inv_ack(new InvAck(Message::MsgType::INV_ACK, to, from, true, msgId, blockAddr));
        proc.L1Caches[inv_ack->to].incomingMsg.push_back(move(inv_ack));
    }
    else {
        // generate inv ack to be sent to 'from' LLC
        unique_ptr<Message> inv_ack(new InvAck(Message::MsgType::INV_ACK, to, from, true, msgId, blockAddr));
        proc.L2Caches[inv_ack->to].incomingMsg.push_back(move(inv_ack));
    }
    l1.lastMsgProcessed = true;
}

void InvAck::handle(Processor &proc){
    if(fromL1){
        auto &l1 = proc.L1Caches[to];
        assert(l1.numInvToCollect.contains(blockAddr)); // since we collect an invalidation this means that this entry should contain this block address
        l1.numInvToCollect[blockAddr]--;
        if(l1.numInvToCollect[blockAddr] == 0){
            l1.numInvToCollect.erase(blockAddr);
        }
    }
    else {
        // this shouldn't happen 
        assert(false);
    }
}

void Get::handle(Processor &proc){
    if(fromL1){ // this means message is sent to LLC bank
        auto &l2 = proc.L2Caches[to];
        if(l2.directory.contains(blockAddr)){
            if(l2.directory[blockAddr].dirty){
                int owner = l2.directory[blockAddr].ownerId;
                unique_ptr<Message> get(new Get(Message::MsgType::GET, to, owner, false, msgId, blockAddr));
                proc.L1Caches[get->to].incomingMsg.push_back(move(get));
                l2.directory[blockAddr].pending = true;
                l2.directory[blockAddr].bitVector.reset();
                l2.directory[blockAddr].bitVector.set(from);
                l2.directory[blockAddr].bitVector.set(owner);
                l2.lastMsgProcessed = true;
            }
            else if(l2.directory[blockAddr].pending){
                l2.lastMsgProcessed = false;
            }
        }
        else {
            unique_ptr<Message> put(new Put(Message::MsgType::PUT, to, from, false, msgId, blockAddr));
            proc.L1Caches[put->to].incomingMsg.push_back(move(put));
            l2.lastMsgProcessed = true;
        }
    }
    else {  // this means that message is an intervention

    }
}