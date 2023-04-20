#pragma once
#include "declaration.hpp"
#include "message.hpp"

class Processor;

// this function only removes this addr from cache & returns if anything got evicted
bool L1::evict(ull addr) {
#ifdef PRINT_DEBUG
    if(addr == 5108736) {
        std::cout << "evicted by L1 : " << id <<"\n";
    }
#endif
    // ASSERT(!upgrReplyWait.contains(addr));  // This ASSERT doesn't hold as block may receive an inv req, if race happens between upgr and upgr/getx from another block
    ASSERT(check_cache(addr));
    ull st = set_from_addr(addr);
    ASSERT(st < cacheData.size());
    if(!cacheData[st].contains(addr)) { return false; }
    int time = cacheData[st][addr].time;
    ASSERT(timeBlockAdded[st].contains({time, addr}));
    cacheData[st].erase(addr);
    timeBlockAdded[st].erase({time, addr});
    return true;
}
// // this function replaces this addr from cache and returns the replaced address & flag stating valid block
// blk Cache::replace(ull addr, ull timeAdded, int maxSetSize) {
//     ull st = set_from_addr(addr);
//     ASSERT(st < cacheData.size());
//     if(cacheData[st].size() < maxSetSize) {cacheData[st][addr] = timeAdded; return {0, false};}
//     ASSERT(cacheData[st].size() == maxSetSize and timeBlockAdded[st].size() == maxSetSize);
//     auto [time_evicted, addr_evicted] = *timeBlockAdded[st].begin();
//     ASSERT(time_evicted < timeAdded);
//     timeBlockAdded.erase(timeBlockAdded.begin());
//     cacheData[st].erase(addr_evicted);
//     cacheData[st][addr] = timeAdded;
//     timeBlockAdded[st].insert({timeAdded, addr});
//     return {0, true};
// }
// AYUSH : Check the implementation
void Cache::update_priority(ull addr) { // this function updates the priority of address
    ull st = set_from_addr(addr);
    ASSERT(st < cacheData.size() && cacheData[st].contains(addr));
    ull time = cacheData[st][addr].time;
    ASSERT(timeBlockAdded[st].contains({time, addr}));
    timeBlockAdded[st].erase({time, addr});
    ull nwTime = (timeBlockAdded[st].empty() ? 1 : (*timeBlockAdded[st].rbegin()).first + 1);
    timeBlockAdded[st].insert({nwTime, addr});
    cacheData[st][addr].time = nwTime;
}

// This function assigns state that is passed to the block.
cacheBlock L1::evict_replace(Processor &proc, ull addr, State state) {
    ASSERT(state != State::I);
    auto st = set_from_addr(addr);
    auto &l1 = proc.L1Caches[id];
    auto &cacheData = l1.cacheData;
    auto &timeBlockAdded = l1.timeBlockAdded;

    if(check_cache(addr)) {
        evict(addr);
    }

    if(cacheData[st].size() < NUM_L1_WAYS) { // no need to evict, need to create new cache block
        ull nwTime = (timeBlockAdded[st].empty() ? 1 : (*timeBlockAdded[st].rbegin()).first + 1);
        // update cache state correctly.
        timeBlockAdded[st].insert({nwTime, addr});
        // we get put request only if in S state in directory.
        cacheData[st][addr] = {nwTime, state};
        return {0, State::I}; // need not evict, return default value;
    }

    ull evictAddr;
    bool flag = false;
    for(auto it : timeBlockAdded[st]) {
        // only need to check upgrReply because its the only case where block can be pending and in the cache.(both upgrAck and Acks havnt arrived)
        // Need to check numAckToCollect too because there can be case that upgrReply has arrived but Acks havent.
        if(numAckToCollect.contains(it.second) or upgrReplyWait.contains(it.second)) continue;
        evictAddr = it.second; flag = true;
        break;
    }
    // if this is true, evictAddrTime hasnt been updated. means, all blocks in set are waiting for Acks. Need to handle this.
    if(!flag) { ASSERT(false); }
    ASSERT(cacheData[st].contains(evictAddr));
    if(cacheData[st][evictAddr].state == State::M or cacheData[st][evictAddr].state == State::E) {
        int l2_bank = get_llc_bank(evictAddr); // get LLc bank.
        unique_ptr<Message> wb(new Wb(MsgType::WB, id, l2_bank, true, evictAddr, false));
        proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
    }
    evict(evictAddr); // update priority and all that.
    ull nwTime = (timeBlockAdded[st].empty() ? 1 : (*timeBlockAdded[st].rbegin()).first + 1);
    timeBlockAdded[st].insert({nwTime, addr});
    cacheData[st][addr] = {nwTime, state};

    // ASSERT(!l1.writeBackAckWait.contains(evictAddr));
    // l1.writeBackAckWait.insert(evictAddr);

    return {evictAddr, State::I}; // dont believe state. just returning time for now.
}

// this wrapper would convert message to required type by taking ownership and then returns ownership
#define CALL_HANDLER(MSG_TYPE, TO_L1) \
    unique_ptr<MSG_TYPE> inv_msg(static_cast<MSG_TYPE *>(msg.release())); \
    inv_msg->handle(proc, TO_L1); \
    msg.reset(static_cast<Message *>(inv_msg.release()))

bool L1::check_nacked_requests(Processor &proc) {
    if(outstandingNacks.empty()) { return false; }
    // AYUSH : should I serve only one nacked request or all nacked requests whose counter is 0? currently only serving one nack.
    ull block_id_nack_request = 0;
    bool issue_nacked_request = false;
    for(auto &it : outstandingNacks) {
        if(it.second.waitForNumCycles > 0) {
            it.second.waitForNumCycles--;
        }
        if(it.second.waitForNumCycles == 0) { issue_nacked_request = true; block_id_nack_request = it.first; }
    }
    if(!issue_nacked_request) { return false; }
    auto &nack_struct = outstandingNacks[block_id_nack_request];
    switch (nack_struct.msg)
    {
        case MsgType::GET: {
            ASSERT(getReplyWait.contains(block_id_nack_request));
            if(check_cache(block_id_nack_request)) { // block in cache
                // do nothing
            }
            else {
                unique_ptr<Message> get(new Get(MsgType::GET, id, get_llc_bank(block_id_nack_request), true, block_id_nack_request));
                proc.L2Caches[get->to].incomingMsg.push_back(move(get));
            }
            break;
        }

        // case MsgType::GETX: {
        //     ASSERT(getXReplyWait.contains(block_id_nack_request));
        //     unique_ptr<Message> getx(new Getx(MsgType::GETX, id, get_llc_bank(block_id_nack_request), true, block_id_nack_request));
        //     proc.L2Caches[getx->to].incomingMsg.push_back(move(getx));
        //     break;
        // }

        // case MsgType::UPGR: {
        //     // ASSERT(upgrReplyWait.contains(block_id_nack_request)); // this won't hold as I'm removing block from upgrReplyWait when it gets nacked.
        //     unique_ptr<Message> upgr(new Upgr(MsgType::UPGR, id, get_llc_bank(block_id_nack_request), true, block_id_nack_request));
        //     proc.L2Caches[upgr->to].incomingMsg.push_back(move(upgr));
        //     break;
        // }

        default:{
            ASSERT(nack_struct.msg == MsgType::GETX or nack_struct.msg == MsgType::UPGR);
            auto st = set_from_addr(block_id_nack_request);
            if(check_cache(block_id_nack_request)) { // if in cache, check state
                if(cacheData[st][block_id_nack_request].state == State::E or cacheData[st][block_id_nack_request].state == State::M) { // no need to send any message
                    // do nothing
                }
                else if(upgrReplyWait.contains(block_id_nack_request)) { // already sent an Upgrade
                    // do nothing
                }
                else  {
                    ASSERT(cacheData[st][block_id_nack_request].state != State::I);
                    upgrReplyWait.insert(block_id_nack_request);
                    unique_ptr<Message> upgr(new Upgr(MsgType::UPGR, id, get_llc_bank(block_id_nack_request), true, block_id_nack_request));
                    proc.L2Caches[upgr->to].incomingMsg.push_back(move(upgr));
                }
            }
            else {  // if not in cache send Getx 
                if(getReplyWait.contains(block_id_nack_request)) {  // if sent a Get
                    ASSERT(!getXReplyWait.contains(block_id_nack_request));
                    getReplyWait[block_id_nack_request].second = true; // send an upgrade when get returns
                }
                else if(upgrReplyWait.contains(block_id_nack_request)) { // situation when we have sent an upgrade, but received inv due to race between upgr and upgr/getx of other L1. This upgr would be nacked, and retried in the future.
                    // do nothing
                }
                else if(getXReplyWait.contains(block_id_nack_request)) { // already sent a Getx
                    // do nothing
                }
                else if(numAckToCollect.contains(block_id_nack_request)) { // received the block waiting for inv acks
                    // do nothing
                    // ASSERT(false);
                }
                else {
#ifdef PRINT_DEBUG
                    if(block_id_nack_request == 5108736 && proc.numCycles > 45723149) {
                        std::cout << "Getx (earlier nacked) sent by L1 : " << id << "\n";
                    }
#endif
                    getXReplyWait.insert(block_id_nack_request);
                    unique_ptr<Message> getx(new Getx(MsgType::GETX, id, get_llc_bank(block_id_nack_request), true, block_id_nack_request));
                    proc.L2Caches[getx->to].incomingMsg.push_back(move(getx));
                }
            }
            break;
        }
    }
    outstandingNacks.erase(block_id_nack_request);
    return true;
}

void L1::process_log(Processor &proc) {
    proc.totL1Accesses[id]++;
    auto log = logs.front();
    logs.pop_front();
    if(check_cache(log.addr)) { // also update priority except for the case of upgr.
        if(outstandingNacks.contains(log.addr) and outstandingNacks[log.addr].msg == MsgType::GET) { // if we were waiting to send Get earlier, but now for some reason have the block in cache, we can remove this nacked request.
            outstandingNacks.erase(log.addr);
        }

        if(log.isStore) { // write
            auto &cache_block = cacheData[set_from_addr(log.addr)][log.addr];
            if(cache_block.state == State::M) {
                // can simply do the store.
                if(getReplyWait.contains(log.addr) or getXReplyWait.contains(log.addr)) {
                    std :: cout << log.addr << "\n";
                }
                ASSERT(!getReplyWait.contains(log.addr) and !getXReplyWait.contains(log.addr));
                if(outstandingNacks.contains(log.addr)) { // if we were waiting to send GetX/Upgr earlier, but now for some reason have the block in cache, we can remove this nacked request.
                    outstandingNacks.erase(log.addr);
                }
                update_priority(log.addr);
            }
            else if(cache_block.state == State::E) {
                ASSERT(!getReplyWait.contains(log.addr) and !getXReplyWait.contains(log.addr));
                if(outstandingNacks.contains(log.addr)) { // if we were waiting to send GetX/Upgr earlier, but now for some reason have the block in cache, we can remove this nacked request.
                    outstandingNacks.erase(log.addr);
                }
                cache_block.state = State::M;   // transition to M
                update_priority(log.addr);
            }
            else if(upgrReplyWait.contains(log.addr)) { // alreadt sent upgrade
                proc.totL1UpgrMisses[id]++;
            }
            else if(numAckToCollect.contains(log.addr)) { // sent upgr/putx and received replies but waiting for replies.
                proc.totL1UpgrMisses[id]++;
            }
            else if(outstandingNacks.contains(log.addr)) {
                // were waiting to send either upgr or getx earlier, so wait until nack counter is 0.
                proc.totL1UpgrMisses[id]++;
            }
            else { // cache state = Shared
                proc.totL1UpgrMisses[id]++;
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
        proc.totL1Misses[id]++;
        if(log.isStore) {
            if(getXReplyWait.contains(log.addr)) {
                // do nothing, already sent a Getx
            }
            else if(upgrReplyWait.contains(log.addr)) { // this will happen when upgrade was sent, but since we received an inv, we invalidated the block, meaning someone sent Getx before, so our upgrade would be nacked.
                // do nothing
            }
            else if(getReplyWait.contains(log.addr)){
                getReplyWait[log.addr].second = true;
            }
            else if(numAckToCollect.contains(log.addr)) { // already sent and reecived upgr/getx replies but waiting for inv acks.
                // do nothing
            }
            else if(outstandingNacks.contains(log.addr)) { // if we were waiting for an upgrade or a getx to be sent, meaning we earlier wanted a write permission for the block, so we won't send a getx now, but would send it when nack counter is 0.
                if(outstandingNacks[log.addr].msg == MsgType::GETX or outstandingNacks[log.addr].msg == MsgType::UPGR) { // were waiting for upgr or getx
                    // do nothing
                }
                else {  // were waiting for get
                    outstandingNacks[log.addr].msg = MsgType::GETX; // would send a getx now, after nacked timer is over.
                }
            }
            else {
                // if(log.addr == 5108736 && proc.numCycles > 45723149) {std::cout << "getx sent by : " << id << " at 177 : " << proc.numCycles << "\n";}
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
                // ASSERT(false);
            }
            else if(outstandingNacks.contains(log.addr)) { // if waiting for either Get/GetX/Upgr do nothing and send request after timer is over.
                // do nothing
            }
            else {
                auto l2_bank_num = get_llc_bank(log.addr);
                getReplyWait[log.addr].first = proc.numCycles;
                getReplyWait[log.addr].second = false;
                // if(log.addr == 5108736 && proc.numCycles > 45723149) {std::cout << "get sent by : " << id << "at 199 : " << proc.numCycles << "\n";}
                unique_ptr<Message> get(new Get(MsgType::GET, id, l2_bank_num, true, log.addr));
                proc.L2Caches[l2_bank_num].incomingMsg.push_back(move(get));
            }
        }
    }
}

bool L1::process(Processor &proc) {
    bool progress = false;
    progress |= check_nacked_requests(proc);
    if(!logs.empty() and proc.nextGlobalMsgToProcess >= logs.front().time) { // process from trace
        if(proc.nextGlobalMsgToProcess == logs.front().time) {proc.nextGlobalMsgToProcess++;}
        progress = true;
        process_log(proc);
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
                CALL_HANDLER(Nack, true);
                break;
            }

            case MsgType::GETX:{
                CALL_HANDLER(Getx, true);
                break;
            }

            case MsgType::PUT:{
                CALL_HANDLER(Put, true);
                break;
            }

            case MsgType::PUTX:{
                CALL_HANDLER(Putx, true);
                break;
            }
            // this wb could also be received in response to an Invalidation due to maintaining inclusivity.
            case MsgType::WB:{
                CALL_HANDLER(Wb, true);
                break;
            }

            case MsgType::UPGR:{
                CALL_HANDLER(Upgr, true);
                break;
            }

            case MsgType::UPGR_ACK:{
                CALL_HANDLER(UpgrAck, true);
                break;
            }
        }
    }
    return progress;
}

bool L1::check_cache(ull addr) {
    ASSERT2((!cacheData[set_from_addr(addr)].contains(addr)) or
            (cacheData[set_from_addr(addr)].contains(addr) and cacheData[set_from_addr(addr)][addr].state != State::I), std::cout << addr <<"\n");
    return cacheData[set_from_addr(addr)].contains(addr);
}

int L1::get_llc_bank(ull addr) {
    return ((addr >> LOG_BLOCK_SIZE) & L2_BANK_BITS);
}

bool LLCBank::process(Processor &proc) {
    // since msgId isn't associated with messages, we can always process a message.
    // if(nextGlobalMsgToProcess < incomingMsg.front()->msgId) { return false; }
    if(incomingMsg.empty()) {return false;}
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
            CALL_HANDLER(Nack, false);
            break;
        }

        case MsgType::GETX:{
            CALL_HANDLER(Getx, false);
            break;
        }

        case MsgType::PUT:{
            CALL_HANDLER(Put, false);
            break;
        }

        case MsgType::PUTX:{
            CALL_HANDLER(Putx, false);
            break;
        }
        // this wb could also be received in response to an Invalidation due to maintaining inclusivity.
        case MsgType::WB:{
            CALL_HANDLER(Wb, false);
            break;
        }

        case MsgType::UPGR:{
            CALL_HANDLER(Upgr, false);
            break;
        }

        case MsgType::UPGR_ACK:{
            CALL_HANDLER(UpgrAck, false);
            break;
        }
    }
    return true;
}

// not reqd as of now, + insane design overhead.
cacheBlock LLCBank::evict_replace(Processor& proc, ull addr, State state) { ASSERT(false); return {0, State::I}; }

bool LLCBank::evict(ull addr) {
    ASSERT(check_cache(addr));
    ull st = set_from_addr(addr);
    ASSERT(st < cacheData.size());
    if(!cacheData[st].contains(addr)) { return false; }
    int time = cacheData[st][addr].time;
    ASSERT(timeBlockAdded[st].contains({time, addr}));
    cacheData[st].erase(addr);
    timeBlockAdded[st].erase({time, addr});
    directory[st].erase(addr);
    return true;
}

void LLCBank::bring_from_mem_and_send_inv(Processor &proc, ull addr, int L1CacheNum, bool Getx) {
    ASSERT(!check_cache(addr));
    auto st = set_from_addr(addr);
    ASSERT(cacheData[st].size() == NUM_L2_WAYS && timeBlockAdded[st].size() == NUM_L2_WAYS);
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
        if(numInvAcksToCollectForIncl.contains(it->second)) {continue;}
        if(directory[st].contains(it->second) and directory[st][it->second].pending) {continue;}
        break;
    }
    if (it == timeBlockAdded[st].end()) {
        // AYUSH : I think this could happen when what if all the blocks are supposed to be invalidated in this set and are waiting for acks; do we need to send nack then? (doing nacks for now)
        unique_ptr<Message> nack(new Nack(MsgType::NACK, (Getx ? MsgType::GETX : MsgType::GET), id, L1CacheNum, false, addr));
        proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
        ASSERT(false);
    }
    auto [time, blockAddr_to_be_replaced] = *it;

    if((!directory[st].contains(blockAddr_to_be_replaced)) or 
       (!directory[st][blockAddr_to_be_replaced].dirty and !directory[st][blockAddr_to_be_replaced].bitVector.any())) { // what if there are no sharers to block
        // Since block was not in directory, we can remove it directly and send a Putx reply since cache should be in M/E state in L1.
        ASSERT(check_cache(blockAddr_to_be_replaced));
        directory[st].erase(blockAddr_to_be_replaced);
        evict(blockAddr_to_be_replaced);
        directory[st].emplace(addr, DirEnt(false, -1));
        directory[st][addr].dirty = true;
        directory[st][addr].ownerId = L1CacheNum;
        // adding to L2 Cache Data
        ull nwTime = (timeBlockAdded[st].empty() ? 1 : (*timeBlockAdded[st].rbegin()).first + 1);
        timeBlockAdded[st].insert({nwTime, addr});
        cacheData[st][addr] = {nwTime, State::I}; // keeping it state 'I' as there's no notion of state in L2
        unique_ptr<Message> putx(new Putx(MsgType::PUTX, id, L1CacheNum, false, addr, 0, (Getx ? State::M : State::E)));
        proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
        return;
    }
    // AYUSH : would this cause any issue?
    directory[st][blockAddr_to_be_replaced].pending = true;
    directory[st][blockAddr_to_be_replaced].toBeReplaced = true;
    auto &inv_ack_struct = numInvAcksToCollectForIncl[blockAddr_to_be_replaced];
    if(directory[st][blockAddr_to_be_replaced].dirty) {
        int owner = directory[st][blockAddr_to_be_replaced].ownerId;
        inv_ack_struct.waitForNumMessages = 1;
        inv_ack_struct.blockAddr = addr;
        inv_ack_struct.L1CacheNums.insert({L1CacheNum, Getx});
        unique_ptr<Message> inv(new Inv(MsgType::INV, id, owner, false, blockAddr_to_be_replaced));
        ASSERT2((inv->to) < proc.L1Caches.size() and (inv->to) >= 0, std :: cerr << (inv->to) << "\n");
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
    for(int i =0 ; i < NUM_CACHE; i++) {
        ASSERT(L2Caches[i].numInvAcksToCollectForIncl.empty());
    }
    for(int i = 0; i < NUM_CACHE; i++) {
        auto &l1 = L1Caches[i];
        // ASSERT(l1.logs.empty());
        ASSERT(l1.outstandingNacks.empty());
        ASSERT2(l1.getReplyWait.empty(), std::cerr << l1.getReplyWait.size() << "\n"; for(auto &x : l1.getReplyWait) {std :: cerr << x.first << " -> " << x.second.first << " " << x.second.second << " | ";} std :: cerr << "\n"; );
        ASSERT(l1.getXReplyWait.empty());
        ASSERT(l1.upgrReplyWait.empty());
        ASSERT(l1.numAckToCollect.empty());
        ASSERT(l1.incomingMsg.empty());
    }
    std::cout << "Number Of Cycles         : " << numCycles << "\n";
    std::cout << "Number Of L1 Accesses    : "; for(int i = 0; i < NUM_CACHE; i++){ std :: cout << std :: setw(8) << totL1Accesses[i] << " ";} std :: cout << "\n";
    std::cout << "Number Of L1 Misses      : "; for(int i = 0; i < NUM_CACHE; i++){ std :: cout << std :: setw(8) << totL1Misses[i] << " ";} std :: cout << "\n";
    std::cout << "Number Of Upgrade misses : "; for(int i = 0; i < NUM_CACHE; i++){ std :: cout << std :: setw(8) << totL1UpgrMisses[i] << " ";} std :: cout << "\n";
    std::cout << "Number Of L2 Misses      : "; for(int i = 0; i < NUM_CACHE; i++){ std :: cout << std :: setw(8) << totL2Misses[i] << " ";} std :: cout << "\n";    
    std::vector <std::pair<std::string, MsgType>> all_enums = {{"Inv", INV}, {"Inv Ack", INV_ACK}, {"Nack", NACK}, {"Writeback", WB}, {"Get", GET}, {"Getx", GETX}, {"Put", PUT}, {"Putx", PUTX}, {"Upgr", UPGR}, {"Upgr Ack", UPGR_ACK}};
    std :: cout << "Number of Messages received by L1/L2:\n";
    for (auto msg : all_enums) {
        std :: cout << msg.first << "\n"; 
        std :: cout << " L1 : "; for(int i = 0; i < NUM_CACHE; i++){std :: cout << std :: setw(8) << msgReceivedL1[i][msg.second] << " ";}
        std :: cout << "\n";
        std :: cout << " L2 : "; for(int i = 0; i < NUM_CACHE; i++){std :: cout << std :: setw(8) << msgReceivedL2[i][msg.second] << " ";}
        std :: cout << "\n"; 
    }
}