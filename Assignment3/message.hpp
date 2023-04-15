#pragma once
#include "declaration.hpp"

bool NACKStruct::operator<(const NACKStruct& other) {
    return ((blockAddr == other.blockAddr) ? msg < other.msg : blockAddr < other.blockAddr);
}

void Nack::handle(Processor &proc, bool toL1) {}
void UpgrAck::handle(Processor &proc, bool toL1) {}

void Inv::handle(Processor &proc, bool toL1) {
    auto &l1 = proc.L1Caches[to];
    // evict from L1
    l1.evict(blockAddr);
    if(fromL1) {    // if the message is sent by L1, it means it is because someone requested S/I -> M
        assert(toL1); // any invalidations sent by L1 would be sent to L1
        // generate the inv ack message to be sent to the 'from' L1 cache as directory informs cache about the receiver of ack in this way.
        unique_ptr<Message> inv_ack(new InvAck(MsgType::INV_ACK, to, from, true, blockAddr));
        proc.L1Caches[inv_ack->to].incomingMsg.push_back(move(inv_ack));
    }
    else {
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
    }
}

void InvAck::handle(Processor &proc, bool toL1) {
    if(fromL1) {
        if(toL1) { // inv ack sent to another L1
            auto &l1 = proc.L1Caches[to];
            // since we collect an invalidation this means that this entry should contain this block address; HOWEVER, can inv acks arrive before invs?
            // assert(l1.numAckToCollect.contains(blockAddr));
            if (l1.numAckToCollect.contains(blockAddr)) {
                l1.numAckToCollect[blockAddr].numAckToCollect--;
            }
            else {
                l1.numAckToCollect[blockAddr].numAckToCollect = 0;
                l1.numAckToCollect[blockAddr].numAckToCollect--;
            }
            // this could happen in PUTX
            //NOTE:: The same code also exists in Putx. If you change this here, change it there too
            if(l1.numAckToCollect[blockAddr].numAckToCollect == 0) { // update priority and put in cache.
                l1.evict_replace(proc, blockAddr, State::M);
                auto &inv_ack_struct = l1.numAckToCollect[blockAddr];
                auto st = l1.set_from_addr(blockAddr);
                if(inv_ack_struct.getReceived) {
                    assert(!inv_ack_struct.getXReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, inv_ack_struct.to, true, blockAddr));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.cacheData[st][blockAddr].state = State::S; // since it received Get earlier, so it transitions to S, after generating a writeback.
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                }
                else if(inv_ack_struct.getXReceived) {
                    assert(!inv_ack_struct.getReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, inv_ack_struct.to, true, blockAddr, 0));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.evict(blockAddr); // since it received Getx, it has to also invalidate the block
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                }
                l1.numAckToCollect.erase(blockAddr);
            }
        }
        else {  // inv ack sent to L2 (for inclusivity)
            auto &l2 = proc.L2Caches[to];
            assert(l2.numInvAcksToCollectForIncl.contains(blockAddr));
            auto &inv_ack_struct = l2.numInvAcksToCollectForIncl[blockAddr];
            assert(inv_ack_struct.waitForNumMessages > 0);
            inv_ack_struct.waitForNumMessages--;
            if(inv_ack_struct.waitForNumMessages == 0) {  // all inv acks received
                auto st = l2.set_from_addr(inv_ack_struct.blockAddr);
                assert(st == l2.set_from_addr(blockAddr)); // since we replaced this to accomodate inv_ack_struct.blockAddr, so they must be from same set.
                l2.directory[st].erase(blockAddr);
                if(inv_ack_struct.L1CacheNums.begin()->second) { // Getx request
                    assert(inv_ack_struct.L1CacheNums.size() == 1); // only 1 Getx can be served
                    int l1_ind_to_send = inv_ack_struct.L1CacheNums.begin()->first;
                    l2.directory[st][inv_ack_struct.blockAddr].dirty = true;
                    l2.directory[st][inv_ack_struct.blockAddr].ownerId = l1_ind_to_send;
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, l1_ind_to_send, false, inv_ack_struct.blockAddr, 0));
                    proc.L1Caches[l1_ind_to_send].incomingMsg.push_back(move(putx));
                }
                else {  // Get request
                    for(auto &[l1_ind_to_send, getx] : inv_ack_struct.L1CacheNums) {
                        assert(!getx);
                        l2.directory[st][inv_ack_struct.blockAddr].bitVector.set(l1_ind_to_send);
                        unique_ptr<Message> put(new Put(MsgType::PUT, to, l1_ind_to_send, false, inv_ack_struct.blockAddr));
                        proc.L1Caches[l1_ind_to_send].incomingMsg.push_back(move(put));
                    }
                }
                l2.numInvAcksToCollectForIncl.erase(blockAddr);
            }
        }
    }
    else {
        // this shouldn't happen
        assert(false);
    }
}

// update priority for cache block when evicted.
void Put::handle(Processor &proc, bool toL1) {
    // VPG : make replace function virtual in Cache class.
    // in GET handle, we have a branch where we replace block and send invs to blocks.
    // basically replace for L2. Put needs us to write replace for L1 basically. Can split into two fns for reuse.
    if(toL1) {
        // L1 is the only receiver, gets data as a block. Now need to place it somewhere.
        auto &l1 = proc.L1Caches[to];
        //if the block already exists, do nothing. (is this even possible?)
        if(l1.check_cache(blockAddr)) { return; }
        assert(l1.getReplyWait.contains(blockAddr));  // since put would only be a reply to Get.
        l1.getReplyWait.erase(blockAddr); // this is not a part of evict_replace, its specific to put
        l1.evict_replace(proc, blockAddr, State::S);
    }
    else { // an LLC can never receive PUT. It's usually a reply w data.
        assert(false); return;
    }
}

// update priority for cache blocks
void Get::handle(Processor &proc, bool toL1) {
    if(fromL1) {
        if(!toL1) {
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            if(l2.directory[st].contains(blockAddr)) {
                auto &dir_ent = l2.directory[st][blockAddr];
                if(dir_ent.pending) {
                    unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::GET, to, from, false, blockAddr));
                    proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
                }
                else if(dir_ent.dirty) { // M state.
                    int owner = dir_ent.ownerId;
                    unique_ptr<Message> get(new Get(MsgType::GET, from, owner, true, blockAddr)); // L2 masks itself as the requestor
                    dir_ent.pending = true;
                    dir_ent.dirty = false;
                    dir_ent.bitVector.reset();
                    dir_ent.bitVector.set(from);
                    dir_ent.bitVector.set(owner);
                    proc.L1Caches[get->to].incomingMsg.push_back(move(get));
                }
                else { // shared state
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, from, false, blockAddr));
                    dir_ent.bitVector.set(from);
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                }
            }
            else if(l2.check_cache(blockAddr)) { // if present in cache but not in dir
                auto &dir_ent = l2.directory[st][blockAddr];
                unique_ptr<Message> put(new Put(MsgType::PUT, to, from, false, blockAddr));
                dir_ent.ownerId = from;
                dir_ent.dirty = true;
                dir_ent.pending = false;
                dir_ent.bitVector.reset();
                proc.L1Caches[put->to].incomingMsg.push_back(move(put));
            }
            else {
                // TODO: evict, replace and update directory state. (done)
                auto &dir_ent = l2.directory[st][blockAddr];
                if(l2.cacheData[st].size() < NUM_L2_WAYS) { // no need to send invalidations
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0));   // Putx to let the L1 cache know that it needs to put cache in E state
                    dir_ent.ownerId = from;
                    dir_ent.dirty = true;   // since this is the E state
                    dir_ent.pending = false;
                    dir_ent.bitVector.reset();
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                }
                else {
                    l2.bring_from_mem_and_send_inv(proc, blockAddr, from, false);
                }
            }
        }
        else {  // this is the case where L2 had earlier sent a Get to owner cacheBlock and it saw this Get as being received from requestor L1
            // Get can also be sent by L2 to L1 if L1 has block in M state.
            auto &l1 = proc.L1Caches[to];
            auto st = l1.set_from_addr(blockAddr);
            if(l1.check_cache(blockAddr)) {  // if in cache
                // AYUSH : do we need to check if the cache state of block is still M or not?; could it happen that the block has transitioned to S?
                assert(l1.cacheData[st][blockAddr].state != State::S);
                auto st = l1.set_from_addr(blockAddr);
                l1.cacheData[st][blockAddr].state = State::S;
                int l2_bank = l1.get_llc_bank(blockAddr);
                unique_ptr<Message> put(new Put(MsgType::PUT, to, from, true, blockAddr));
                unique_ptr<Message> wb(new Wb(MsgType::WB, to, l2_bank, true, blockAddr, true));
                proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                // add entry to wait for wb ack
                assert(!l1.writeBackAckWait.contains(blockAddr));
                l1.writeBackAckWait.insert(blockAddr);
            }
            else if(l1.numAckToCollect.contains(blockAddr)) { // we are in the midst of receiving inv acks for this block, we do not yet own the block.
                l1.numAckToCollect[blockAddr].getReceived = true;
                l1.numAckToCollect[blockAddr].to = from;
            }
            else {  // AYUSH : if we received get, but block has already been evicted , what to do??? I think we should drop this Get ; currently dropping
                // assert(false);
            }
        }
    }
    else {  // this should not happen since whenever L2 sends a Get to L1, it masks itself as the requestor L1, to let the receiver of Get know whom to send the message.
        assert(false);
    }
}

//update priority of blocks as much as possible who were in the pending state.
void Putx::handle(Processor &proc, bool toL1) {
    if(toL1) {
        auto &l1 = proc.L1Caches[to];
        assert(l1.getXReplyWait.contains(blockAddr)); // putx send in reply only to Getx
        l1.getXReplyWait.erase(blockAddr); // got a reply!
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
                //NOTE:: The same code also exists in InvAck. If you change this here, change it there too
                if(inv_ack_struct.getReceived) {
                    assert(!inv_ack_struct.getXReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> put(new Put(MsgType::PUT, to, inv_ack_struct.to, true, blockAddr));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.cacheData[st][blockAddr].state = State::S; // since it received Get earlier, so it transitions to S, after generating a writeback.
                    proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                }
                else if(inv_ack_struct.getXReceived) {
                    assert(!inv_ack_struct.getReceived); // cannot have received both get and getx as directory would go in pending state.
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, inv_ack_struct.to, true, blockAddr, 0));
                    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l1.get_llc_bank(blockAddr), true, blockAddr, true));
                    l1.evict(blockAddr); // since it received Getx, it has to also invalidate the block
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                }
                l1.numAckToCollect.erase(blockAddr);
            }
        }
    }
    else { // putx request cant be sent to L2.
        assert(false);
    }
}

// update priority for cache blocks
void Getx::handle(Processor &proc, bool toL1) {
    if(fromL1) {
        if(!toL1) { // received at LLC bank
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            if(l2.directory[st].contains(blockAddr)) {
                auto &dir_ent = l2.directory[st][blockAddr];
                if(dir_ent.pending) {
                    unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::GETX, to, from, false, blockAddr));
                    proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
                }
                else if(dir_ent.dirty) {
                    int owner = dir_ent.ownerId;
                    unique_ptr<Message> getx(new Getx(MsgType::GETX, from, owner, true, blockAddr)); // L2 masks itself as the requestor
                    dir_ent.ownerId = from;
                    dir_ent.pending = true;
                    proc.L1Caches[getx->to].incomingMsg.push_back(move(getx));
                }
                else { // shared; would send invalidations to everyone
                    dir_ent.ownerId = from;
                    dir_ent.pending = true;
                    dir_ent.dirty = true;
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, dir_ent.bitVector.count(), State::M));
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                    for(int i = 0; i < dir_ent.bitVector.size(); i++) {
                        if(!dir_ent.bitVector.test(i)) { continue; }
                        unique_ptr<Message> inv(new Inv(MsgType::INV, from, i, true, blockAddr)); // L2 masks itself as the receiver of Getx so that all inv acks are sent to it.
                        proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
                    }
                }
            }
            else if(l2.check_cache(blockAddr)) { // if present in cache but not in dir
                //PRAMODH:: Should this even happen?? If inserting correctly, shouldnt occur
                auto &dir_ent = l2.directory[st][blockAddr];
                // if not present in Dir, first time block goes in E state
                unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0, State::E));
                dir_ent.dirty = true;
                dir_ent.ownerId = from;
                dir_ent.pending = false;
                dir_ent.bitVector.reset();
                proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
            }
            else {
                auto &dir_ent = l2.directory[st][blockAddr];
                if(l2.cacheData[st].size() < NUM_L2_WAYS) { // no need to send invalidations
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0, State::E));
                    dir_ent.dirty = true;
                    dir_ent.ownerId = from;
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
            auto &l1 = proc.L1Caches[to];
            if(l1.check_cache(blockAddr)) {  // if in cache
                auto st = l1.set_from_addr(blockAddr);
                l1.evict(blockAddr);
                int l2_bank = l1.get_llc_bank(blockAddr);
                unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, true, blockAddr, 0, State::M));
                unique_ptr<Message> wb(new Wb(MsgType::WB, to, l2_bank, true, blockAddr, true));
                proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                assert(!l1.writeBackAckWait.contains(blockAddr));
                l1.writeBackAckWait.insert(blockAddr);
            }
            else if(l1.numAckToCollect.contains(blockAddr)) { // we are in the midst of receiving inv acks for this block, we do not yet own the block.
                l1.numAckToCollect[blockAddr].getXReceived = true;
                l1.numAckToCollect[blockAddr].to = from;
            }
            else {  // currently dropping this Getx; this would happen when L1 evicted this block and is now receiving

            }
        }
    }
    else { // this should not happen since whenever L2 sends a Getx to L1, it masks itself as the requestor L1, to let the receiver of Get know whom to send the message.
        assert(false);
    }
}

void Wb::handle(Processor &proc, bool toL1) {
    if(fromL1) {
        if(!toL1) { // wb sent to L2
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            auto &dir_ent = l2.directory[st][blockAddr];
            if(dir_ent.pending) {  // we expected a writeback.
                dir_ent.pending = false;
                if(!inResponseToGet) { // need to forward it.
                    if(dir_ent.ownerId == from) { // directory going from M -> S, since owner did not change; forward a Put
                        int l1_to_send_get = -1;
                        for(int i = 0; i < dir_ent.bitVector.size(); i++) {
                            if(dir_ent.bitVector.test(i) and i != from) {
                                l1_to_send_get = i;
                            }
                        }
                        assert(l1_to_send_get != -1);
                        unique_ptr<Message> put(new Put(MsgType::PUT, from, l1_to_send_get, true, blockAddr));
                        proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                    }
                    else { // since owner changed, directory going from M -> M state, forward a Putx.
                        unique_ptr<Message> putx(new Putx(MsgType::PUTX, from, dir_ent.ownerId, true, blockAddr, 0, State::M));
                        proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                    }
                }
                else {  // this means that writeback was in response to a get and we do not need to forward it

                }
            }
            else {  // we did not expect a writeback; this must mean that cache block has been evicted.
                if(l2.numInvAcksToCollectForIncl.contains(blockAddr)) { // if evicted due to inclusivity purpose
                    auto &inv_ack_struct = l2.numInvAcksToCollectForIncl[blockAddr];
                    assert(inv_ack_struct.waitForNumMessages == 1 and inv_ack_struct.L1CacheNums.begin()->second);
                    int l1_cache_num = (inv_ack_struct.L1CacheNums.begin()->first);
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, l1_cache_num, false, inv_ack_struct.blockAddr, 0, State::M));
                    proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                }
                else { // this means that cache block was evicted and we did not send any inv request.
                    assert(dir_ent.dirty);
                    dir_ent.bitVector.reset();
                }
            }
        }
        else { // cannot happen; wb cannot be sent to L1
            assert(false);
        }
    }
    else { // cannot happen; wb cannot come from L2
        assert(false);
    }
}

void Upgr::handle(Processor &proc, bool toL1) {
    if(fromL1) {
        if(!toL1) { // to L2
            auto &l2 = proc.L2Caches[to];
            auto st = l2.set_from_addr(blockAddr);
            assert(l2.directory[st].contains(blockAddr)); // since we receive a upgr message, directory should have this entry
            auto &dir_ent = l2.directory[st][blockAddr];
            if(dir_ent.pending) {
                unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::UPGR, to, from, false, blockAddr));
                proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
            }
            else if(dir_ent.dirty) { // this means that some other block requested a GetX, and before it's invalidation reached L1, L1 generated Upgr, need to NACK this.
                unique_ptr<Message> nack(new Nack(MsgType::NACK, MsgType::UPGR, to, from, false, blockAddr));
                proc.L1Caches[nack->to].incomingMsg.push_back(move(nack));
            }
            else {
                unique_ptr<Message> upgr_ack(new UpgrAck(MsgType::UPGR_ACK, to, from, false, blockAddr, dir_ent.bitVector.count()));
                proc.L1Caches[upgr_ack->to].incomingMsg.push_back(move(upgr_ack));
                for(int i = 0; i < dir_ent.bitVector.size(); i++) {
                    if(!dir_ent.bitVector.test(i)) {continue;}
                    unique_ptr<Message> inv(new Inv(MsgType::INV, from, i, true, blockAddr)); // masking to let this l1 know it needs to send inv to 'from' l1.
                    proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
                }
            }
        }
        else {  // should not happen as L2 won't forward an Upgr request
            assert(false);
        }
    }
    else { // should not happen
        assert(false);
    }
}

