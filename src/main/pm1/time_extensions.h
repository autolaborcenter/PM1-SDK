//
// Created by ydrml on 2019/2/23.
//

#ifndef PM1_SDK_TIME_EXTENSION_H
#define PM1_SDK_TIME_EXTENSION_H


#include <chrono>
#include <functional>

namespace mechdancer {
	namespace common {
		/**
		 * 转换为[duration]
		 *
		 * @param seconds 秒数
		 * @return 对应的 std::chrono::duration
		 */
		inline auto seconds_duration(double seconds)
		-> std::chrono::duration<double, std::ratio<1>> {
			return std::chrono::duration<double, std::ratio<1>>(seconds);
		}
		
		/**
		 * 从高精度时钟获取当前时间
		 *
		 * @return 当前时间
		 */
		inline auto now()
		-> decltype(std::chrono::high_resolution_clock::now()) {
			return std::chrono::high_resolution_clock::now();
		}
		
		/**
		 * 测量一段代码的执行时间
		 *
		 * @tparam TimeUnit 时间间隔的单位
		 * @param function 待测代码块
		 * @return 用[TimeUnit]表示的时间间隔
		 */
		template<class TimeUnit = std::chrono::duration<double, std::ratio<1>>>
		inline auto measure_time(const std::function<void()> &function)
		-> TimeUnit {
			const auto origin = now();
			function();
			return now() - origin;
		}
	}
}


#endif //PM1_SDK_TIME_EXTENSION_H