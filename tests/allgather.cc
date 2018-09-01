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
    int world_size = rabit::GetWorldSize();
    int rank = rabit::GetRank();
    std::vector<std::vector<int>> a(world_size);
    for (int i = 0; i < world_size; ++i) {
        a[i].resize(i + N);
        if (i == rank) {
            for (int j = 0; j < i + N; ++j) {
                a[i][j] = i + j;
            }
        }
    }
    rabit::Allgather(a);
    for (int i = 0; i < world_size; ++i) {
        for (int j = 0; j < i + N; ++j) {
            CHECK_F(a[i][j] == i + j);
        }
    }
}
