/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"
#include "common/config.h"
#include "errors.h"
#include "record/bitmap.h"
#include "record/rm_defs.h"
#include "storage/page.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>

void RmFileHandle::flush_file_hdr() const {
    disk_manager_->write_page(fd_, RM_FILE_HDR_PAGE, reinterpret_cast<const char *>(&file_hdr_), sizeof(file_hdr_));
}

RmPageGuard::RmPageGuard(const RmFileHdr * fhdr_, Page* page_, BufferPoolManager* bpm, PageLatchMode mode) : handle_(fhdr_, page_),
  bpm_(bpm), mode_(mode), is_valid(true){
  if(mode == PageLatchMode::READ) {
    handle_.page->rwlatch_.lock_shared();
  }
  else if(mode == PageLatchMode::WRITE) {
    handle_.page->rwlatch_.lock();
  }
}

RmPageGuard::RmPageGuard(RmPageGuard&& other) noexcept : handle_(other.handle_), bpm_(other.bpm_), mode_(other.mode_), is_valid(other.is_valid) {
  other.is_valid = false;
  other.bpm_ = nullptr;
}


RmPageGuard& RmPageGuard::operator=(RmPageGuard&& other) noexcept {
  if(this == &other) {
    return *this;
  }
  else {
    Drop();
    handle_ = other.handle_;
    bpm_ = other.bpm_;
    mode_ = other.mode_;
    is_valid = other.is_valid;
    other.is_valid = false;
  }
  return *this;
}

void RmPageGuard::Drop() {
  if(!is_valid) {
    return;
  }
  else {
    if(mode_ == PageLatchMode::READ) {
      handle_.page->rwlatch_.unlock_shared();
      bpm_->unpin_page(handle_.page->id_, false);
    }
    else if(mode_ == PageLatchMode::WRITE) {
      handle_.page->rwlatch_.unlock();
      bpm_->unpin_page(handle_.page->id_, true);
    }
  }
}

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 初始化一个指向RmRecord的指针（赋值其内部的data和size）
  page_id_t page_no = rid.page_no;
  int slot_no = rid.slot_no;
  if(page_no >= file_hdr_.num_pages) {
    throw RecordNotFoundError(page_no, slot_no);
  }
  auto page_handle = fetch_page_handle(page_no, PageLatchMode::READ);
  if(slot_no >= file_hdr_.num_records_per_page || !Bitmap::is_set(page_handle->bitmap, slot_no)) {
    throw RecordNotFoundError(page_no, slot_no);
  }
  if(Bitmap::is_set(page_handle ->bitmap, slot_no)) {
    return std::make_unique<RmRecord>(file_hdr_.record_size, page_handle->get_slot(slot_no));
  }
  else return nullptr;
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char* buf, Context* context, const std::string *tab_name) {
    // Todo:
    // 1. 获取当前未满的page handle
    // 2. 在page handle中找到空闲slot位置
    // 3. 将buf复制到空闲slot位置
    // 4. 更新page_handle.page_hdr中的数据结构
    // 注意考虑插入一条记录后页面已满的情况，需要更新file_hdr_.first_free_page_no
  std::lock_guard<std::mutex> guard(latch_);
  auto page_write_handle = create_write_page_handle();
  int first_empty_bit = Bitmap::first_bit(false, page_write_handle->bitmap,file_hdr_.num_records_per_page);
  assert(first_empty_bit >= 0 && first_empty_bit < file_hdr_.num_records_per_page);
  page_write_handle->page_hdr->num_records++;
  Bitmap::set(page_write_handle->bitmap, first_empty_bit);
  bool isfull = (page_write_handle->page_hdr->num_records == file_hdr_.num_records_per_page);
  if(isfull) {
    file_hdr_.first_free_page_no = page_write_handle->page_hdr->next_free_page_no;
    page_write_handle->page_hdr->next_free_page_no = RM_NO_PAGE;
  }
  char* offset = page_write_handle->get_slot(first_empty_bit);
  memcpy(offset, buf, file_hdr_.record_size);
  Rid rid{page_write_handle->page->id_.page_no, first_empty_bit};

  if (context != nullptr && context->log_mgr_ != nullptr && context->txn_ != nullptr && tab_name != nullptr) {
    RmRecord inserted_record(file_hdr_.record_size, offset);
    InsertLogRecord insert_log(context->txn_->get_transaction_id(), inserted_record, rid, *tab_name,
                               context->txn_->get_prev_lsn());
    lsn_t lsn = context->log_mgr_->add_log_to_buffer(&insert_log);
    context->txn_->set_prev_lsn(lsn);
    context->log_mgr_->flush_log_to_disk();
  }

  return rid;
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
  std::lock_guard<std::mutex> guard(latch_);
  page_id_t page_no = rid.page_no;
  int slot_no = rid.slot_no;
  // 如果页面不存在（例如崩溃恢复时页面未刷盘），创建新页
  while (file_hdr_.num_pages <= page_no) {
    create_new_write_page_handle();
  }
  auto write_page_handle = fetch_page_handle(page_no, PageLatchMode::WRITE);
  if(!Bitmap::is_set(write_page_handle->bitmap, slot_no)) {
    write_page_handle->page_hdr->num_records++;
    Bitmap::set(write_page_handle->bitmap, slot_no);
    //更新freelist
    if(write_page_handle->page_hdr->num_records == file_hdr_.num_records_per_page) {
      page_id_t first_free_page_no = file_hdr_.first_free_page_no;
      if(first_free_page_no == page_no) {
        file_hdr_.first_free_page_no = write_page_handle->page_hdr->next_free_page_no;
      }
      else {
        page_id_t prev_page = find_prev_free_page(page_no);
        if(prev_page != RM_NO_PAGE) {
          auto prev_page_handle = fetch_page_handle(prev_page, PageLatchMode::WRITE);
          prev_page_handle->page_hdr->next_free_page_no = write_page_handle->page_hdr->next_free_page_no;
          write_page_handle->page_hdr->next_free_page_no = RM_NO_PAGE;
        }
      }
    }
    char* offset = write_page_handle->get_slot(slot_no);
    memcpy(offset, buf,  file_hdr_.record_size);
  }
  else {
    throw RMDBError();
  }

}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新page_handle.page_hdr中的数据结构
    // 注意考虑删除一条记录后页面未满的情况，需要调用release_page_handle()
  std::lock_guard<std::mutex> guard(latch_);
  page_id_t page_no = rid.page_no;
  int slot_no = rid.slot_no;
  if(page_no >= file_hdr_.num_pages) {
    throw RMDBError();
  }else {
    auto write_page_handle = fetch_page_handle(page_no, PageLatchMode::WRITE);
    bool prev_is_full = (write_page_handle->page_hdr->num_records == file_hdr_.num_records_per_page);
    if(Bitmap::is_set(write_page_handle->bitmap, slot_no)) {
      Bitmap::reset(write_page_handle->bitmap, slot_no);
      write_page_handle->page_hdr->num_records--;
      assert(write_page_handle->page_hdr->num_records >= 0 && write_page_handle->page_hdr->num_records <= file_hdr_.num_records_per_page);
      if(prev_is_full) {
        release_page_handle(write_page_handle);
      }
    }
    else {
      throw RMDBError();
    }
  }
}


/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新记录
  std::lock_guard<std::mutex> guard(latch_);
  page_id_t page_no = rid.page_no;
  int slot_no = rid.slot_no;
  if(page_no >= file_hdr_.num_pages) {
    throw RMDBError();
  }
  auto write_page_handle = fetch_page_handle(page_no, PageLatchMode::WRITE);
  if(!Bitmap::is_set(write_page_handle->bitmap, slot_no)) {
    throw RMDBError();
  }
  char* offset = write_page_handle->get_slot(slot_no);
  memcpy(offset, buf,  file_hdr_.record_size);
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageGuard RmFileHandle::fetch_page_handle(int page_no, PageLatchMode mode) const {
    // Todo:
    // 使用缓冲池获取指定页面，并生成page_handle返回给上层
    // if page_no is invalid, throw PageNotExistError exception
  if(page_no >= file_hdr_.num_pages) {
    throw RMDBError();
  }
  PageId page_id{fd_, page_no};
  Page* target_page = buffer_pool_manager_->fetch_page(page_id);
  return RmPageGuard(&file_hdr_, target_page, buffer_pool_manager_, mode);
}

/**
 * @description: 创建一个新的page guard
 * @return {RmPageHandle} 新的PageGuard
 */
RmPageGuard RmFileHandle::create_new_write_page_handle() {
    // Todo:
    // 1.使用缓冲池来创建一个新page
    // 2.更新page handle中的相关信息
    // 3.更新file_hdr_
  PageId page_id{fd_};
  Page* new_page = buffer_pool_manager_->new_page(&page_id);
  file_hdr_.num_pages++;
  auto Write_PageHandle = RmPageGuard(&file_hdr_,new_page , buffer_pool_manager_, PageLatchMode::WRITE);
  assert(Write_PageHandle.is_valid);
  Write_PageHandle->page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
  file_hdr_.first_free_page_no = page_id.page_no;
  return Write_PageHandle;
}

/**
 * @brief 创建或获取一个空闲的page guard
 *
 * @return RmPageGuard 返回生成的空闲page guard
 * @note pin the page, remember to unpin it outside!
 */
RmPageGuard RmFileHandle::create_write_page_handle() {
    // Todo:
    // 1. 判断file_hdr_中是否还有空闲页
    //     1.1 没有空闲页：使用缓冲池来创建一个新page；可直接调用create_new_page_handle()
    //     1.2 有空闲页：直接获取第一个空闲页
    // 2. 生成page handle并返回给上层
  if(file_hdr_.first_free_page_no == RM_NO_PAGE) {
    return create_new_write_page_handle();
  }
  else {
    auto write_page_handle = fetch_page_handle(file_hdr_.first_free_page_no, PageLatchMode::WRITE);
    while(write_page_handle->page_hdr->num_records == file_hdr_.num_records_per_page) {
      page_id_t next_page_no = write_page_handle->page_hdr->next_free_page_no;
      if(next_page_no == RM_NO_PAGE) {
        return create_new_write_page_handle();
      }
      write_page_handle = fetch_page_handle(next_page_no, PageLatchMode::WRITE);
    }
    return write_page_handle;
  }
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageGuard& page_handle) {
    // Todo:
    // 当page从已满变成未满，考虑如何更新：
    // 1. page_handle.page_hdr->next_free_page_no
    // 2. file_hdr_.first_free_page_no
  assert(page_handle.mode_ == PageLatchMode::WRITE);
  page_handle->page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
  file_hdr_.first_free_page_no = page_handle->page->get_page_id().page_no;
}

page_id_t RmFileHandle::find_prev_free_page(page_id_t page_no) {
  page_id_t first_free_page_no = file_hdr_.first_free_page_no;
  page_id_t curr_page_no = first_free_page_no;
  while(curr_page_no != RM_NO_PAGE) {
    auto page_handle = fetch_page_handle(curr_page_no, PageLatchMode::READ);
    if(page_handle->page_hdr->next_free_page_no == page_no) {
      return page_handle->page->id_.page_no;
    }
    curr_page_no = page_handle->page_hdr->next_free_page_no;
  }
  return RM_NO_PAGE;
}
