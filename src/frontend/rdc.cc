#include "rdc.h"
#include <pybind11/pybind11.h>
#include <functional>
#include "comm/communicator.h"
#include "transport/buffer.h"

namespace py = pybind11;
using namespace rdc;

PYBIND11_MODULE(pyrdc, m) {
    py::class_<Buffer> buffer(m, "Buffer", py::buffer_protocol());
    buffer.def(py::init<>())
        .def(py::init<void*, uint64_t>())
        .def(py::init([](py::buffer b) {
            py::buffer_info info = b.request();
            auto buf = new Buffer(info.ptr, info.itemsize * info.size);
            buf->set_item_size(info.itemsize);
            buf->set_format(info.format);
            CHECK(info.strides.size() == 1);
            buf->set_stride(info.strides[0]);
            return buf;
        }))
        .def(py::init([](std::string str) {
            auto buf = new Buffer(str.c_str(), str.size());
            return buf;
        }))
        .def(py::init([](py::bytes bytes) {
            std::string str(bytes);
            auto buf = new Buffer(str.c_str(), str.size());
            return buf;
        }))
        .def_buffer([](Buffer& buf) -> py::buffer_info {
            return py::buffer_info(buf.addr(), buf.item_size(), buf.format(), 1,
                                   {buf.Count()}, {buf.stride()});
        })
        .def("bytes", [](const Buffer& buf) {
            return py::bytes(static_cast<const char*>(buf.addr()),
                             buf.size_in_bytes());
        });
    py::class_<comm::ICommunicator> comm(m, "Comm");
    comm.def("send", (void (comm::ICommunicator::*)(void*, uint64_t, int)) &
                         comm::ICommunicator::Send)
        .def("send", (void (comm::ICommunicator::*)(Buffer, int)) &
                         comm::ICommunicator::Send)
        .def("recv", (void (comm::ICommunicator::*)(void*, uint64_t, int)) &
                         comm::ICommunicator::Send)
        .def("recv", (void (comm::ICommunicator::*)(Buffer, int)) &
                         comm::ICommunicator::Send);
    m.def("new_comm", &NewCommunicator);
    m.def("init", [] { Init(0, nullptr); });
    m.def("get_rank", &GetRank);
}
