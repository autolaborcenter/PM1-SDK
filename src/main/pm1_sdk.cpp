//
// Created by ydrml on 2019/2/22.
//

#include "pm1_sdk.h"

#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <iostream>
#include "internal/time_extensions.h"
#include "internal/chassis.hh"
#include "exception.h"

using namespace autolabor::pm1;

result::operator bool() const { return error_info.empty(); }

std::shared_ptr<chassis> _ptr;

constexpr auto serial_error_prefix     = "IO Exception";
constexpr auto chassis_not_initialized = "chassis has not been initialized";
constexpr auto action_cannot_complete  = "this action will never complete";
constexpr auto illegal_target          = "target state should greater than 0";

/** 空安全检查 */
inline std::shared_ptr<chassis> ptr() {
	auto copy = _ptr;
	if (!copy) throw std::exception(chassis_not_initialized);
	return copy;
}

/** 记录暂停状态 */
volatile bool paused = false;

/** 循环的间隔 */
inline void loop_delay() { delay(0.01); }

/** 检查并执行 */
inline result run(const std::function<void()> &code) {
	union_error_code error{};
	
	try { code(); }
	catch (std::exception &e) {
		const auto what = std::string(e.what());
		
		if (what.find_first_of(serial_error_prefix) == 0) {
			error.bits.serial_error = true;
			return {error.code, e.what()};
		}
		
		if (what == chassis_not_initialized) {
			error.bits.not_initialized = true;
			return {error.code, e.what()};
		}
		
		if (what == action_cannot_complete || what == illegal_target) {
			error.bits.illegal_argument = true;
			return {error.code, e.what()};
		}
	}
	catch (...) {
		error.bits.others = true;
		return {error.code, "unknown and not-exception error"};
	}
	return {};
}

struct process_controller {
	double x0, y0, x1, y1, k;
	
	constexpr process_controller(
			double x0, double y0, double x1, double y1)
			: x0(x0), y0(y0),
			  x1(x1), y1(y1),
			  k((y1 - y0) / (x1 - x0)) {
		if (x0 < 0 || x1 < x0 ||
		    y0 < 0 || y1 < y0)
			throw std::exception("illegal parameters");
	}
	
	inline double operator()(double x) const {
		return x < x0 ? y0
		              : x > x1 ? y1
		                       : k * (x - x0) + y0;
	}
};

const auto max_v = 3 * 2 * pi_f,
           max_w = max_v / default_config.width;

const process_controller
		move_up(0, 0.01, 0.5, max_v),                  // NOLINT(cert-err58-cpp)
		move_down(0.01, 0.01, 3, max_v),               // NOLINT(cert-err58-cpp)
		rotate_up(0, pi_f / 18, pi_f / 4, max_w),      // NOLINT(cert-err58-cpp)
		rotate_down(pi_f / 9, pi_f / 36, pi_f, max_w); // NOLINT(cert-err58-cpp)

namespace block {
	/** 阻塞等待后轮转动 */
	inline void wait_or_drive(double v, double w) {
		if (paused)
			ptr()->set_target({0, NAN});
		else {
			velocity temp = {static_cast<float>(v), static_cast<float>(w)};
			ptr()->set_target(velocity_to_physical(&temp, &default_config));
		}
	}
	
	/** 阻塞并抑制输出 */
	autolabor::seconds_floating inhibit() {
		return autolabor::measure_time([] {
			while (paused) {
				ptr()->set_target({0, NAN});
				loop_delay();
			}
		});
	}
	
	/** 按固定控制量运行指定时间 */
	void go_timing(double v, double w, double seconds) {
		auto ending = autolabor::now() + autolabor::seconds_duration(seconds);
		while (autolabor::now() < ending) {
			if (paused) ending += inhibit();
			else wait_or_drive(v, w);
			
			loop_delay();
		}
	}
}

std::vector<std::string> autolabor::pm1::serial_ports() {
	auto                     info = serial::list_ports();
	std::vector<std::string> result(info.size());
	std::transform(info.begin(), info.end(), result.begin(),
	               [](const serial::PortInfo &it) { return it.port; });
	return result;
}

result autolabor::pm1::initialize(const std::string &port) {
	if (port.empty()) {
		std::stringstream builder;
		for (const auto   &item : serial_ports()) {
			auto result = initialize(item);
			if (result) return {};
			builder << item << ": " << result.error_info << std::endl;
		}
		
		union_error_code error{};
		error.bits.no_serial = true;
		auto msg = builder.str();
		
		return {error.code, msg.empty() ? "no available port" : msg};
	} else {
		try {
			_ptr = std::make_shared<chassis>(port);
			return {};
		}
		catch (std::exception &e) {
			_ptr = nullptr;
			union_error_code error{};
			error.bits.no_serial = true;
			return {error.code, e.what()};
		}
	}
}

result autolabor::pm1::shutdown() {
	if (_ptr) {
		_ptr = nullptr;
		return {};
	} else {
		union_error_code error{};
		error.bits.not_initialized = true;
		return {error.code, chassis_not_initialized};
	}
}

result autolabor::pm1::go_straight(double speed, double distance) {
	return run([speed, distance] {
		if (speed == 0) {
			if (distance == 0) return;
			throw std::exception(action_cannot_complete);
		}
		if (distance <= 0)
			throw std::exception(illegal_target);
		
		const auto o = ptr()->odometry().s;
		
		while (true) {
			auto current = std::abs(ptr()->odometry().s - o),
			     rest    = distance - current;
			if (rest < 0) break;
			auto actual = std::min({std::abs(speed),
			                        move_up(current),
			                        move_down(rest)});
			block::wait_or_drive(speed > 0 ? actual : -actual, 0);
			loop_delay();
		}
	});
}

result autolabor::pm1::go_straight_timing(double speed, double time) {
	return run([speed, time] { block::go_timing(speed, 0, time); });
}

result autolabor::pm1::go_arc(double speed, double r, double rad) {
	return run([speed, r, rad] {
		if (speed == 0) {
			if (rad == 0) return;
			throw std::exception(action_cannot_complete);
		}
		if (rad <= 0)
			throw std::exception(illegal_target);
		
		const auto o = ptr()->odometry().s;
		const auto d = std::abs(r * rad);
		
		while (true) {
			auto current = std::abs(ptr()->odometry().s - o),
			     rest    = d - current;
			if (rest < 0) break;
			auto actual    = std::min({std::abs(speed),
			                           move_up(current),
			                           move_down(rest)}),
			     available = speed > 0 ? actual : -actual;
			block::wait_or_drive(available, available / r);
			loop_delay();
			
			std::cout << available << std::endl;
		}
	});
}

result autolabor::pm1::go_arc_timing(double speed, double r, double time) {
	return run([speed, r, time] {
		if (r == 0) throw std::exception(illegal_target);
		block::go_timing(speed, speed / r, time);
	});
}

result autolabor::pm1::turn_around(double speed, double rad) {
	return run([speed, rad] {
		if (speed == 0) {
			if (rad == 0) return;
			throw std::exception(action_cannot_complete);
		}
		if (rad <= 0)
			throw std::exception(illegal_target);
		
		const auto o = ptr()->odometry().theta;
		while (true) {
			auto current = std::abs(ptr()->odometry().theta - o),
			     rest    = rad - current;
			if (rest < 0) break;
			auto actual = std::min({std::abs(speed),
			                        rotate_up(current),
			                        rotate_down(rest)});
			block::wait_or_drive(0, speed > 0 ? actual : -actual);
			loop_delay();
		}
	});
}

result autolabor::pm1::turn_around_timing(double speed, double time) {
	return run([speed, time] { block::go_timing(0, speed, time); });
}

result autolabor::pm1::pause() {
	return run([] {
		block::wait_or_drive(0, 0);
		paused = true;
	});
}

result autolabor::pm1::resume() {
	return run([] { paused = false; });
}

void autolabor::pm1::delay(double time) {
	std::this_thread::sleep_for(autolabor::seconds_duration(time));
}

autolabor::pm1::odometry autolabor::pm1::get_odometry() {
	try {
		auto odometry = ptr()->odometry();
		return {odometry.x,
		        odometry.y,
		        odometry.theta,
		        odometry.vx,
		        odometry.vy,
		        odometry.w};
	} catch (std::exception &) {
		return {NAN, NAN, NAN, NAN, NAN, NAN};
	}
}

result autolabor::pm1::drive(double v, double w) {
	return run([v, w] {
		velocity temp = {static_cast<float>(v), static_cast<float>(w)};
		ptr()->set_target(velocity_to_physical(&temp, &default_config));
	});
}

result autolabor::pm1::reset_odometry() {
	return run([] { ptr()->clear_odometry(); });
}

result autolabor::pm1::check_state() {
	return run([] {
		ptr()->check_state();
	});
}

result autolabor::pm1::lock() {
	return run([] { ptr()->disable(); });
}

result autolabor::pm1::unlock() {
	return run([] { ptr()->enable(); });
}
