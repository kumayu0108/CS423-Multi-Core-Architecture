#include <stdio.h>
#include<iostream>
#include "pin.H"

#define ull unsigned long long
#define blk_sz ((ull)64)
#define log_lag 1000000

FILE * trace;
PIN_LOCK pinLock;

struct __data {

    std::map<ull, ull>tla; // maps time of last access of a block.
    std::map<ull, ull> tshare; // each block to a 8 bit vector to see log if thread touches it.
    std::map<ull, ull> adis; // maps access distance to number of times that distance occurs.
    ull time = 0; // added bonus, time at end of simulation is total machine accesses
} mdata;

// write any size, any addr, as much as you want wrt 1, 2, 4, 8 bytes.
// Should handle 0 byte writes correctly;
inline void upd_mdata(ull addr, int tid) {
    ull blk_id = addr / blk_sz;
    // if previous entry exists, log access distance too.
    if(mdata.tla.find(blk_id) != mdata.tla.end())
        mdata.adis[mdata.time - mdata.tla[blk_id] - 1]++; // initialise to zero if not present, increment it too.
    mdata.tla[blk_id] = mdata.time; // log this for lru cache simulation and access distance.
    mdata.tshare[blk_id] |= (1<<tid); // set tid'th bit to 1, if accessed by tid.
    mdata.time++; // increase time for each access to compute distances correctly.
    if(mdata.time % log_lag== 0) {
        fprintf(trace, "updating metadata w %20llu, %2d, TIME: %10llu\n", addr, tid, mdata.time/log_lag);
        fflush(trace);
    }
    return;
}
void log_bdry(ull addr, ull size, int tid) {
    while(size >> 3 != 0) {
        // traverse in multiples of 8 until size < 8.
        upd_mdata(addr, tid);
        // fprintf(trace, "size is %llu\n", size);
        addr+=8; size-=8;
    }
    // fflush(trace);
    for(int i = 4; i > 0; i>>=1) {
        // fprintf(trace, "stuck with size %llu\n", (ull)i);
        if(size / i != 0) {
        // checking only once because (size / i) <= 1 at every iteration.
            assert(size != 0);
            upd_mdata(addr, tid);
            addr+=i; size-=i;
        }
    }
    return;
}
VOID log(VOID *ip, VOID* addr_p, UINT64 size, THREADID tid) {
    PIN_GetLock(&pinLock, tid + 1);

    // fprintf(trace, "calling log w ADDR: %20llu, SIZE: %3lu, TID: %d\n", (ull)addr_p, size, tid);
    ull addr = (ull)addr_p;
    ull start_blk = addr / blk_sz;
    ull end_blk = (addr + size)/ blk_sz;
    if(start_blk == end_blk){ // to the same block, no complex logic required.
        // fprintf(trace, "calling log boundary, same block\n");
        log_bdry(addr, size, (int)tid);
        // fflush(trace);
        PIN_ReleaseLock(&pinLock);
        return;
    }
    ull start_acc = (start_blk + 1) * blk_sz - addr; // write until block boundary.
    ull end_acc = addr + size - (end_blk * blk_sz); // bytes to be written at last;
    assert((addr + start_acc) % blk_sz == 0);
    assert((addr + size - end_acc) % blk_sz == 0);

    log_bdry(addr, start_acc, (int)tid);
    addr = (start_blk + 1) * blk_sz;
    size -= (start_acc + end_acc); // size should be a multiple of blk_sz now
    assert(size % blk_sz == 0);
    log_bdry(addr, size, (int)tid);
    addr+=size;
    log_bdry(addr, end_acc, (int)tid);
    // fflush(trace);

    PIN_ReleaseLock(&pinLock);
    return;
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
                ins, IPOINT_BEFORE, (AFUNPTR)log,
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
                ins, IPOINT_BEFORE, (AFUNPTR)log,
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
    ull arr[9] = {(ull)0};
    ull tot_acc = 0;
    fprintf(trace, "total machine accesses: %llu\n", mdata.time);
    // number of set bits for each entry, and add it. minimum number 1, maximum 8;
    for(auto x: mdata.tshare)
        arr[__builtin_popcount(x.second)]++;
    for(auto x: mdata.adis)
        fprintf(trace, "DIS: %llu, ACC: %llu\n", x.first, x.second);
    for(int i = 0; i < 9; i++) {
        tot_acc+=arr[i];
        fprintf(trace, "blocks touched by %d threads is %llu\n", i, arr[i]);
    }
    fclose(trace);
    // total accesses is equal to number of blocks logged in Time to Last access and tshare.
    assert(tot_acc == mdata.tla.size());
    assert(mdata.tshare.size() == mdata.tla.size());
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
