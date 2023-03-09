#include <stdio.h>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <math.h>
#include "pin.H"

#define ll long long
#define ull unsigned ll
#define blk_sz ((ull)64)
#define log_lag 1000000
#define LOG_DATA 1        // uncomment to log data
//defines a cache block. contains a tag and a valid bit.
#define blk std::pair<ull, bool>
#define timeTag std::pair<ull, ull>
constexpr int LOG_BLOCK_SIZE = 6;
constexpr int L3_SETS = 2048;
constexpr int NUM_L3_TAGS = 16;
constexpr int LOG_L3_SETS = 11;
constexpr ull L3_SET_BITS = 0x7ff;
constexpr ull MAX_L3_ASSOC = 32768;

FILE * trace;
PIN_LOCK pinLock;

static struct __data {
    std::unordered_map<ull, unsigned long> tla; // maps time of last access of a block.
    // std::unordered_map<ull, int> tshare; // each block to a 8 bit vector to see log if thread touches it.
    std::unordered_map<ull, unsigned long> adis; // maps access distance to number of times that distance occurs.
    ull time = 0; // added bonus, time at end of simulation is total machine accesses
} mdata;


class Cache{
    public:
        ull l3_hits, l3_misses;
        Cache():
            l3_hits(0), l3_misses(0),
            L3(L3_SETS),
            timeBlockAddedL3(L3_SETS) {}

        void simulator(ull addr) {
            addr = ((addr >> LOG_BLOCK_SIZE) << LOG_BLOCK_SIZE);
            auto set_tag_l3 = decode_address(addr);
            auto setL3 = set_tag_l3.first, tagL3 = set_tag_l3.second;
#ifdef LOG_DATA
            assert(L3[setL3].size() == timeBlockAddedL3[setL3].size());
#endif
            if(check_in_cache(setL3, tagL3)){
                l3_hits++;
                update_priority(setL3, tagL3);
            }
            else {
                bring_from_memory(addr, setL3, tagL3);
                // only updating metadata for miss traces
                ull blk_id = addr / blk_sz;
                // if previous entry exists, log access distance too.
                if(mdata.tla.find(blk_id) != mdata.tla.end())
                    mdata.adis[mdata.time - mdata.tla[blk_id]]++; // initialise to zero if not present, increment it too.
                mdata.tla[blk_id] = mdata.time; // log this for lru cache simulation and access distance.
                mdata.time++; // increase time for each access to compute distances correctly.
            }
        }
    private:
        std::vector <std::unordered_map<ull, ull>> L3; // tag -> ull, active? -> bool
        std::vector <std::set<timeTag>> timeBlockAddedL3; // stores time and -> for eviction.

        void bring_from_memory(ull addr, ull setL3, ull tagL3){
            l3_misses++;
            replace(setL3, tagL3);
        }
        // given a certain set, it uses LRU policy to replace cache block.
        //returns address evicted, along with a bool denoting if it actually existed or if it was an empty slot.
        blk replace(ull st, ull tag){
            //find first invalid block, fill it, return.
            blk retBlock = {0, false};
            // calculate index of minimum timestamp for LRU replacement.
            // all blocks are valid, so we're actually evicting an existing block.
            if(L3[st].size() == NUM_L3_TAGS){
                retBlock.second = true;
                auto tmTag = *timeBlockAddedL3[st].begin();
                //get address evicted.
                retBlock.first = get_addr(st, tmTag.second);
                L3[st].erase(tmTag.second);
                timeBlockAddedL3[st].erase(tmTag);
            }
            L3[st][tag] = (timeBlockAddedL3[st].empty() ? 0 : (timeBlockAddedL3[st].rbegin()->first)) + 1;
            timeBlockAddedL3[st].insert({L3[st][tag], tag});
            return retBlock;
        }
        //helper function for decoding addresses
        std::pair<ull, ull> decode_address(ull addr){
            ull st = (addr >> LOG_BLOCK_SIZE) & L3_SET_BITS;
            ull tag = (addr >> (LOG_BLOCK_SIZE + LOG_L3_SETS));
            return std::pair<ull, ull>(st, tag);
        }
        // returns tag index in current set if found & is valid. Else returns -1
        bool check_in_cache(ull st, ull tag){
            if(L3[st].find(tag) != L3[st].end()){return true;}
            return false;
        }
        //helper function to get address from set and tag bits
        ull get_addr(ull st, ull tag){
            return ((tag << (LOG_BLOCK_SIZE + LOG_L3_SETS)) | (st << LOG_BLOCK_SIZE));
        }
        // updates LRU list of times.
        void update_priority(ull st, ull tag){
            ull nwTime = (timeBlockAddedL3[st].rbegin()->first) + 1;
            timeBlockAddedL3[st].erase({L3[st][tag], tag});
            L3[st][tag] = nwTime;
            timeBlockAddedL3[st].insert({nwTime, tag});
        }
};
Cache cache;
// write any size, any addr, as much as you want wrt 1, 2, 4, 8 bytes.
// Should handle 0 byte writes correctly;
inline VOID upd_mdata(ull addr, int tid) {
    cache.simulator(addr);

#ifdef LOG_DATA
    if(mdata.time % log_lag== 0) {
        fprintf(trace, "updating metadata w %20llu, %2d, TIME: %10llu\n", addr, tid, mdata.time/log_lag);
        fflush(trace);
    }
#endif

    return;
}
inline VOID log_bdry(ull addr, ull size, int tid) {
    while(size >> 3 != 0) {
        // traverse in multiples of 8 until size < 8.
        upd_mdata(addr, tid);
        addr+=8; size-=8;
    }
    if(size == 0) return; // help branch prediction? idk. most of sizes are 8 bytes.
    for(int i = 4; i > 0; i>>=1) {
        if(size / i != 0) {
        // checking only once because (size / i) <= 1 at every iteration.
#ifdef LOG_DATA
            assert(size != 0);
#endif
            upd_mdata(addr, tid);
            addr+=i; size-=i;
        }
    }
    return;
}

VOID log_mem(VOID *ip, VOID* addr_p, UINT64 size, THREADID tid) {
    ull addr = (ull)addr_p;
    ull start_blk = addr / blk_sz;
    ull end_blk = (addr + size)/ blk_sz;
    PIN_GetLock(&pinLock, tid + 1);
    
    if(start_blk == end_blk){ // to the same block, no complex logic required.
        log_bdry(addr, size, (int)tid);
        PIN_ReleaseLock(&pinLock);
        return;
    }
    ull start_acc = (start_blk + 1) * blk_sz - addr; // write until block boundary.
    ull end_acc = addr + size - (end_blk * blk_sz); // bytes to be written at last;
#ifdef LOG_DATA
    assert((addr + start_acc) % blk_sz == 0);
    assert((addr + size - end_acc) % blk_sz == 0);
#endif

    log_bdry(addr, start_acc, (int)tid);
    addr = (start_blk + 1) * blk_sz;
    size -= (start_acc + end_acc); // size should be a multiple of blk_sz now
#ifdef LOG_DATA
    assert(size % blk_sz == 0);
#endif
    log_bdry(addr, size, (int)tid);
    addr+=size;
    log_bdry(addr, end_acc, (int)tid);

    PIN_ReleaseLock(&pinLock);
}
// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    // Insert a call to printip before every instruction, and pass it the IP
    // INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printip, IARG_INST_PTR, IARG_THREAD_ID, IARG_END);
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    for(UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        // no distinction between read and write, so can call same function.
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            UINT64 memSize = INS_MemoryOperandSize(ins, memOp);
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)log_mem,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_UINT64, memSize,
                IARG_THREAD_ID,
                IARG_END);
        }
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            UINT64 memSize = INS_MemoryOperandSize(ins, memOp);
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)log_mem,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_UINT64, memSize,
                IARG_THREAD_ID,
                IARG_END);
        }
    }

}
// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    // ull arr[9] = {(ull)0};
    // ull tot_acc = 0;
    fprintf(trace, "total machine accesses: %llu\n", mdata.time);
    // number of set bits for each entry, and add it. minimum number 1, maximum 8;
    // for(auto x: mdata.tshare)
    //     arr[__builtin_popcount(x.second)]++;
    std::map <float, ull> logDis;
    for(auto x: mdata.adis){ // convert access distance into log base 10 with 2 decimal rounding
        logDis[(((float)((ll)(log10(x.first) * 100)))/100)] += x.second;
    }
    // printf("\nMax Dis: %llu ; accesses : %llu\n", mdata.adis.rbegin()->first, mdata.adis.rbegin()->second);
    for(auto x: logDis){ // log access distance and parse it later
       fprintf(trace, "Access Distance (LOG): %5f, Times: %5llu\n", x.first, x.second);
    }
    // for(int i = 0; i < 9; i++) {
    //     tot_acc+=arr[i];
    //     fprintf(trace, "blocks touched by %d threads is %llu\n", i, arr[i]);
    // }
    fclose(trace);
#ifdef LOG_DATA
    // assert(tot_acc == mdata.tla.size());
    // assert(mdata.tshare.size() == mdata.tla.size());
#endif
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR("This Pintool prints the IPs of every instruction executed\n"
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    trace = fopen("addrtrace.out", "w");

    PIN_InitLock(&pinLock);

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);
    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
