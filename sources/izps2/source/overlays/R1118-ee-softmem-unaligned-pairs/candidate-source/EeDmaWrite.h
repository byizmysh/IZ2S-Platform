#pragma once

#include <cstddef>
#include <cstdint>

class EeDmaWrite
{
public:
	explicit EeDmaWrite(void* destination, size_t bytes);
	~EeDmaWrite();
	EeDmaWrite(const EeDmaWrite&) = delete;
	EeDmaWrite& operator=(const EeDmaWrite&) = delete;

private:
	bool m_tracked = false;
};
