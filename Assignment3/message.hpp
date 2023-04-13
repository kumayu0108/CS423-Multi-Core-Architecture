#pragma once
#include "declaration.hpp"

bool NACKStruct::operator<(const NACKStruct& other) {
    return ((blockAddr == other.blockAddr) ? msg < other.msg : blockAddr < other.blockAddr);
}

void Putx::handle(Processor &proc, bool toL1) {}
void Nack::handle(Processor &proc, bool toL1) {}
void Wb::handle(Processor &proc, bool toL1) {}

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
        // generate inv ack to be sent to 'from' LLC
        unique_ptr<Message> inv_ack(new InvAck(MsgType::INV_ACK, to, from, true, blockAddr));
        proc.L2Caches[inv_ack->to].incomingMsg.push_back(move(inv_ack));
    }
}

void InvAck::handle(Processor &proc, bool toL1) {
    if(fromL1) {
        if(toL1) { // inv ack sent to another L1
            auto &l1 = proc.L1Caches[to];
            // since we collect an invalidation this means that this entry should contain this block address; HOWEVER, can inv acks arrive before invs?
            // assert(l1.numInvToCollect.contains(blockAddr));
            if (l1.numInvToCollect.contains(blockAddr)) {
                l1.numInvToCollect[blockAddr]--;
            }
            else {
                l1.numInvToCollect[blockAddr]++;
            }
            // this could happen in PUTX
            if(l1.numInvToCollect[blockAddr] == 0) {
                l1.numInvToCollect.erase(blockAddr);
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
    if(!toL1) {assert(false); return; } // an LLC can never receive PUT. It's usually a reply w data.
    // L1 is the only receiver, gets data as a block. Now need to place it somewhere.
    auto &l1 = proc.L1Caches[to];
    //if the block already exists, do nothing. (is this even possible?)
    if(l1.check_cache(blockAddr)) { return; }
    auto st = l1.set_from_addr(blockAddr);
    auto &cacheData  = l1.cacheData;
    auto &timeBlockAdded = l1.timeBlockAdded;
    if(cacheData[st].size() < NUM_L1_WAYS) { // no need to evict, need to create new cache block
        ull nwTime = (timeBlockAdded[st].empty() ? 1 : (*timeBlockAdded[st].rbegin()).first + 1);
        // update cache state correctly.
        timeBlockAdded[st].insert({nwTime, blockAddr});
        // we get put request only if in S state in directory.
        cacheData[st][blockAddr] = {nwTime, State::S};
    }
    // evict and replace.
    l1.evict(blockAddr); // update priority and all that.
    int l2_bank = l1.get_llc_bank(blockAddr); // get LLc bank.
    unique_ptr<Message> wb(new Wb(MsgType::WB, to, l2_bank, true, blockAddr));
    proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
    assert(!l1.writeBackAckWait.contains(blockAddr));
    l1.writeBackAckWait.insert(blockAddr);
}

// update priority for cache blocks
void Get::handle(Processor &proc, bool toL1) {
    if(fromL1) { // this means message is sent to LLC bank
        // assert(toL1); // L1 won't send Get to L1; but L2 would send Get to L1 masking itself as L1
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
            if(l1.check_cache(blockAddr)) {  // if in cache
                // AYUSH : do we need to check if the cache state of block is still M or not?; could it happen that the block has transitioned to S?
                auto st = l1.set_from_addr(blockAddr);
                l1.cacheData[st][blockAddr].state = State::S;
                int l2_bank = l1.get_llc_bank(blockAddr);
                unique_ptr<Message> put(new Put(MsgType::PUT, to, from, true, blockAddr));
                unique_ptr<Message> wb(new Wb(MsgType::WB, to, l2_bank, true, blockAddr));
                proc.L1Caches[put->to].incomingMsg.push_back(move(put));
                proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                // add entry to wait for wb ack
                assert(!l1.writeBackAckWait.contains(blockAddr));
                l1.writeBackAckWait.insert(blockAddr);
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
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, dir_ent.bitVector.count()));
                    for(int i = 0; i < dir_ent.bitVector.size(); i++) {
                        if(!dir_ent.bitVector.test(i)) { continue; }
                        unique_ptr<Message> inv(new Inv(MsgType::INV, from, i, true, blockAddr)); // L2 masks itself as the receiver of Getx so that all inv acks are sent to it.
                        proc.L1Caches[inv->to].incomingMsg.push_back(move(inv));
                    }
                }
            }
            else if(l2.check_cache(blockAddr)) { // if present in cache but not in dir
                auto &dir_ent = l2.directory[st][blockAddr];
                unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0));
                dir_ent.dirty = true;
                dir_ent.ownerId = from;
                dir_ent.pending = false;
                dir_ent.bitVector.reset();
                proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
            }
            else {
                auto &dir_ent = l2.directory[st][blockAddr];
                if(l2.cacheData[st].size() < NUM_L2_WAYS) { // no need to send invalidations
                    unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, false, blockAddr, 0));
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
                unique_ptr<Message> putx(new Putx(MsgType::PUTX, to, from, true, blockAddr, 0));
                unique_ptr<Message> wb(new Wb(MsgType::WB, to, l2_bank, true, blockAddr));
                proc.L1Caches[putx->to].incomingMsg.push_back(move(putx));
                proc.L2Caches[wb->to].incomingMsg.push_back(move(wb));
                assert(!l1.writeBackAckWait.contains(blockAddr));
                l1.writeBackAckWait.insert(blockAddr);
            }
            else {  // currently dropping this Getx; this would happen when L1 evicted this block and is now receiving

            }
        }
    }
    else { // this should not happen since whenever L2 sends a Getx to L1, it masks itself as the requestor L1, to let the receiver of Get know whom to send the message.
        assert(false);
    }
}