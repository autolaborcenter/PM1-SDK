//
// Created by User on 2019/7/25.
//

#include "odometry.h"

#include <cmath>
#include <limits>

extern "C" {
#include <internal/control_model/motor_map.h>
}

autolabor::odometry_t<autolabor::odometry_type::delta>
autolabor::pm1::wheels_to_odometry(
    double left,
    double right,
    const chassis_config_t &config) {
    const auto l = config.left_radius * left,
               r = config.right_radius * right,
               s = (r + l) / 2,
               a = (r - l) / config.width;
    double     x, y;
    if (std::abs(a) < std::numeric_limits<double>::epsilon()) {
        x = s;
        y = 0;
    } else {
        auto _r = s / a;
        x = _r * std::sin(a);
        y = _r * (1 - std::cos(a));
    }
    return {std::abs(s), std::abs(a), x, y, a};
}

void
autolabor::pm1::pm1_odometry_t::ask(serial_port &port) {
    const auto msg = autolabor::can::pack<ecu<>::current_position_tx>();
    port.send(bytes_begin(msg), sizeof(decltype(msg)));
    ++wheels_seq;
}

autolabor::pm1::pm1_odometry_t::result_type
autolabor::pm1::pm1_odometry_t::try_parse(
    decltype(now()) _now,
    const pack_with_data &msg,
    const chassis_config_t &config
) {
    if (ecu<0>::current_position_rx::match(msg)) {
        update(true, _now, msg, config);
        return result_type::left;
    }
    if (ecu<1>::current_position_rx::match(msg)) {
        update(false, _now, msg, config);
        return result_type::right;
    }
    return result_type::none;
}

autolabor::stamped_t<autolabor::odometry_t<>>
autolabor::pm1::pm1_odometry_t::value() const {
    std::lock_guard<decltype(update_lock)> lock(update_lock);
    return _odometry;
}

void
autolabor::pm1::pm1_odometry_t::update(bool left,
                                       decltype(now()) _now,
                                       const pack_with_data &msg,
                                       const chassis_config_t &config) {
    auto &motor = _right;
    auto &mark  = r_mark;
    if (left) {
        motor = _left;
        mark  = l_mark;
    }
    
    const auto last  = motor;
    const auto value = RAD_OF(get_data_value<int>(msg), default_wheel_k);
    motor = {_now, {value, value - last.value.position / duration_seconds(_now - last.time)}};
    mark.seq = wheels_seq.load();
    
    if (l_mark.seq == 0 && r_mark.seq == 0)
        mark.last = value;
    else if (l_mark.seq == r_mark.seq) {
        std::lock_guard<decltype(update_lock)> lock(update_lock);
        _odometry.value += wheels_to_odometry(_left.value.position - l_mark.last,
                                              _right.value.position - r_mark.last,
                                              config);
        _odometry.time = _now;
        l_mark.last    = _left.value.position;
        r_mark.last    = _right.value.position;
    }
}


