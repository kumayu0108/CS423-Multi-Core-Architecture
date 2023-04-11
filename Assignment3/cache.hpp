#pragma once
#include "declaration.hpp"

class Processor;

// this function only removes this addr from cache & returns if anything got evicted
bool Cache::evict(ull addr){
    ull st = set_from_addr(addr);
    assert(st < cacheData.size());
    if(!cacheData[st].contains(addr)){ return false; }
    int time = cacheData[st][addr];
    assert(timeBlockAdded[st].contains({time, addr}));
    timeBlockAdded[st].erase({time, addr});
    return true;
}
// this function replaces this addr from cache and returns the replaced address & flag stating valid block
blk Cache::replace(ull addr, ull timeAdded, int maxSetSize){
    ull st = set_from_addr(addr);
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
// AYUSH : Check the implementation
void Cache::update_priority(ull addr){ // this function updates the priority of address
    ull st = set_from_addr(addr);
    assert(st < cacheData.size() && cacheData[st].contains(addr));
    int time = cacheData[st][addr];
    timeBlockAdded[st].erase({time, addr});
    int nwTime = (timeBlockAdded[st].empty() ? 1 : (*timeBlockAdded[st].rbegin()).first + 1);
    timeBlockAdded[st].insert({nwTime, addr});
    cacheData[st][addr] = nwTime;
}

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