/*!
 *  Copyright (c) 2018 by Contributors
 * \file serializable.h
 * \brief defines serializable interface of rdc
 * \author Ankun Zheng
 */
#pragma once
#include <vector>
#include <string>
#include "utils/utils.h"
#include "dmlc/io.h"

namespace rdc {
/*!
 * \brief defines stream used in rdc
 * see definition of Stream in dmlc/io.h
 */
typedef dmlc::Stream Stream;
/*!
 * \brief defines serializable objects used in rdc
 * see definition of Serializable in dmlc/io.h
 */
typedef dmlc::Serializable Serializable;

}  // namespace rdc
