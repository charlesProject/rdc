#include "rabit.h"

int main(int argc, char** argv) {
    if (rabit::GetRank() == 0) {
        char s[1024] = "hello world";
        rabit::Send(s, 1024, 1);
    }
    if (rabit::GetRank() == 1) {
        char s[1024] = {0};
        rabit::Recv(s, 1024, 0);
        rabit::TrackerPrintf(s);
    }
    return 0;
}
