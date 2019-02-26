//
// Created by ydrml on 2019/2/26.
//

#include "Tests.h"

#include "../main/pm1/api.h"
#include "../main/pm1/extensions.h"
#include "../main/pm1/time_extensions.h"
#include "../main/pm1/internal/serial/serial.h"
#include "../main/pm1/internal/can_message.h"

using namespace mechdancer::common;
using namespace autolabor::pm1;

void test::test_string_print() {
	println(join_to_string("", 1, 2, 3, 4, 5));
	println(join_to_string(", ", 1, 2, 3, 4, 5));
	println(join_to_string("", '[', join_to_string(", ", 1, 2, 3, 4, 5), ']'));
	
	println(measure_time([] { delay(1); }).count());
}

void test::test_serial_port() {
	try {
		//enumerate_ports
		for (const auto &it : serial::list_ports()) {
			std::cout << "("
			          << it.port.c_str() << ", "
			          << it.description.c_str() << ", "
			          << it.hardware_id.c_str() << ")"
			          << std::endl;
		}
		// port, baudrate, timeout in milliseconds
		serial::Serial  my_serial("com3", 9600, serial::Timeout::simpleTimeout(1000));
		
		std::cout << "Is the serial port open?"
		          << (my_serial.isOpen() ? " Yes." : " No.")
		          << std::endl;
		
		// Get the Test string
		int         count       = 0;
		std::string test_string = "Testing.";
		
		// Test the timeout, there should be 1 second between prints
		std::cout << "Timeout == 1000ms, asking for 1 more byte than written." << std::endl;
		while (count < 10) {
			size_t bytes_wrote = my_serial.write(test_string);
			
			std::string result = my_serial.read(test_string.length() + 1);
			
			std::cout << "Iteration: " << count
			          << ", Bytes written: " << bytes_wrote
			          << ", Bytes read: " << result.length()
			          << ", String read: " << result
			          << std::endl;
			
			count += 1;
		}
		
		// Test the timeout at 250ms
		my_serial.setTimeout(serial::Timeout::max(), 250, 0, 250, 0);
		count = 0;
		std::cout << "Timeout == 250ms, asking for 1 more byte than written." << std::endl;
		while (count < 10) {
			size_t bytes_wrote = my_serial.write(test_string);
			
			std::string result = my_serial.read(test_string.length() + 1);
			
			std::cout << "Iteration: " << count
			          << ", Bytes written: " << bytes_wrote
			          << ", Bytes read: " << result.length()
			          << ", String read: " << result
			          << std::endl;
			
			count += 1;
		}
		
		// Test the timeout at 250ms, but asking exactly for what was written
		count = 0;
		std::cout << "Timeout == 250ms, asking for exactly what was written." << std::endl;
		while (count < 10) {
			size_t bytes_wrote = my_serial.write(test_string);
			
			std::string result = my_serial.read(test_string.length());
			
			std::cout << "Iteration: " << count
			          << ", Bytes written: " << bytes_wrote
			          << ", Bytes read: " << result.length()
			          << ", String read: " << result
			          << std::endl;
			
			count += 1;
		}
		
		// Test the timeout at 250ms, but asking for 1 less than what was written
		count = 0;
		std::cout << "Timeout == 250ms, asking for 1 less than was written." << std::endl;
		while (count < 10) {
			size_t bytes_wrote = my_serial.write(test_string);
			
			std::string result = my_serial.read(test_string.length() - 1);
			
			std::cout << "Iteration: " << count
			          << ", Bytes written: " << bytes_wrote
			          << ", Bytes read: " << result.length()
			          << ", String read: " << result
			          << std::endl;
			
			count += 1;
		}
	} catch (std::exception &e) {
		std::cerr << "Unhandled Exception: " << e.what() << std::endl;
	}
}

void test::test_crc_check() {
	std::cout << sizeof(can_pack_no_data) << std::endl;
	std::cout << sizeof(can_pack_with_data) << std::endl;
	
	union_no_data
			temp1{0xfe, 0x31, 0x32, 0x33, 0x34, 0xf1};
	
	std::cout << std::boolalpha
	          << crc_check(temp1) << std::endl;
	
	union_no_data
			temp2{0x00, 0x31, 0x32, 0x33, 0x34, 0x00};
	
	reformat(temp2);
	
	std::cout << std::boolalpha
	          << (0xfe == temp2.bytes[0]) << std::endl
	          << (0xf1 == temp2.bytes[sizeof(temp2) - 1]) << std::endl;
}

void test::test_info_fill() {
	union_no_data temp1{};
	fill_info(temp1, 1, false, 2, 3, 4);
	temp1.data.type    = 5;
	temp1.data.reserve = 6;
	reformat(temp1);
	auto info = temp1.data.info();
	std::cout << "network:\t" << (int) info.network() << std::endl
	          << "data_field:\t" << std::boolalpha << info.data_field() << std::endl
	          << "property:\t" << (int) info.property() << std::endl
	          << "node_type:\t" << (int) info.node_type() << std::endl
	          << "node_index:\t" << (int) info.node_index() << std::endl;
}