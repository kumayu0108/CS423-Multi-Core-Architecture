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

using std::set, std::unordered_map, std::vector, std::deque, std::bitset, std::map, std::unordered_set, std::pair;
using std::unique_ptr, std::make_unique, std::move;

#define ull unsigned long long
#define timeAddr pair<unsigned long, ull>
#define blk pair<ull, bool>

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

enum MsgType { INV, INV_ACK, NACK, WB, WB_ACK, GET, GETX, PUT, PUTX, UPGR, UPGR_ACK};
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
    ull blockAddr;

    NACKStruct(): msg(MsgType::GET), blockAddr(0) {}
    NACKStruct(NACKStruct&& other) noexcept: msg(move(other.msg)), blockAddr(move(other.blockAddr)) {}
    bool operator<(const NACKStruct& other) { return ((blockAddr == other.blockAddr) ? msg < other.msg : blockAddr < other.blockAddr); }
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

class Wb : public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc, bool toL1);
        Wb(const Wb&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Wb& operator=(const Wb&) = delete; // delete copy assignment ctor too.
        Wb() : Message(), blockAddr(0) {}
        Wb(Wb&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Wb(MsgType msgType, int from, int to, bool fromL1, ull blockId) :
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

class Putx: public Message {
    public:
        ull blockAddr; // which cache block ADDR to be evicted?
        void handle(Processor &proc, bool toL1);
        Putx(const Putx&) = delete; // delete copy ctor explicitly, since it's a move only cls.
        Putx& operator=(const Putx&) = delete; // delete copy assignment ctor too.
        Putx() : Message() {}
        Putx(Putx&& other) noexcept : Message(move(other)), blockAddr(move(other.blockAddr)) {}
        Putx(MsgType msgType, int from, int to, bool fromL1, ull blockId) :
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
        struct cacheBlock {
            ull time; 
            State state;
            cacheBlock() : time(0), state(State::I) {}
            cacheBlock(cacheBlock && other) noexcept : time(move(other.time)), state(move(other.state)) {}
        };
        vector<unordered_map<ull, cacheBlock>> cacheData;    // addr -> time map
        vector<set<timeAddr>> timeBlockAdded;  // stores time and addr for eviction.
        map<NACKStruct, int> outstandingNacks;
        unordered_map<ull, State> cacheState;
        int id; // id of cache
    public:
        deque<unique_ptr<Message>> incomingMsg; // incoming messages from L2 and other L1s
        virtual bool check_cache(ull addr) = 0;
        virtual ull set_from_addr(ull addr) = 0;
        // this function only removes this addr from cache & returns if anything got evicted
        bool evict(ull addr){
            ull st = set_from_addr(addr);
            assert(st < cacheData.size());
            if(!cacheData[st].contains(addr)){ return false; }
            int time = cacheData[st][addr].time;
            assert(timeBlockAdded[st].contains({time, addr}));
            timeBlockAdded[st].erase({time, addr});
            return true;
        }
        // // this function replaces this addr from cache and returns the replaced address & flag stating valid block
        // blk replace(ull addr, ull timeAdded, int maxSetSize){
        //     ull st = set_from_addr(addr);
        //     assert(st < cacheData.size());
        //     if(cacheData[st].size() < maxSetSize){cacheData[st][addr].time = timeAdded; return {0, false};}
        //     assert(cacheData[st].size() == maxSetSize and timeBlockAdded[st].size() == maxSetSize);
        //     auto [time_evicted, addr_evicted] = *timeBlockAdded[st].begin();
        //     assert(time_evicted < timeAdded);
        //     timeBlockAdded.erase(timeBlockAdded.begin());
        //     cacheData[st].erase(addr_evicted);
        //     cacheData[st][addr].time = timeAdded;
        //     timeBlockAdded[st].insert({timeAdded, addr});
        //     return {addr_evicted, true};
        // }
        // AYUSH : Check the implementation
        void update_priority(ull addr){ // this function updates the priority of address and shouldn't change cacheState
            ull st = set_from_addr(addr);
            assert(st < cacheData.size() && cacheData[st].contains(addr));
            int time = cacheData[st][addr].time;
            timeBlockAdded[st].erase({time, addr});
            int nwTime = (timeBlockAdded[st].empty() ? 1 : (*timeBlockAdded[st].rbegin()).first + 1);
            timeBlockAdded[st].insert({nwTime, addr});
            cacheData[st][addr].time = nwTime;
        }
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
        unordered_map<ull, ull> numInvToCollect; // block -> num; used when we want to collect inv acks for Get request.
        inline ull set_from_addr(ull addr) { return ((addr << LOG_BLOCK_SIZE) & L1_SET_BITS); }
        bool check_cache(ull addr){ 
            assert((!cacheData[set_from_addr(addr)].contains(addr)) or 
                   (cacheData[set_from_addr(addr)].contains(addr) and cacheData[set_from_addr(addr)][addr].state != State::I));
            return cacheData[set_from_addr(addr)].contains(addr); 
        }
        // int get_llc_bank(ull addr) {
        //     return ();
        // }
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
            numInvToCollect(move(other.numInvToCollect)),
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
        friend class InvAck;
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

// this wrapper would convert message to required type by taking ownership and then returns ownership
#define CALL_HANDLER(MSG_TYPE, TO_L1) \
    unique_ptr<MSG_TYPE> inv_msg(static_cast<MSG_TYPE *>(msg.release())); \
    inv_msg->handle(proc, TO_L1); \
    msg.reset(static_cast<Message *>(inv_msg.release()))

bool L1::process(Processor &proc){
    bool progress = false;
    read_if_reqd();
    if(!logs.empty() and proc.nextGlobalMsgToProcess >= logs.front().time){ // process from trace
        if(proc.nextGlobalMsgToProcess == logs.front().time){proc.nextGlobalMsgToProcess++;}
        progress = true;
        auto log = logs.front();
        logs.pop_front();
        if(log.isStore){

        }
        else {
            
        }
    }
    if(!incomingMsg.empty()){ 
        progress = true;
        // since msgId isn't associated with messages, we can always process a message.
        // if(nextGlobalMsgToProcess < incomingMsg.front()->msgId){ return false; }
        auto msg = move(incomingMsg.front());
        incomingMsg.pop_front();
        // cast unique pointer of base class to derived class with appropriate variables
        switch (msg->msgType) {
            case MsgType::INV: {
                CALL_HANDLER(Inv, true);
                break;
            }
            case MsgType::INV_ACK:{
                CALL_HANDLER(Inv, true);
                break;
            }

            case MsgType::GET:{
                CALL_HANDLER(Inv, true);
                break;
            }

            case MsgType::NACK:{
                break;
            }

            case MsgType::GETX:{
                break;
            }

            case MsgType::PUT:{
                break;
            }

            case MsgType::PUTX:{
                break;
            }

            case MsgType::WB:{
                break;
            }

            case MsgType::WB_ACK:{
                break;
            }
        }
    }
    return progress;
}

bool LLCBank::process(Processor &proc){
    // since msgId isn't associated with messages, we can always process a message.
    // if(nextGlobalMsgToProcess < incomingMsg.front()->msgId){ return false; }
    auto msg = move(incomingMsg.front());
    incomingMsg.pop_front();
    // cast unique pointer of base class to derived class with appropriate variables
    switch (msg->msgType) {
        case MsgType::INV: {
            CALL_HANDLER(Inv, false);
            break;
        }
        case MsgType::INV_ACK:{
            CALL_HANDLER(Inv, false);
            break;
        }

        case MsgType::GET:{
            CALL_HANDLER(Inv, false);
            break;
        }

        case MsgType::NACK:{
            break;
        }

        case MsgType::GETX:{
            break;
        }

        case MsgType::PUT:{
            break;
        }

        case MsgType::PUTX:{
            break;
        }
        // this wb could also be received in response to an Invalidation due to maintaining inclusivity.
        case MsgType::WB:{
            break;
        }

        case MsgType::WB_ACK:{
            break;
        }
    }
    return false;
}

void LLCBank::bring_from_mem_and_send_inv(Processor &proc, ull addr, int L1CacheNum, bool Getx){
    assert(!check_cache(addr));
    auto st = set_from_addr(addr);
    assert(cacheData[st].size() == NUM_L2_WAYS && timeBlockAdded[st].size() == NUM_L2_WAYS);
    // check if we've already called this func for this address;
    for(auto &it : numInvAcksToCollectForIncl) {
        if(it.second.blockAddr == addr){
            if(Getx){
                // AYUSH : What to do here? NACKS?
            }
            else {
              it.second.L1CacheNums.insert({L1CacheNum, Getx});
            }
            return;
        }
    }
    // Need to check if this has been called before and address to be evicted is also being evicted before. (waiting for inv acks)
    auto it = timeBlockAdded[st].begin();
    for( ; it != timeBlockAdded[st].end(); it++){
        if(directory[st][it->second].pending){continue;}
        if(numInvAcksToCollectForIncl.contains(it->second)){continue;}
        break;
    }
    if (it == timeBlockAdded[st].end()){
        // AYUSH : I think this could happen when what if all the blocks are supposed to be invalidated in this set and are waiting for acks; do we need to send nack then?
        assert(false);
    }
    auto [time, blockAddr_to_be_replaced] = *it;
    directory[st][blockAddr_to_be_replaced].pending = true;
    auto &inv_ack_struct = numInvAcksToCollectForIncl[blockAddr_to_be_replaced];
    if(directory[st][blockAddr_to_be_replaced].dirty) {
        int owner = directory[st][blockAddr_to_be_replaced].ownerId;
        inv_ack_struct.waitForNumMessages = 1;
        inv_ack_struct.blockAddr = addr;
        inv_ack_struct.L1CacheNums.insert({L1CacheNum, Getx});
        unique_ptr<Message> inv(new Inv(MsgType::INV, owner, id, false, blockAddr_to_be_replaced));
        proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
    }
    else {
        inv_ack_struct.waitForNumMessages = directory[st][blockAddr_to_be_replaced].bitVector.count();
        inv_ack_struct.blockAddr = addr;
        inv_ack_struct.L1CacheNums.insert({L1CacheNum, Getx});
        for(int i = 0; i < directory[st][blockAddr_to_be_replaced].bitVector.size(); i++){
            if(!directory[st][blockAddr_to_be_replaced].bitVector.test(i)){ continue; }
            unique_ptr<Message> inv(new Inv(MsgType::INV, i, id, false, blockAddr_to_be_replaced));
            proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
        }
    }
}

void Inv::handle(Processor &proc, bool toL1){
    auto &l1 = proc.L1Caches[to];
    // evict from L1
    l1.evict(blockAddr);
    if(fromL1){    // if the message is sent by L1, it means it is because someone requested S/I -> M
        assert(toL1); // any invalidations sent by L1 would be sent to L1
        // generate the inv ack message to be sent to the 'from' L1 cache as directory informs cache about the receiver of ack in this way.
        unique_ptr<Message> inv_ack(new InvAck(MsgType::INV_ACK, to, from, true, blockAddr));
        proc.L1Caches[inv_ack->to].incomingMsg.push_back(move(inv_ack));
    }
    else {
        // generate inv ack to be sent to 'from' LLC
        unique_ptr<Message> inv_ack(new InvAck(MsgType::INV_ACK, to, from, true, blockAddr));
        proc.L2Caches[inv_ack->to].incomingMsg.push_back(move(inv_ack));
    }
}

void InvAck::handle(Processor &proc, bool toL1){
    if(fromL1){
        if(toL1){ // inv ack sent to another L1
            auto &l1 = proc.L1Caches[to];
            // since we collect an invalidation this means that this entry should contain this block address; HOWEVER, can inv acks arrive before invs?
            assert(l1.numInvToCollect.contains(blockAddr)); 
            l1.numInvToCollect[blockAddr]--;
            if(l1.numInvToCollect[blockAddr] == 0){
                l1.numInvToCollect.erase(blockAddr);
            }
        }
        else {  // inv ack sent to L2 (for inclusivity)
            auto &l2 = proc.L2Caches[to];
            assert(l2.numInvAcksToCollectForIncl.contains(blockAddr));
            auto &inv_ack_struct = l2.numInvAcksToCollectForIncl[blockAddr];
            inv_ack_struct.waitForNumMessages--;
            if(inv_ack_struct.waitForNumMessages == 0){  // all inv acks received
                auto st = l2.set_from_addr(inv_ack_struct.blockAddr);
                assert(st == l2.set_from_addr(blockAddr)); // since we replaced this to accomodate inv_ack_struct.blockAddr, so they must be from same set.
                l2.directory[st].erase(blockAddr);
                if(inv_ack_struct.L1CacheNums.begin()->second){ // Getx request
                    assert(inv_ack_struct.L1CacheNums.size() == 1); // only 1 Getx can be served
                    int l1_ind_to_send = inv_ack_struct.L1CacheNums.begin()->first;
                    l2.directory[st][inv_ack_struct.blockAddr].dirty = true;
                    l2.directory[st][inv_ack_struct.blockAddr].ownerId = l1_ind_to_send;
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, l1_ind_to_send, false, inv_ack_struct.blockAddr));
                    proc.L1Caches[l1_ind_to_send].incomingMsg.push_back(move(putx));
                }
                else {  // Get request
                    for(auto &[l1_ind_to_send, getx] : inv_ack_struct.L1CacheNums){
                        assert(!getx);
                        l2.directory[st][inv_ack_struct.blockAddr].bitVector.set(l1_ind_to_send);
                        unique_ptr<Message> put(new Put(MsgType::PUT, to, l1_ind_to_send, false, inv_ack_struct.blockAddr));
                        proc.L1Caches[l1_ind_to_send].incomingMsg.push_back(move(put));
                    }
                }   
            }
        }
    }
    else {
        // this shouldn't happen
        assert(false);
    }
}

void Get::handle(Processor &proc, bool toL1){
    if(fromL1){ // this means message is sent to LLC bank
        // assert(toL1); // L1 won't send Get to L1; but L2 would send Get to L1 masking itself as L1
        if(!toL1){
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            if(l2.directory[st].contains(blockAddr)){
                if(l2.directory[st][blockAddr].dirty){
                    int owner = l2.directory[st][blockAddr].ownerId;
                    unique_ptr<Message> get(new Get(MsgType::GET, from, owner, true, blockAddr)); // L2 masks itself as the requestor
                    l2.directory[st][blockAddr].pending = true;
                    l2.directory[st][blockAddr].bitVector.reset();
                    l2.directory[st][blockAddr].bitVector.set(from);
                    l2.directory[st][blockAddr].bitVector.set(owner);
                    proc.L1Caches[get->to].incomingMsg.push_back(move(get));
                }
                else if(l2.directory[st][blockAddr].pending){
                    unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::GET, to, from, false, blockAddr));
                    proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
                }
                else {
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, from, false, blockAddr));
                    l2.directory[st][blockAddr].bitVector.set(from);
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                }
            }
            else if(l2.check_cache(blockAddr)) { // if present in cache but not in dir
                unique_ptr<Message> put(new Put(MsgType::PUT, to, from, false, blockAddr));
                l2.directory[st][blockAddr].ownerId = from;
                l2.directory[st][blockAddr].dirty = true;
                l2.directory[st][blockAddr].pending = false;
                l2.directory[st][blockAddr].bitVector.reset();
                proc.L1Caches[put->to].incomingMsg.push_back(move(put));
            }
            else {
                // TODO: evict, replace and update directory state. (done)
                auto st = l2.set_from_addr(blockAddr);
                if(l2.cacheData[st].size() < NUM_L2_WAYS){ // no need to send invalidations
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, from, false, blockAddr));
                    l2.directory[st][blockAddr].ownerId = from;
                    l2.directory[st][blockAddr].dirty = true;
                    l2.directory[st][blockAddr].pending = false;
                    l2.directory[st][blockAddr].bitVector.reset();
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                }
                else {
                    l2.bring_from_mem_and_send_inv(proc, blockAddr, from);
                }
            }
        }
        else {  // this is the case where L2 had earlier sent a Get to owner cacheBlock and it saw this Get as being received from requestor L1
            auto &l1 = proc.L1Caches[to];
            if(l1.check_cache(blockAddr)){  // if in cache
                // AYUSH : do we need to check if the cache state of block is still M or not?; could it happen that the block has transitioned to S?
                auto st = l1.set_from_addr(blockAddr);
                l1.cacheData[st][blockAddr].state = State::S;
                // int l2_bank = get_llc_bank(addr);
                // unique_ptr<Message> put(new Put(MsgType::PUT, , from, true, blockAddr));
                // unique_ptr<Message> wb(new Wb(MsgType::WB, , from, true, blockAddr));
            }
            else {  // if we received get, but block has already been evicted , what to do???
                assert(false);
            }
        }
    }
    else {  // this should not happen since whenever L2 sends a Get to L1, it masks itself as the requestor L1, to let the receiver of Get know whom to send the message.
        assert(false);
    }
}

void Put::handle(Processor &proc, bool toL1) {

}

void Putx::handle(Processor &proc, bool toL1) {

}

void Nack::handle(Processor &proc, bool toL1) {

}

void Wb::handle(Processor &proc, bool toL1) {

}
// Increase counter after processing. (done)
// Maintain outstanding misses table. (done)
// remove message ID from message class, no need to look at order.In each cycle, exactly one machine access should be issued, increment counter. (done)
// For processing messages, in each cycle, deque one message from each queue, and process it. (done)
// Implement NACK. Remove lastMsgProcessed(Done, need to implement NACK). (done)
// maintain a nastruct ck table, {GetX/Get, block} -> countdown_timer. map<NACKStruct, timer> (done)
// Add boolean variable to CALL_HANDLE macro (toL1, invalidations). (done)
// Maintain separate cache state, per block inside L1. -> modify cacheData to contain a struct (done)
// Replace and Evict, make them virtual and implement them with messages for L1 and L2. Add appropriate asserts.
// split files.