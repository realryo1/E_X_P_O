#pragma once

#include <Windows.h>
#include <sstream>

namespace hal
{
	class debugbuf : public std::basic_stringbuf<char, std::char_traits<char>>
	{
	public:
		virtual ~debugbuf()
		{
			sync();
		}
	protected:
		int sync()
		{
			const std::string message = str();
			if (!message.empty())
			{
				int requiredChars = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
				if (requiredChars > 0)
				{
					std::wstring wideMessage(static_cast<std::size_t>(requiredChars), L'\0');
					MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, &wideMessage[0], requiredChars);
					OutputDebugStringW(wideMessage.c_str());
				}
				else
				{
					OutputDebugStringA(message.c_str());
				}
			}

			str(std::basic_string<char>());
			return 0;
		}
	};
	class debug_ostream : public std::basic_ostream<char, std::char_traits<char>>
	{
	public:
		debug_ostream() : std::basic_ostream<char, std::char_traits<char>>(new debugbuf()) {}
		~debug_ostream() { delete rdbuf(); }
	};

	extern debug_ostream dout;
}
