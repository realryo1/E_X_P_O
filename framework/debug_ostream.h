#pragma once

#include <Windows.h>
#include <sstream>
#include <string>

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
		int sync() override
		{
#if defined(_DEBUG) && defined(EXPO_VERBOSE_DEBUG_LOG)
			const std::string message = str();
			if (!message.empty())
			{
				int count = MultiByteToWideChar(
					CP_UTF8,
					0,
					message.c_str(),
					-1,
					nullptr,
					0);
				if (count > 0)
				{
					std::wstring wide(
						static_cast<size_t>(count),
						L'\0');
					MultiByteToWideChar(
						CP_UTF8,
						0,
						message.c_str(),
						-1,
						wide.data(),
						count);
					OutputDebugStringW(wide.c_str());
				}
			}
#endif
			str(std::string());
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
