/*!
 *  Copyright (c) 2018 by Contributors
 * \file basic.cc
 * \brief This is an example demonstrating what is Allreduce
 *
 * \author AnkunZheng
 */
#include <vector>
#include "rabit.h"
using namespace rabit;
int main(int argc, char *argv[]) {
    rabit::Init(argc, argv);
    int N = atoi(argv[1]);
//    int N = rabit::GetWorldSize();
    std::vector<int> a(N);
    for (int i = 0; i < N; ++i) {
        a[i] = rabit::GetRank() + N + i;
    }
    std::vector<int> result_max(N, 0);
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < rabit::GetWorldSize(); ++j) {
            result_max[i] = std::max(result_max[i], j + N + i);
        }
    }

//  if (rabit::GetRank() == 0) {char s[1024] = "hello world"; Send(s, 1024, 1);}
//  if (rabit::GetRank() == 1) {char s[1024] = {}; Recv(s, 1024, 0); TrackerPrintf(s);}
//    LOG_F(INFO, "@node[%d] before-allreduce: a={%d, %d, %d}\n",
//            rabit::GetRank(), a[0], a[1], a[2]);
//    // allreduce take max of each elements in all processes
//    Allreduce<op::Max>(&a[0], N);
//    LOG_F(INFO, "@node[%d] after-allreduce-max: a={%d, %d, %d}\n",
//            rabit::GetRank(), a[0], a[1], a[2]);
//    for (int i = 0; i < N; ++i) {
//        DCHECK_F(a[i] == result_max[i]);
//    }
    std::vector<int> result_sum(N, 0);
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < rabit::GetWorldSize(); ++j) {
            result_sum[i] += (j + i);
        }
    }
 
    for (int i = 0; i < N; ++i) {
        a[i] = rabit::GetRank() + i;
    }
    // second allreduce that sums everything up
    Allreduce<op::Sum>(&a[0], N);
    LOG_F(INFO, "@node[%d] after-allreduce-sum: a={%d, %d, %d}\n",
            rabit::GetRank(), a[0], a[1], a[2]);
    for (int i = 0; i < N; ++i) {
        DCHECK_EQ_F(a[i], result_sum[i]);
    }
    //rabit::Finalize();
    return 0;
}
