#pragma once
class TypeMgr {
public:
    static TypeMgr* Get() {
        static TypeMgr mgr;
        return &mgr;
    }
//    std::unordered_set<std::type_index> registered_types_;
};
