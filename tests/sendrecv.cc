#include "rdc.h"

int main(int argc, char** argv) {
    rdc::Init(argc, argv);
    if (rdc::GetRank() == 0) {
        char s[1024] = "hello world";
        rdc::Send(s, 1024, 1);
    }
    if (rdc::GetRank() == 1) {
        char s[1024] = {0};
        rdc::Recv(s, 1024, 0);
        rdc::TrackerPrint(s);
        DCHECK_EQ(strncmp(s, "hello world", 11), 0);
    }
    return 0;
}
