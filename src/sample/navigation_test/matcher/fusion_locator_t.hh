﻿//
// Created by User on 2019/7/18.
//

#ifndef PM1_SDK_FUSION_LOCATOR_T_HH
#define PM1_SDK_FUSION_LOCATOR_T_HH

#include <deque>
#include <fstream>

#include "../telementry_t.h"
#include "matcher_t.hpp"
#include "transformer_t.hpp"

namespace autolabor {
    /**
     * 融合定位器
     */
    class fusion_locator_t {
        using location_pair = std::pair<telementry_t, telementry_t>;
        using stamped_data = stamped_t<telementry_t>;
        
        std::deque<location_pair> pairs;
        
        matcher_t<telementry_t, telementry_t> matcher;
        transformer_t<>                       transformer;
        
        size_t queue_size;
        
        std::ofstream plot;
        
        // 更新队列
        bool update_queue();
    
    public:
        explicit fusion_locator_t(size_t queue_size);
        
        ~fusion_locator_t();
        
        /** 向主配队列添加元素 */
        void push_back_master(const stamped_data &data);
        
        /** 向匹配队列增加元素 */
        void push_back_helper(const stamped_data &data);
        
        /** 匹配 */
        bool refresh();
        
        /** 变换一个坐标 */
        [[nodiscard]] pose_t operator[](pose_t pose) const;
    };
} // namespace autolabor

#endif // PM1_SDK_FUSION_LOCATOR_T_HH