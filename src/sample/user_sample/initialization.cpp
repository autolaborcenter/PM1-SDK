#include <iostream>
#include "pm1_sdk.h"                 // ͷ�ļ�
using namespace autolabor::pm1;      // �����ռ�

#ifdef _DEBUG                        // ��̬��
#pragma comment(lib, "pm1_sdk_debug.lib")
#else
#pragma comment(lib, "pm1_sdk.lib")
#endif

int main()
{
	std::cout << "initializing..." << std::endl;
	auto result = initialize();     // ��ʼ������
	if (result)
	{
		std::cout << "connected to " << result.value << std::endl;
		unlock();                   // ����
		while (check_state() != chassis_state::unlocked)
		{
			delay(0.1);
		}
		std::cout << "moving..." << std::endl;
		turn_around(0.25, 1.57);    // ��0.25rad/s���ٶ�ԭ��ת90��
		shutdown();                 // �Ͽ�����
	}
	else
	{
		std::cerr << result.error_info << std::endl;
	}
	system("pause");
	return 0;
}