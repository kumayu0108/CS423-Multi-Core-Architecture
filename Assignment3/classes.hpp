#include<bits/stdc++.h>
#define ull unsigned long long

class Message {
    int from, to; // store from and to cache id.
    bool from_l1; // store if the message comes from l1 or l2. useful in l1 to l1 transfers
};
// just serves as an example to show inheritance model.
class Inv : public Message {
    ull block_id; // which cache block to be evicted?
};
class Cache {
    std::deque<Message> incoming_msg; // incoming messages
    int id; // id of cache
    Message temp_space; // when we deque, we need to store the top message.
};
class L1 : public Cache {
    FILE *input_trace; // from where you would read line to line
};
class LLC : public Cache {
    // class storage structures for  directory??
};