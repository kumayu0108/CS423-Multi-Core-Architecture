#include <stdio.h>
#include "pin.H"

FILE * trace;
PIN_LOCK pinLock;
#define ull unsigned long long
#define blk_sz ((ull)64)

// write any size, any addr, as much as you want wrt 1, 2, 4, 8 bytes.
// Should handle 0 byte writes correctly;
inline void write_bdry(ull addr, ull size, char rw, int tid) {
    while(size >> 3 != 0) {
        // traverse in multiples of 8 until size < 8.
        fprintf(trace, "ADDR: %20llu, SIZE: 8, %c, TID: %2d\n", addr, rw, tid);
        addr+=8; size-=8;
    }
    for(int i = 4; i > 0; i>>=1)
    {
        if(size / i != 0) // checking only once because number < i now.
        {
            assert(size != 0);
            fprintf(trace, "ADDR: %20llu, SIZE: %d, %c, TID: %2d\n", addr, i, rw, tid);
            addr+=i; size-=i;
        }
    }
    return;
}
void write_file(ull addr, ull size, char rw, int tid)
{
    ull start_blk = addr / blk_sz;
    ull end_blk = (addr + size)/ blk_sz;
    if(start_blk == end_blk){ // to the same block, no complex logic required.
        write_bdry(addr, size, rw, tid);
        return;
    }
    ull start_write = (start_blk + 1) * blk_sz - addr; // write until block boundary.
    ull end_write = addr + size - (end_blk * blk_sz); // bytes to be written at last;
    assert((addr + start_write) % blk_sz == 0);
    assert((addr + size - end_write) % blk_sz == 0);

    write_bdry(addr, start_write, rw, tid);
    addr = (start_blk + 1) * blk_sz;
    size -= (start_write + end_write); // size should be a multiple of blk_sz now
    assert(size % blk_sz == 0);
    write_bdry(addr, size, rw, tid);
    addr+=size;
    write_bdry(addr, end_write, rw, tid);
    return;
}
VOID printlog_read(VOID *ip, VOID* addr, UINT64 size, THREADID tid)
{
    PIN_GetLock(&pinLock, tid + 1);
    // fprintf(trace, "READ: IP: %p ADDR: %llu, SIZE: %lu, TID: %d\n", ip, (ull)addr, size, tid);
    write_file((ull)addr, size, 'r', tid);
    fflush(trace);
    PIN_ReleaseLock(&pinLock);
}
VOID printlog_write(VOID *ip, VOID* addr, UINT64 size, THREADID tid)
{
    PIN_GetLock(&pinLock, tid + 1);
    // fprintf(trace, "WRITE: IP: %p ADDR: %llu, SIZE: %lu, TID: %d\n", ip, (ull)addr, size, tid);
    write_file((ull)addr, size, 'w', tid);
    fflush(trace);
    PIN_ReleaseLock(&pinLock);
}// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    // Insert a call to printip before every instruction, and pass it the IP
    // INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printip, IARG_INST_PTR, IARG_THREAD_ID, IARG_END);
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    for(UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            UINT64 memSize = INS_MemoryOperandSize(ins, memOp);
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)printlog_read,
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
                ins, IPOINT_BEFORE, (AFUNPTR)printlog_write,
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
    fprintf(trace, "#eof\n");
    fclose(trace);
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
