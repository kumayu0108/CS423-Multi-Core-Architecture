#pragma once
#include "declaration.hpp"
#include "message.hpp"

class Processor;

// this function only removes this addr from cache & returns if anything got evicted
bool Cache::evict(ull addr) {
    ull st = set_from_addr(addr);
    assert(st < cacheData.size());
    if(!cacheData[st].contains(addr)) { return false; }
    int time = cacheData[st][addr].time;
    assert(timeBlockAdded[st].contains({time, addr}));
    cacheData[st].erase(addr);
    timeBlockAdded[st].erase({time, addr});
    return true;
}
// // this function replaces this addr from cache and returns the replaced address & flag stating valid block
// blk Cache::replace(ull addr, ull timeAdded, int maxSetSize) {
//     ull st = set_from_addr(addr);
//     assert(st < cacheData.size());
//     if(cacheData[st].size() < maxSetSize) {cacheData[st][addr] = timeAdded; return {0, false};}
//     assert(cacheData[st].size() == maxSetSize and timeBlockAdded[st].size() == maxSetSize);
//     auto [time_evicted, addr_evicted] = *timeBlockAdded[st].begin();
//     assert(time_evicted < timeAdded);
//     timeBlockAdded.erase(timeBlockAdded.begin());
//     cacheData[st].erase(addr_evicted);
//     cacheData[st][addr] = timeAdded;
//     timeBlockAdded[st].insert({timeAdded, addr});
//     return {0, true};
// }
// AYUSH : Check the implementation
void Cache::update_priority(ull addr) { // this function updates the priority of address
    ull st = set_from_addr(addr);
    assert(st < cacheData.size() && cacheData[st].contains(addr));
    ull time = cacheData[st][addr].time;
    timeBlockAdded[st].erase({time, addr});
    ull nwTime = (timeBlockAdded[st].empty() ? 1 : (*timeBlockAdded[st].rbegin()).first + 1);
    timeBlockAdded[st].insert({nwTime, addr});
    cacheData[st][addr].time = nwTime;
}

// This function should not assign any cache state to the block, since the state could either be M, S or E.
cacheBlock L1::evict_replace(ull addr) {

}

// this wrapper would convert message to required type by taking ownership and then returns ownership
#define CALL_HANDLER(MSG_TYPE, TO_L1) \
    unique_ptr<MSG_TYPE> inv_msg(static_cast<MSG_TYPE *>(msg.release())); \
    inv_msg->handle(proc, TO_L1); \
    msg.reset(static_cast<Message *>(inv_msg.release()))

bool L1::process(Processor &proc) {
    bool progress = false;
    read_if_reqd();
    if(!logs.empty() and proc.nextGlobalMsgToProcess >= logs.front().time) { // process from trace
        if(proc.nextGlobalMsgToProcess == logs.front().time) {proc.nextGlobalMsgToProcess++;}
        progress = true;
        auto log = logs.front();
        logs.pop_front();
        if(check_cache(log.addr)) { // also update priority except for the case of upgr.
            if(log.isStore) { // write
                auto &cache_block = cacheData[set_from_addr(log.addr)][log.addr];
                if(cache_block.state == State::M) {
                    // can simply do the store.
                    assert(!getReplyWait.contains(log.addr) and !getXReplyWait.contains(log.addr));
                    update_priority(log.addr);
                }
                else if(cache_block.state == State::E) {
                    assert(!getReplyWait.contains(log.addr) and !getXReplyWait.contains(log.addr));
                    cache_block.state = State::M;   // transition to M
                    update_priority(log.addr);
                }
                else { // cache state = Shared
                    auto l2_bank_num = get_llc_bank(log.addr);
                    upgrReplyWait.insert(log.addr);
                    unique_ptr<Message> upgr(new Upgr(MsgType::UPGR, id, l2_bank_num, true, log.addr));
                    proc.L2Caches[l2_bank_num].incomingMsg.push_back(move(upgr));
                }
            }
            else {  // read; can always do if in cache
                update_priority(log.addr);
            }
        }
        else {
            if(log.isStore) {
                if(getXReplyWait.contains(log.addr)) {
                    // do nothing, already sent a Getx
                }
                else if(upgrReplyWait.contains(log.addr)) {
                    // AYUSH : what to do? send Getx?
                }
                else { // even if we have already sent a Get request, we need to send a Getx.
                    // AYUSH : do we send a upgr if we have sent a Get request???, but if we send a upgr and
                    auto l2_bank_num = get_llc_bank(log.addr);
                    getXReplyWait.insert(log.addr);
                    unique_ptr<Message> getx(new Getx(MsgType::GETX, id, l2_bank_num, true, log.addr));
                    proc.L2Caches[l2_bank_num].incomingMsg.push_back(move(getx));
                }
            }
            else {
                if(getReplyWait.contains(log.addr)) {
                    // already sent Get
                }
                else if(getXReplyWait.contains(log.addr)) {
                    // already sent Getx
                }
                else if(upgrReplyWait.contains(log.addr)) {
                    // AYUSH : what to do? send Getx?
                }
                else {
                    auto l2_bank_num = get_llc_bank(log.addr);
                    getReplyWait.insert(log.addr);
                    unique_ptr<Message> get(new Get(MsgType::GET, id, l2_bank_num, true, log.addr));
                    proc.L2Caches[l2_bank_num].incomingMsg.push_back(move(get));
                }
            }
        }
    }
    if(!incomingMsg.empty()) {
        progress = true;
        // since msgId isn't associated with messages, we can always process a message.
        // if(nextGlobalMsgToProcess < incomingMsg.front()->msgId) { return false; }
        auto msg = move(incomingMsg.front());
        incomingMsg.pop_front();
        // cast unique pointer of base class to derived class with appropriate variables
        switch (msg->msgType) {
            case MsgType::INV: {
                CALL_HANDLER(Inv, true);
                break;
            }
            case MsgType::INV_ACK:{
                CALL_HANDLER(InvAck, true);
                break;
            }

            case MsgType::GET:{
                CALL_HANDLER(Get, true);
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

bool L1::check_cache(ull addr) {
    assert((!cacheData[set_from_addr(addr)].contains(addr)) or
            (cacheData[set_from_addr(addr)].contains(addr) and cacheData[set_from_addr(addr)][addr].state != State::I));
    return cacheData[set_from_addr(addr)].contains(addr);
}

int L1::get_llc_bank(ull addr) {
    return ((addr >> LOG_BLOCK_SIZE) & L2_BANK_BITS);
}

bool LLCBank::process(Processor &proc) {
    // since msgId isn't associated with messages, we can always process a message.
    // if(nextGlobalMsgToProcess < incomingMsg.front()->msgId) { return false; }
    auto msg = move(incomingMsg.front());
    incomingMsg.pop_front();
    // cast unique pointer of base class to derived class with appropriate variables
    switch (msg->msgType) {
        case MsgType::INV: {
            CALL_HANDLER(Inv, false);
            break;
        }
        case MsgType::INV_ACK:{
            CALL_HANDLER(InvAck, false);
            break;
        }

        case MsgType::GET:{
            CALL_HANDLER(Get, false);
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

cacheBlock LLCBank::evict_replace(ull addr) {

}

void LLCBank::bring_from_mem_and_send_inv(Processor &proc, ull addr, int L1CacheNum, bool Getx) {
    assert(!check_cache(addr));
    auto st = set_from_addr(addr);
    assert(cacheData[st].size() == NUM_L2_WAYS && timeBlockAdded[st].size() == NUM_L2_WAYS);
    // check if we've already called this func for this address;
    for(auto &it : numInvAcksToCollectForIncl) {
        if(it.second.blockAddr == addr) {
            if(Getx) {
                // AYUSH : What to do here? NACKS? (doing nacks for now)
                unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::GETX, id, L1CacheNum, false, addr));
                proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
            }
            else {
                it.second.L1CacheNums.insert({L1CacheNum, Getx});
            }
            return;
        }
    }
    // Need to check if this has been called before and address to be evicted is also being evicted before. (waiting for inv acks)
    auto it = timeBlockAdded[st].begin();
    for( ; it != timeBlockAdded[st].end(); it++) {
        if(directory[st][it->second].pending) {continue;}
        if(numInvAcksToCollectForIncl.contains(it->second)) {continue;}
        break;
    }
    if (it == timeBlockAdded[st].end()) {
        // AYUSH : I think this could happen when what if all the blocks are supposed to be invalidated in this set and are waiting for acks; do we need to send nack then? (doing nacks for now)
        unique_ptr<Message> nack(new Nack(MsgType::NACK, (Getx ? MsgType::GETX : MsgType::GET), id, L1CacheNum, false, addr));
        proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
        assert(false);
    }
    auto [time, blockAddr_to_be_replaced] = *it;
    // AYUSH : would this cause any issue?
    directory[st][blockAddr_to_be_replaced].pending = true;
    auto &inv_ack_struct = numInvAcksToCollectForIncl[blockAddr_to_be_replaced];
    if(directory[st][blockAddr_to_be_replaced].dirty) {
        int owner = directory[st][blockAddr_to_be_replaced].ownerId;
        inv_ack_struct.waitForNumMessages = 1;
        inv_ack_struct.blockAddr = addr;
        inv_ack_struct.L1CacheNums.insert({L1CacheNum, Getx});
        unique_ptr<Message> inv(new Inv(MsgType::INV, id, owner, false, blockAddr_to_be_replaced));
        proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
    }
    else {
        inv_ack_struct.waitForNumMessages = directory[st][blockAddr_to_be_replaced].bitVector.count();
        inv_ack_struct.blockAddr = addr;
        inv_ack_struct.L1CacheNums.insert({L1CacheNum, Getx});
        for(int i = 0; i < directory[st][blockAddr_to_be_replaced].bitVector.size(); i++) {
            if(!directory[st][blockAddr_to_be_replaced].bitVector.test(i)) { continue; }
            unique_ptr<Message> inv(new Inv(MsgType::INV, id, i, false, blockAddr_to_be_replaced));
            proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
        }
    }
}

void Processor::run() {
    while(true) {
        bool progressMade = false;
        for(int i = 0; i < NUM_CACHE; i++) {
            // process L1
            progressMade |= L1Caches[i].process(*this);
        }
        for(int i = 0; i < NUM_CACHE; i++) {
            // process L2
            progressMade |= L2Caches[i].process(*this);
        }
        numCycles++;
        if(!progressMade) {break;}
    }
}