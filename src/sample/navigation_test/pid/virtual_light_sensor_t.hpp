﻿//
// Created by User on 2019/7/4.
//

#ifndef PM1_SDK_VIRTUAL_LIGHT_SENSOR_T_HPP
#define PM1_SDK_VIRTUAL_LIGHT_SENSOR_T_HPP


#include "point_t.h"
#include "shape_t.hpp"

/**
 * 虚拟光电传感器
 */
struct virtual_light_sensor_t {
    virtual_light_sensor_t(
        point_t position,
        double radius
    ) : _position(position),
        range(32, radius) {}
    
    struct result_t {
        size_t local_count;
        bool   tip_begin;
        double error;
    };
    
    template<class t>
    [[nodiscard]] result_t
    operator()(point_t position,
               double direction,
               t &local_begin,
               t &local_end);

private:
    point_t  _position;
    circle_t range;
};

template<class t>
virtual_light_sensor_t::result_t
virtual_light_sensor_t::operator()(
    point_t position,
    double direction,
    t &local_begin,
    t &local_end
) {
    // 重新定位传感器
    auto cos = std::cos(direction),
         sin = std::sin(direction);
    range.center    = {
        position.x + cos * _position.x - sin * _position.y,
        position.y + sin * _position.x + cos * _position.y,
    };
    range.direction = direction;
    
    // 确定局部路径起点
    const auto check = [&](point_t point) { return range.check_inside(point); };
    const auto end   = local_end;
    while (local_begin < local_end) {
        if (check(*local_begin)) {
            local_end = local_begin + 1;
            break;
        }
        ++local_begin;
    }
    
    if (local_begin->type == point_type_t::tip) {
        std::cout << "tip!" << std::endl;
        return {1, true, NAN};
    }
    
    // 确定局部路径终点
    while (local_end < end && check(*local_end)) {
        if (local_end->type == point_type_t::tip) {
            break;
        }
        ++local_end;
    }
    
    size_t local_count = local_end - local_begin;
    if (local_count == 0)
        return {local_count, false, NAN};
    
    // 连接面积范围
    auto shape  = range.to_vector();
    auto index1 = max_by(shape, [=](point_t point) {
        return -std::hypot(local_begin->x - point.x,
                           local_begin->y - point.y);
    });
    
    auto index0 = local_end->type == point_type_t::tip
                  ? max_by(shape, [=](point_t point) {
            auto x0 = point.x - local_end->x,
                 x1 = local_end->x - (local_end - 1)->x,
                 y0 = point.y - local_end->y,
                 y1 = local_end->y - (local_end - 1)->y;
            return x0 * x1 + y0 * y1;
        })
                  : max_by(shape, [=](point_t point) {
            return -std::hypot((local_end - 1)->x - point.x,
                               (local_end - 1)->y - point.y);
        });
    
    if (index1 < index0) index1 += range.point_count();
    
    shape.resize(local_count + index1 - index0);
    std::copy(local_begin, local_end, shape.begin());
    for (auto i = local_count; i < shape.size(); ++i)
        shape[i] = range[(index0++) % range.point_count()];
    
    return {
        local_count,
        false,
        2 * (0.5 - any_shape(shape).size() / range.size())
    };
}


#endif //PM1_SDK_VIRTUAL_LIGHT_SENSOR_T_HPP
