//
// Created by ydrml on 2019/2/22.
//

#include "pm1_sdk.h"

#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include "internal/time_extensions.h"
#include "internal/chassis.hh"
#include "internal/mechanical.h"

using namespace autolabor::pm1;

result::operator bool() const { return error_info.empty(); }

using seconds = std::chrono::duration<double, std::ratio<1>>;

std::shared_ptr<chassis> _ptr;

/** 空安全检查 */
inline std::shared_ptr<chassis> ptr() {
	auto copy = _ptr;
	if (!copy) throw std::exception("chassis has not been initialized!");
	return copy;
}

/** 记录暂停状态 */
volatile bool paused = false;

/** 循环的间隔 */
inline void loop_delay() { delay(0.05); }

/** 检查并执行 */
inline result run(const std::function<void()> &code,
                  const std::function<void()> &recover = [] {}) {
	try { code(); } catch (std::exception &e) { return {e.what()}; }
	return {};
}

class move {
public:
	/** 开始减速距离 */
	constexpr static auto slow_down_begin = 3.0;
	
	/** 减到最小距离 */
	constexpr static auto slow_down_end = 0.05;
	
	/** 最小线速度 */
	constexpr static auto min_speed = 0.1;
	
	/** 最大线速度 */
	constexpr static auto max_speed = mechanical::max_v;
};

class rotate {
public:
	/** 开始减速距离 */
	constexpr static auto slow_down_begin = mechanical::pi;    // == 180°
	
	/** 减到最小距离 */
	constexpr static auto slow_down_end = mechanical::pi / 18; // == 10°
	
	/** 最小角速度 */
	constexpr static auto min_speed = slow_down_end * 2;
	
	/** 最大角速度 */
	constexpr static auto max_speed = mechanical::max_w;
};

/** 求与目标特定距离时的最大速度 */
template<class t>
inline double max_speed_when(double rest_distance) {
	constexpr static auto k = (t::max_speed - t::min_speed) / (t::slow_down_begin - t::slow_down_end);
	
	return rest_distance > t::slow_down_begin
	       ? t::max_speed
	       : rest_distance < t::slow_down_end
	         ? t::min_speed
	         : k * (rest_distance - t::slow_down_end) + t::min_speed;
}

/** 求目标速度下实际应有的速度 */
template<class t>
inline double actual_speed(double target, double rest_distance) {
	const auto actual = std::min(std::abs(target), max_speed_when<t>(rest_distance));
	return target > 0 ? +actual : -actual;
}

namespace block {
	/** 阻塞等待后轮转动 */
	inline void wait_or_drive(double v, double w) {
		if ((v == 0 && w == 0) || paused)
			ptr()->set_state(0, ptr()->rudder());
		else {
			auto rudder_target = v == 0
			                     ? w > 0
			                       ? -mechanical::pi / 2
			                       : +mechanical::pi / 2
			                     : -std::atan(w * mechanical::length / v);
			
			if (std::abs(ptr()->rudder() - rudder_target) > mechanical::pi / 36)
				ptr()->set_state(0, rudder_target);
			else
				ptr()->set_target(v, w);
		}
	}
	
	/** 阻塞并抑制输出 */
	seconds inhibit() {
		return mechdancer::common::measure_time([] {
			while (paused) {
				wait_or_drive(0, 0);
				loop_delay();
			}
		});
	}
	
	/** 按固定控制量运行指定时间 */
	void go_timing(double v, double w, double seconds) {
		using namespace mechdancer::common;
		
		auto ending = now() + seconds_duration(seconds);
		while (now() < ending) {
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
		for (const auto &item:serial_ports())
			if (initialize(item))
				return {};
		
		return {"no available port"};
	} else {
		try {
			_ptr = std::make_shared<chassis>(port);
			return {};
		}
		catch (std::exception &e) {
			_ptr = nullptr;
			return {e.what()};
		}
	}
}

result autolabor::pm1::shutdown() {
	return _ptr ? _ptr = nullptr, result{} : result{"chassis doesn't exist"};
}

result autolabor::pm1::go_straight(double speed, double distance) {
	if (speed == 0 && distance != 0) return {"this action will never complete"};
	return run([speed, distance] {
		const auto o = ptr()->odometry().s;
		double     rest_distance;
		
		while ((rest_distance = distance - std::abs(ptr()->odometry().s - o)) > 0) {
			block::wait_or_drive(actual_speed<move>(speed, rest_distance), 0);
			loop_delay();
		}
	});
}

result autolabor::pm1::go_straight_timing(double speed, double time) {
	return run([speed, time] { block::go_timing(speed, 0, time); });
}

result autolabor::pm1::go_arc(double speed, double r, double rad) {
	if (speed == 0 && rad != 0) return {"this action will never complete"};
	return run([speed, r, rad] {
		const auto o = ptr()->odometry().s;
		const auto d = std::abs(r * rad);
		double     rest_distance;
		
		while ((rest_distance = d - std::abs(ptr()->odometry().s - o)) > 0) {
			auto available = actual_speed<move>(speed, rest_distance);
			block::wait_or_drive(available, available / r);
			loop_delay();
		}
	});
}

result autolabor::pm1::go_arc_timing(double speed, double r, double time) {
	return run([speed, r, time] { block::go_timing(speed, speed / r, time); });
}

result autolabor::pm1::turn_around(double speed, double rad) {
	if (speed == 0 && rad != 0) return {"this action will never complete"};
	return run([speed, rad] {
		const auto o = ptr()->odometry().theta;
		double     rest_distance;
		
		while ((rest_distance = rad - std::abs(ptr()->odometry().theta - o)) > 0) {
			block::wait_or_drive(0, actual_speed<rotate>(speed, rest_distance));
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
	std::this_thread::sleep_for(mechdancer::common::seconds_duration(time));
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
	return run([v, w] { ptr()->set_target(v, w); });
}
