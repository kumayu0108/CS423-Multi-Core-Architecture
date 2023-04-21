#include <stdio.h>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <math.h>
#include <fcntl.h>
#include "pin.H"

#define ll long long
#define ull unsigned ll
#define blk_sz ((ull)64)
//defines a cache block. contains a tag and a valid bit.
// #define blk std::pair<ull, bool>
// #define timeTag std::pair<unsigned long, ull>
constexpr int LOG_BLOCK_SIZE = 6;
// constexpr int L3_SETS = 2048;
// constexpr int NUM_L3_TAGS = 16;
// constexpr int LOG_L3_SETS = 11;
// constexpr ull L3_SET_BITS = 0x7ff;
// constexpr ull MAX_L3_ASSOC = 32768;
constexpr int MAX_PROC = 64;

int trace[MAX_PROC];
PIN_LOCK pinLock;

struct MetaData {
    struct LogStruct {
        bool isStore;
        unsigned int time;
        ull addr;
    };
    std::unordered_map<ull, unsigned long> tla; // maps time of last access of a block.
    std::unordered_map<ull, int> tshare; // each block to a 8 bit vector to see log if thread touches it.
    std::unordered_map<ull, unsigned long> adis; // maps access distance to number of times that distance occurs.
    // bool is_store; // boolean; tells if the current machine access is a store or not
    unsigned int time = 0; // added bonus, time at end of simulation is total machine accesses
    std::vector <LogStruct> logData[MAX_PROC];

    inline void update(ull addr, int tid, bool isStore, bool cache = false){
        ull blk_id = addr / blk_sz;
        // if previous entry exists, log access distance too.
        if(tla.find(blk_id) != tla.end())
            adis[time - tla[blk_id]]++; // initialise to zero if not present, increment it too.
        tla[blk_id] = time; // log this for lru cache simulation and access distance.
        if(!cache)
            tshare[blk_id] |= (1<<tid); // set tid'th bit to 1, if accessed by tid.
        logData[tid].push_back({isStore, time, (blk_id << LOG_BLOCK_SIZE)});
        time++; // increase time for each access to compute distances correctly.
    }
} globalMData;

// write any size, any addr, as much as you want wrt 1, 2, 4, 8 bytes.
// Should handle 0 byte writes correctly;
inline VOID log_metrics(ull addr, int tid, bool isStore) {
    // cache.simulator(addr, tid);
    PIN_GetLock(&pinLock, tid + 1);
    globalMData.update(addr, tid, isStore);
    PIN_ReleaseLock(&pinLock);
    return;
}
inline VOID log_bdry(ull addr, ull size, int tid, bool isStore) {
    while(size >> 3 != 0) {
        // traverse in multiples of 8 until size < 8.
        log_metrics(addr, tid, isStore);
        addr+=8; size-=8;
    }
    if(size == 0) return; // help branch prediction? idk. most of sizes are 8 bytes.
    for(int i = 4; i > 0; i>>=1) {
        if(size / i != 0) {
        // checking only once because (size / i) <= 1 at every iteration.
            log_metrics(addr, tid, isStore);
            addr+=i; size-=i;
        }
    }
    return;
}

VOID log_mem_load(VOID *ip, VOID* addr_p, UINT64 size, THREADID tid) {
    ull addr = (ull)addr_p;
    ull start_blk = addr / blk_sz;
    ull end_blk = (addr + size - 1)/ blk_sz;
    // PIN_GetLock(&pinLock, tid + 1);
    // globalMData.is_store = false;
    if(start_blk == end_blk){ // to the same block, no complex logic required.
        log_bdry(addr, size, (int)tid, false);
        // PIN_ReleaseLock(&pinLock);
        return;
    }
    ull start_acc = (start_blk + 1) * blk_sz - addr; // write until block boundary.
    ull end_acc = addr + size - (end_blk * blk_sz); // bytes to be written at last;

    log_bdry(addr, start_acc, (int)tid, false);
    addr = (start_blk + 1) * blk_sz;
    size -= (start_acc + end_acc); // size should be a multiple of blk_sz now
    log_bdry(addr, size, (int)tid, false);
    addr+=size;
    log_bdry(addr, end_acc, (int)tid, false);

    // PIN_ReleaseLock(&pinLock);
}

VOID log_mem_store(VOID *ip, VOID* addr_p, UINT64 size, THREADID tid) {
    ull addr = (ull)addr_p;
    ull start_blk = addr / blk_sz;
    ull end_blk = (addr + size - 1)/ blk_sz;
    // PIN_GetLock(&pinLock, tid + 1);
    // globalMData.is_store = true;
    if(start_blk == end_blk){ // to the same block, no complex logic required.
        log_bdry(addr, size, (int)tid, true);
        // PIN_ReleaseLock(&pinLock);
        return;
    }
    ull start_acc = (start_blk + 1) * blk_sz - addr; // write until block boundary.
    ull end_acc = addr + size - (end_blk * blk_sz); // bytes to be written at last;

    log_bdry(addr, start_acc, (int)tid, true);
    addr = (start_blk + 1) * blk_sz;
    size -= (start_acc + end_acc); // size should be a multiple of blk_sz now
    log_bdry(addr, size, (int)tid, true);
    addr+=size;
    log_bdry(addr, end_acc, (int)tid, true);

    // PIN_ReleaseLock(&pinLock);
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
                ins, IPOINT_BEFORE, (AFUNPTR)log_mem_load,
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
                ins, IPOINT_BEFORE, (AFUNPTR)log_mem_store,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_UINT64, memSize,
                IARG_THREAD_ID,
                IARG_END);
        }
    }
}

VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    std::string tmp = "traces/addrtrace_" + std::to_string(tid) + ".out";
    trace[tid] = open(tmp.c_str(), O_CREAT | O_WRONLY, 0600);
}


// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    for(int i = 0; i < MAX_PROC; i++){
        if(trace[i] != -1){
            write(trace[i], &globalMData.logData[i][0], globalMData.logData[i].size() * sizeof(globalMData.logData[i][0]));
            close(trace[i]);
        }
    }
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
    for(int i = 0; i < MAX_PROC; i++){
        trace[i] = -1;
    }
    // trace = fopen("addrtrace.out", "w");

    PIN_InitLock(&pinLock);

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    PIN_AddThreadStartFunction(ThreadStart, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
