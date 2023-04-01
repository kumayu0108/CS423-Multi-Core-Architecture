#include <fcntl.h>
#include <unistd.h>
#include <deque>
#include <string>
#define ull unsigned long long

constexpr int MAX_PROC = 64;
int trace[MAX_PROC];

// The data is logged in binary in the format of the following struct
struct logStruct {
    bool is_store;
    unsigned int time;
    ull blk_id;
};

class Message {
    int from, to; // store from and to cache id.
    bool from_l1; // store if the message comes from l1 or l2. useful in l1 to l1 transfers
};
// just serves as an example to show inheritance model.
class Inv : public Message {
    ull block_id; // which cache block to be evicted?
};
class Cache {
    protected:
        std::deque<Message> incoming_msg; // incoming messages
        int id; // id of cache
        Message temp_space; // when we deque, we need to store the top message.

};
class L1 : public Cache {
    int input_trace; // from where you would read line to line
    public:
        L1(int id){
            this->id = id;
            std::string tmp = "traces/addrtrace_" + std::to_string(id) + ".out";
            this->input_trace = open(tmp.c_str(), O_RDONLY);
        }
        ~L1(){
            close(input_trace);
        }
};
class LLC : public Cache {
    // class storage structures for  directory??
};