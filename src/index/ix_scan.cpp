/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "ix_scan.h"

IxScan::IxScan(const IxIndexHandle *ih, const Iid &lower, const Iid &upper, BufferPoolManager *bpm)
    : ih_(ih), iid_(lower), end_(upper), bpm_(bpm) {
    if (!is_end()) {
        node_ = ih_->fetch_node(iid_.page_no);
    }
}

IxScan::~IxScan() {
    if (node_ != nullptr) {
        bpm_->unpin_page(node_->get_page_id(), false);
    }
}

/**
 * @brief 
 * @todo 加上读锁（需要使用缓冲池得到page）
 */
void IxScan::next() {
    assert(!is_end());
    assert(node_ != nullptr && node_->is_leaf_page());
    if (iid_.slot_no < node_->get_size()) {
        iid_.slot_no++;
    }
    while (iid_.page_no != ih_->file_hdr_->last_leaf_ && iid_.slot_no >= node_->get_size()) {
        page_id_t next_page_no = node_->get_next_leaf();
        bpm_->unpin_page(node_->get_page_id(), false);
        iid_.slot_no = 0;
        iid_.page_no = next_page_no;
        node_ = ih_->fetch_node(iid_.page_no);
    }
}

Rid IxScan::rid() const {
    assert(!is_end());
    assert(node_ != nullptr && iid_.slot_no < node_->get_size());
    return *node_->get_rid(iid_.slot_no);
}
