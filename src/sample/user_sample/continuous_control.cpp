#include <iostream>
#include <windows.h>

#include "pm1_sdk.h"                 // ͷ�ļ�
using namespace autolabor::pm1;      // �����ռ�
using namespace std;

int main()
{
	cout << "initializing..." << endl;
	auto result = initialize();	// ��ʼ������
	if (result)
	{
		cout << "connected to " << result.value << endl;
		unlock();				// ����
		while (check_state() != chassis_state::unlocked)
		{
			delay(0.1);
		}
		cout << "����\n[���������ǰ������]\n[Esc���˳�����]" << endl;
		while (GetKeyState(VK_ESCAPE) >= 0)
		{
			double v = 0, w = 0;
			bool up = GetKeyState(VK_UP) < 0;
			bool down = GetKeyState(VK_DOWN) < 0;
			bool left = GetKeyState(VK_LEFT) < 0;
			bool right = GetKeyState(VK_RIGHT) < 0;
			if (up && !down && !left && !right)		// ǰ
			{
				v = 0.1;
				w = 0;
			}
			else if (up && !down && left && !right)	// ��ǰ
			{
				v = 0.1;
				w = 0.2;
			}
			else if (up && !down && !left && right)	// ��ǰ
			{
				v = 0.1;
				w = -0.2;
			}
			else if (!up && down && !left && !right)// ��
			{
				v = -0.1;
				w = 0;
			}
			else if (!up && down && left && !right)	// ���
			{
				v = -0.1;
				w = -0.2;
			}
			else if (!up && down && !left && right)	// �Һ�
			{
				v = -0.1;
				w = 0.2;
			}
			else if (!up && !down && left && !right)// ��ʱ��ԭ��ת
			{
				v = 0;
				w = 0.2;
			}
			else if (!up && !down && !left && right)// ˳ʱ��ԭ��ת
			{
				v = 0;
				w = -0.2;
			}
			drive(v, w);
			delay(0.1);
		}
		shutdown();		// �Ͽ�����
	}
	else
	{
		cerr << "[" << result.error_info << "]" << endl;
		system("pause");
	}
	return 0;
}
