#pragma once
#include "declaration.hpp"

// bool NACKStruct::operator<(const NACKStruct& other) {
//     return ((blockAddr == other.blockAddr) ? msg < other.msg : blockAddr < other.blockAddr);
// }

// It could happen that we receive inv requests for a block which is waiting for upgr, if upgr request races with another upgr/getx or if L2 sends inv for inclusive purpose.
void Inv::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::INV]++ : proc.msgReceivedL2[to][MsgType::INV]++;
    auto &l1 = proc.L1Caches[to];
    // evict from L1
    // if(l1.check_cache(blockAddr)){
    //     l1.evict(blockAddr);
    // }
    if(fromL1) {    // if the message is sent by L1, it means it is because someone requested S/I -> M
#ifdef PRINT_DEBUG
        if(blockAddr == 5108736 && proc.numCycles > 45723149) {
            std::cout << "inv received to L1 : " << to << "from L1: " << from << " at 18 : " << proc.numCycles << "\n";
        }
#endif
        ASSERT(from != to);
        ASSERT(toL1); // any invalidations sent by L1 would be sent to L1
        auto st = l1.set_from_addr(blockAddr);
        if(l1.check_cache(blockAddr) and (l1.cacheData[st][blockAddr].state == State::E or l1.cacheData[st][blockAddr].state == State::M)) {
            ASSERT(false);
        }
        else {
            // generate the inv ack message to be sent to the 'from' L1 cache as directory informs cache about the receiver of ack in this way.
            unique_ptr<Message> inv_ack(new InvAck(MsgType::INV_ACK, to, from, true, blockAddr));
            proc.L1Caches[inv_ack->to].incomingMsg.push_back(move(inv_ack));
        }
        if(l1.check_cache(blockAddr)) {
            l1.evict(blockAddr);
        }
    }
    else {
#ifdef PRINT_DEBUG
        if(blockAddr == 5108736 && proc.numCycles > 45723149) {
            std::cout << "inv received to L1 : " << to << "from L2 at 39 : " << proc.numCycles << "\n";
        }
#endif
        // ASSERT(!l1.upgrReplyWait.contains(blockAddr));
        // generate inv ack to be sent to 'from' LLC; for inclusive purpose.
        auto st = l1.set_from_addr(blockAddr);
        if(l1.check_cache(blockAddr) and (l1.cacheData[st][blockAddr].state == State::M or l1.cacheData[st][blockAddr].state == State::E)) { // send writeback if cachestate in M or E
            unique_ptr<Message> wb(new Wb(MsgType::WB, to, from, true, blockAddr, false));
            proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
        }
        else {
            unique_ptr<Message> inv_ack(new InvAck(MsgType::INV_ACK, to, from, true, blockAddr));
            proc.L2Caches[inv_ack->to].incomingMsg.push_back(move(inv_ack));
        }
        if(l1.check_cache(blockAddr)) {
            l1.evict(blockAddr);
        }
    }
}

void InvAck::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::INV_ACK]++ : proc.msgReceivedL2[to][MsgType::INV_ACK]++;
    if(fromL1) {
        if(toL1) { // inv ack sent to another L1
#ifdef PRINT_DEBUG
        if(blockAddr == 5108736 && proc.numCycles > 45723149) {
            std::cout << "inv ack received to L1 : " << to << " from L1: " << from << " at 64 : " << proc.numCycles << "\n";
        }
#endif
            auto &l1 = proc.L1Caches[to];
            // since we collect an invalidation this means that this entry should contain this block address; HOWEVER, can inv acks arrive before invs?
            // ASSERT(l1.numAckToCollect.contains(blockAddr));
            if (l1.numAckToCollect.contains(blockAddr)) {
                l1.numAckToCollect[blockAddr].numAckToCollect--;
            }
            else {
                l1.numAckToCollect[blockAddr].numAckToCollect = 0;
                l1.numAckToCollect[blockAddr].numAckToCollect--;
            }
            // this could happen in PUTX
            //NOTE:: The same code also exists in PutX/UpgrAck. If you change this here, change it there too
            if(l1.numAckToCollect[blockAddr].numAckToCollect == 0) { // update priority and put in cache.
                l1.evict_replace(proc, blockAddr, State::M);
                auto &inv_ack_struct = l1.numAckToCollect[blockAddr];
                auto st = l1.set_from_addr(blockAddr);
                if(inv_ack_struct.getReceived) {
                    ASSERT(!inv_ack_struct.getXReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, inv_ack_struct.to, true, blockAddr, "inv ack sent this after collecting all wb"));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.cacheData[st][blockAddr].state = State::S; // since it received Get earlier, so it transitions to S, after generating a writeback.
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                }
                else if(inv_ack_struct.getXReceived) {
                    ASSERT(!inv_ack_struct.getReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, inv_ack_struct.to, true, blockAddr, 0, State::M));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.evict(blockAddr); // since it received Getx, it has to also invalidate the block
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                }
                l1.numAckToCollect.erase(blockAddr);
            }
        }
        else {  // inv ack sent to L2 (for inclusivity)
#ifdef PRINT_DEBUG
        if(blockAddr == 5108736 && proc.numCycles > 45723149) {
            std::cout << "inv ack received to L2 from L1: " << from << " at 105 : " << proc.numCycles << "\n";
        }
#endif
            auto &l2 = proc.L2Caches[to];
            if(!l2.numInvAcksToCollectForIncl.contains(blockAddr)) { // this would happen when L2 sent inv for inc. and at the same time L1 evicted, thus L2's inv reached L1 afterwards.
                return;
            }
            ASSERT(l2.numInvAcksToCollectForIncl.contains(blockAddr));
            auto &inv_ack_struct = l2.numInvAcksToCollectForIncl[blockAddr];
            ASSERT(inv_ack_struct.waitForNumMessages > 0);
            ASSERT(l2.check_cache(blockAddr));
            inv_ack_struct.waitForNumMessages--;
            if(inv_ack_struct.waitForNumMessages == 0) {  // all inv acks received
                auto st = l2.set_from_addr(inv_ack_struct.blockAddr);
                ASSERT(st == l2.set_from_addr(blockAddr)); // since we replaced this to accomodate inv_ack_struct.blockAddr, so they must be from same set.
                ASSERT(!l2.check_cache(inv_ack_struct.blockAddr));
                l2.directory[st].erase(blockAddr);
                l2.evict(blockAddr);
                // l2.cacheData[st][inv_ack_struct]
                if(inv_ack_struct.L1CacheNums.begin()->second or inv_ack_struct.L1CacheNums.size() == 1) { // Getx request
                    ASSERT(inv_ack_struct.L1CacheNums.size() == 1); // only 1 Getx can be served
                    ASSERT(!l2.directory[st].contains(inv_ack_struct.blockAddr)); // cannot be in directory before.
                    int l1_ind_to_send = inv_ack_struct.L1CacheNums.begin()->first;
                    l2.directory[st].emplace(inv_ack_struct.blockAddr, DirEnt(false, -1));
                    l2.directory[st][inv_ack_struct.blockAddr].dirty = true;
                    l2.directory[st][inv_ack_struct.blockAddr].ownerId = l1_ind_to_send;
                    auto &dir_ent = l2.directory[st][inv_ack_struct.blockAddr];
                    ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
                    ASSERT(l2.cacheData[st].size() < NUM_L2_WAYS);
                    // Add to cache.
                    ull nwTime = (l2.timeBlockAdded[st].empty() ? 1 : (*l2.timeBlockAdded[st].rbegin()).first + 1);
                    l2.timeBlockAdded[st].insert({nwTime, inv_ack_struct.blockAddr});
                    l2.cacheData[st][inv_ack_struct.blockAddr] = {nwTime, State::I}; // keeping it state 'I' as there's no notion of state in L2
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, l1_ind_to_send, false, inv_ack_struct.blockAddr, 0, State::M));
                    proc.L1Caches[l1_ind_to_send].incomingMsg.push_back(move(putx));
                }
                else {  // Get request
                    l2.directory[st].emplace(inv_ack_struct.blockAddr, DirEnt(false, -1));
                    for(auto &[l1_ind_to_send, getx] : inv_ack_struct.L1CacheNums) {
                        ASSERT(!getx);
                        l2.directory[st][inv_ack_struct.blockAddr].bitVector.set(l1_ind_to_send);
                        unique_ptr<Message> put(new Put(MsgType::PUT, to, l1_ind_to_send, false, inv_ack_struct.blockAddr, "inv ack sent this for inclus. after collecting all wb"));
                        proc.L1Caches[l1_ind_to_send].incomingMsg.push_back(move(put));
                    }
                    ASSERT(l2.cacheData[st].size() < NUM_L2_WAYS);
                    // add to cache.
                    ull nwTime = (l2.timeBlockAdded[st].empty() ? 1 : (*l2.timeBlockAdded[st].rbegin()).first + 1);
                    l2.timeBlockAdded[st].insert({nwTime, inv_ack_struct.blockAddr});
                    l2.cacheData[st][inv_ack_struct.blockAddr] = {nwTime, State::I}; // keeping it state 'I' as there's no notion of state in L2
                }
                l2.numInvAcksToCollectForIncl.erase(blockAddr);
            }
        }
    }
    else {
        // this shouldn't happen
        ASSERT(false);
    }
}

// this also updates priority for cache block when evicted.
void Put::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::PUT]++ : proc.msgReceivedL2[to][MsgType::PUT]++;
    // VPG : make replace function virtual in Cache class.
    // in GET handle, we have a branch where we replace block and send invs to blocks.
    // basically replace for L2. Put needs us to write replace for L1 basically. Can split into two fns for reuse.
    if(toL1) {
        // L1 is the only receiver, gets data as a block. Now need to place it somewhere.
        ASSERT(!fromL1 or (fromL1 and from != to));
#ifdef PRINT_DEBUG
        if(blockAddr == 5108736 && proc.numCycles > 45723149) {
            if(fromL1)
                std::cout << "put sent by L1 : " << from << " to L1 : " << to << " at 117 : " << proc.numCycles << "\n";
            else 
                std::cout << "put sent by L2 : " << from << " to L1 : " << to << " at 119 : " << proc.numCycles << "\n";
        }
#endif
        auto &l1 = proc.L1Caches[to];
        //if the block already exists, do nothing. (is this even possible?)
        if(l1.check_cache(blockAddr)) { ASSERT(false); return; }
        ASSERT2(l1.getReplyWait.contains(blockAddr), std :: cerr << blockAddr << " L1:" << to << " fromL1:" << fromL1 << " numcycles : " << proc.numCycles << " " << debug_msg << "\n");  // since put would only be a reply to Get.
        if(l1.getReplyWait[blockAddr].second) { // meaning there was atleast a store miss.
            l1.upgrReplyWait.insert(blockAddr);
#ifdef PRINT_DEBUG
        if(blockAddr == 5108736 && proc.numCycles > 45723149) {
            std::cout << "upgr sent due to put at 133 : " << proc.numCycles << "\n";
        }
#endif
            unique_ptr<Message> upgr(new Upgr(MsgType::UPGR, to, l1.get_llc_bank(blockAddr), true, blockAddr));
            proc.L2Caches[upgr->to].incomingMsg.push_back(move(upgr));
        }
        l1.getReplyWait.erase(blockAddr); // this is not a part of evict_replace, its specific to put
        l1.evict_replace(proc, blockAddr, State::S);
        l1.update_priority(blockAddr);
    }
    else { // an LLC can never receive PUT. It's usually a reply w data.
        ASSERT(false); return;
    }
}

// update priority for cache blocks
void Get::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::GET]++ : proc.msgReceivedL2[to][MsgType::GET]++;
    if(fromL1) {
        if(!toL1) {
#ifdef PRINT_DEBUG
            if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "get sent by L1 : " << from << " to L2 at 136 : " << proc.numCycles << "\n";}
#endif            
            ASSERT(proc.L1Caches[from].getReplyWait.contains(blockAddr));
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            if(l2.directory[st].contains(blockAddr)) {
                ASSERT(l2.check_cache(blockAddr));
                auto &dir_ent = l2.directory[st][blockAddr];
                if(dir_ent.pending) {
                    unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::GET, to, from, false, blockAddr));
                    proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
                }
                else if(dir_ent.dirty) { // M state.
                    int owner = dir_ent.ownerId;
                    ASSERT(owner != from);
                    ASSERT(proc.L1Caches[from].getReplyWait.contains(blockAddr));
                    unique_ptr<Message> get(new Get(MsgType::GET, from, owner, true, blockAddr)); // L2 masks itself as the requestor
                    dir_ent.pending = true;
                    dir_ent.debug_string = "Get from L1:" + std::to_string(from) + " set pending true";
                    dir_ent.dirty = false;
                    dir_ent.bitVector.reset();
                    dir_ent.bitVector.set(from);
                    dir_ent.bitVector.set(owner);
                    proc.L1Caches[get->to].incomingMsg.push_back(move(get));
                }
                else { // shared state
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, from, false, blockAddr, "get sent this since dir was in shared state"));
                    dir_ent.bitVector.set(from);
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                }
            }
            else if(l2.check_cache(blockAddr)) { // if present in cache but not in dir
                l2.directory[st].emplace(blockAddr, DirEnt(false, -1));
                auto &dir_ent = l2.directory[st][blockAddr];
                unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0, State::E));
                dir_ent.ownerId = from;
                dir_ent.dirty = true;   // since this is the E state
                ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
                dir_ent.pending = false;
                dir_ent.bitVector.reset();
                proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
            }
            else {
                proc.totL2Misses[to]++;
                // TODO: evict, replace and update directory state. (done)
                if(l2.cacheData[st].size() < NUM_L2_WAYS) { // no need to send invalidations
                    l2.directory[st].emplace(blockAddr, DirEnt(false, -1));
                    auto &dir_ent = l2.directory[st][blockAddr]; 
                    // add to cache.
                    ull nwTime = (l2.timeBlockAdded[st].empty() ? 1 : (*l2.timeBlockAdded[st].rbegin()).first + 1);
                    l2.timeBlockAdded[st].insert({nwTime, blockAddr});
                    l2.cacheData[st][blockAddr] = {nwTime, State::I}; // keeping it state 'I' as there's no notion of state in L2

                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0, State::E));   // Putx to let the L1 cache know that it needs to put cache in E state
                    dir_ent.ownerId = from;
                    dir_ent.dirty = true;   // since this is the E state
                    ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
                    dir_ent.pending = false;
                    dir_ent.bitVector.reset();
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                }
                else {
                    ASSERT(from != -1);
                    l2.bring_from_mem_and_send_inv(proc, blockAddr, from, false);
                }
            }
        }
        else {  // this is the case where L2 had earlier sent a Get to owner cacheBlock and it saw this Get as being received from requestor L1
            // Get can also be sent by L2 to L1 if L1 has block in M state.
#ifdef PRINT_DEBUG
            if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "get sent by L1 : " << from << " to L1 : " << to << " at 190 : " << proc.numCycles << "\n";}
#endif
            ASSERT(proc.L1Caches[from].getReplyWait.contains(blockAddr));
            auto &l1 = proc.L1Caches[to];
            auto st = l1.set_from_addr(blockAddr);
            if(l1.check_cache(blockAddr)) {  // if in cache
                // AYUSH : do we need to check if the cache state of block is still M or not?; could it happen that the block has transitioned to S?
                ASSERT(l1.cacheData[st][blockAddr].state != State::S);
                auto st = l1.set_from_addr(blockAddr);
                l1.cacheData[st][blockAddr].state = State::S;
                int l2_bank = l1.get_llc_bank(blockAddr);
                ASSERT(proc.L1Caches[from].getReplyWait.contains(blockAddr));
                ASSERT(to != from);
                unique_ptr<Message> put(new Put(MsgType::PUT, to, from, true, blockAddr, "put sent after get reached another L1"));
                unique_ptr<Message> wb(new Wb(MsgType::WB, to, l2_bank, true, blockAddr, true));
                proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                // add entry to wait for wb ack
                // ASSERT(!l1.writeBackAckWait.contains(blockAddr));
                // l1.writeBackAckWait.insert(blockAddr);
            }
            else if(l1.numAckToCollect.contains(blockAddr)) { // we are in the midst of receiving inv acks for this block, we do not yet own the block.
                l1.numAckToCollect[blockAddr].getReceived = true;
                l1.numAckToCollect[blockAddr].to = from;
            }
            else {  // AYUSH : if we received get, but block has already been evicted , what to do??? I think we should drop this Get ; currently dropping
                // ASSERT(false);
#ifdef PRINT_DEBUG
                if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "dropping this Get\n";}
#endif
            }
        }
    }
    else {  // this should not happen since whenever L2 sends a Get to L1, it masks itself as the requestor L1, to let the receiver of Get know whom to send the message.
        ASSERT(false);
    }
}

// this also updates priority of blocks as much as possible who were in the pending state.
void Putx::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::PUTX]++ : proc.msgReceivedL2[to][MsgType::PUTX]++;
    if(toL1) {
#ifdef PRINT_DEBUG
        if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "putx sent by fromL1:" << fromL1 << " to L1: " << to << " received at 221 : " << proc.numCycles << "\n";}
#endif

        auto &l1 = proc.L1Caches[to];
        ASSERT(l1.getXReplyWait.contains(blockAddr) xor // putx send in reply to Getx
               l1.getReplyWait.contains(blockAddr)); // putx sent in reply to Get (E state)
        ASSERT(!l1.upgrReplyWait.contains(blockAddr));
        
        l1.getXReplyWait.contains(blockAddr) ? l1.getXReplyWait.erase(blockAddr) : l1.getReplyWait.erase(blockAddr);

        if(numAckToCollect == 0) { // no need to wait, can include block immediately.
            l1.evict_replace(proc, blockAddr, state);
        }
        else {
            if(l1.numAckToCollect.contains(blockAddr)) {
                l1.numAckToCollect[blockAddr].numAckToCollect += numAckToCollect;
            }
            else {
                l1.numAckToCollect[blockAddr].numAckToCollect = 0;
                l1.numAckToCollect[blockAddr].numAckToCollect += numAckToCollect;
            }
            auto &inv_ack_struct = l1.numAckToCollect[blockAddr];
            auto st = l1.set_from_addr(blockAddr);
            if(inv_ack_struct.numAckToCollect == 0) {
                // can go ahead and allocate block
                l1.evict_replace(proc, blockAddr, state);
                //NOTE:: The same code also exists in InvAck/UpgrAck. If you change this here, change it there too
                if(inv_ack_struct.getReceived) {
                    ASSERT(!inv_ack_struct.getXReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, inv_ack_struct.to, true, blockAddr, "put sent by putx"));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.cacheData[st][blockAddr].state = State::S; // since it received Get earlier, so it transitions to S, after generating a writeback.
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                    l1.update_priority(blockAddr);
                }
                else if(inv_ack_struct.getXReceived) {
                    ASSERT(!inv_ack_struct.getReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, inv_ack_struct.to, true, blockAddr, 0, State::M));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.evict(blockAddr); // since it received Getx, it has to also invalidate the block
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                }
                else {
                    l1.update_priority(blockAddr);
                }
                l1.numAckToCollect.erase(blockAddr);
            }
        }
    }
    else { // putx request cant be sent to L2.
        ASSERT(false);
    }
}

// update priority for cache blocks
void Getx::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::GETX]++ : proc.msgReceivedL2[to][MsgType::GETX]++;
    if(fromL1) {
        if(!toL1) { // received at LLC bank
#ifdef PRINT_DEBUG
            if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "getx sent by L1 : " << from << " to L2 at 279 : " << proc.numCycles << "\n";}
#endif
            
            ASSERT2(proc.L1Caches[from].getXReplyWait.contains(blockAddr), std :: cout << blockAddr << "\n");
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            if(l2.directory[st].contains(blockAddr)) {
                ASSERT(l2.check_cache(blockAddr));
                auto &dir_ent = l2.directory[st][blockAddr];
                if(dir_ent.pending) {
                    unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::GETX, to, from, false, blockAddr));
                    proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
                }
                else if(dir_ent.dirty) {
                    int owner = dir_ent.ownerId;
                    ASSERT2(owner != from, std :: cout << blockAddr << "\n");
                    unique_ptr<Message> getx(new Getx(MsgType::GETX, from, owner, true, blockAddr)); // L2 masks itself as the requestor
                    dir_ent.ownerId = from;
                    ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
                    dir_ent.pending = true;
                    dir_ent.debug_string = "GetX from (dir dirty) L1:" + std::to_string(from) + " set pending true ";
                    proc.L1Caches[getx->to].incomingMsg.push_back(move(getx));
                }
                else { // shared; would send invalidations to everyone
                    dir_ent.ownerId = from;
                    // dir_ent.debug_string = "GetX from (dir shared) L1:" + std::to_string(from) + " set pending true ";
                    dir_ent.dirty = true;
                    ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
                    int numInvToSend = 0;
                    for(int i = 0; i < dir_ent.bitVector.size(); i++) {
                        if(!dir_ent.bitVector.test(i) or i == from) { continue; }
                        dir_ent.bitVector.reset(i);
                        numInvToSend++;
                        unique_ptr<Message> inv(new Inv(MsgType::INV, from, i, true, blockAddr)); // L2 masks itself as the receiver of Getx so that all inv acks are sent to it.
                        proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
                    }
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, numInvToSend, State::M));
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                }
            }
            else if(l2.check_cache(blockAddr)) { // if present in cache but not in dir
                //PRAMODH:: Should this even happen?? If inserting correctly, shouldnt occur
                l2.directory[st].emplace(blockAddr, DirEnt(false, -1));
                auto &dir_ent = l2.directory[st][blockAddr];
                // if not present in Dir, first time block goes in E state
                unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0, State::E));
                dir_ent.dirty = true;
                dir_ent.ownerId = from;
                ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
                dir_ent.pending = false;
                dir_ent.bitVector.reset();
                proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
            }
            else {
                proc.totL2Misses[to]++;
                
                if(l2.cacheData[st].size() < NUM_L2_WAYS) { // no need to send invalidations
                    l2.directory[st].emplace(blockAddr, DirEnt(false, -1));
                    auto &dir_ent = l2.directory[st][blockAddr];
                    // add to cache.
                    ull nwTime = (l2.timeBlockAdded[st].empty() ? 1 : (*l2.timeBlockAdded[st].rbegin()).first + 1);
                    l2.timeBlockAdded[st].insert({nwTime, blockAddr});
                    l2.cacheData[st][blockAddr] = {nwTime, State::I}; // keeping it state 'I' as there's no notion of state in L2

                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0, State::E));
                    dir_ent.dirty = true;
                    dir_ent.ownerId = from;
                    ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
                    dir_ent.pending = false;
                    dir_ent.bitVector.reset();
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                }
                else {
                    l2.bring_from_mem_and_send_inv(proc, blockAddr, from, true);
                }
            }
        }
        else { // received at L1 cache
#ifdef PRINT_DEBUG
            if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "getx sent by L1 : " << from << " to L1 : " << to << " at 136 : " << proc.numCycles << "\n";}
#endif
            ASSERT(proc.L1Caches[from].getXReplyWait.contains(blockAddr));
            auto &l1 = proc.L1Caches[to];
            if(l1.check_cache(blockAddr)) {  // if in cache
                auto st = l1.set_from_addr(blockAddr);
                l1.evict(blockAddr);
                int l2_bank = l1.get_llc_bank(blockAddr);
                unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, true, blockAddr, 0, State::M));
                unique_ptr<Message> wb(new Wb(MsgType::WB, to, l2_bank, true, blockAddr, true));
                proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                // ASSERT(!l1.writeBackAckWait.contains(blockAddr));
                // l1.writeBackAckWait.insert(blockAddr);
            }
            else if(l1.numAckToCollect.contains(blockAddr)) { // we are in the midst of receiving inv acks for this block, we do not yet own the block.
                l1.numAckToCollect[blockAddr].getXReceived = true;
                l1.numAckToCollect[blockAddr].to = from;
            }
            else {  // currently dropping this Getx; this would happen when L1 evicted this block and is now receiving GetX
#ifdef PRINT_DEBUG
                if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "dropping this GetX\n";}
#endif
            }
        }
    }
    else { // this should not happen since whenever L2 sends a Getx to L1, it masks itself as the requestor L1, to let the receiver of Get know whom to send the message.
        ASSERT(false);
    }
}

void Wb::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::WB]++ : proc.msgReceivedL2[to][MsgType::WB]++;
    if(fromL1) {
        if(!toL1) { // wb sent to L2
#ifdef PRINT_DEBUG
            if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "wb sent by L1 : " << from << " to L2 at 136 : " << proc.numCycles << "\n";}
#endif
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            auto &dir_ent = l2.directory[st][blockAddr];
            if(dir_ent.pending) {  // we expected a writeback.
                if(!dir_ent.toBeReplaced) {
                    dir_ent.pending = false;
                    if(!inResponseToGet) { // need to forward it.
#ifdef PRINT_DEBUG
                        if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "forwarding this wb\n";}
#endif
                        if(dir_ent.ownerId == from) { // directory going from M -> S, since owner did not change; forward a Put
                            int l1_to_send_get = -1;
                            for(int i = 0; i < dir_ent.bitVector.size(); i++) {
                                if(dir_ent.bitVector.test(i) and i != from) {
                                    l1_to_send_get = i;
                                }
                            }
                            ASSERT(!proc.L1Caches[from].check_cache(blockAddr));
                            ASSERT2(l1_to_send_get != -1, std::cout << blockAddr << "\n");
                            ASSERT(proc.L1Caches[l1_to_send_get].getReplyWait.contains(blockAddr));
                            dir_ent.ownerId = l1_to_send_get;
                            dir_ent.dirty = true;
                            unique_ptr<Message> putx(new Putx(MsgType::PUTX, from, l1_to_send_get, true, blockAddr, 0, State::M));
                            proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                        }
                        else { // since owner changed, directory going from M -> M state, forward a Putx.
                            unique_ptr<Message> putx(new Putx(MsgType::PUTX, from, dir_ent.ownerId, true, blockAddr, 0, State::M));
                            proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                        }
                    }
                    else {  // this means that writeback was in response to a get and we do not need to forward it

                    }
                }
                else { // evicted for inclusive purpose; handled directory.
                    ASSERT(dir_ent.ownerId == from);
                    ASSERT(l2.numInvAcksToCollectForIncl.contains(blockAddr));
                    l2.directory[st].erase(blockAddr);
                    l2.evict(blockAddr);
                    auto &inv_ack_struct = l2.numInvAcksToCollectForIncl[blockAddr];
                    ASSERT(inv_ack_struct.waitForNumMessages == 1);
                    // The requesting message could be Get/GetX both.
                    ASSERT(!l2.directory[st].contains(inv_ack_struct.blockAddr));
                    // ASSERT(inv_ack_struct.L1CacheNums.size() == 1);
                    if(inv_ack_struct.L1CacheNums.size() == 1) { // if only one requestor, send Putx
                        // Add to cache
                        ull nwTime = (l2.timeBlockAdded[st].empty() ? 1 : (*l2.timeBlockAdded[st].rbegin()).first + 1);
                        l2.timeBlockAdded[st].insert({nwTime, inv_ack_struct.blockAddr});
                        l2.cacheData[st][inv_ack_struct.blockAddr] = {nwTime, State::I}; // keeping it state 'I' as there's no notion of state in L2
                        int l1_cache_num = (inv_ack_struct.L1CacheNums.begin()->first);
                        l2.directory[st].emplace(inv_ack_struct.blockAddr, DirEnt(false, -1));
                        l2.directory[st][inv_ack_struct.blockAddr].ownerId = l1_cache_num;
                        l2.directory[st][inv_ack_struct.blockAddr].dirty = true;
                        ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
                        l2.directory[st][inv_ack_struct.blockAddr].bitVector.reset();
                        l2.directory[st][inv_ack_struct.blockAddr].pending = false;
                        l2.directory[st][inv_ack_struct.blockAddr].toBeReplaced = false;

                        unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, l1_cache_num, false, inv_ack_struct.blockAddr, 0, State::M));
                        proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                    }
                    else {  // if many requestors, send Put
                        // Add to cache
                        ull nwTime = (l2.timeBlockAdded[st].empty() ? 1 : (*l2.timeBlockAdded[st].rbegin()).first + 1);
                        l2.timeBlockAdded[st].insert({nwTime, inv_ack_struct.blockAddr});
                        l2.cacheData[st][inv_ack_struct.blockAddr] = {nwTime, State::I}; // keeping it state 'I' as there's no notion of state in L2
                        for(auto &[l1_cache_num, getx] : inv_ack_struct.L1CacheNums) {
                            ASSERT(!getx);
                            l2.directory[st].emplace(inv_ack_struct.blockAddr, DirEnt(false, -1));
                            l2.directory[st][inv_ack_struct.blockAddr].bitVector.set(l1_cache_num);
                            unique_ptr<Message> put(new Put(MsgType::PUT, to, l1_cache_num, false, inv_ack_struct.blockAddr));
                            proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                        }
                    }
                    l2.numInvAcksToCollectForIncl.erase(blockAddr);
                }
            }
            else {  // this means that cache block was evicted and we did not send any inv request.
                ASSERT(dir_ent.dirty);
                ASSERT(dir_ent.ownerId == from);
                ASSERT(!dir_ent.toBeReplaced);
#ifdef PRINT_DEBUG
                if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "wb because getting evicted " << proc.numCycles << "\n";}
#endif
                // since block gets evicted, i think we should erase entry from directory
                l2.directory[st].erase(blockAddr);
                // dir_ent.dirty = false;
                // dir_ent.bitVector.reset();
                // dir_ent.bitVector.set(from);
            }
        }
        else { // cannot happen; wb cannot be sent to L1
            ASSERT(false);
        }
    }
    else { // cannot happen; wb cannot come from L2
        ASSERT(false);
    }
}

void UpgrAck::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::UPGR_ACK]++ : proc.msgReceivedL2[to][MsgType::UPGR_ACK]++;
    if(!fromL1) {
        if(toL1) {
#ifdef PRINT_DEBUG
            if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "upgr_ack sent by L2 to L1 : " << to << " at 136 : " << proc.numCycles << "\n";}
#endif
            auto &l1 = proc.L1Caches[to];
            ASSERT(l1.upgrReplyWait.contains(blockAddr)); // waiting for upgrAck
            l1.upgrReplyWait.erase(blockAddr); // got the reply.
            if(l1.numAckToCollect.contains(blockAddr)) { // already receiving acks
                l1.numAckToCollect[blockAddr].numAckToCollect += numAckToCollect;
            }
            else { // initalise the structure :)
                l1.numAckToCollect[blockAddr].numAckToCollect = 0;
                l1.numAckToCollect[blockAddr].numAckToCollect += numAckToCollect;
            }
            auto &inv_ack_struct = l1.numAckToCollect[blockAddr];
            auto st = l1.set_from_addr(blockAddr);
            if(inv_ack_struct.numAckToCollect == 0) {
                // can finally go ahead and change state of block
                l1.cacheData[st][blockAddr].state = State::M;
                //NOTE:: The same code also exists in InvAck/PutX. If you change this here, change it there too
                if(inv_ack_struct.getReceived) {
                    ASSERT(!inv_ack_struct.getXReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, inv_ack_struct.to, true, blockAddr, "put sent after upgrading"));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.cacheData[st][blockAddr].state = State::S; // since it received Get earlier, so it transitions to S, after generating a writeback.
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                    l1.update_priority(blockAddr);
                }
                else if(inv_ack_struct.getXReceived) {
                    ASSERT(!inv_ack_struct.getReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, inv_ack_struct.to, true, blockAddr, 0, State::M));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.evict(blockAddr); // since it received Getx, it has to also invalidate the block
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                }
                else { l1.update_priority(blockAddr); }
                l1.numAckToCollect.erase(blockAddr);
            }
        }
        else { // upgrAck cant go to L2.
            ASSERT(false);
        }
    }
    else { // upgrack cant come from L1 at all
        ASSERT(false);
    }
}

void Upgr::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::UPGR]++ : proc.msgReceivedL2[to][MsgType::UPGR]++;
    if(fromL1) {
        if(!toL1) { // to L2
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            ASSERT(l2.directory[st].contains(blockAddr)); // since we receive a upgr message, directory should have this entry
            ASSERT(l2.check_cache(blockAddr));
#ifdef PRINT_DEBUG
            if(blockAddr == 5108736 && proc.numCycles > 45723149) {
                std::cout << "upgr sent by L1 : " << from << " to L2 at 485 : " << proc.numCycles << " bitvec:" << l2.directory[st][blockAddr].bitVector << "\n";
            }
#endif
            auto &dir_ent = l2.directory[st][blockAddr];
            if(dir_ent.pending) {
                unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::UPGR, to, from, false, blockAddr));
                proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
            }
            else if(dir_ent.dirty) { // this means that some other block requested a GetX, and before it's invalidation reached L1, L1 generated Upgr, need to NACK this.
                if(dir_ent.ownerId == from) {
                    std :: cout << blockAddr << "\n";
                }
                ASSERT(dir_ent.ownerId != from);
                unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::UPGR, to, from, false, blockAddr));
                proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
            }
            else {
                ASSERT2(proc.L1Caches[from].upgrReplyWait.contains(blockAddr), std::cout << blockAddr << "\n");
                ASSERT(dir_ent.bitVector.test(from));
                unique_ptr<Message> upgr_ack(new UpgrAck(MsgType::UPGR_ACK, to, from, false, blockAddr, dir_ent.bitVector.count() - 1));
                proc.L1Caches[upgr_ack->to].incomingMsg.push_back(move(upgr_ack));
                for(int i = 0; i < dir_ent.bitVector.size(); i++) {
                    if(!dir_ent.bitVector.test(i) or i == from) continue;
                    dir_ent.bitVector.reset(i);
                    unique_ptr<Message> inv(new Inv(MsgType::INV, from, i, true, blockAddr)); // masking to let this l1 know it needs to send inv to 'from' l1.
                    proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
                }
                dir_ent.dirty = true;
                dir_ent.ownerId = from;
                ASSERT2(dir_ent.ownerId >= 0 and dir_ent.ownerId < proc.L1Caches.size(), std :: cerr << dir_ent.ownerId);
            }
        }
        else {  // should not happen as L2 won't forward an Upgr request
            ASSERT(false);
        }
    }
    else { // should not happen
        ASSERT(false);
    }
}

void Nack::handle(Processor &proc, bool toL1) {
    toL1 ? proc.msgReceivedL1[to][MsgType::NACK]++ : proc.msgReceivedL2[to][MsgType::NACK]++;
    if(fromL1) { // L1 won't send a NACK
        ASSERT(false);
    }
    else {
        if(toL1) {
            auto &l1 = proc.L1Caches[to];
            ASSERT(!l1.outstandingNacks.contains(blockAddr));
            switch (nackedMsg)
            {
                case MsgType::GET: {
                    ASSERT(l1.getReplyWait.contains(blockAddr));
#ifdef PRINT_DEBUG
                    if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "Get request for L1:" << to << " nacked at 523 : " << proc.numCycles << "\n";}
#endif
                    l1.outstandingNacks[blockAddr].msg = MsgType::GET;
                    break;
                }

                case MsgType::GETX: {
                    ASSERT(l1.getXReplyWait.contains(blockAddr));
#ifdef PRINT_DEBUG
                    if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "GetX request for L1:" << to << " nacked at 523 : " << proc.numCycles << "\n";}
#endif
                    l1.outstandingNacks[blockAddr].msg = MsgType::GETX;
                    break;
                }

                case MsgType::UPGR: {
                    ASSERT(l1.upgrReplyWait.contains(blockAddr));
#ifdef PRINT_DEBUG
                    if(blockAddr == 5108736 && proc.numCycles > 45723149) {std::cout << "Upgr request for L1:" << to << " nacked at 523 : " << proc.numCycles << "\n";}
#endif
                    l1.upgrReplyWait.erase(blockAddr);
                    l1.outstandingNacks[blockAddr].msg = MsgType::UPGR;
                    break;
                }

                default: {
                    std :: cout << "got invalid nack : " << msgType << "\n";
                    ASSERT(false);  // only these Nacks would occur
                    break;
                }
            }
            l1.outstandingNacks[blockAddr].waitForNumCycles = NACK_WAIT_CYCLES;
        }
        else { // L2 won't send a NACK to L2
            ASSERT(false);
        }
    }
}