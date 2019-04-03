//
// Created by User on 2019/4/2.
//

#include "pm1_sdk.h"

#include "internal/chassis.hh"
#include "internal/serial/serial.h"
#include "internal/raii/weak_shared_lock.hh"
#include "internal/process_controller.hh"
#include "internal/raii/weak_lock_guard.hh"
#include "internal/time_extensions.h"

extern "C" {
#include "internal/control_model/model.h"
}

std::atomic<autolabor::odometry_t>
	odometry_mark = ATOMIC_VAR_INIT({});

std::shared_ptr<autolabor::pm1::chassis>
	ptr;

std::shared_mutex
	mutex;

// =====================================================================

constexpr auto
	chassis_pointer_busy  = "chassis pointer is busy",
	null_chassis_pointer  = "null chassis pointer",
	infinite_action       = "action never complete",
	invalid_target        = "invalid target",
	action_canceled       = "action canceled";

#define READ_ASSERT                      \
weak_shared_lock lk0(mutex);             \
if (!lk0) return {chassis_pointer_busy}; \
if (!ptr) return {null_chassis_pointer}

#define READ_ASSERT_OR(DEFAULT)                   \
weak_shared_lock lk0(mutex);                      \
if (!lk0) return {chassis_pointer_busy, DEFAULT}; \
if (!ptr) return {null_chassis_pointer, DEFAULT}

// =====================================================================

std::vector<std::string> autolabor::pm1::serial_ports() {
	auto                     info = serial::list_ports();
	std::vector<std::string> result(info.size());
	std::transform(info.begin(), info.end(), result.begin(),
	               [](const serial::PortInfo &it) { return it.port; });
	return result;
}

autolabor::pm1::result<std::string>
autolabor::pm1::initialize(const std::string &port) {
	if (port.empty()) {
		std::stringstream builder;
		for (const auto   &item : serial_ports()) {
			auto result = initialize(item);
			if (result) return {"", item};
			builder << item << ": " << result.error_info << std::endl;
		}
		
		auto msg = builder.str();
		return {msg.empty() ? "no available port" : msg};
	} else {
		std::unique_lock<std::shared_mutex> lock(mutex);
		try {
			ptr = std::make_shared<chassis>(port);
			odometry_mark.store({});
			return {"", port};
		}
		catch (std::exception &e) {
			ptr = nullptr;
			return {e.what()};
		}
	}
}

autolabor::pm1::result<void>
autolabor::pm1::shutdown() {
	std::unique_lock<std::shared_mutex> lock(mutex);
	
	if (ptr) {
		ptr = nullptr;
		return {};
	} else {
		return {null_chassis_pointer};
	}
}

autolabor::pm1::result<void>
autolabor::pm1::drive(double v, double w) {
	READ_ASSERT;
	
	velocity temp{static_cast<float>(v), static_cast<float>(w)};
	auto     actual = velocity_to_physical(&temp, &default_config);
	ptr->set_target(actual.speed, actual.rudder);
	
	return {};
}

autolabor::pm1::result<autolabor::pm1::odometry>
autolabor::pm1::get_odometry() {
	const static autolabor::pm1::odometry
		nan{NAN, NAN, NAN, NAN, NAN, NAN};
	
	READ_ASSERT_OR(nan);
	auto temp = ptr->odometry() - odometry_mark;
	return {"", {temp.x, temp.y, temp.theta, temp.vx, temp.vy, temp.w}};
}

autolabor::pm1::result<void>
autolabor::pm1::reset_odometry() {
	READ_ASSERT;
	odometry_mark = ptr->odometry();
	return {};
}

autolabor::pm1::result<void>
autolabor::pm1::lock() {
	READ_ASSERT;
	ptr->disable();
	return {};
}

autolabor::pm1::result<void> autolabor::pm1::unlock() {
	READ_ASSERT;
	ptr->enable();
	return {};
}

autolabor::pm1::result<autolabor::pm1::chassis_state>
autolabor::pm1::get_chassis_state() {
	READ_ASSERT;
	
	auto temp = ptr->state();
	return {"", {(node_state) temp._ecu0,
	             (node_state) temp._ecu1,
	             (node_state) temp._tcu}};
}

void
autolabor::pm1::delay(double time) {
	std::this_thread::sleep_for(seconds_duration(time));
}

// =====================================================================

#define READ_STATEMENT(ACTION) \
{ READ_ASSERT;                 \
  ACTION;                      \
}

#define READ_SCOPE \
{ READ_ASSERT;

#define ACTION_ASSERT                          \
weak_lock_guard<std::mutex> lk1(action_mutex); \
if (!lk1) return {"another action is invoking"}

#define STATE_ASSERT                             \
{                                                \
    auto _ = ptr->state();                       \
    if (!_.check_all()) {                        \
        if (_.check_all(node_state_t::disabled)) \
            return {"chassis is locked"};        \
                                                 \
        return {"critical error!"};              \
    }                                            \
}
// =====================================================================

inline double seconds_cast(std::chrono::steady_clock::time_point time) {
	using s_t = autolabor::seconds_floating;
	return std::chrono::duration_cast<s_t>(time.time_since_epoch()).count();
}

// =====================================================================

std::mutex    action_mutex;
volatile bool pause_flag  = false,
              cancel_flag = false;

autolabor::pm1::result<void>
autolabor::pm1::go_straight(double speed, double distance) {
	if (speed == 0)
		return {distance == 0 ? "" : infinite_action};
	if (distance <= 0)
		return {invalid_target};
	
	ACTION_ASSERT;
	
	constexpr static autolabor::process_controller
		move_controller(0.5, 0.1, 20, 10);
	
	double    rudder;
	process_t process{};
	
	READ_STATEMENT(process.begin = ptr->odometry().s)
	process.end = process.begin + distance;
	{
		velocity temp{static_cast<float>(speed), 0};
		auto     target = velocity_to_physical(&temp, &default_config);
		
		process.speed = target.speed;
		rudder = target.rudder;
	}
	
	auto paused = pause_flag;
	while (!cancel_flag) {
		READ_SCOPE
			if (pause_flag) {
				paused = true;
				ptr->set_target(0, NAN);
			} else {
				// STATE_ASSERT
				
				auto current = ptr->odometry().s;
				if (current > process.end) break;
				
				if (paused) {
					paused = false;
					process.begin = current;
				}
				auto actual = rudder != ptr->rudder().position
				              ? 0
				              : move_controller(process, current);
				
				ptr->set_target(actual, 0);
			}
		}
		
		delay(0.05);
	}
	ptr->set_target(0, NAN);
	return {cancel_flag ? action_canceled : ""};
}

autolabor::pm1::result<void>
autolabor::pm1::go_straight_timing(double speed, double time) {
	if (time < 0)
		return {invalid_target};
	
	ACTION_ASSERT;
	
	constexpr static autolabor::process_controller
		move_controller(0.5, 0.1, 5, 2);
	
	double    rudder;
	process_t process{seconds_cast(now())};
	
	process.end = process.begin + time;
	{
		velocity temp{static_cast<float>(speed), 0};
		auto     target = velocity_to_physical(&temp, &default_config);
		
		process.speed = target.speed;
		rudder = target.rudder;
	}
	
	auto paused = pause_flag;
	while (!cancel_flag) {
		READ_SCOPE
			if (pause_flag) {
				paused = true;
				ptr->set_target(0, NAN);
			} else {
				// STATE_ASSERT
				
				auto current = seconds_cast(now());
				if (current > process.end) break;
				
				if (paused) {
					paused = false;
					process.begin = current;
				}
				auto actual = rudder != ptr->rudder().position
				              ? 0
				              : move_controller(process, current);
				
				ptr->set_target(actual, 0);
			}
		}
		
		delay(0.05);
	}
	ptr->set_target(0, NAN);
	return {cancel_flag ? action_canceled : ""};
}

autolabor::pm1::result<void>
autolabor::pm1::go_arc(double speed, double r, double rad) {
	if (std::abs(r) < 0.05)
		return {"radius is too little, use turn_around instead"};
	if (speed == 0)
		return {rad == 0 ? "" : infinite_action};
	if (rad <= 0)
		return {invalid_target};
	
	ACTION_ASSERT;
	
	constexpr static autolabor::process_controller
		move_controller(0, 0.01, 0.2, 0.1);
	
	double    rudder;
	process_t process{seconds_cast(now())};
	{
		velocity temp{static_cast<float>(speed), 0};
		auto     target = velocity_to_physical(&temp, &default_config);
		
		process.speed = target.speed;
		rudder = target.rudder;
	}
	
	bool paused = pause_flag;
	while (!cancel_flag) {
		READ_SCOPE
			if (pause_flag) {
				paused = true;
				ptr->set_target(0, NAN);
			} else {
				// STATE_ASSERT
				
				if (paused) {
					paused = false;
				}
			}
		}
		
		delay(0.05);
	}
	ptr->set_target(0, NAN);
	return {cancel_flag ? action_canceled : ""};
}

autolabor::pm1::result<void>
autolabor::pm1::go_arc_timing(double speed, double r, double time) {
	if (std::abs(r) < 0.05)
		return {"radius is too little, use turn_around instead"};
	if (time < 0)
		return {invalid_target};
	
	ACTION_ASSERT;
	
	constexpr static autolabor::process_controller
		move_controller(0, 0.01, 0.2, 0.1);
	
	process_t process{seconds_cast(now())};
	process.end   = process.begin + time;
	process.speed = speed;
	
	bool paused = pause_flag;
	while (!cancel_flag) {
		READ_SCOPE
			if (pause_flag) {
				paused = true;
				ptr->set_target(0, NAN);
			} else {
				// STATE_ASSERT
				
				auto current = seconds_cast(now());
				if (current > process.end) break;
				
				if (paused) {
					paused = false;
					process.begin = current;
				}
			}
		}
		
		delay(0.05);
	}
	ptr->set_target(0, NAN);
	return {cancel_flag ? action_canceled : ""};
}

autolabor::pm1::result<void>
autolabor::pm1::turn_around(double speed, double rad) {
	if (speed == 0)
		return {rad == 0 ? "" : infinite_action};
	if (rad <= 0)
		return {invalid_target};
	
	ACTION_ASSERT;
	
	constexpr static autolabor::process_controller
		move_controller(0, 0.01, 0.2, 0.1);
	
	bool paused = pause_flag;
	while (!cancel_flag) {
		READ_SCOPE
			if (pause_flag) {
				paused = true;
				ptr->set_target(0, NAN);
			} else {
				// STATE_ASSERT
				
				if (paused) {
					paused = false;
				}
			}
		}
		
		delay(0.05);
	}
	ptr->set_target(0, NAN);
	return {cancel_flag ? action_canceled : ""};
}

autolabor::pm1::result<void>
autolabor::pm1::turn_around_timing(double speed, double time) {
	if (time < 0)
		return {invalid_target};
	
	ACTION_ASSERT;
	
	constexpr static autolabor::process_controller
		move_controller(0, 0.01, 0.2, 0.1);
	
	process_t process{seconds_cast(now())};
	process.end   = process.begin + time;
	process.speed = speed;
	
	bool paused = pause_flag;
	while (!cancel_flag) {
		READ_SCOPE
			if (pause_flag) {
				paused = true;
				ptr->set_target(0, NAN);
			} else {
				// STATE_ASSERT
				
				auto current = seconds_cast(now());
				if (current > process.end) break;
				
				if (paused) {
					paused = false;
					process.begin = current;
				}
			}
		}
		
		delay(0.05);
	}
	ptr->set_target(0, NAN);
	return {cancel_flag ? action_canceled : ""};
}

autolabor::pm1::result<void>
autolabor::pm1::pause() {
	pause_flag = true;
	return {};
}

autolabor::pm1::result<void>
autolabor::pm1::resume() {
	pause_flag = false;
	return {};
}

autolabor::pm1::result<void>
autolabor::pm1::cancel_all() {
	cancel_flag = true;
	{ std::lock_guard<std::mutex> wait(action_mutex); }
	cancel_flag = false;
	return {};
}
