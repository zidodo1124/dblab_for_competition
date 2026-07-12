/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <memory>

#include "rm_defs.h"

class RmFileHandle;
class Page;

class RmScan : public RecScan {
    const RmFileHandle *file_handle_;
    Rid rid_;
    Page *page_{nullptr};
    int pinned_page_no_{RM_NO_PAGE};

    void release_page();
public:
    RmScan(const RmFileHandle *file_handle);
    ~RmScan() override;

    RmScan(const RmScan &) = delete;
    RmScan &operator=(const RmScan &) = delete;

    void next() override;

    bool is_end() const override;

    Rid rid() const override;

    std::unique_ptr<RmRecord> get_record() const;
};
