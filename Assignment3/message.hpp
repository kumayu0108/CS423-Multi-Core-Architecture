#pragma once
#include "declaration.hpp"


bool NACKStruct::operator<(const NACKStruct& other) {
    return ((blockAddr == other.blockAddr) ? msg < other.msg : blockAddr < other.blockAddr);
}

void Put::handle(Processor &proc, bool toL1){}
void Nack::handle(Processor &proc, bool toL1){}

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
            assert(l1.outstadingMessages[MsgType::INV].contains(blockAddr));
            l1.numInvToCollect[blockAddr]--;
            if(l1.numInvToCollect[blockAddr] == 0){
                l1.numInvToCollect.erase(blockAddr);
            }
        }
        else {  // inv ack sent to L2 (for inclusivity)

        }
    }
    else {
        // this shouldn't happen
        assert(false);
    }
}

void Get::handle(Processor &proc, bool toL1){
    if(fromL1){ // this means message is sent to LLC bank
        assert(!toL1); // L1 won't send Get to L1
        auto &l2 = proc.L2Caches[to];
        auto st = l2.set_from_addr(blockAddr);
        if(l2.directory[st].contains(blockAddr)){
            if(l2.directory[st][blockAddr].dirty){
                int owner = l2.directory[st][blockAddr].ownerId;
                unique_ptr<Message> get(new Get(MsgType::GET, to, owner, false, blockAddr));
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
            // TODO: evict, replace and update directory state.
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
    else {  // this means that message is an intervention

    }
}