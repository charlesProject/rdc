/*!
 *  Copyright (c) 2018 by Contributors
 * \file basic.cc
 * \brief This is an example demonstrating setup and finalize of rdc
 *
 * \author AnkunZheng
 */
#include <vector>
#include "rdc.h"
using namespace rdc;
int main(int argc, char *argv[]) {
    rdc::Init(argc, argv);
    rdc::Finalize();
    return 0;
}
