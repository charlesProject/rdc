// This program is used to test the speed of rdc API
#include <rdc.h>
#include <utils/timer.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <time.h>

using namespace rdc;

double tdiff = 0;

inline void TestEcho(size_t n, size_t k) {
    int rank = rdc::GetRank();
    std::vector<char> ndata(n);
    double tstart = utils::GetTimeInMs();
    ChainWorkCompletion send_wc, recv_wc;
    for (auto i = 0U; i < k; i++) {
        if (rank == 0) {
            send_wc << rdc::comm::GetCommunicator()->ISend(&ndata[0], ndata.size(), 1);
//            wc.Wait();
        }
        if (rank == 1) {
            recv_wc << rdc::comm::GetCommunicator()->IRecv(&ndata[0], ndata.size(), 0);
//            wc.Wait();
        }
    }
    if (rank == 0) send_wc.Wait();
    if (rank == 1) recv_wc.Wait();
    tdiff += utils::GetTimeInMs() - tstart;
}

inline void PrintStats(double tdiff, int n, int nrep, size_t size) {
    if (rdc::GetRank() == 1) {
        std::string msg = "diff=" + std::to_string(tdiff) + ":millisec";
        rdc::TrackerPrint(msg);
        double ndata = n;
        ndata *= nrep * size;
        if (n != 0) {
            std::string msg = "speed: " +
                              std::to_string((ndata/ tdiff) / 1024 / 1024 * 1000) +
                              " MB/sec";
            rdc::TrackerPrint(msg);
      }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
      printf("Usage: <ndata> <nrepeat>\n");
      return 0;
    }
    srand(0);
    int n = atoi(argv[1]);
    int nrep = atoi(argv[2]);
    CHECK_F(nrep >= 1, "need to at least repeat running once");
    rdc::Init(argc, argv);
    //int rank = rdc::GetRank();
    tdiff = 0;
    rdc::Barrier();
    double tstart = utils::GetTimeInMs();
    TestEcho(n, nrep);
    //  TestBcast(n, rand() % nproc);
    PrintStats(tdiff, n, nrep, sizeof(char));
    rdc::Finalize();
    return 0;
}
