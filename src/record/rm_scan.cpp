/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_scan.h"
#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    rid_ = {RM_FIRST_RECORD_PAGE, -1};
    next();
}

RmScan::~RmScan() { release_page(); }

void RmScan::release_page() {
    if (page_ != nullptr) {
        file_handle_->buffer_pool_manager_->unpin_page(page_->get_page_id(), false);
        page_ = nullptr;
        pinned_page_no_ = RM_NO_PAGE;
    }
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    int page_no = rid_.page_no;
    int slot_no = rid_.slot_no;
    while (page_no < file_handle_->file_hdr_.num_pages) {
        if (page_ == nullptr || pinned_page_no_ != page_no) {
            release_page();
            RmPageHandle page_handle = file_handle_->fetch_page_handle(page_no);
            page_ = page_handle.page;
            pinned_page_no_ = page_no;
        }
        RmPageHandle page_handle(&file_handle_->file_hdr_, page_);
        slot_no = Bitmap::next_bit(true, page_handle.bitmap, file_handle_->file_hdr_.num_records_per_page, slot_no);
        if (slot_no < file_handle_->file_hdr_.num_records_per_page) {
            rid_ = {page_no, slot_no};
            return;
        }
        release_page();
        page_no++;
        slot_no = -1;
    }
    rid_ = {file_handle_->file_hdr_.num_pages, -1};
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    return rid_.page_no >= file_handle_->file_hdr_.num_pages;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const {
    return rid_;
}

std::unique_ptr<RmRecord> RmScan::get_record() const {
    if (page_ == nullptr || is_end() || rid_.page_no != pinned_page_no_ ||
        !Bitmap::is_set(RmPageHandle(&file_handle_->file_hdr_, page_).bitmap, rid_.slot_no)) {
        throw RecordNotFoundError(rid_.page_no, rid_.slot_no);
    }
    RmPageHandle page_handle(&file_handle_->file_hdr_, page_);
    return std::make_unique<RmRecord>(file_handle_->file_hdr_.record_size, page_handle.get_slot(rid_.slot_no));
}
